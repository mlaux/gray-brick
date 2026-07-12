// host stand-ins for everything src/*.c normally gets from the system6/
// Mac port: settings globals, status bar, video/audio output, the code
// arena, Mac time, and the jit_ctx global from system6/jit.c

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "types.h"
#include "lcd.h"
#include "../system6/jit.h"
#include "../system6/settings.h"
#include "host.h"

// settings normally loaded from Mac prefs (dialogs.c / emulator.c)
int frame_skip;
int video_mode;
int screen_scale = 1;
unsigned char sound_enabled;
unsigned char limit_fps;
unsigned char gbc_enabled = 1;
unsigned char ignore_double_speed;

// normally defined in system6/jit.c
jit_context jit_ctx;
int jit_halted;

int host_show_status;

void set_status_bar(const char *str)
{
    if (host_show_status) {
        fprintf(stderr, "[status] %s\n", str);
    }
}

// dmg.c calls lcd_draw once per rendered frame; the runner hooks this for
// frame dumps and hashes
u32 host_frames_drawn;
void (*host_lcd_draw_hook)(struct lcd *lcd);

void lcd_draw(struct lcd *lcd)
{
    // hook sees host_frames_drawn as the 0-based index of this frame
    if (host_lcd_draw_hook) {
        host_lcd_draw_hook(lcd);
    }
    host_frames_drawn++;
}

void audio_mac_sync(int cycles)
{
    (void) cycles;
}

// seconds since Jan 1 1904, as GetDateTime returns on a real Mac.
// note: real wall clock, so MBC3 RTC state is not deterministic across runs
#define MAC_EPOCH_OFFSET 2082844800ul

void GetDateTime(unsigned long *secs)
{
    *secs = (unsigned long) time(NULL) + MAC_EPOCH_OFFSET;
}

// bump allocator with the same interface as system6/arena.c. the runner
// points it at a region of m68k_mem via arena_set_region so compiled code
// is executable by Musashi in place; the malloc fallback keeps link-only
// uses working
#define ARENA_DEFAULT_SIZE (8u * 1024 * 1024)

static u8 *arena_base;
static size_t arena_capacity;
static size_t arena_used;

void arena_set_region(u8 *base, size_t size)
{
    arena_base = base;
    arena_capacity = size;
    arena_used = 0;
}

int arena_init(void)
{
    if (!arena_base) {
        arena_set_region(malloc(ARENA_DEFAULT_SIZE), ARENA_DEFAULT_SIZE);
    }
    return arena_base != NULL;
}

void *arena_alloc(size_t size)
{
    void *p;

    // 8-byte alignment: cache.c stores host pointers in arena arrays, and
    // 68k code needs at least 2
    size = (size + 7) & ~(size_t) 7;
    if (arena_used + size > arena_capacity) {
        return NULL;
    }
    p = arena_base + arena_used;
    arena_used += size;
    return p;
}

void arena_reset(void)
{
    arena_used = 0;
}

size_t arena_remaining(void)
{
    return arena_capacity - arena_used;
}

size_t arena_size(void)
{
    return arena_capacity;
}

void arena_destroy(void)
{
}
