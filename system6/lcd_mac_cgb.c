/* Game Boy Color emulator for 68k Macs
   lcd_mac_cgb.c - CGB LCD rendering with per-pixel palette lookup */

#include <Quickdraw.h>
#include <Windows.h>
#include <Palettes.h>
#include <string.h>

#include "../src/lcd.h"
#include "../src/cgb.h"
#include "emulator.h"
#include "lcd_mac.h"
#include "settings.h"

// CGB color palette cache (Color2Index mapped)
// 32 colors for BG (8 palettes * 4 colors) + 32 for OBJ
static unsigned char cgb_bg_color_cache[32];
static unsigned char cgb_obj_color_cache[32];

// Combined color LUT: indexed by [attr & ATTR_LUT_MASK][color_index]
// Maps: BG palettes 0-7 -> indices 0-7, sprite palettes 0-7 -> indices 8-15
static unsigned char cgb_color_lut[16][4];

// Convert RGB555 to RGBColor for Palette Manager
static void rgb555_to_rgbcolor(unsigned short rgb555, RGBColor *out)
{
    // RGB555: 0bbbbbgggggrrrrr
    unsigned short r5 = rgb555 & 0x1f;
    unsigned short g5 = (rgb555 >> 5) & 0x1f;
    unsigned short b5 = (rgb555 >> 10) & 0x1f;

    // Expand 5-bit to 16-bit
    out->red = (r5 << 11) | (r5 << 6) | (r5 << 1) | (r5 >> 4);
    out->green = (g5 << 11) | (g5 << 6) | (g5 << 1) | (g5 >> 4);
    out->blue = (b5 << 11) | (b5 << 6) | (b5 << 1) | (b5 >> 4);
}

// Update CGB palette cache from current palette RAM
// Maps RGB555 colors to nearest screen color table entries
// Only updates entries that have been marked dirty since last call
static void update_cgb_palette_cache(struct lcd *lcd_ptr)
{
    int k;
    RGBColor rgb;
    unsigned short color16;
    u32 bg_dirty = lcd_ptr->bg_palette_dirty;
    u32 obj_dirty = lcd_ptr->obj_palette_dirty;

    // Early exit if nothing changed
    if (bg_dirty == 0 && obj_dirty == 0) {
        return;
    }

    // Update only dirty BG palette colors
    for (k = 0; k < 32; k++) {
        if (bg_dirty & (1UL << k)) {
            color16 = lcd_ptr->bg_palette_ram[k * 2] |
                      (lcd_ptr->bg_palette_ram[k * 2 + 1] << 8);
            rgb555_to_rgbcolor(color16, &rgb);
            cgb_bg_color_cache[k] = Color2Index(&rgb);
            // Update the combined LUT for this color
            cgb_color_lut[k >> 2][k & 3] = cgb_bg_color_cache[k];
        }
    }

    // Update only dirty OBJ palette colors
    for (k = 0; k < 32; k++) {
        if (obj_dirty & (1UL << k)) {
            color16 = lcd_ptr->obj_palette_ram[k * 2] |
                      (lcd_ptr->obj_palette_ram[k * 2 + 1] << 8);
            rgb555_to_rgbcolor(color16, &rgb);
            cgb_obj_color_cache[k] = Color2Index(&rgb);
            // Update the combined LUT for this color (sprites at indices 8-15)
            cgb_color_lut[8 + (k >> 2)][k & 3] = cgb_obj_color_cache[k];
        }
    }

    // Clear dirty flags
    lcd_ptr->bg_palette_dirty = 0;
    lcd_ptr->obj_palette_dirty = 0;
}

static unsigned char band_bg[64], band_obj[64];
static int band_active, band_next;

static void cgb_lut_set_color(int obj, int color, const unsigned char *ram)
{
    RGBColor rgb;
    unsigned short color16 = ram[color * 2] | (ram[color * 2 + 1] << 8);
    unsigned char ci;

    rgb555_to_rgbcolor(color16, &rgb);
    ci = Color2Index(&rgb);
    if (obj) {
        cgb_obj_color_cache[color] = ci;
        cgb_color_lut[8 + (color >> 2)][color & 3] = ci;
    } else {
        cgb_bg_color_cache[color] = ci;
        cgb_color_lut[color >> 2][color & 3] = ci;
    }
}

// rolld every logged color back to its line 0 value, then replays
// the log up to the current row
static void cgb_band_start(struct lcd *lcd_ptr)
{
    int k;

    band_active = lcd_palette_banded(lcd_ptr);
    band_next = 0;
    if (!band_active) {
        return;
    }

    memcpy(band_bg, lcd_ptr->frame_bg_palette, 64);
    memcpy(band_obj, lcd_ptr->frame_obj_palette, 64);
    for (k = 0; k < lcd_ptr->palette_log_count; k++) {
        const struct palette_log_entry *e = &lcd_ptr->palette_log[k];
        int obj = (e->index & 0x40) != 0;

        cgb_lut_set_color(obj, (e->index & 0x3f) >> 1,
                obj ? band_obj : band_bg);
    }
}

