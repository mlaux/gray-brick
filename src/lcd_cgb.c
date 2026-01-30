/* CGB-specific LCD rendering functions
   Separate from lcd.c to keep DMG rendering unchanged */

#include <string.h>

#include "types.h"
#include "lcd.h"
#include "dmg.h"
#include "cgb.h"

// helper to extract single pixel from packed byte (pixel 0 is bits 7-6)
static inline u8 packed_get_pixel(u8 packed, int pixel_idx)
{
    return (packed >> (6 - pixel_idx * 2)) & 3;
}

// helper to set single pixel in packed byte
static inline u8 packed_set_pixel(u8 packed, int pixel_idx, u8 value)
{
    int shift = 6 - pixel_idx * 2;
    return (packed & ~(3 << shift)) | ((value & 3) << shift);
}

// CGB version: render 8 pixels without palette mapping (raw 2bpp values)
// Also writes 8 bytes of per-pixel attribute data
static inline void render_tile_row_cgb(u8 *p, u8 *a, u8 data1, u8 data2, u8 attr_val)
{
    // Decode 8 pixels to 2 packed bytes (4 pixels each)
    // Use base decode - no palette mapping for CGB (palette is per-pixel)
    int b;
    u8 packed0 = 0, packed1 = 0;

    for (b = 0; b < 4; b++) {
        int bit = 7 - b;
        int col = ((data1 >> bit) & 1) | (((data2 >> bit) & 1) << 1);
        packed0 |= col << (6 - b * 2);
    }
    for (b = 0; b < 4; b++) {
        int bit = 3 - b;
        int col = ((data1 >> bit) & 1) | (((data2 >> bit) & 1) << 1);
        packed1 |= col << (6 - b * 2);
    }

    p[0] = packed0;
    p[1] = packed1;

    // Store per-pixel attribute for all 8 pixels
    a[0] = attr_val;
    a[1] = attr_val;
    a[2] = attr_val;
    a[3] = attr_val;
    a[4] = attr_val;
    a[5] = attr_val;
    a[6] = attr_val;
    a[7] = attr_val;
}

