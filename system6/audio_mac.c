/* Game Boy emulator for 68k Macs
   audio_mac.c - Sound Manager integration using SndPlayDoubleBuffer */

// The Sound Manager callback generates samples from the current APU state at
// interrupt time, so playback never introduces gaps when emulation falls
// behind real time. Register writes are queued with emulated-time stamps
// and applied at their exact sample offsets during generation, which keeps
// note timing accurate at full speed

#include <Sound.h>
#include <Memory.h>
#include <Gestalt.h>
#include <OSUtils.h>
#include <string.h>

#include "audio_mac.h"
#include "../src/audio.h"
#include "../src/prof.h"

// do everything but start the sound manager
#define AUDIO_BENCH_MUTE 0

// 4194304 / 11127 = about 377
#define CYCLES_PER_SAMPLE 377

// each buffer has ~46 ms of samples
#define BUFFER_SAMPLES 512
#define SAMPLE_RATE_FIXED 0x2b7745d1

// how far emulated time can get ahead before the frame limiter blocks
#define MAX_LEAD_SAMPLES 768

// turn off the audio if real time > this period (if the user opens a menu)
#define STALL_SAMPLES 1024

// how far sample generation can get ahead when the frame limiter is off
#define MAX_BANK_SAMPLES 2048

static int cycle_accum;
static u32 emu_samples;          // emulated time in samples, main loop owns
static volatile u32 samples_out; // real time: samples handed to Sound Manager

// APU register writes produced by the "main thread" waiting to be applied
// during generation
struct apu_event {
    u32 t;
    u16 addr;
    u8 value;
};

#define EVQ_SIZE 512
#define EVQ_MASK (EVQ_SIZE - 1)

static struct apu_event evq[EVQ_SIZE];
static volatile int evq_write;
static volatile int evq_read;

static SndChannelPtr snd_channel;
static struct audio *g_audio;
static int audio_inited;
static volatile int snd_running;

// bypass the Sound Manager, decided at init from the machine type
static int use_direct;

typedef struct {
    long dbNumFrames;
    long dbFlags;
    long dbUserInfo[2];
    unsigned char dbSoundData[BUFFER_SAMPLES];
} MyDoubleBuffer;

static SndDoubleBufferHeader dbl_header;
static MyDoubleBuffer dbl_buffers[2];

#define LM_TICKS (*(volatile u32 *) 0x16a)

#if AUDIO_BENCH_MUTE
static unsigned char bench_buf[BUFFER_SAMPLES];
static u32 bench_t0;
static void gen_samples(unsigned char *p, int samples);
static void bench_drain(void);
#endif

#define LM_SDVOLUME (*(volatile u8 *) 0x260)

#define ASC_BASE 0x50f14000
#define ASC_FIFO_DEPTH 1024

#define ASC_FIFO_A   (*(volatile u8 *) (ASC_BASE + 0x000))
#define ASC_VERSION  (*(volatile u8 *) (ASC_BASE + 0x800))
#define ASC_MODE     (*(volatile u8 *) (ASC_BASE + 0x801))
#define ASC_CONTROL  (*(volatile u8 *) (ASC_BASE + 0x802))
#define ASC_FIFOMODE (*(volatile u8 *) (ASC_BASE + 0x803))
#define ASC_FIFOSTAT (*(volatile u8 *) (ASC_BASE + 0x804))
#define ASC_VOLUME   (*(volatile u8 *) (ASC_BASE + 0x806))
#define ASC_CLOCK    (*(volatile u8 *) (ASC_BASE + 0x807))

#define ASC_CHUNK 128 // mono samples per push, x2 = quarter FIFO

// diagnostics "dl @a78"
// +0 = chunks pushed
// +4 = total level-2 interrupts through the thunk
// +6 = byte0 = OR of every FIFOSTAT byte read,
//      byte1 = OR of VIA2 IFR at every entry
//      byte2 = watchdog revivals mod 256
//      byte3 = CB1 claims mod 256
// +8 = FIFOSTAT bit1 but not bit0, either empty or full
static struct {
    u32 dbg;
    u16 pushes;
    u16 entries;
    u16 bit1_only;
} asc_stats;

static void gen_samples(unsigned char *p, int samples);

// VIA2 on discrete-VIA2 machines, TODO verify fancy AV quadras, powerbooks
#define VIA2_BASE 0x50f02000
#define VIA2_PCR (*(volatile u8 *) (VIA2_BASE + 0x1800))
#define VIA2_IFR (*(volatile u8 *) (VIA2_BASE + 0x1a00))
#define VIA2_IER (*(volatile u8 *) (VIA2_BASE + 0x1c00))
#define VIA2_CB1_BIT 0x10

