#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "lcd.h"
#include "dmg.h"

// LUT for decoding 4 pixels at once from tile data
// Index = (data1_nibble << 4) | data2_nibble
// Output = 4 palette-mapped pixel values packed into one byte (p0<<6|p1<<4|p2<<2|p3)
static u8 tile_decode_packed[256];

// Base LUT with color indices 0-3 (palette-independent)
static u8 tile_decode_base[256][4];

// Packed pixel buffer: 4 pixels per byte, 42 bytes per row (168 pixels)
// Renders 21 tiles starting at tile-aligned position for fast path always
static u8 pixels[42 * 144];

// CGB attribute buffer: 1 byte per pixel, stores palette/priority
static u8 attr_buffer[168 * 144];

// LUT for horizontal flip: reverses bit order in a byte
u8 hflip_lut[256];

// Shift amounts for packed pixel access (avoids 6 - idx * 2 multiply)
static const u8 pixel_shift[4] = { 6, 4, 2, 0 };

void lcd_init_lut(void)
{
    int n1, n2, k;
    for (n1 = 0; n1 < 16; n1++) {
        for (n2 = 0; n2 < 16; n2++) {
            int idx = (n1 << 4) | n2;
            // Bit 3 -> pixel 0, bit 2 -> pixel 1, etc. (MSB first)
            tile_decode_base[idx][0] = ((n1 >> 3) & 1) | (((n2 >> 3) & 1) << 1);
            tile_decode_base[idx][1] = ((n1 >> 2) & 1) | (((n2 >> 2) & 1) << 1);
            tile_decode_base[idx][2] = ((n1 >> 1) & 1) | (((n2 >> 1) & 1) << 1);
            tile_decode_base[idx][3] = (n1 & 1) | ((n2 & 1) << 1);
        }
    }
    // Initialize with identity palette (no mapping)
    lcd_update_palette_lut(0xe4); // default: 3,2,1,0

    // Initialize horizontal flip LUT (reverses bit order)
    for (k = 0; k < 256; k++) {
        u8 v = k;
        u8 r = 0;
        r |= (v & 0x80) >> 7;
        r |= (v & 0x40) >> 5;
        r |= (v & 0x20) >> 3;
        r |= (v & 0x10) >> 1;
        r |= (v & 0x08) << 1;
        r |= (v & 0x04) << 3;
        r |= (v & 0x02) << 5;
        r |= (v & 0x01) << 7;
        hflip_lut[k] = r;
    }
}

void lcd_update_palette_lut(u8 palette)
{
    u8 pal[4];
    int k;

    pal[0] = palette & 3;
    pal[1] = (palette >> 2) & 3;
    pal[2] = (palette >> 4) & 3;
    pal[3] = (palette >> 6) & 3;

    for (k = 0; k < 256; k++) {
        u8 p0 = pal[tile_decode_base[k][0]];
        u8 p1 = pal[tile_decode_base[k][1]];
        u8 p2 = pal[tile_decode_base[k][2]];
        u8 p3 = pal[tile_decode_base[k][3]];
        tile_decode_packed[k] = (p0 << 6) | (p1 << 4) | (p2 << 2) | p3;
    }
}

struct oam_entry {
    u8 pos_y;
    u8 pos_x;
    u8 tile;
    u8 attrs;
};

void lcd_new(struct lcd *lcd)
{
    // todo < 8 bpp
    lcd->pixels = pixels;
    lcd->attrs = attr_buffer;
    // Initialize CGB palette dirty flags to all-dirty so first frame updates everything
    lcd->bg_palette_dirty = 0xFFFFFFFF;
    lcd->obj_palette_dirty = 0xFFFFFFFF;
}

u8 lcd_is_valid_addr(u16 addr)
{
    return (addr >= 0xfe00 && addr < 0xfea0) || (addr >= REG_LCD_BASE && addr <= REG_LCD_LAST);
}

