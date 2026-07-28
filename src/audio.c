#include <string.h>
#include "audio.h"

// sampling is done at 11127.27 Hz which is half of the Mac's native sample
// rate. this is so that it can just play every sample twice instead of
// rescaling. whether the Sound Manager actually implements this optimization,
// i do not know, but it probably does.

// (131072 * 65536) / (divisor * 11127.27)
#define PHASE_INC_SQUARE 771971
// (65536 * 65536) / (divisor * 11127.27)
#define PHASE_INC_WAVE 385986
// (4194304 * 65536) / (divisor * 11127.27)
#define PHASE_INC_NOISE 24703086

// DAC enable bits for each channel:
//   CH1: NR12 (0xff12) bits 3-7 nonzero
//   CH2: NR22 (0xff17) bits 3-7 nonzero
//   CH3: NR30 (0xff1a) bit 7
//   CH4: NR42 (0xff21) bits 3-7 nonzero
static const u8 dac_reg_off[4] = { 0x02, 0x07, 0x0a, 0x11 };
static const u8 dac_mask[4]    = { 0xf8, 0xf8, 0x80, 0xf8 };

static int dac_on(struct audio *audio, int ch)
{
    return (audio->regs[dac_reg_off[ch]] & dac_mask[ch]) != 0;
}

// for regular square waves...
// static const u8 duty_table[4] = {
//     0x01, // 00000001
//     0x03, // 00000011
//     0x0f, // 00001111
//     0xfc, // 11111100
// };

// for noise channel
static const u8 divisor_table[8] = { 8, 16, 32, 48, 64, 80, 96, 112 };

// precomputed LFSR output, one byte per step so playback is a plain load
static u8 lfsr15_bytes[32768]; // 15-bit mode, period 32767
static u8 lfsr7_bytes[128]; // 7-bit mode, period 127

// precomputed bandlimited square wave tables. this sounds better than the
// basic square wave, but still not great because of the low sample rate.
// premultiplied by volume so playback is one table read per sample
// [volume 0-15][duty 0-3][band 0-3][sample 0-31]
// band 0: divisor >= 512 (< 256 Hz), 21 harmonics
// band 1: divisor 256-511 (256-512 Hz), 11 harmonics
// band 2: divisor 128-255 (512-1024 Hz), 5 harmonics
// band 3: divisor < 128 (> 1024 Hz), 3 harmonics
#define BL_TABLE_SIZE 32
// square/wave phase accumulators are pre-scaled by 32 so the sample index
// us in bits 16-20, which gcc extracts with swap
#define BL_TABLE_SHIFT 16
#include "bl_tables.h"

static void update_bl_table(struct audio_channel *ch)
{
    ch->bl_table = bl_square_vol[ch->volume][ch->duty][ch->band];
}

static void update_wave_table(struct audio *audio)
{
    int k;
    int shift = audio->ch3.volume;

    if (shift == 0) {
        memset(audio->wave_tab, 0, sizeof(audio->wave_tab));
        return;
    }
    shift--; // now 0=100%, 1=50%, 2=25%

    for (k = 0; k < 32; k++) {
        int nibble = audio->wave_ram[k >> 1];
        if (k & 1)
            nibble &= 0x0f;
        else
            nibble = (nibble >> 4) & 0x0f;
        audio->wave_tab[k] = (s8) ((nibble - 8) >> shift);
    }
}

static void update_mix_table(struct audio *audio)
{
    int k;
    u8 master = ((audio->master_vol_left + audio->master_vol_right) >> 1) + 1;

    if (master == audio->mixtab_master)
        return;
    audio->mixtab_master = master;

    // >>3 would be "most correct" in terms of keeping the original scale
    // but this scales to -106 - 104 to make it louder
    for (k = 0; k < 256; k++) {
        s16 v = (s16) ((k - 128) * master) >> 2;
        audio->mixtab[k] = (u8) (v + 128);
    }
}

