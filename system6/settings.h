#ifndef _SETTINGS_H
#define _SETTINGS_H

extern int cycles_per_exit;
extern int frame_skip;
extern int video_mode;
extern int screen_scale;
extern unsigned char sound_enabled;
extern unsigned char limit_fps;
extern unsigned char gbc_enabled;
extern unsigned char ignore_double_speed;

#define VIDEO_DITHER_COPYBITS 0
#define VIDEO_DITHER_DIRECT 1
#define VIDEO_INDEXED 2

#endif