int lcd_step(struct lcd *lcd)
{
    // step to next scanline 0-153
    u8 next_scanline = (lcd_read(lcd, REG_LY) + 1) % 154;
    lcd_write(lcd, REG_LY, next_scanline);

    return next_scanline;
}

// render 8 aligned pixels (full tile row) to packed output
// writes 2 bytes: pixels 0-3 and pixels 4-7
static inline void render_tile_row_packed(u8 *p, u8 data1, u8 data2)
{
    int idx_hi = (data1 & 0xf0) | (data2 >> 4);
    int idx_lo = ((data1 & 0x0f) << 4) | (data2 & 0x0f);
    p[0] = tile_decode_packed[idx_hi];
    p[1] = tile_decode_packed[idx_lo];
}

// helper to extract single pixel from packed byte (pixel 0 is bits 7-6)
static inline u8 packed_get_pixel(u8 packed, int pixel_idx)
{
    return (packed >> pixel_shift[pixel_idx]) & 3;
}

// helper to set single pixel in packed byte
static inline u8 packed_set_pixel(u8 packed, int pixel_idx, u8 value)
{
    int shift = pixel_shift[pixel_idx];
    return (packed & ~(3 << shift)) | ((value & 3) << shift);
}

// render partial pixels at start of a tile row (handles SCX misalignment)
// pixel_offset: which pixel in the tile to start from (1-7)
// count: how many pixels to render (1-7)
// byte_idx: which pixel position in the output byte to start writing (0-3)
static inline void render_partial_start(
    u8 *p,
    u8 data1,
    u8 data2,
    int pixel_offset,
    int count,
    int byte_idx
) {
    int idx_hi = (data1 & 0xf0) | (data2 >> 4);
    int idx_lo = ((data1 & 0x0f) << 4) | (data2 & 0x0f);
    u8 packed_hi = tile_decode_packed[idx_hi];
    u8 packed_lo = tile_decode_packed[idx_lo];
    int k;

    for (k = 0; k < count; k++) {
        int src_pixel = pixel_offset + k;
        u8 value = (src_pixel < 4)
            ? packed_get_pixel(packed_hi, src_pixel)
            : packed_get_pixel(packed_lo, src_pixel - 4);
        int dst_pixel = byte_idx + k;
        int dst_byte = dst_pixel >> 2;
        int dst_bit = dst_pixel & 3;
        p[dst_byte] = packed_set_pixel(p[dst_byte], dst_bit, value);
    }
}