void lcd_cgb_render_background(struct dmg *dmg, int lcdc, int window_enabled)
{
    u8 *vram = dmg->video_ram;
    u8 *out = dmg->lcd->pixels;
    u8 *out_attr = dmg->lcd->attrs;

    int scx = lcd_read(dmg->lcd, REG_SCX);
    int scy = lcd_read(dmg->lcd, REG_SCY);
    int wx = lcd_read(dmg->lcd, REG_WX) - 7;
    int wy = lcd_read(dmg->lcd, REG_WY);

    int bg_map_off = (lcdc & LCDC_BG_TILE_MAP) ? 0x1c00 : 0x1800;
    int win_map_off = (lcdc & LCDC_WINDOW_TILE_MAP) ? 0x1c00 : 0x1800;
    int unsigned_mode = lcdc & LCDC_BG_TILE_DATA;
    int tile_base_off = unsigned_mode ? 0 : 0x1000;

    int scx_offset = scx & 7;

    int sy;
    for (sy = 0; sy < 144; sy++) {
        u8 *row = out + sy * 42;
        u8 *row_attr = out_attr + sy * 168;
        int window_active = window_enabled && sy >= wy && wx < 160;

        int bg_y = (sy + scy) & 0xff;
        int tile_row = bg_y >> 3;
        int row_in_tile = bg_y & 7;
        int bg_x = scx & ~7;

        int tile;
        for (tile = 0; tile < 21; tile++) {
            int tile_col = (bg_x >> 3) & 31;
            int map_offset = bg_map_off + tile_row * 32 + tile_col;
            int tile_idx = vram[map_offset];

            // CGB: read tile attributes from VRAM bank 1
            u8 tile_attr = vram[0x2000 + map_offset];
            int vram_bank = (tile_attr & CGB_ATTR_VRAM_BANK) ? 0x2000 : 0;

            // Apply vertical flip
            int use_row = (tile_attr & CGB_ATTR_VFLIP) ? (7 - row_in_tile) : row_in_tile;

            int tile_off = unsigned_mode
                ? tile_base_off + 16 * tile_idx
                : tile_base_off + 16 * (signed char) tile_idx;

            u8 data1 = vram[vram_bank + tile_off + use_row * 2];
            u8 data2 = vram[vram_bank + tile_off + use_row * 2 + 1];

            // Apply horizontal flip
            if (tile_attr & CGB_ATTR_HFLIP) {
                data1 = hflip_lut[data1];
                data2 = hflip_lut[data2];
            }

            // Build attribute byte: palette (0-2), priority (3)
            u8 attr_val = (tile_attr & CGB_ATTR_PALETTE) |
                          ((tile_attr & CGB_ATTR_PRIORITY) ? ATTR_PRIORITY : 0);

            render_tile_row_cgb(row + tile * 2, row_attr + tile * 8, data1, data2, attr_val);
            bg_x = (bg_x + 8) & 0xff;
        }

        // overlay window if active
        if (window_active) {
            int win_y = sy - wy;
            int win_tile_row = win_y >> 3;
            int win_row_in_tile = win_y & 7;
            int win_start = (wx > 0 ? wx : 0) + scx_offset;
            int win_end = 160 + scx_offset;
            int win_x = wx < 0 ? -wx : 0;

            while (win_start < win_end) {
                int tile_col = (win_x >> 3) & 31;
                int pixel_in_tile = win_x & 7;
                int map_offset = win_map_off + win_tile_row * 32 + tile_col;
                int tile_idx = vram[map_offset];

                u8 tile_attr = vram[0x2000 + map_offset];
                int vram_bank = (tile_attr & CGB_ATTR_VRAM_BANK) ? 0x2000 : 0;
                int use_row = (tile_attr & CGB_ATTR_VFLIP) ? (7 - win_row_in_tile) : win_row_in_tile;

                int tile_off = unsigned_mode
                    ? tile_base_off + 16 * tile_idx
                    : tile_base_off + 16 * (signed char) tile_idx;

                u8 data1 = vram[vram_bank + tile_off + use_row * 2];
                u8 data2 = vram[vram_bank + tile_off + use_row * 2 + 1];

                if (tile_attr & CGB_ATTR_HFLIP) {
                    data1 = hflip_lut[data1];
                    data2 = hflip_lut[data2];
                }

                u8 attr_val = (tile_attr & CGB_ATTR_PALETTE) |
                              ((tile_attr & CGB_ATTR_PRIORITY) ? ATTR_PRIORITY : 0);

                if (pixel_in_tile == 0 && (win_start & 3) == 0 && win_start + 8 <= win_end) {
                    render_tile_row_cgb(row + (win_start >> 2), row_attr + win_start,
                                       data1, data2, attr_val);
                    win_start += 8;
                    win_x += 8;
                } else {
                    // Partial tile for CGB - use per-pixel approach
                    int pixels_to_draw = 8 - pixel_in_tile;
                    int k;
                    if (win_start + pixels_to_draw > win_end) {
                        pixels_to_draw = win_end - win_start;
                    }
                    for (k = 0; k < pixels_to_draw; k++) {
                        int src_pixel = pixel_in_tile + k;
                        int bit = 7 - src_pixel;
                        int col = ((data1 >> bit) & 1) | (((data2 >> bit) & 1) << 1);
                        int dst_pixel = win_start + k;
                        int dst_byte = dst_pixel >> 2;
                        int dst_bit = dst_pixel & 3;
                        row[dst_byte] = packed_set_pixel(row[dst_byte], dst_bit, col);
                        row_attr[dst_pixel] = attr_val;
                    }
                    win_start += pixels_to_draw;
                    win_x += pixels_to_draw;
                }
            }
        }
    }
}

