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

static const u8 duty_table[4] = {
    0x01, // 00000001
    0x03, // 00000011
    0x0f, // 00001111
    0xfc, // 11111100
};

// for noise channel
static const u8 divisor_table[8] = { 8, 16, 32, 48, 64, 80, 96, 112 };

// precomputed LFSR output bits
static u8 lfsr15_bits[4096]; // 32768 bits for 15-bit mode
static u8 lfsr7_bits[16]; // 128 bits for 7-bit mode

// precomputed bandlimited square wave tables. this sounds better than the
// basic square wave, but still not great because of the low sample rate.
// [duty 0-3][band 0-3][sample 0-31]
// band 0: divisor >= 512 (< 256 Hz), 21 harmonics
// band 1: divisor 256-511 (256-512 Hz), 11 harmonics
// band 2: divisor 128-255 (512-1024 Hz), 5 harmonics
// band 3: divisor < 128 (> 1024 Hz), 3 harmonics
#define BL_TABLE_SIZE 32
#define BL_TABLE_SHIFT 11  // phase >> 11 gives 0-31 index
static const s8 bl_square[4][4][BL_TABLE_SIZE] = {
    { // duty 0 (12.5%)
        {   0,  4, 15,  6,  5,  5,  3,  3,  4,  3,  3,  5,  5,  6, 15,  4,
            0, -4,-15, -6, -5, -5, -3, -3, -4, -3, -3, -5, -5, -6,-15, -4 },
        {   0,  7, 15, 11,  5,  6,  5,  4,  5,  4,  5,  6,  5, 11, 15,  7,
            0, -7,-15,-11, -5, -6, -5, -4, -5, -4, -5, -6, -5,-11,-15, -7 },
        {   0, 10, 15, 14,  9,  5,  4,  6,  6,  6,  4,  5,  9, 14, 15, 10,
            0,-10,-15,-14, -9, -5, -4, -6, -6, -6, -4, -5, -9,-14,-15,-10 },
        {   0,  9, 15, 15, 15, 13,  8,  4,  3,  4,  8, 13, 15, 15, 15,  9,
            0, -9,-15,-15,-15,-13, -8, -4, -3, -4, -8,-13,-15,-15,-15, -9 }
    },
    { // duty 1 (25.0%)
        {   0,  2,  4,  7, 15,  8,  7,  6,  6,  6,  7,  8, 15,  7,  4,  2,
            0, -2, -4, -7,-15, -8, -7, -6, -6, -6, -7, -8,-15, -7, -4, -2 },
        {   0,  3,  4, 10, 15, 11,  8,  7,  7,  7,  8, 11, 15, 10,  4,  3,
            0, -3, -4,-10,-15,-11, -8, -7, -7, -7, -8,-11,-15,-10, -4, -3 },
        {   0,  3,  7, 12, 15, 15, 12,  8,  6,  8, 12, 15, 15, 12,  7,  3,
            0, -3, -7,-12,-15,-15,-12, -8, -6, -8,-12,-15,-15,-12, -7, -3 },
        {   0,  6, 11, 14, 15, 14, 13, 11, 11, 11, 13, 14, 15, 14, 11,  6,
            0, -6,-11,-14,-15,-14,-13,-11,-11,-11,-13,-14,-15,-14,-11, -6 }
    },
    { // duty 2 (50.0%)
        {   0,  1,  2,  2,  3,  4,  6,  7, 15,  7,  6,  4,  3,  2,  2,  1,
            0, -1, -2, -2, -3, -4, -6, -7,-15, -7, -6, -4, -3, -2, -2, -1 },
        {   0,  1,  2,  2,  4,  5,  6, 11, 15, 11,  6,  5,  4,  2,  2,  1,
            0, -1, -2, -2, -4, -5, -6,-11,-15,-11, -6, -5, -4, -2, -2, -1 },
        {   0,  2,  3,  3,  3,  6, 10, 13, 15, 13, 10,  6,  3,  3,  3,  2,
            0, -2, -3, -3, -3, -6,-10,-13,-15,-13,-10, -6, -3, -3, -3, -2 },
        {   0,  0,  1,  3,  5,  9, 12, 14, 15, 14, 12,  9,  5,  3,  1,  0,
            0,  0, -1, -3, -5, -9,-12,-14,-15,-14,-12, -9, -5, -3, -1,  0 }
    },
    { // duty 3 (75.0%)
        {   0,  2,  4,  7, 15,  8,  7,  6,  6,  6,  7,  8, 15,  7,  4,  2,
            0, -2, -4, -7,-15, -8, -7, -6, -6, -6, -7, -8,-15, -7, -4, -2 },
        {   0,  3,  4, 10, 15, 11,  8,  7,  7,  7,  8, 11, 15, 10,  4,  3,
            0, -3, -4,-10,-15,-11, -8, -7, -7, -7, -8,-11,-15,-10, -4, -3 },
        {   0,  3,  7, 12, 15, 15, 12,  8,  6,  8, 12, 15, 15, 12,  7,  3,
            0, -3, -7,-12,-15,-15,-12, -8, -6, -8,-12,-15,-15,-12, -7, -3 },
        {   0,  6, 11, 14, 15, 14, 13, 11, 11, 11, 13, 14, 15, 14, 11,  6,
            0, -6,-11,-14,-15,-14,-13,-11,-11,-11,-13,-14,-15,-14,-11, -6 }
    }
};