static void update_phase_inc(struct audio_channel *ch, int base)
{
    u32 freq_reg = ch->freq_reg;
    if (freq_reg >= 2048)
        freq_reg = 2047;

    u32 divisor = 2048 - freq_reg;
    if (divisor == 0)
        divisor = 1;

    ch->phase_inc = (base / divisor) << 5;

    // compute band from divisor
    // divisor >> 7: 0 = >1024Hz, 1 = 512-1024Hz, 2-3 = 256-512Hz, 4+ = <256Hz
    int d7 = divisor >> 7;
    if (d7 >= 4) {
        ch->band = 0;
    }
    else if (d7 >= 2) {
        ch->band = 1;
    } else if (d7 >= 1) {
        ch->band = 2;
    } else {
        ch->band = 3;
    }

    update_bl_table(ch);
}

static void update_phase_inc_noise(struct audio *audio)
{
    u32 divisor = divisor_table[audio->noise_divisor];
    if (audio->noise_shift < 14)
        divisor <<= audio->noise_shift;

    audio->ch4.phase_inc = 0;
    if (divisor > 0) {
        audio->ch4.phase_inc = PHASE_INC_NOISE / divisor;
    }
}

static void trigger_non_wave(struct audio_channel *ch, int dac)
{
    if (dac)
        ch->enabled = 1;
    ch->phase = 0;
    ch->volume = ch->env_initial;
    ch->env_timer = ch->env_pace;
    update_bl_table(ch);
}

static void step_envelope(struct audio_channel *ch)
{
    if (ch->env_pace == 0) {
        return;
    }

    if (ch->env_timer > 0) {
        ch->env_timer--;
    }

    if (ch->env_timer == 0) {
        ch->env_timer = ch->env_pace;

        if (ch->env_dir) {
            // increase
            if (ch->volume < 15) {
                ch->volume++;
            }
        } else {
            // decrease
            if (ch->volume > 0) {
                ch->volume--;
            }
        }
        update_bl_table(ch);
    }
}

static void step_sweep(struct audio *audio)
{
    struct audio_channel *ch = &audio->ch1;

    if (ch->sweep_pace == 0)
        return;

    if (ch->sweep_timer > 0)
        ch->sweep_timer--;

    if (ch->sweep_timer == 0) {
        ch->sweep_timer = ch->sweep_pace;

        if (ch->sweep_step > 0) {
            u16 delta = ch->sweep_freq >> ch->sweep_step;
            u16 new_freq;

            if (ch->sweep_dir) {
                // decrease frequency
                new_freq = ch->sweep_freq - delta;
            } else {
                // increase frequency
                new_freq = ch->sweep_freq + delta;
                // overflow check - disable channel if > 2047
                if (new_freq > 2047) {
                    ch->enabled = 0;
                    return;
                }
            }

            ch->sweep_freq = new_freq;
            ch->freq_reg = new_freq;
            update_phase_inc(ch, PHASE_INC_SQUARE);
        }
    }
}

static void step_length(struct audio_channel *ch, u16 max_length)
{
    if (!ch->length_enable)
        return;

    ch->length_counter++;
    if (ch->length_counter >= max_length)
        ch->enabled = 0;
}

void audio_init(struct audio *audio)
{
    int k;
    u16 lfsr;

    memset(audio, 0, sizeof(*audio));

    update_bl_table(&audio->ch1);
    update_bl_table(&audio->ch2);
    update_bl_table(&audio->ch3);
    update_bl_table(&audio->ch4);

    // generate 15-bit LFSR output table (period 32767)
    lfsr = 0x7fff;
    for (k = 0; k < 32767; k++) {
        lfsr15_bytes[k] = lfsr & 1;
        int xor_bit = (lfsr ^ (lfsr >> 1)) & 1;
        lfsr = (lfsr >> 1) | (xor_bit << 14);
    }

    // generate 7-bit LFSR output table (period 127)
    lfsr = 0x7f;
    for (k = 0; k < 127; k++) {
        lfsr7_bytes[k] = lfsr & 1;
        int xor_bit = (lfsr ^ (lfsr >> 1)) & 1;
        lfsr = (lfsr >> 1) | (xor_bit << 6);
    }
}

