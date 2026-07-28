#ifndef _AUDIO_H
#define _AUDIO_H

#include "types.h"

// register base addresses
#define REG_NR10 0xff10
#define REG_NR52 0xff26
#define REG_WAVE_START 0xff30
#define REG_WAVE_END 0xff3f

struct audio_channel {
    u16 freq_reg;       // 11-bit frequency register value
    u32 phase;          // phase accumulator; square/wave are pre-scaled by 32
                        // so bits 16-20 give the 32-entry table index
    u32 phase_inc;      // phase increment per sample
    const s8 *bl_table; // bl_square_vol row for volume/duty/band (squares)
    u8 volume;          // 4-bit volume (0-15)
    u8 duty;            // duty cycle (0-3)
    u8 band;            // bandlimit table index (0-3)
    u8 enabled;

    // envelope
    u8 env_initial;
    u8 env_dir;         // 0 = decrease, 1 = increase
    u8 env_pace;
    u8 env_timer;

    // length counter
    u16 length_counter; // counts up to 64 (pulse/noise) or 256 (wave)
    u8 length_enable;

    // sweep (CH1 only)
    u8 sweep_pace;
    u8 sweep_dir;       // 0 = increase, 1 = decrease
    u8 sweep_step;
    u8 sweep_timer;
    u16 sweep_freq;     // shadow frequency for sweep calculation
};

struct audio {
    struct audio_channel ch1;   // square + sweep
    struct audio_channel ch2;   // square
    struct audio_channel ch3;   // wave
    struct audio_channel ch4;   // noise

    u8 wave_ram[16];            // 32 4-bit samples
    u8 lfsr_width;              // 0 = 15-bit, 1 = 7-bit
    u8 noise_divisor;
    u8 noise_shift;

    u8 master_enable;           // NR52 bit 7
    u8 master_vol_left;
    u8 master_vol_right;
    u8 panning;                 // NR51

    u16 env_counter;            // sample counter for 64 Hz envelope tick
    u16 sweep_counter;          // sample counter for 128 Hz sweep tick
    u16 length_counter;         // sample counter for 256 Hz length tick

    // wave RAM unpacked to centered, volume-shifted samples
    s8 wave_tab[32];

    // mixtab[mix + 128] = ((mix * master) >> 2) + 128
    u8 mixtab[256];
    u8 mixtab_master;

    // raw register storage for reads
    u8 regs[0x30];
};

void audio_init(struct audio *audio);
void audio_write(struct audio *audio, u16 addr, u8 value);
u8 audio_read(struct audio *audio, u16 addr);
void audio_generate(struct audio *audio, u8 *buffer, int samples);

#endif