// Lvl2DT did not work on my machines, use autovector directly, handle
// CB1 interrupts and chain the rest to the old handler
#define VEC_LEVEL2 0x68

#define ASC_STAT_A_HALF          0x01
#define ASC_STAT_A_EMPTY_OR_FULL 0x02

static u32 asc_saved_vec;
static u8 asc_saved_ier;
static u32 asc_watch_t;
static u32 asc_watch_pushes;
static volatile u8 asc_vec_claimed;
static volatile u8 asc_in_push;
static u32 asc_last_push_tick;
extern void asc_thunk_entry(void);
static void asc_watchdog(void);

// this is always 0.....
static u32 read_vbr(void)
{
    register u32 v asm("d0");
    // compiled for 68000 so needs to be .shorts, but this is only called on
    // 020 and up
    asm volatile(".short 0x4e7a, 0x0801" : "=r"(v));
    return v;
}

#define LEVEL2_VECTOR (*(volatile u32 *) (read_vbr() + VEC_LEVEL2))

static u16 ints_off(void)
{
    u16 sr;
    asm volatile("move.w %%sr,%0\n\tori.w #0x0700,%%sr" : "=d"(sr));
    return sr;
}

static void ints_restore(u16 sr)
{
    asm volatile("move.w %0,%%sr" : : "d"(sr));
}

// for Mac Plus/SE/Classic
// #define VIA1_T1CL (*(volatile u8 *) 0xEFE9FE)
// #define VIA1_T1CH (*(volatile u8 *) 0xEFEBFE)
// for Mac II
// #define VIA1_T1CL (*(volatile u8 *) 0x50f00800)
// #define VIA1_T1CH (*(volatile u8 *) 0x50f00a00)
// u32 audio_timer_accum;
// u32 audio_calls;

// static u16 read_via_t1(void)
// {
//     u8 lo = VIA1_T1CL;
//     u8 hi = VIA1_T1CH;
//     return (hi << 8) | lo;
// }

static int HasSndPlayDoubleBuffer(void)
{
    long response;
    OSErr err;

    // first check for gestaltSndPlayDoubleBuffer flag (newer Sound Manager)
    err = Gestalt(gestaltSoundAttr, &response);
    if (err == noErr && (response & (1L << gestaltSndPlayDoubleBuffer)))
        return 1;

    // fall back to checking for ASC (older Sound Manager didn't define the flag)
    err = Gestalt(gestaltHardwareAttr, &response);
    if (err == noErr && (response & (1L << gestaltHasASC)))
        return 1;

    return 0;
}

int audio_mac_available(void)
{
    return HasSndPlayDoubleBuffer();
}

// called from main loop to advance the emulated-time clock
void audio_mac_sync(int cycles)
{
    u32 out;

    if (!audio_inited)
        return;

    cycle_accum += cycles;
    if (cycle_accum >= CYCLES_PER_SAMPLE) {
        emu_samples += cycle_accum / CYCLES_PER_SAMPLE;
        cycle_accum %= CYCLES_PER_SAMPLE;
    }

    // if emulation fell behind real time, snap forward
    out = samples_out;
    if ((s32) (emu_samples - out) < 0)
        emu_samples = out;
    else if ((s32) (emu_samples - out) > MAX_BANK_SAMPLES)
        emu_samples = out + MAX_BANK_SAMPLES;

    if (use_direct && snd_running) {
        asc_watchdog();
    }

#if AUDIO_BENCH_MUTE
    if (snd_running)
        bench_drain();
#endif
}

void audio_mac_write(struct audio *audio, u16 addr, u8 value)
{
    int w, next;

    if (!snd_running) {
        audio_write(audio, addr, value);
        return;
    }

    // mirror the raw byte now so register readback doesn't see stale values
    if (addr >= 0xff10 && addr <= 0xff3f) {
        audio->regs[addr - 0xff10] = value;
        if (addr >= REG_WAVE_START)
            audio->wave_ram[addr - REG_WAVE_START] = value;
    }

    w = evq_write;
    next = (w + 1) & EVQ_MASK;
    if (next == evq_read) {
        // queue full, just write it out of order so it's not lost
        audio_write(audio, addr, value);
        return;
    }

    evq[w].t = emu_samples;
    evq[w].addr = addr;
    evq[w].value = value;
    evq_write = next;
}