void audio_write(struct audio *audio, u16 addr, u8 value)
{
    if (addr < 0xff10 || addr > 0xff3f)
        return;

    int reg = addr - 0xff10;
    audio->regs[reg] = value;

    // wave RAM
    if (addr >= REG_WAVE_START && addr <= REG_WAVE_END) {
        audio->wave_ram[addr - REG_WAVE_START] = value;
        update_wave_table(audio);
        return;
    }

    switch (addr) {
    // CH1 - square with sweep
    case 0xff10:    // NR10 - sweep
        audio->ch1.sweep_pace = (value >> 4) & 0x07;
        audio->ch1.sweep_dir = (value >> 3) & 0x01;
        audio->ch1.sweep_step = value & 0x07;
        break;
    case 0xff11:    // NR11 - duty/length
        audio->ch1.duty = (value >> 6) & 0x03;
        audio->ch1.length_counter = value & 0x3f;
        update_bl_table(&audio->ch1);
        break;
    case 0xff12:    // NR12 - envelope
        audio->ch1.env_initial = (value >> 4) & 0x0f;
        audio->ch1.env_dir = (value >> 3) & 0x01;
        audio->ch1.env_pace = value & 0x07;
        if (!dac_on(audio, 0))
            audio->ch1.enabled = 0;
        break;
    case 0xff13:    // NR13 - freq low
        audio->ch1.freq_reg = (audio->ch1.freq_reg & 0x700) | value;
        update_phase_inc(&audio->ch1, PHASE_INC_SQUARE);
        break;
    case 0xff14:    // NR14 - freq high + trigger
        audio->ch1.freq_reg = (audio->ch1.freq_reg & 0xff) | ((value & 0x07) << 8);
        audio->ch1.length_enable = (value >> 6) & 0x01;
        update_phase_inc(&audio->ch1, PHASE_INC_SQUARE);
        if (value & 0x80) {
            trigger_non_wave(&audio->ch1, dac_on(audio, 0));
            // initialize sweep shadow frequency
            audio->ch1.sweep_freq = audio->ch1.freq_reg;
            audio->ch1.sweep_timer = audio->ch1.sweep_pace;
            // reset length if expired
            if (audio->ch1.length_counter >= 64)
                audio->ch1.length_counter = 0;
        }
        break;

    // CH2 - square (no sweep)
    case 0xff16:    // NR21 - duty/length
        audio->ch2.duty = (value >> 6) & 0x03;
        audio->ch2.length_counter = value & 0x3f;
        update_bl_table(&audio->ch2);
        break;
    case 0xff17:    // NR22 - envelope
        audio->ch2.env_initial = (value >> 4) & 0x0f;
        audio->ch2.env_dir = (value >> 3) & 0x01;
        audio->ch2.env_pace = value & 0x07;
        if (!dac_on(audio, 1))
            audio->ch2.enabled = 0;
        break;
    case 0xff18:    // NR23 - freq low
        audio->ch2.freq_reg = (audio->ch2.freq_reg & 0x700) | value;
        update_phase_inc(&audio->ch2, PHASE_INC_SQUARE);
        break;
    case 0xff19:    // NR24 - freq high + trigger
        audio->ch2.freq_reg = (audio->ch2.freq_reg & 0xff) | ((value & 0x07) << 8);
        audio->ch2.length_enable = (value >> 6) & 0x01;
        update_phase_inc(&audio->ch2, PHASE_INC_SQUARE);
        if (value & 0x80) {
            trigger_non_wave(&audio->ch2, dac_on(audio, 1));
            if (audio->ch2.length_counter >= 64)
                audio->ch2.length_counter = 0;
        }
        break;

    // CH3 - wave
    case 0xff1a:    // NR30 - DAC enable
        // DAC off force-disables the channel, on doesn't force-enable it
        if (!dac_on(audio, 2))
            audio->ch3.enabled = 0;
        break;
    case 0xff1b:    // NR31 - length
        audio->ch3.length_counter = value;
        break;
    case 0xff1c:    // NR32 - volume
        // volume code: 0=mute, 1=100%, 2=50%, 3=25%
        audio->ch3.volume = (value >> 5) & 0x03;
        update_wave_table(audio);
        break;
    case 0xff1d:    // NR33 - freq low
        audio->ch3.freq_reg = (audio->ch3.freq_reg & 0x700) | value;
        update_phase_inc(&audio->ch3, PHASE_INC_WAVE);
        break;
    case 0xff1e:    // NR34 - freq high + trigger
        audio->ch3.freq_reg = (audio->ch3.freq_reg & 0xff) | ((value & 0x07) << 8);
        audio->ch3.length_enable = (value >> 6) & 0x01;
        update_phase_inc(&audio->ch3, PHASE_INC_WAVE);
        if (value & 0x80) {
            if (dac_on(audio, 2))
                audio->ch3.enabled = 1;
            audio->ch3.phase = 0;
            // wave doesn't use envelope
            if (audio->ch3.length_counter >= 256)
                audio->ch3.length_counter = 0;
        }
        break;

    // CH4 - noise
    case 0xff20:    // NR41 - length
        audio->ch4.length_counter = value & 0x3f;
        break;
    case 0xff21:    // NR42 - envelope
        audio->ch4.env_initial = (value >> 4) & 0x0f;
        audio->ch4.env_dir = (value >> 3) & 0x01;
        audio->ch4.env_pace = value & 0x07;
        if (!dac_on(audio, 3))
            audio->ch4.enabled = 0;
        break;
    case 0xff22:    // NR43 - noise params
        audio->noise_shift = (value >> 4) & 0x0f;
        audio->lfsr_width = (value >> 3) & 0x01;
        audio->noise_divisor = value & 0x07;
        update_phase_inc_noise(audio);
        break;
    case 0xff23:    // NR44 - trigger
        audio->ch4.length_enable = (value >> 6) & 0x01;
        if (value & 0x80) {
            trigger_non_wave(&audio->ch4, dac_on(audio, 3));
            if (audio->ch4.length_counter >= 64)
                audio->ch4.length_counter = 0;
        }
        break;

    // master control
    case 0xff24:    // NR50 - master volume
        audio->master_vol_left = (value >> 4) & 0x07;
        audio->master_vol_right = value & 0x07;
        break;
    case 0xff25:    // NR51 - panning
        audio->panning = value;
        break;
    case 0xff26:    // NR52 - master enable
        audio->master_enable = (value & 0x80) ? 1 : 0;
        if (!audio->master_enable) {
            // disable all channels when master is off
            audio->ch1.enabled = 0;
            audio->ch2.enabled = 0;
            audio->ch3.enabled = 0;
            audio->ch4.enabled = 0;
        }
        break;
    }
}