static void update_phase_inc(struct audio_channel *ch, int base)
{
    u32 freq_reg = ch->freq_reg;
    if (freq_reg >= 2048)
        freq_reg = 2047;

    u32 divisor = 2048 - freq_reg;
    if (divisor == 0)
        divisor = 1;

    ch->phase_inc = base / divisor;

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

static void trigger_non_wave(struct audio_channel *ch)
{
    // only enable if DAC is on (env_initial > 0 OR env_dir is increase)
    if (ch->env_initial != 0 || ch->env_dir != 0)
        ch->enabled = 1;
    ch->phase = 0;
    ch->volume = ch->env_initial;
    ch->env_timer = ch->env_pace;
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

    // generate 15-bit LFSR output table (period 32767)
    memset(lfsr15_bits, 0, sizeof(lfsr15_bits));
    lfsr = 0x7fff;
    for (k = 0; k < 32767; k++) {
        if (lfsr & 1)
            lfsr15_bits[k >> 3] |= (1 << (k & 7));
        int xor_bit = (lfsr ^ (lfsr >> 1)) & 1;
        lfsr = (lfsr >> 1) | (xor_bit << 14);
    }

    // generate 7-bit LFSR output table (period 127)
    memset(lfsr7_bits, 0, sizeof(lfsr7_bits));
    lfsr = 0x7f;
    for (k = 0; k < 127; k++) {
        if (lfsr & 1)
            lfsr7_bits[k >> 3] |= (1 << (k & 7));
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
        break;
    case 0xff12:    // NR12 - envelope
        audio->ch1.env_initial = (value >> 4) & 0x0f;
        audio->ch1.env_dir = (value >> 3) & 0x01;
        audio->ch1.env_pace = value & 0x07;
        // DAC disable: if upper 5 bits are 0, channel is disabled
        if ((value & 0xf8) == 0)
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
            trigger_non_wave(&audio->ch1);
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
        break;
    case 0xff17:    // NR22 - envelope
        audio->ch2.env_initial = (value >> 4) & 0x0f;
        audio->ch2.env_dir = (value >> 3) & 0x01;
        audio->ch2.env_pace = value & 0x07;
        // DAC disable: if upper 5 bits are 0, channel is disabled
        if ((value & 0xf8) == 0)
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
            trigger_non_wave(&audio->ch2);
            if (audio->ch2.length_counter >= 64)
                audio->ch2.length_counter = 0;
        }
        break;

    // CH3 - wave
    case 0xff1a:    // NR30 - DAC enable
        audio->ch3.enabled = (value & 0x80) ? 1 : 0;
        break;
    case 0xff1b:    // NR31 - length
        audio->ch3.length_counter = value;
        break;
    case 0xff1c:    // NR32 - volume
        // volume code: 0=mute, 1=100%, 2=50%, 3=25%
        audio->ch3.volume = (value >> 5) & 0x03;
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
        // DAC disable: if upper 5 bits are 0, channel is disabled
        if ((value & 0xf8) == 0)
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
            trigger_non_wave(&audio->ch4);
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

static s8 generate_square(struct audio_channel *ch /*, u8 duty_pattern */)
{
    if (!ch->enabled || ch->volume == 0)
        return 0;

    // int duty_pos = (ch->phase >> 13) & 0x07;
    // int high = (duty_pattern >> (7 - duty_pos)) & 1;

    // return high ? ch->volume : -(s8) ch->volume;
    
    // phase is 16.16 fixed point, use upper 5 bits for table index
    int idx = (ch->phase >> BL_TABLE_SHIFT) & (BL_TABLE_SIZE - 1);
    s8 sample = bl_square[ch->duty][ch->band][idx];

    // scale by volume
    return (sample * ch->volume) >> 4;
}

static s8 generate_wave(struct audio *audio)
{
    struct audio_channel *ch = &audio->ch3;
    if (!ch->enabled || ch->volume == 0)
        return 0;

    // phase is 16.16 fixed point, use upper 5 bits of low 16 for sample index
    int sample_idx = (ch->phase >> 11) & 0x1f;
    int byte_idx = sample_idx >> 1;
    int nibble = audio->wave_ram[byte_idx];

    if (sample_idx & 1)
        nibble &= 0x0f;
    else
        nibble = (nibble >> 4) & 0x0f;

    // volume shift: 0=mute, 1=100%, 2=50%, 3=25%
    int shift = ch->volume;
    if (shift == 0)
        return 0;
    shift--; // now 0=100%, 1=50%, 2=25%

    // center around 0 (nibble is 0-15, center at 7.5)
    return (s8)((nibble - 8) >> shift);
}

static s8 generate_noise(struct audio *audio)
{
    struct audio_channel *ch = &audio->ch4;
    if (!ch->enabled || ch->volume == 0)
        return 0;

    // get output bit from precomputed table
    int pos, high;
    if (audio->lfsr_width) {
        // should be 0-126 but whatever, use & bc modulo is slow
        pos = (audio->ch4.phase >> 16) & 0x7f;
        high = (lfsr7_bits[pos >> 3] >> (pos & 7)) & 1;
    } else {
        pos = (audio->ch4.phase >> 16) & 0x7fff;
        high = (lfsr15_bits[pos >> 3] >> (pos & 7)) & 1;
    }

    return high ? ch->volume : -(s8)ch->volume;
}

void audio_generate(struct audio *audio, u8 *buffer, int samples)
{
    int k;

    if (!audio->master_enable) {
        memset(buffer, 0, samples);
        return;
    }

    for (k = 0; k < samples; k++) {
        s16 mix = 0;

        mix += generate_square(&audio->ch1 /* , duty_table[audio->ch1.duty] */);
        mix += generate_square(&audio->ch2 /* , duty_table[audio->ch1.duty] */);
        mix += generate_wave(audio);
        mix += generate_noise(audio);

        // advance phase accumulators
        audio->ch1.phase += audio->ch1.phase_inc;
        audio->ch2.phase += audio->ch2.phase_inc;
        audio->ch3.phase += audio->ch3.phase_inc;
        audio->ch4.phase += audio->ch4.phase_inc;

        // envelope tick at 64 Hz, 11127 / 64 = 174 samples
        audio->env_counter++;
        if (audio->env_counter >= 174) {
            audio->env_counter = 0;
            step_envelope(&audio->ch1);
            step_envelope(&audio->ch2);
            step_envelope(&audio->ch4);
        }

        // sweep tick at 128 Hz, 11127 / 128 = 87 samples
        audio->sweep_counter++;
        if (audio->sweep_counter >= 87) {
            audio->sweep_counter = 0;
            step_sweep(audio);
        }

        // length tick at 256 Hz, 11127 / 256 = 43 samples
        audio->length_counter++;
        if (audio->length_counter >= 43) {
            audio->length_counter = 0;
            step_length(&audio->ch1, 64);
            step_length(&audio->ch2, 64);
            step_length(&audio->ch3, 256);
            step_length(&audio->ch4, 64);
        }

        int master = audio->master_vol_right + 1;
        // >>3 would be "most correct" in terms of keeping the original scale
        // but this scales to -106 - 104 to make it louder
        mix = (mix * master) >> 2;
        // direct to unsigned bc it's what sound manager wants
        buffer[k] = (u8) (mix + 128);
    }
}