void lcd_render_background(struct dmg *dmg, int lcdc, int window_enabled)
{
    u8 *vram = dmg->video_ram;
    u8 *out = dmg->lcd->pixels;

    int scx = lcd_read(dmg->lcd, REG_SCX);
    int scy = lcd_read(dmg->lcd, REG_SCY);
    int wx = lcd_read(dmg->lcd, REG_WX) - 7;
    int wy = lcd_read(dmg->lcd, REG_WY);

    int bg_map_off = (lcdc & LCDC_BG_TILE_MAP) ? 0x1c00 : 0x1800;
    int win_map_off = (lcdc & LCDC_WINDOW_TILE_MAP) ? 0x1c00 : 0x1800;
    int unsigned_mode = lcdc & LCDC_BG_TILE_DATA;
    int tile_base_off = unsigned_mode ? 0 : 0x1000;

    // scx offset within buffer - sprites and display need this
    int scx_offset = scx & 7;

    // calculate how many BG tiles to render when window is active,
    // skip tiles that would be fully covered by the window
    int bg_tile_limit = 21;
    int window_on_screen = window_enabled && wx < 160 && wy < 144;
    if (window_on_screen) {
        int win_start_buf = (wx > 0 ? wx : 0) + scx_offset;
        bg_tile_limit = (win_start_buf + 7) / 8;
        if (bg_tile_limit > 21) bg_tile_limit = 21;
        if (bg_tile_limit < 0) bg_tile_limit = 0;
    }

    int sy_limit = window_on_screen ? wy : 144;
    int sy;

    // before window: render all 21 BG tiles, no window overlay
    for (sy = 0; sy < sy_limit; sy++) {
        u8 *row = out + sy * 42;
        int bg_y = (sy + scy) & 0xff;
        int tile_row = bg_y >> 3;
        int row_in_tile = bg_y & 7;
        int bg_x = scx & ~7;

        int tile;
        for (tile = 0; tile < 21; tile++) {
            int tile_col = (bg_x >> 3) & 31;
            int tile_idx = vram[bg_map_off + tile_row * 32 + tile_col];
            int tile_off = unsigned_mode
                ? tile_base_off + 16 * tile_idx
                : tile_base_off + 16 * (signed char) tile_idx;

            u8 data1 = vram[tile_off + row_in_tile * 2];
            u8 data2 = vram[tile_off + row_in_tile * 2 + 1];

            render_tile_row_packed(row + tile * 2, data1, data2);
            bg_x = (bg_x + 8) & 0xff;
        }
    }

    // lines with window: render only visible BG tiles, then window overlay
    for (sy = sy_limit; sy < 144; sy++) {
        u8 *row = out + sy * 42;
        int bg_y = (sy + scy) & 0xff;
        int tile_row = bg_y >> 3;
        int row_in_tile = bg_y & 7;
        int bg_x = scx & ~7;

        int tile;
        for (tile = 0; tile < bg_tile_limit; tile++) {
            int tile_col = (bg_x >> 3) & 31;
            int tile_idx = vram[bg_map_off + tile_row * 32 + tile_col];
            int tile_off = unsigned_mode
                ? tile_base_off + 16 * tile_idx
                : tile_base_off + 16 * (signed char) tile_idx;

            u8 data1 = vram[tile_off + row_in_tile * 2];
            u8 data2 = vram[tile_off + row_in_tile * 2 + 1];

            render_tile_row_packed(row + tile * 2, data1, data2);
            bg_x = (bg_x + 8) & 0xff;
        }

        // Window overlay
        int win_y = sy - wy;
        int win_tile_row = win_y >> 3;
        int win_row_in_tile = win_y & 7;
        int win_start = (wx > 0 ? wx : 0) + scx_offset;
        int win_end = 160 + scx_offset;
        int win_x = wx < 0 ? -wx : 0;

        while (win_start < win_end) {
            int tile_col = (win_x >> 3) & 31;
            int pixel_in_tile = win_x & 7;
            int tile_idx = vram[win_map_off + win_tile_row * 32 + tile_col];
            int tile_off = unsigned_mode
                ? tile_base_off + 16 * tile_idx
                : tile_base_off + 16 * (signed char) tile_idx;

            u8 data1 = vram[tile_off + win_row_in_tile * 2];
            u8 data2 = vram[tile_off + win_row_in_tile * 2 + 1];

            if (pixel_in_tile == 0 && win_start + 8 <= win_end) {
                // full tile - decode with LUT then write (possibly misaligned)
                int idx_hi = (data1 & 0xf0) | (data2 >> 4);
                int idx_lo = ((data1 & 0x0f) << 4) | (data2 & 0x0f);
                u8 tile0 = tile_decode_packed[idx_hi];
                u8 tile1 = tile_decode_packed[idx_lo];

                int byte_idx = win_start >> 2;
                int off = win_start & 3;

                if (off == 0) {
                    // aligned: direct write
                    row[byte_idx] = tile0;
                    row[byte_idx + 1] = tile1;
                } else {
                    // misaligned: shift and merge across 3 bytes
                    int shift = off * 2;
                    row[byte_idx] = (row[byte_idx] & (0xff << (8 - shift))) | (tile0 >> shift);
                    row[byte_idx + 1] = (tile0 << (8 - shift)) | (tile1 >> shift);
                    row[byte_idx + 2] = (row[byte_idx + 2] & (0xff >> shift)) | (tile1 << (8 - shift));
                }
                win_start += 8;
                win_x += 8;
            } else {
                // partial tile (start or end of window, or mid-tile start)
                int pixels_to_draw = 8 - pixel_in_tile;
                if (win_start + pixels_to_draw > win_end) {
                    pixels_to_draw = win_end - win_start;
                }
                render_partial_start(row, data1, data2, pixel_in_tile, pixels_to_draw, win_start);
                win_start += pixels_to_draw;
                win_x += pixels_to_draw;
            }
        }
    }
}