// applies log entries that take effect at buffer row gy
static void cgb_band_row(struct lcd *lcd_ptr, int gy)
{
    while (band_active && band_next < lcd_ptr->palette_log_count) {
        const struct palette_log_entry *e =
                &lcd_ptr->palette_log[band_next];
        int obj;

        if (e->line + lcd_ptr->row_voff > gy) {
            return;
        }
        obj = (e->index & 0x40) != 0;
        if (obj) {
            band_obj[e->index & 0x3f] = e->value;
        } else {
            band_bg[e->index] = e->value;
        }
        cgb_lut_set_color(obj, (e->index & 0x3f) >> 1,
                obj ? band_obj : band_bg);
        band_next++;
    }
}

// maps one packed pixel byte + its 4 attr bytes to 4 screen color indices
static void cgb_convert_byte(
    unsigned char packed,
    const unsigned char *attr,
    unsigned char *c)
{
    c[0] = cgb_color_lut[attr[0] & ATTR_LUT_MASK][(packed >> 6) & 3];
    c[1] = cgb_color_lut[attr[1] & ATTR_LUT_MASK][(packed >> 4) & 3];
    c[2] = cgb_color_lut[attr[2] & ATTR_LUT_MASK][(packed >> 2) & 3];
    c[3] = cgb_color_lut[attr[3] & ATTR_LUT_MASK][packed & 3];
}

// CGB 1x indexed rendering - reads pixel and attr buffers
static void lcd_draw_1x_cgb(struct lcd *lcd_ptr, int all)
{
    int gy;
    unsigned char *src = lcd_ptr->pixels;
    unsigned char *attrs = lcd_ptr->attrs;
    unsigned long *dst = (unsigned long *) offscreen_color_buf;

    if (screen_depth == 1) {
        return;
    }
    if (!all && !lcd_ptr->frame_dirty) {
        return;
    }

    // Update palette cache from CGB palette RAM
    update_cgb_palette_cache(lcd_ptr);
    cgb_band_start(lcd_ptr);

    // convert all 168 buffer pixels per row; per-band scroll offsets are
    // handled by the band blit's source rects
    for (gy = 0; gy < LCD_BUF_ROWS; gy++) {
        unsigned char *row_attr = attrs + gy * 168;
        int gx;

        cgb_band_row(lcd_ptr, gy);
        if (!all && !(lcd_ptr->row_dirty[gy] & ROW_DIRTY_CONTENT)) {
            src += 42;
            dst += 42;
            continue;
        }

        for (gx = 0; gx < 42; gx++) {
            unsigned char c[4];

            cgb_convert_byte(src[gx], row_attr + gx * 4, c);
            dst[gx] = ((unsigned long)c[0] << 24) | ((unsigned long)c[1] << 16) |
                      ((unsigned long)c[2] << 8) | c[3];
        }
        src += 42;
        dst += 42;
    }

    lcd_blit_color_bands(&offscreen_pixmap, lcd_ptr, 1, all);
}

// CGB 2x indexed rendering
static void lcd_draw_2x_cgb(struct lcd *lcd_ptr, int all)
{
    int gy;
    unsigned char *src = lcd_ptr->pixels;
    unsigned char *attrs = lcd_ptr->attrs;
    unsigned long *dst = (unsigned long *) offscreen_color_buf;

    if (screen_depth == 1) {
        return;
    }
    if (!all && !lcd_ptr->frame_dirty) {
        return;
    }

    // Update palette cache from CGB palette RAM
    update_cgb_palette_cache(lcd_ptr);
    cgb_band_start(lcd_ptr);

    // convert all 168 buffer pixels per row; per-band scroll offsets are
    // handled by the band blit's source rects
    for (gy = 0; gy < LCD_BUF_ROWS; gy++) {
        // row stride in longs: 336 bytes / 4 = 84 longs
        unsigned long *row0 = dst;
        unsigned long *row1 = dst + 84;
        unsigned char *row_attr = attrs + gy * 168;
        int gx;

        cgb_band_row(lcd_ptr, gy);
        if (!all && !(lcd_ptr->row_dirty[gy] & ROW_DIRTY_CONTENT)) {
            src += 42;
            dst += 168;
            continue;
        }

        for (gx = 0; gx < 42; gx++) {
            unsigned char c[4];
            unsigned long lo, hi;

            cgb_convert_byte(src[gx], row_attr + gx * 4, c);

            // 2x: each pixel doubled horizontally
            lo = ((unsigned long)c[0] << 24) | ((unsigned long)c[0] << 16) |
                 ((unsigned long)c[1] << 8) | c[1];
            hi = ((unsigned long)c[2] << 24) | ((unsigned long)c[2] << 16) |
                 ((unsigned long)c[3] << 8) | c[3];

            row0[0] = lo; row0[1] = hi;
            row1[0] = lo; row1[1] = hi;

            row0 += 2;
            row1 += 2;
        }

        src += 42;
        dst += 168;  // 2 rows * 84 longs per row
    }

    lcd_blit_color_bands(&offscreen_pixmap, lcd_ptr, 2, all);
}

// Main CGB draw function - called from lcd_mac.c
void lcd_draw_cgb(struct lcd *lcd_ptr, int all)
{
    // forced frames follow palette-menu/depth changes: the Color2Index
    // results are stale against the new clut, rebuild them all
    if (all) {
        lcd_ptr->bg_palette_dirty = 0xffffffff;
        lcd_ptr->obj_palette_dirty = 0xffffffff;
    }

    if (screen_scale == 2) {
        lcd_draw_2x_cgb(lcd_ptr, all);
    } else {
        lcd_draw_1x_cgb(lcd_ptr, all);
    }
}