u8 audio_read(struct audio *audio, u16 addr)
{
    if (addr < 0xff10 || addr > 0xff3f)
        return 0xff;

    // NR52 returns channel status in low bits
    if (addr == 0xff26) {
        u8 status = audio->master_enable ? 0x80 : 0;
        status |= audio->ch1.enabled ? 0x01 : 0;
        status |= audio->ch2.enabled ? 0x02 : 0;
        status |= audio->ch3.enabled ? 0x04 : 0;
        status |= audio->ch4.enabled ? 0x08 : 0;
        return status | 0x70;   // bits 4-6 always read as 1
    }

    if (addr >= REG_WAVE_START && addr <= REG_WAVE_END)
        return audio->wave_ram[addr - REG_WAVE_START];

    return audio->regs[addr - 0xff10];
}

static void advance_phase(struct audio_channel *ch, int n)
{
    ch->phase += ch->phase_inc * (u32) n;
}

// generate one run of samples where envelope/sweep/length don't change
static void render_run(struct audio *audio, u8 *buffer, int n, u8 audible)
{
    int k;
    u8 active = audible;
    const u8 *mt = audio->mixtab + 128;
    const s8 *t1, *t2, *t3;
    const u8 *nz;
    u32 ph1, ph2, ph3, ph4, inc1, inc2, inc3, inc4, nmask;
    s8 pm[2];

    if (!audio->ch1.enabled || audio->ch1.volume == 0)
        active &= ~0x01;
    if (!audio->ch2.enabled || audio->ch2.volume == 0)
        active &= ~0x02;
    if (!audio->ch3.enabled || audio->ch3.volume == 0)
        active &= ~0x04;
    if (!audio->ch4.enabled || audio->ch4.volume == 0)
        active &= ~0x08;

    // inactive channels still advance
    if (!(active & 0x01))
        advance_phase(&audio->ch1, n);
    if (!(active & 0x02))
        advance_phase(&audio->ch2, n);
    if (!(active & 0x04))
        advance_phase(&audio->ch3, n);
    if (!(active & 0x08))
        advance_phase(&audio->ch4, n);

    if (active == 0) {
        memset(buffer, mt[0], n);
        return;
    }

    t1 = audio->ch1.bl_table;
    t2 = audio->ch2.bl_table;
    t3 = audio->wave_tab;
    ph1 = audio->ch1.phase;
    ph2 = audio->ch2.phase;
    ph3 = audio->ch3.phase;
    ph4 = audio->ch4.phase;
    inc1 = audio->ch1.phase_inc;
    inc2 = audio->ch2.phase_inc;
    inc3 = audio->ch3.phase_inc;
    inc4 = audio->ch4.phase_inc;

    pm[0] = -(s8) audio->ch4.volume;
    pm[1] = audio->ch4.volume;
    if (audio->lfsr_width) {
        nz = lfsr7_bytes;
        nmask = 0x7f;
    } else {
        nz = lfsr15_bytes;
        nmask = 0x7fff;
    }

    for (k = 0; k < n; k++) {
        int mix = 0;

        if (active & 0x01) {
            mix = t1[(ph1 >> BL_TABLE_SHIFT) & (BL_TABLE_SIZE - 1)];
            ph1 += inc1;
        }
        if (active & 0x02) {
            mix += t2[(ph2 >> BL_TABLE_SHIFT) & (BL_TABLE_SIZE - 1)];
            ph2 += inc2;
        }
        if (active & 0x04) {
            mix += t3[(ph3 >> BL_TABLE_SHIFT) & (BL_TABLE_SIZE - 1)];
            ph3 += inc3;
        }
        // noise phase is not pre-scaled, it needs bits 16-30
        if (active & 0x08) {
            mix += pm[nz[(ph4 >> 16) & nmask]];
            ph4 += inc4;
        }

        buffer[k] = mt[mix];
    }

    if (active & 0x01)
        audio->ch1.phase = ph1;
    if (active & 0x02)
        audio->ch2.phase = ph2;
    if (active & 0x04)
        audio->ch3.phase = ph3;
    if (active & 0x08)
        audio->ch4.phase = ph4;
}

