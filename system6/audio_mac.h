#ifndef _AUDIO_MAC_H
#define _AUDIO_MAC_H

#include "../src/types.h"

struct audio;

int audio_mac_available(void);
int audio_mac_init(struct audio *audio);
void audio_mac_start(void);
void audio_mac_stop(void);
void audio_mac_shutdown(void);

// called from dmg_sync_hw to advance the emulated-time clock (pacing only)
void audio_mac_sync(int cycles);

// APU register write; queued and applied sample-accurately at interrupt time
void audio_mac_write(struct audio *audio, u16 addr, u8 value);

// block if emulated time is too far ahead of real time (for frame limiting)
void audio_mac_wait_if_ahead(void);

#endif