// generate one buffer, applying queued register writes at their offsets
static void gen_samples(unsigned char *p, int samples)
{
    int done = 0;

    while (done < samples) {
        int n = samples - done;

        if (evq_read != evq_write) {
            struct apu_event *e = &evq[evq_read];
            s32 off = (s32) (e->t - samples_out) - done;

            if (off <= 0) {
                audio_write(g_audio, e->addr, e->value);
                evq_read = (evq_read + 1) & EVQ_MASK;
                continue;
            }
            if (off < n)
                n = off;
        }

        audio_generate(g_audio, p + done, n);
        done += n;
    }
}

#if AUDIO_BENCH_MUTE
// stand in for DoubleBackProc
static void bench_drain(void)
{
    u32 real = ((LM_TICKS - bench_t0) * 11127) / 60;

    while ((s32) (real - samples_out) >= BUFFER_SAMPLES) {
        gen_samples(bench_buf, BUFFER_SAMPLES);
        samples_out += BUFFER_SAMPLES;
    }
}
#endif

static int asc_hw_present(void)
{
    long response;

    return Gestalt(gestaltHardwareAttr, &response) == noErr &&
           (response & (1L << gestaltHasASC));
}

// this driver assumes a plain ASC at 0x50f14000 paced off FIFO A's half-empty
// bit, with a discrete VIA2 at 0x50f02000. later machines all differ somehow:
// IIci/IIsi replace VIA2 with the RBV, the IIfx uses the OSS and moves the
// ASC, Quadras have an EASC or IOSB. those fall back to the Sound Manager
static int asc_direct_supported(void)
{
    long machine;

    if (!asc_hw_present())
        return 0;

    if (Gestalt(gestaltMachineType, &machine) != noErr)
        return 0;

    return machine == gestaltMacII || machine == gestaltMacIIx ||
           machine == gestaltMacIIcx || machine == gestaltMacSE030;
}

static void asc_setup_regs(void)
{
    ASC_MODE = 0;
    ASC_FIFOMODE = 0x80; // clear both FIFOs
    ASC_FIFOMODE = 0;
    ASC_CONTROL = 0; // mono: FIFO A drives both outputs
    ASC_CLOCK = 0; // 22257 Hz
    ASC_VOLUME = (u8) ((LM_SDVOLUME & 7) << 5);
    ASC_MODE = 1; // FIFO mode, starts draining
}

static u8 asc_dbg_or, asc_dbg_ifr_or, asc_dbg_revive, asc_dbg_claimed;

static void asc_dbg_update(void)
{
    asc_stats.dbg = ((u32) asc_dbg_claimed << 24) | ((u32) asc_dbg_revive << 16) |
              ((u32) asc_dbg_ifr_or << 8) | asc_dbg_or;
}

// the idea here was to make it so that system sounds still work and the
// emulator can re-setup the ASC after they're done, but i don't think this
// works properly - TODO revisit
static void asc_reassert(void)
{
    ASC_CONTROL = 0;
    ASC_CLOCK = 0;
    ASC_VOLUME = (u8) ((LM_SDVOLUME & 7) << 5);
    ASC_MODE = 1;
    asc_dbg_update();
}

// generate and push each sample twice for the 2:1 rate
static void asc_push_chunk(void)
{
    static unsigned char chunk[ASC_CHUNK];
    signed char mmu;
    int k;

    // reentrancy guard
    asc_in_push = 1;

    // same stall rule as the SM path, for menus etc
    if (!g_audio || (s32) (samples_out - emu_samples) > STALL_SAMPLES) {
        memset(chunk, 0x80, ASC_CHUNK);
    } else {
        gen_samples(chunk, ASC_CHUNK);
    }
    samples_out += ASC_CHUNK;

    mmu = true32b;
    SwapMMUMode(&mmu);

    if (ASC_MODE != 1)
        asc_reassert();

    for (k = 0; k < ASC_CHUNK; k++) {
        u8 b = chunk[k];
        ASC_FIFO_A = b;
        ASC_FIFO_A = b;
    }

    SwapMMUMode(&mmu);

    asc_stats.pushes++;
    asc_last_push_tick = LM_TICKS;
    asc_in_push = 0;
}

