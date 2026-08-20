/* CGB-specific LCD rendering functions
   Separate from lcd.c to keep DMG rendering unchanged */

#include <string.h>

#include "types.h"
#include "lcd.h"
#include "dmg.h"
#include "cgb.h"

// LUT for CGB tile decoding - maps nibble pairs to packed 4-pixel bytes
// Index = (data1_nibble << 4) | data2_nibble
// This is the same format as DMG but without palette mapping
static u8 tile_decode_cgb[256];

void lcd_cgb_init_lut(void)
{
    int k;
    for (k = 0; k < 256; k++) {
        // k = (data1_nibble << 4) | data2_nibble
        // Decode to 4 pixels in packed format
        u8 p0 = ((k >> 7) & 1) | (((k >> 3) & 1) << 1);
        u8 p1 = ((k >> 6) & 1) | (((k >> 2) & 1) << 1);
        u8 p2 = ((k >> 5) & 1) | (((k >> 1) & 1) << 1);
        u8 p3 = ((k >> 4) & 1) | ((k & 1) << 1);
        tile_decode_cgb[k] = (p0 << 6) | (p1 << 4) | (p2 << 2) | p3;
    }
}

// CGB version: render 8 pixels without doing the palette mapping yet
// Also writes 8 bytes of per-pixel attribute data
static inline void render_tile_row_cgb(u8 *p, u8 *a, u8 data1, u8 data2, u8 attr_val)
{
    p[0] = tile_decode_cgb[(data1 & 0xf0) | (data2 >> 4)];
    p[1] = tile_decode_cgb[((data1 & 0x0f) << 4) | (data2 & 0x0f)];

    a[0] = attr_val;
    a[1] = attr_val;
    a[2] = attr_val;
    a[3] = attr_val;
    a[4] = attr_val;
    a[5] = attr_val;
    a[6] = attr_val;
    a[7] = attr_val;
}

// render scanlines [sy_start, sy_end) of the background and window
void lcd_cgb_render_band(
    struct dmg *dmg,
    int sy_start,
    int sy_end,
    const struct raster_regs *regs)
{
    u8 *vram = dmg->vram;
    u8 *out = dmg->lcd->pixels;
    u8 *out_attr = dmg->lcd->attrs;

    int lcdc = regs->lcdc;
    int scx = regs->scx;
    int scy = regs->scy;
    int wx = regs->wx - 7;
    int wy = regs->wy;
    int window_enabled = lcdc & LCDC_ENABLE_WINDOW;

    int bg_map_off = (lcdc & LCDC_BG_TILE_MAP) ? 0x1c00 : 0x1800;
    int win_map_off = (lcdc & LCDC_WINDOW_TILE_MAP) ? 0x1c00 : 0x1800;
    int unsigned_mode = lcdc & LCDC_BG_TILE_DATA;
    int tile_base_off = unsigned_mode ? 0 : 0x1000;

    int scx_offset = scx & 7;

    int odd = dmg->lcd->row_stride - 1;
    int voff = dmg->lcd->row_voff;
    int sy;
    for (sy = sy_start; sy < sy_end; sy++) {
        u8 *row = out + (sy + voff) * 42;
        u8 *row_attr = out_attr + (sy + voff) * 168;
        int window_active = window_enabled && sy >= wy && wx < 160;

        int bg_y;

        if (sy & odd) {
            dmg->lcd->window_line += window_active != 0;
            continue;
        }

        bg_y = (sy + scy) & 0xff;
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

            u8 attr_val = tile_attr & (CGB_ATTR_PALETTE | ATTR_PRIORITY);

            render_tile_row_cgb(row + tile * 2, row_attr + tile * 8, data1, data2, attr_val);
            bg_x = (bg_x + 8) & 0xff;
        }

        // overlay window if active
        if (window_active) {
            int win_y = dmg->lcd->window_line++;
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

                u8 attr_val = tile_attr & (CGB_ATTR_PALETTE | ATTR_PRIORITY);

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

void lcd_cgb_render_objs_band(
    struct dmg *dmg,
    int sy_start,
    int sy_end,
    const struct raster_regs *regs)
{
    struct oam_entry *oam = &((struct oam_entry *) dmg->lcd->oam)[39];
    int tall = regs->lcdc & LCDC_OBJ_SIZE;
    u8 *vram = dmg->vram;
    u8 *pixels = dmg->lcd->pixels;
    u8 *attrs_buf = dmg->lcd->attrs;
    int bg_enabled = regs->lcdc & LCDC_ENABLE_BG;
    int odd = dmg->lcd->row_stride - 1;
    int voff = dmg->lcd->row_voff;
    u16 sel[40];

    int scx_offset = regs->scx & 7;

    lcd_select_objs(dmg->lcd->oam, tall, sy_start, sy_end, sel);

    int k;
    for (k = 39; k >= 0; k--, oam--) {
        if (!sel[k]) {
            continue;
        }
        if (oam->pos_x == 0 || oam->pos_x >= 168) {
            continue;
        }

        // CGB: bits 0-2 = palette number, bit 3 = VRAM bank
        int vram_bank = (oam->attrs & OAM_ATTR_CGB_VRAM_BANK) ? 0x2000 : 0;
        int tile_off = vram_bank + 16 * (tall ? (oam->tile & 0xfe) : oam->tile);
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
            if (!(sel[k] & (1 << (b >> 1))) || (row_y & odd)) {
                continue;
            }

            int use_b = mirror_y ? (tile_bytes - 2) - b : b;
            u8 data1 = vram[tile_off + use_b];
            u8 data2 = vram[tile_off + use_b + 1];

            int x_start = lcd_x < 0 ? -lcd_x : 0;
            int x_end = lcd_x + 8 > 160 ? 160 - lcd_x : 8;

            u8 *row = pixels + (row_y + voff) * 42;
            u8 *row_attr = attrs_buf + (row_y + voff) * 168;
            int x;

            if (mirror_x) {
                data1 = hflip_lut[data1];
                data2 = hflip_lut[data2];
            }

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

                    // BG wins only if LCDC bit 0 set, BG color 1-3, and
                    // either priority bit set
                    if (bg_enabled && bg_pixel != 0 &&
                            ((bg_attr & ATTR_PRIORITY) || behind_bg)) {
                        continue;
                    }

                    row[byte_idx] = packed_set_pixel(row[byte_idx], bit_idx, col_index);
                    row_attr[px] = ATTR_IS_SPRITE | cgb_palette;
                }
            }
        }
    }
}