void lcd_cgb_render_objs(struct dmg *dmg)
{
    struct oam_entry {
        u8 pos_y;
        u8 pos_x;
        u8 tile;
        u8 attrs;
    };

    struct oam_entry *oam = &((struct oam_entry *) dmg->lcd->oam)[39];
    int tall = lcd_isset(dmg->lcd, REG_LCDC, LCDC_OBJ_SIZE);
    u8 *vram = dmg->video_ram;
    u8 *pixels = dmg->lcd->pixels;
    u8 *attrs_buf = dmg->lcd->attrs;
    int bg_enabled = lcd_isset(dmg->lcd, REG_LCDC, LCDC_ENABLE_BG);

    int scx_offset = lcd_read(dmg->lcd, REG_SCX) & 7;

    int k;
    for (k = 39; k >= 0; k--, oam--) {
        if (oam->pos_y == 0 || oam->pos_y >= 160) {
            continue;
        }
        if (oam->pos_x == 0 || oam->pos_x >= 168) {
            continue;
        }

        // CGB: bits 0-2 = palette number, bit 3 = VRAM bank
        int vram_bank = (oam->attrs & OAM_ATTR_CGB_VRAM_BANK) ? 0x2000 : 0;
        int tile_off = vram_bank + 16 * oam->tile;
        u8 cgb_palette = oam->attrs & OAM_ATTR_CGB_PALETTE;

        int lcd_x = oam->pos_x - 8;
        int lcd_y = oam->pos_y - 16;
        int tile_bytes = tall ? 32 : 16;
        int mirror_x = oam->attrs & OAM_ATTR_MIRROR_X;
        int mirror_y = oam->attrs & OAM_ATTR_MIRROR_Y;
        int behind_bg = oam->attrs & OAM_ATTR_BEHIND_BG;

        int b;
        for (b = 0; b < tile_bytes; b += 2) {
            int row_y = lcd_y + (b >> 1);
            if (row_y < 0 || row_y >= 144) {
                continue;
            }

            int use_b = mirror_y ? (tile_bytes - 2) - b : b;
            u8 data1 = vram[tile_off + use_b];
            u8 data2 = vram[tile_off + use_b + 1];

            int x_start = lcd_x < 0 ? -lcd_x : 0;
            int x_end = lcd_x + 8 > 160 ? 160 - lcd_x : 8;

            u8 *row = pixels + row_y * 42;
            u8 *row_attr = attrs_buf + row_y * 168;
            int x;

            if (mirror_x) {
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

                        u8 bg_attr = row_attr[px];
                        u8 bg_pixel = packed_get_pixel(row[byte_idx], bit_idx);

                        // Sprite hidden if:
                        // - bg_enabled AND bg_attr has priority AND bg_pixel != 0
                        // - OR behind_bg AND bg_pixel != 0
                        if (bg_enabled && (bg_attr & ATTR_PRIORITY) && bg_pixel != 0) {
                            continue;
                        }
                        if (behind_bg && bg_pixel != 0) {
                            continue;
                        }

                        row[byte_idx] = packed_set_pixel(row[byte_idx], bit_idx, col_index);
                        row_attr[px] = ATTR_IS_SPRITE | cgb_palette;
                    }
                }
            } else {
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

                        u8 bg_attr = row_attr[px];
                        u8 bg_pixel = packed_get_pixel(row[byte_idx], bit_idx);

                        if (bg_enabled && (bg_attr & ATTR_PRIORITY) && bg_pixel != 0) {
                            continue;
                        }
                        if (behind_bg && bg_pixel != 0) {
                            continue;
                        }

                        row[byte_idx] = packed_set_pixel(row[byte_idx], bit_idx, col_index);
                        row_attr[px] = ATTR_IS_SPRITE | cgb_palette;
                    }
                }
            }
        }
    }
}