// called from the thunk at interrupt level 2 with d0-d3/a0-a3 saved
// handles if it's from the ASC, otherwise chains to the saved handler
static __attribute__((used)) void asc_vec_handler(void)
{
    u8 stat, ifr;
    signed char mmu;

    mmu = true32b;
    SwapMMUMode(&mmu);
    ifr = VIA2_IFR;
    asc_stats.entries++; // every level-2, audio or not
    asc_dbg_ifr_or |= ifr & 0x7f;
    if (!(ifr & VIA2_CB1_BIT)) {
        // not audio
        SwapMMUMode(&mmu);
        asc_vec_claimed = 0;
        asc_dbg_update();
        return;
    }
    VIA2_IFR = VIA2_CB1_BIT;
    stat = ASC_FIFOSTAT; // read clears the chip's IRQ source
    SwapMMUMode(&mmu);
    asc_vec_claimed = 1;
    asc_dbg_claimed++;

    if (stat)
        asc_dbg_or |= stat;
    if (stat == ASC_STAT_A_EMPTY_OR_FULL)
        asc_stats.bit1_only++;
    asc_dbg_update();

    if (!snd_running || asc_in_push)
        return;

    if (stat & ASC_STAT_A_HALF) {
        // ready for more audio
        asc_push_chunk();
    } else if (stat == ASC_STAT_A_EMPTY_OR_FULL &&
               LM_TICKS - asc_last_push_tick >= 2) {
        // empty? use ticks to distinguish between full and empty bc
        // this bit is set for both. this feels like a hack but works well
        asc_push_chunk();
        asc_push_chunk();
        asc_push_chunk();
    }
}

// level 2 interrupt vector entry - save scratch, call C, then rte if claimed
// or chain  to the saved handler for everything else
asm(
    ".text\n"
    ".balign 2\n"
    "asc_thunk_entry:\n"
    "    movem.l %d0-%d3/%a0-%a3,-(%sp)\n"
    "    jsr asc_vec_handler\n"
    "    movem.l (%sp)+,%d0-%d3/%a0-%a3\n"
    "    tst.b asc_vec_claimed\n"
    "    beq.s .Lchain\n"
    "    rte\n"
    ".Lchain:\n"
    "    move.l asc_saved_vec,-(%sp)\n"
    "    rts\n"
);

// check if no pushes in 1/2 second, if so restart the sound
static void asc_watchdog(void)
{
    signed char mmu;

    if (LM_TICKS - asc_watch_t < 30)
        return;
    asc_watch_t = LM_TICKS;

    if (asc_stats.pushes != asc_watch_pushes) {
        asc_watch_pushes = asc_stats.pushes;
        return;
    }

    mmu = true32b;
    SwapMMUMode(&mmu);
    if (ASC_MODE != 1)
        asc_reassert();
    SwapMMUMode(&mmu);

    asc_push_chunk();
    asc_push_chunk();
    asc_push_chunk();
    asc_watch_pushes = asc_stats.pushes;
    asc_dbg_revive++;
    asc_dbg_update();
}

static int asc_start(void)
{
    signed char mmu;
    u16 sr;
    int k;

    if (!asc_hw_present())
        return 0;

    mmu = true32b;
    SwapMMUMode(&mmu);
    sr = ints_off();
    ASC_MODE = 0; // no new FIFO IRQs while we swap the vector
    VIA2_IFR = VIA2_CB1_BIT;
    VIA2_PCR &= (u8) ~VIA2_CB1_BIT; // CB1 negative edge (assert = low)
    asc_saved_ier = VIA2_IER & VIA2_CB1_BIT;
    VIA2_IER = 0x80 | VIA2_CB1_BIT;
    asc_saved_vec = LEVEL2_VECTOR;
    LEVEL2_VECTOR = (u32) asc_thunk_entry;
    asc_setup_regs();
    ints_restore(sr);
    SwapMMUMode(&mmu);

    memset(&asc_stats, 0, sizeof(asc_stats));
    asc_dbg_or = 0;
    asc_dbg_ifr_or = 0;
    asc_dbg_revive = 0;
    asc_dbg_claimed = 0;

    *(volatile u32 *) 0xa78 = (u32) &asc_stats;

    asc_watch_t = LM_TICKS;
    asc_watch_pushes = 0;
    // fill to 768 bytes, then the interrupts take over
    for (k = 0; k < 3; k++)
        asc_push_chunk();
    return 1;
}

static void asc_stop(void)
{
    signed char mmu;
    u16 sr;

    if (!asc_hw_present())
        return;

    mmu = true32b;
    SwapMMUMode(&mmu);
    sr = ints_off();
    ASC_MODE = 0;
    ASC_FIFOMODE = 0x80;
    ASC_FIFOMODE = 0;
    VIA2_IFR = VIA2_CB1_BIT;
    if (!asc_saved_ier)
        VIA2_IER = VIA2_CB1_BIT; // SM had CB1 disabled; put it back
    LEVEL2_VECTOR = asc_saved_vec;
    ints_restore(sr);
    SwapMMUMode(&mmu);
}

