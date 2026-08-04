#ifndef _SETTINGS_H
#define _SETTINGS_H

#include <MacTypes.h>

extern Boolean sound_enabled;
extern Boolean stat_ints_enabled;
extern Boolean limit_fps;
extern int frame_skip;
extern int screen_scale;
extern Boolean gbc_enabled;
extern Boolean ignore_double_speed;

#define VIDEO_BW 0
#define VIDEO_INDEXED 1
#define VIDEO_GRAY2 2
#define VIDEO_GRAY4 3
#define VIDEO_INDEXED4 4
#define VIDEO_MODE_COUNT 5

// modes that use the 2bpp/4bpp pixmap
#define VIDEO_IS_LOW_DEPTH(m) ((m) >= VIDEO_GRAY2)

// Palette Manager modes
#define VIDEO_HAS_PALETTES(m) ((m) == VIDEO_INDEXED || (m) == VIDEO_INDEXED4)

#endif
