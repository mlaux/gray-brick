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
#include <string.h>

#include "audio_mac.h"
#include "../src/audio.h"
#include "../src/prof.h"

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

// APU register writes waiting to be applied during generation.
// main loop produces, interrupt gets
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

typedef struct {
    long dbNumFrames;
    long dbFlags;
    long dbUserInfo[2];
    unsigned char dbSoundData[BUFFER_SAMPLES];
} MyDoubleBuffer;

static SndDoubleBufferHeader dbl_header;
static MyDoubleBuffer dbl_buffers[2];

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
static void gen_samples(unsigned char *p)
{
    int done = 0;

    while (done < BUFFER_SAMPLES) {
        int n = BUFFER_SAMPLES - done;

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

// called at interrupt time when a buffer is exhausted
static pascal void DoubleBackProc(SndChannelPtr chan, SndDoubleBufferPtr buf)
{
#ifdef GB6_PROFILING
    unsigned char prev_phase = prof_phase;
    PROF_SET(PROF_AUDIO);
#endif

    if (!g_audio || (s32) (samples_out - emu_samples) > STALL_SAMPLES) {
        memset(buf->dbSoundData, 0x80, BUFFER_SAMPLES);
    } else {
        gen_samples(buf->dbSoundData);
    }

    samples_out += BUFFER_SAMPLES;
    buf->dbNumFrames = BUFFER_SAMPLES;
    buf->dbFlags |= dbBufferReady;

#ifdef GB6_PROFILING
    PROF_SET(prev_phase);
#endif
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

    snd_channel = NULL;
    err = SndNewChannel(&snd_channel, sampledSynth, initMono, NULL);
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

    if (!audio_inited || !snd_channel)
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

    snd_running = 1;
    SndPlayDoubleBuffer(snd_channel, &dbl_header);
}

void audio_mac_stop(void)
{
    SndCommand cmd;

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

    while ((s32) (emu_samples - samples_out) > MAX_LEAD_SAMPLES)
        ;
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