// called at interrupt time when a buffer is exhausted
static pascal void DoubleBackProc(SndChannelPtr chan, SndDoubleBufferPtr buf)
{
    if (!g_audio || (s32) (samples_out - emu_samples) > STALL_SAMPLES) {
        memset(buf->dbSoundData, 0x80, BUFFER_SAMPLES);
    } else {
        gen_samples(buf->dbSoundData, BUFFER_SAMPLES);
    }

    samples_out += BUFFER_SAMPLES;
    buf->dbNumFrames = BUFFER_SAMPLES;
    buf->dbFlags |= dbBufferReady;
}

int audio_mac_init(struct audio *audio)
{
    OSErr err;
    int k;

    if (!audio_mac_available()) {
        return 0;
    }

    if (audio_inited) {
        return 1;
    }

    g_audio = audio;

    use_direct = asc_direct_supported();
    if (use_direct) {
        audio_inited = 1;
        return 1;
    }

    long init_opts = initMono | initNoInterp;

    snd_channel = NULL;
    err = SndNewChannel(&snd_channel, sampledSynth, init_opts, NULL);
    if (err != noErr) {
        return 0;
    }

    // initialize double buffers
    for (k = 0; k < 2; k++) {
        memset(&dbl_buffers[k], 0, sizeof(dbl_buffers[k]));
        memset(dbl_buffers[k].dbSoundData, 0x80, BUFFER_SAMPLES);
        dbl_buffers[k].dbNumFrames = BUFFER_SAMPLES;
        dbl_buffers[k].dbFlags = dbBufferReady;
    }

    // initialize double buffer header
    memset(&dbl_header, 0, sizeof(dbl_header));
    dbl_header.dbhNumChannels = 1;
    dbl_header.dbhSampleSize = 8;
    dbl_header.dbhCompressionID = 0;
    dbl_header.dbhPacketSize = 0;
    dbl_header.dbhSampleRate = SAMPLE_RATE_FIXED;
    dbl_header.dbhBufferPtr[0] = (SndDoubleBufferPtr)&dbl_buffers[0];
    dbl_header.dbhBufferPtr[1] = (SndDoubleBufferPtr)&dbl_buffers[1];
    dbl_header.dbhDoubleBack = (SndDoubleBackProcPtr)DoubleBackProc;

    audio_inited = 1;

    return 1;
}

void audio_mac_start(void)
{
    int k;

    if (!audio_inited)
        return;
    if (!use_direct && !snd_channel)
        return;

    cycle_accum = 0;
    emu_samples = 0;
    samples_out = 0;
    evq_read = 0;
    evq_write = 0;

    // pre-fill both buffers with silence
    for (k = 0; k < 2; k++) {
        memset(dbl_buffers[k].dbSoundData, 0x80, BUFFER_SAMPLES);
        dbl_buffers[k].dbNumFrames = BUFFER_SAMPLES;
        dbl_buffers[k].dbFlags = dbBufferReady;
    }

    if (use_direct) {
        snd_running = asc_start();
        return;
    }

#if AUDIO_BENCH_MUTE
    snd_running = 1;
    bench_t0 = LM_TICKS;
#else
    snd_running = 1;
    SndPlayDoubleBuffer(snd_channel, &dbl_header);
#endif
}

void audio_mac_stop(void)
{
    SndCommand cmd;

    if (use_direct) {
        if (!snd_running)
            return;

        snd_running = 0;
        asc_stop();
        return;
    }

    if (!snd_channel)
        return;

    snd_running = 0;

    cmd.cmd = quietCmd;
    cmd.param1 = 0;
    cmd.param2 = 0;
    SndDoImmediate(snd_channel, &cmd);

    cmd.cmd = flushCmd;
    SndDoImmediate(snd_channel, &cmd);
}

void audio_mac_wait_if_ahead(void)
{
    if (!snd_running)
        return;

    while ((s32) (emu_samples - samples_out) > MAX_LEAD_SAMPLES) {
#if AUDIO_BENCH_MUTE
        bench_drain();
#endif
    }
}

void audio_mac_shutdown(void)
{
    audio_mac_stop();

    if (snd_channel) {
        SndDisposeChannel(snd_channel, true);
        snd_channel = NULL;
    }

    g_audio = NULL;
    audio_inited = 0;
}