// TODO: only ten per scanline, priority, attributes
void lcd_render_objs(struct dmg *dmg)
{
    struct oam_entry *oam = &((struct oam_entry *) dmg->lcd->oam)[39];
    int tall = lcd_isset(dmg->lcd, REG_LCDC, LCDC_OBJ_SIZE);
    u8 *vram = dmg->video_ram;
    u8 *pixels = dmg->lcd->pixels;

    // sprites are rendered at screen position + scx offset within the wider buffer
    int scx_offset = lcd_read(dmg->lcd, REG_SCX) & 7;

    int k;
    for (k = 39; k >= 0; k--, oam--) {
        if (oam->pos_y == 0 || oam->pos_y >= 160) {
            continue;
        }
        if (oam->pos_x == 0 || oam->pos_x >= 168) {
            continue;
        }

        int tile_off = 16 * oam->tile;
        u8 palette = oam->attrs & OAM_ATTR_PALETTE
            ? lcd_read(dmg->lcd, REG_OBP1)
            : lcd_read(dmg->lcd, REG_OBP0);
        u8 pal[4] = {
            palette & 3,
            (palette >> 2) & 3,
            (palette >> 4) & 3,
            (palette >> 6) & 3
        };

        int lcd_x = oam->pos_x - 8;
        int lcd_y = oam->pos_y - 16;
        int tile_bytes = tall ? 32 : 16;
        int mirror_x = oam->attrs & OAM_ATTR_MIRROR_X;
        int mirror_y = oam->attrs & OAM_ATTR_MIRROR_Y;

        int b;
        for (b = 0; b < tile_bytes; b += 2) {
            int row_y = lcd_y + (b >> 1);
            if (row_y < 0 || row_y >= 144) {
                continue;
            }

            int use_b = mirror_y ? (tile_bytes - 2) - b : b;
            u8 data1 = vram[tile_off + use_b];
            u8 data2 = vram[tile_off + use_b + 1];

            // calculate visible x range for this row
            int x_start = lcd_x < 0 ? -lcd_x : 0;
            int x_end = lcd_x + 8 > 160 ? 160 - lcd_x : 8;

            u8 *row = pixels + row_y * 42;
            int x;
            if (mirror_x) {
                // mirrored: read bits from LSB to MSB
                data1 >>= x_start;
                data2 >>= x_start;
                for (x = x_start; x < x_end; x++) {
                    int col_index = (data1 & 1) | ((data2 & 1) << 1);
                    data1 >>= 1;
                    data2 >>= 1;
                    if (col_index) {
                        int px = lcd_x + x + scx_offset;
                        int byte_idx = px >> 2;
                        int bit_idx = px & 3;
                        row[byte_idx] = packed_set_pixel(row[byte_idx], bit_idx, pal[col_index]);
                    }
                }
            } else {
                // normal: read bits from MSB to LSB
                data1 <<= x_start;
                data2 <<= x_start;
                for (x = x_start; x < x_end; x++) {
                    int col_index = ((data1 >> 7) & 1) | (((data2 >> 7) & 1) << 1);
                    data1 <<= 1;
                    data2 <<= 1;
                    if (col_index) {
                        int px = lcd_x + x + scx_offset;
                        int byte_idx = px >> 2;
                        int bit_idx = px & 3;
                        row[byte_idx] = packed_set_pixel(row[byte_idx], bit_idx, pal[col_index]);
                    }
                }
            }
        }
    }
}