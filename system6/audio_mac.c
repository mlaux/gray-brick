/* Game Boy emulator for 68k Macs
   audio_mac.c - Sound Manager integration using SndPlayDoubleBuffer */

// audio samples are generated synchronized to GB execution via audio_mac_sync(),
// which is called from dmg_sync_hw. Samples accumulate in a ring buffer. the
// Sound Manager callback reads from the ring buffer at interrupt time

#include <Sound.h>
#include <Memory.h>
#include <Gestalt.h>
#include <string.h>

#include "audio_mac.h"
#include "../src/audio.h"
#include "../src/prof.h"

// 1 frame worth of samples at 11127 Hz / 60 fps
#define BUFFER_SAMPLES 512
#define SAMPLE_RATE_FIXED 0x2b7745d1

// ring buffer - must be power of 2 for masking
#define RING_SIZE 1024
#define RING_MASK (RING_SIZE - 1)

static unsigned char ring_buffer[RING_SIZE];
static volatile int ring_write;  // main loop writes here
static volatile int ring_read;   // interrupt reads here

// cycles per sample: 4194304 / 11127 = about 377
#define CYCLES_PER_SAMPLE 377

static int cycle_accum;

static SndChannelPtr snd_channel;
static struct audio *g_audio;
static int audio_inited;

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

// called from main loop (dmg_sync_hw) to generate samples into ring buffer
void audio_mac_sync(int cycles)
{
    int samples_needed, avail, chunk;

    if (!g_audio || !audio_inited)
        return;

    cycle_accum += cycles;
    samples_needed = cycle_accum / CYCLES_PER_SAMPLE;

    if (samples_needed == 0)
        return;

    cycle_accum -= samples_needed * CYCLES_PER_SAMPLE;

    // check available space in ring buffer
    avail = (ring_read - ring_write - 1) & RING_MASK;
    if (samples_needed > avail)
        samples_needed = avail; // drop excess rather than overwrite

    if (samples_needed == 0)
        return;

    // generate in up to 2 chunks to handle wrap-around
    chunk = RING_SIZE - ring_write;
    if (chunk > samples_needed)
        chunk = samples_needed;

    PROF_SET(PROF_AUDIO);
    audio_generate(g_audio, &ring_buffer[ring_write], chunk);
    ring_write = (ring_write + chunk) & RING_MASK;

    if (chunk < samples_needed) {
        audio_generate(g_audio, ring_buffer, samples_needed - chunk);
        ring_write = samples_needed - chunk;
    }
    PROF_SET(PROF_SYNC);
}

// read samples from ring buffer into Sound Manager buffer (interrupt time)
static void FillBuffer(unsigned char *p)
{
    int avail, k;

    for (k = 0; k < BUFFER_SAMPLES; k++) {
        avail = (ring_write - ring_read) & RING_MASK;
        if (avail > 0) {
            p[k] = ring_buffer[ring_read];
            ring_read = (ring_read + 1) & RING_MASK;
        } else {
            // underrun - output silence
            p[k] = 0x80;
        }
    }
}

// called at interrupt time when a buffer is exhausted
static pascal void DoubleBackProc(SndChannelPtr chan, SndDoubleBufferPtr buf)
{
    // u16 t0, t1, delta;

    // t0 = read_via_t1();
    FillBuffer(buf->dbSoundData);
    buf->dbNumFrames = BUFFER_SAMPLES;
    buf->dbFlags |= dbBufferReady;
    // t1 = read_via_t1();
    // if (t0 >= t1) {
    //     delta = t0 - t1;
    // } else {
    //     // wrapped - t0 was near 0, t1 is near latch value after reload
    //     // latch is roughly 13050 for 60Hz tick rate
    //     delta = t0 + (13050 - t1);
    // }
    // audio_timer_accum += delta;
    // audio_calls++;
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
    if (!audio_inited || !snd_channel)
        return;

    // reset ring buffer state
    ring_read = 0;
    ring_write = 0;
    cycle_accum = 0;

    // pre-fill both buffers with silence (ring buffer is empty at start)
    memset(dbl_buffers[0].dbSoundData, 0x80, BUFFER_SAMPLES);
    memset(dbl_buffers[1].dbSoundData, 0x80, BUFFER_SAMPLES);

    SndPlayDoubleBuffer(snd_channel, &dbl_header);
}

void audio_mac_stop(void)
{
    SndCommand cmd;

    if (!snd_channel)
        return;

    cmd.cmd = quietCmd;
    cmd.param1 = 0;
    cmd.param2 = 0;
    SndDoImmediate(snd_channel, &cmd);

    cmd.cmd = flushCmd;
    SndDoImmediate(snd_channel, &cmd);
}

void audio_mac_wait_if_ahead(void)
{
    int fill;

    if (!audio_inited)
        return;

    // wait while buffer is more than 3/4 full
    while (1) {
        fill = (ring_write - ring_read) & RING_MASK;
        if (fill < RING_SIZE * 3 / 4)
            break;
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
