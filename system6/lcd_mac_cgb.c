/* Game Boy Color emulator for 68k Macs
   lcd_mac_cgb.c - CGB LCD rendering with per-pixel palette lookup */

#include <Quickdraw.h>
#include <Windows.h>
#include <Palettes.h>

#include "../src/lcd.h"
#include "../src/cgb.h"
#include "emulator.h"
#include "lcd_mac.h"
#include "settings.h"

// CGB color palette cache (Color2Index mapped)
// 32 colors for BG (8 palettes * 4 colors) + 32 for OBJ
static unsigned char cgb_bg_color_cache[32];
static unsigned char cgb_obj_color_cache[32];

// Combined color LUT: indexed by [lut_index][color_index]
// lut_index = ((attr >> 1) & 0x08) | (attr & 0x07)
// Maps: BG palettes 0-7 → indices 0-7, sprite palettes 0-7 → indices 8-15
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

// CGB 1x indexed rendering - reads pixel and attr buffers
static void lcd_draw_1x_cgb(struct lcd *lcd_ptr)
{
    int gy;
    unsigned char *src = lcd_ptr->pixels;
    unsigned char *attrs = lcd_ptr->attrs;
    unsigned long *dst = (unsigned long *) offscreen_color_buf;
    CGrafPtr port;
    int scx_offset = lcd_read(lcd_ptr, REG_SCX) & 7;
    Rect srcRect;

    if (screen_depth == 1) {
        return;
    }

    // Update palette cache from CGB palette RAM
    update_cgb_palette_cache(lcd_ptr);

    for (gy = 0; gy < 144; gy++) {
        int gx;
        unsigned char *row_attr = attrs + gy * 168;
        for (gx = 0; gx < 42; gx++) {
            // packed byte: p0 p1 p2 p3 (2 bits each)
            unsigned char packed = src[gx];
            int px = gx * 4;
            unsigned char c0, c1, c2, c3;
            unsigned char attr0, attr1, attr2, attr3;
            int lut0, lut1, lut2, lut3;

            // Get color indices for each pixel
            int p0 = (packed >> 6) & 3;
            int p1 = (packed >> 4) & 3;
            int p2 = (packed >> 2) & 3;
            int p3 = packed & 3;

            // Convert attr to LUT index: ((attr >> 1) & 0x08) | (attr & 0x07)
            // This maps BG palettes 0-7 to indices 0-7, sprites to 8-15
            attr0 = row_attr[px];
            attr1 = row_attr[px + 1];
            attr2 = row_attr[px + 2];
            attr3 = row_attr[px + 3];

            lut0 = ((attr0 >> 1) & 0x08) | (attr0 & 0x07);
            lut1 = ((attr1 >> 1) & 0x08) | (attr1 & 0x07);
            lut2 = ((attr2 >> 1) & 0x08) | (attr2 & 0x07);
            lut3 = ((attr3 >> 1) & 0x08) | (attr3 & 0x07);

            c0 = cgb_color_lut[lut0][p0];
            c1 = cgb_color_lut[lut1][p1];
            c2 = cgb_color_lut[lut2][p2];
            c3 = cgb_color_lut[lut3][p3];

            *dst++ = ((unsigned long)c0 << 24) | ((unsigned long)c1 << 16) |
                     ((unsigned long)c2 << 8) | c3;
        }
        src += 42;
    }

    // source rect is offset by scroll amount, destination is full window
    srcRect.top = 0;
    srcRect.left = scx_offset;
    srcRect.bottom = 144;
    srcRect.right = scx_offset + 160;

    SetPort(g_wp);
    port = (CGrafPtr) g_wp;
    CopyBits(
        (BitMap *) &offscreen_pixmap,
        (BitMap *) *port->portPixMap,
        &srcRect, &offscreen_rect, srcCopy, NULL
    );
}

// CGB 2x indexed rendering
static void lcd_draw_2x_cgb(struct lcd *lcd_ptr)
{
    int gy;
    unsigned char *src = lcd_ptr->pixels;
    unsigned char *attrs = lcd_ptr->attrs;
    unsigned long *dst = (unsigned long *) offscreen_color_buf;
    CGrafPtr port;
    int scx_offset = lcd_read(lcd_ptr, REG_SCX) & 7;
    Rect srcRect;

    if (screen_depth == 1) {
        return;
    }

    // Update palette cache from CGB palette RAM
    update_cgb_palette_cache(lcd_ptr);

    for (gy = 0; gy < 144; gy++) {
        // row stride in longs: 336 bytes / 4 = 84 longs
        unsigned long *row0 = dst;
        unsigned long *row1 = dst + 84;
        unsigned char *row_attr = attrs + gy * 168;
        int gx;

        for (gx = 0; gx < 42; gx++) {
            unsigned char packed = src[gx];
            int px = gx * 4;
            unsigned char c0, c1, c2, c3;
            unsigned char attr0, attr1, attr2, attr3;
            int lut0, lut1, lut2, lut3;
            unsigned long lo, hi;

            int p0 = (packed >> 6) & 3;
            int p1 = (packed >> 4) & 3;
            int p2 = (packed >> 2) & 3;
            int p3 = packed & 3;

            // Convert attr to LUT index: ((attr >> 1) & 0x08) | (attr & 0x07)
            attr0 = row_attr[px];
            attr1 = row_attr[px + 1];
            attr2 = row_attr[px + 2];
            attr3 = row_attr[px + 3];

            lut0 = ((attr0 >> 1) & 0x08) | (attr0 & 0x07);
            lut1 = ((attr1 >> 1) & 0x08) | (attr1 & 0x07);
            lut2 = ((attr2 >> 1) & 0x08) | (attr2 & 0x07);
            lut3 = ((attr3 >> 1) & 0x08) | (attr3 & 0x07);

            c0 = cgb_color_lut[lut0][p0];
            c1 = cgb_color_lut[lut1][p1];
            c2 = cgb_color_lut[lut2][p2];
            c3 = cgb_color_lut[lut3][p3];

            // 2x: each pixel doubled horizontally
            lo = ((unsigned long)c0 << 24) | ((unsigned long)c0 << 16) |
                 ((unsigned long)c1 << 8) | c1;
            hi = ((unsigned long)c2 << 24) | ((unsigned long)c2 << 16) |
                 ((unsigned long)c3 << 8) | c3;

            row0[0] = lo; row0[1] = hi;
            row1[0] = lo; row1[1] = hi;

            row0 += 2;
            row1 += 2;
        }

        src += 42;
        dst += 168;  // 2 rows * 84 longs per row
    }

    // source rect is offset by scroll amount, destination is full window
    srcRect.top = 0;
    srcRect.left = scx_offset * 2;
    srcRect.bottom = 288;
    srcRect.right = scx_offset * 2 + 320;

    SetPort(g_wp);
    port = (CGrafPtr) g_wp;
    CopyBits(
        (BitMap *) &offscreen_pixmap,
        (BitMap *) *port->portPixMap,
        &srcRect, &offscreen_rect, srcCopy, NULL
    );
}

// Main CGB draw function - called from lcd_mac.c
void lcd_draw_cgb(struct lcd *lcd_ptr)
{
    if (screen_scale == 2) {
        lcd_draw_2x_cgb(lcd_ptr);
    } else {
        lcd_draw_1x_cgb(lcd_ptr);
    }
}