void audio_generate(struct audio *audio, u8 *buffer, int samples)
{
    int done, n, d;
    u8 pan, audible;

    if (!audio->master_enable) {
        memset(buffer, 0x80, samples);
        return;
    }

    update_mix_table(audio);

    // for mono, i'll make a channel audible if it's panned to either side
    pan = audio->panning;
    audible = pan | (pan >> 4);

    done = 0;
    while (done < samples) {
        // run until the next envelope (64 Hz = 174 samples), sweep (128 Hz
        // = 87) or length (256 Hz = 43) tick, whichever comes first
        n = samples - done;
        d = 174 - audio->env_counter;
        if (d < n)
            n = d;
        d = 87 - audio->sweep_counter;
        if (d < n)
            n = d;
        d = 43 - audio->length_counter;
        if (d < n)
            n = d;

        render_run(audio, buffer + done, n, audible);
        done += n;

        audio->env_counter += n;
        if (audio->env_counter >= 174) {
            audio->env_counter = 0;
            step_envelope(&audio->ch1);
            step_envelope(&audio->ch2);
            step_envelope(&audio->ch4);
        }

        audio->sweep_counter += n;
        if (audio->sweep_counter >= 87) {
            audio->sweep_counter = 0;
            step_sweep(audio);
        }

        audio->length_counter += n;
        if (audio->length_counter >= 43) {
            audio->length_counter = 0;
            step_length(&audio->ch1, 64);
            step_length(&audio->ch2, 64);
            step_length(&audio->ch3, 256);
            step_length(&audio->ch4, 64);
        }
    }
}
