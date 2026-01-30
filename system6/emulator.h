/* Game Boy emulator for 68k Macs
   emulator.h - declarations for emulator.c */
   
#ifndef EMULATOR_H
#define EMULATOR_H

#include <Quickdraw.h>
#include <Windows.h>

#include "types.h"
#include "dmg.h"
#include "lcd.h"
#include "rom.h"
#include "audio.h"

extern WindowPtr g_wp;
extern int screen_depth;

extern struct rom rom;
extern struct lcd lcd;
extern struct audio audio;
extern struct dmg dmg;

extern char offscreen_buf[];
extern Rect offscreen_rect;
extern BitMap offscreen_bmp;

extern char offscreen_color_buf[];
extern PixMap offscreen_pixmap;

#define APP_VERSION "1.3.1 ${GIT_SHA}"

#define WINDOW_X 8
#define WINDOW_Y 40
#define WINDOW_WIDTH 320
#define WINDOW_HEIGHT 299

#define ALRT_NOT_ENOUGH_RAM 128
#define ALRT_4_LINE 129

#define MBAR_DEFAULT 128

#define MENU_APPLE 128
#define MENU_FILE 129
#define MENU_EDIT 130
#define MENU_PALETTES 131

#define APPLE_ABOUT 1

#define FILE_OPEN 1
#define FILE_SAVE_GAME 3
#define FILE_SCREENSHOT 4
#define FILE_SOFT_RESET 5
#define FILE_CLOSE 7
#define FILE_QUIT 8

#define EDIT_SOUND 1
#define EDIT_LIMIT_FPS 2
#define EDIT_SCALE_1X 5
#define EDIT_SCALE_2X 6
#define EDIT_PREFERENCES 8
#define EDIT_KEY_MAPPINGS 9
#define EDIT_GBC_MODE 11

#define RES_MDEF_ID 128

// 0.5 sec
#define SOFT_RESET_TICKS 30

#define BASE_MEMORY_REQUIRED (2 * 1024 * 1024) 

int LoadRom(Str63, short);
void SetScreenScale(int scale);

void set_status_bar(const char *str);

#endif