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

// previous rendered frame for lcd_diff_rows
static u8 prev_pixels[42 * 144];
static u8 prev_attrs[168 * 144];
static u8 prev_row_scx[144];
static int diff_valid;

// 1 if bg color index is nonzero
static u8 bg_opacity[21 * 144 + 1];

// LUT for horizontal flip: reverses bit order in a byte
u8 hflip_lut[256];

// expands an 8-bit opacity mask to packed 2bpp: bit k -> bits 2k+1,2k
static u16 mask_expand[256];

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

    for (k = 0; k < 256; k++) {
        u16 m = 0;
        int b;
        for (b = 0; b < 8; b++) {
            if (k & (1 << b)) {
                m |= 3 << (b * 2);
            }
        }
        mask_expand[k] = m;
    }
}

static void build_packed_lut(u8 *out, u8 palette)
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
        out[k] = (p0 << 6) | (p1 << 4) | (p2 << 2) | p3;
    }
}

static int lut_palette = -1;

void lcd_update_palette_lut(u8 palette)
{
    if (palette == lut_palette) {
        return;
    }
    lut_palette = palette;
    build_packed_lut(tile_decode_packed, palette);
}

// same encoding as tile_decode_packed
static u8 obj_decode_packed[2][256];
static int obj_lut_palette[2] = { -1, -1 };

static const u8 *obj_palette_lut(int sel, u8 palette)
{
    if (obj_lut_palette[sel] != palette) {
        obj_lut_palette[sel] = palette;
        build_packed_lut(obj_decode_packed[sel], palette);
    }
    return obj_decode_packed[sel];
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
    lcd->row_scx_uniform = 1;
    memset(lcd->row_dirty, ROW_DIRTY_CONTENT | ROW_DIRTY_OFFSET, 144);
    lcd->frame_dirty = ROW_DIRTY_CONTENT | ROW_DIRTY_OFFSET;
    diff_valid = 0;
    // Initialize CGB palette dirty flags to all-dirty so first frame updates everything
    lcd->bg_palette_dirty = 0xFFFFFFFF;
    lcd->obj_palette_dirty = 0xFFFFFFFF;
    lcd->palette_frame_dirty = 0;
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

// reject scrolling frames fast, static rows have to compare the whole
// row but that's still faster than drawing
void lcd_diff_rows(struct lcd *lcd, int cgb)
{
    u8 dirty_all = 0;
    int y;

    if (!diff_valid) {
        diff_valid = 1;
        memcpy(prev_pixels, lcd->pixels, 42 * 144);
        memcpy(prev_row_scx, lcd->row_scx, 144);
        if (cgb) {
            memcpy(prev_attrs, lcd->attrs, 168 * 144);
        }
        memset(lcd->row_dirty, ROW_DIRTY_CONTENT | ROW_DIRTY_OFFSET, 144);
        lcd->frame_dirty = ROW_DIRTY_CONTENT | ROW_DIRTY_OFFSET;
        return;
    }

    for (y = 0; y < 144; y++) {
        const u16 *row = (const u16 *) (lcd->pixels + y * 42);
        u16 *prev = (u16 *) (prev_pixels + y * 42);
        u8 dirty = 0;
        int k;

        for (k = 0; k < 21; k++) {
            if (row[k] != prev[k]) {
                dirty = ROW_DIRTY_CONTENT;
                memcpy(prev, row, 42);
                break;
            }
        }
        if (cgb) {
            const u16 *arow = (const u16 *) (lcd->attrs + y * 168);
            u16 *aprev = (u16 *) (prev_attrs + y * 168);

            if (!dirty) {
                for (k = 0; k < 84; k++) {
                    if (arow[k] != aprev[k]) {
                        dirty = ROW_DIRTY_CONTENT;
                        break;
                    }
                }
            }
            if (dirty) {
                memcpy(aprev, arow, 168);
            }
        }
        if (lcd->row_scx[y] != prev_row_scx[y]) {
            dirty |= ROW_DIRTY_OFFSET;
            prev_row_scx[y] = lcd->row_scx[y];
        }
        lcd->row_dirty[y] = dirty;
        dirty_all |= dirty;
    }
    lcd->frame_dirty = dirty_all;
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

// mirror of render_partial_start for the bg_opacity bits
static inline void render_partial_opacity(
    u8 *opac,
    u8 op,
    int pixel_offset,
    int count,
    int start_pixel
) {
    int k;

    for (k = 0; k < count; k++) {
        int dst = start_pixel + k;
        u8 bit = 0x80 >> (dst & 7);

        if ((op << (pixel_offset + k)) & 0x80) {
            opac[dst >> 3] |= bit;
        } else {
            opac[dst >> 3] &= ~bit;
        }
    }
}

// render scanlines [sy_start, sy_end) of the background and window from
// one register state
void lcd_render_band(
    struct dmg *dmg,
    int sy_start,
    int sy_end,
    const struct raster_regs *regs)
{
    u8 *vram = dmg->video_ram;
    u8 *out = dmg->lcd->pixels;

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

    int sy_limit = sy_end;
    if (window_on_screen) {
        sy_limit = wy;
        if (sy_limit < sy_start) sy_limit = sy_start;
        if (sy_limit > sy_end) sy_limit = sy_end;
    }
    int sy;

    // before window: render all 21 BG tiles, no window overlay
    for (sy = sy_start; sy < sy_limit; sy++) {
        u8 *row = out + sy * 42;
        u8 *opac = bg_opacity + sy * 21;
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
            opac[tile] = data1 | data2;
            bg_x = (bg_x + 8) & 0xff;
        }
    }

    // lines with window: render only visible BG tiles, then window overlay
    for (sy = sy_limit; sy < sy_end; sy++) {
        u8 *row = out + sy * 42;
        u8 *opac = bg_opacity + sy * 21;
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
            opac[tile] = data1 | data2;
            bg_x = (bg_x + 8) & 0xff;
        }

        // Window overlay
        int win_y = dmg->lcd->window_line++;
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

                // window replaces bg opacity across two bytes
                int oi = win_start >> 3;
                int os = win_start & 7;
                int ow = (data1 | data2) << (8 - os);
                int om = 0xff00 >> os;

                opac[oi] = (opac[oi] & ~(om >> 8)) | (ow >> 8);
                opac[oi + 1] = (opac[oi + 1] & ~om) | ow;

                win_start += 8;
                win_x += 8;
            } else {
                // partial tile (start or end of window, or mid-tile start)
                int pixels_to_draw = 8 - pixel_in_tile;
                if (win_start + pixels_to_draw > win_end) {
                    pixels_to_draw = win_end - win_start;
                }
                render_partial_start(row, data1, data2, pixel_in_tile, pixels_to_draw, win_start);
                render_partial_opacity(opac, data1 | data2, pixel_in_tile, pixels_to_draw, win_start);
                win_start += pixels_to_draw;
                win_x += pixels_to_draw;
            }
        }
    }
}

// lcdc bit 0 off on dmg shows bgp color 0
void lcd_render_blank_band(
    struct dmg *dmg,
    int sy_start,
    int sy_end,
    const struct raster_regs *regs)
{
    u8 fill = 0x55 * (regs->bgp & 3);
    int sy;

    for (sy = sy_start; sy < sy_end; sy++) {
        memset(dmg->lcd->pixels + sy * 42, fill, 42);
        memset(bg_opacity + sy * 21, 0, 21);
    }
}

// pick the first ten sprites per scanline in oam order
void lcd_select_objs(
    const u8 *oam,
    int tall,
    int sy_start,
    int sy_end,
    u16 *sel)
{
    u8 count[144];
    int h = tall ? 16 : 8;
    int k;

    memset(&count[sy_start], 0, sy_end - sy_start);

    for (k = 0; k < 40; k++) {
        int top = oam[k * 4] - 16;
        int y0 = top < sy_start ? sy_start : top;
        int y1 = top + h > sy_end ? sy_end : top + h;
        u16 m = 0;
        int y;

        for (y = y0; y < y1; y++) {
            if (count[y] < 10) {
                count[y]++;
                m |= 1 << (y - top);
            }
        }
        sel[k] = m;
    }
}

// render sprites with rows in [sy_start, sy_end) from one register state
void lcd_render_objs_band(
    struct dmg *dmg,
    int sy_start,
    int sy_end,
    const struct raster_regs *regs)
{
    struct oam_entry *oam_base = (struct oam_entry *) dmg->lcd->oam;
    int tall = regs->lcdc & LCDC_OBJ_SIZE;
    int bg_on = regs->lcdc & LCDC_ENABLE_BG;
    u8 *vram = dmg->video_ram;
    u8 *pixels = dmg->lcd->pixels;

    // sprites are rendered at screen position + scx offset within the wider buffer
    int scx_offset = regs->scx & 7;

    u16 sel[40];
    u8 order[40];
    int active = 0;
    int k;

    lcd_select_objs(dmg->lcd->oam, tall, sy_start, sy_end, sel);

    // sort drawable sprites by x, oam order breaks ties;
    for (k = 0; k < 40; k++) {
        struct oam_entry *e = &oam_base[k];
        int i;

        if (!sel[k]) {
            continue;
        }
        if (e->pos_x == 0 || e->pos_x >= 168) {
            continue;
        }
        i = active++;
        while (i > 0 && oam_base[order[i - 1]].pos_x > e->pos_x) {
            order[i] = order[i - 1];
            i--;
        }
        order[i] = k;
    }

    for (k = active - 1; k >= 0; k--) {
        struct oam_entry *oam = &oam_base[order[k]];
        u16 rows = sel[order[k]];

        int tile_off = 16 * (tall ? (oam->tile & 0xfe) : oam->tile);
        const u8 *lut = obj_palette_lut(
            !!(oam->attrs & OAM_ATTR_PALETTE),
            oam->attrs & OAM_ATTR_PALETTE ? regs->obp1 : regs->obp0);

        int lcd_x = oam->pos_x - 8;
        int lcd_y = oam->pos_y - 16;
        int tile_bytes = tall ? 32 : 16;
        int mirror_x = oam->attrs & OAM_ATTR_MIRROR_X;
        int mirror_y = oam->attrs & OAM_ATTR_MIRROR_Y;
        int behind = bg_on && (oam->attrs & OAM_ATTR_BEHIND_BG);

        // screen-edge clip as an opacity mask, pixel 0 in the msb
        int clip = 0xff;
        if (lcd_x < 0) {
            clip &= 0xff >> -lcd_x;
        }
        if (lcd_x > 152) {
            clip &= (0xff << (lcd_x - 152)) & 0xff;
        }

        int pos0 = lcd_x + scx_offset;
        int oidx = pos0 >> 3;
        int osh = pos0 & 7;

        int b;
        for (b = 0; b < tile_bytes; b += 2) {
            int row_y = lcd_y + (b >> 1);
            if (!(rows & (1 << (b >> 1)))) {
                continue;
            }

            int use_b = mirror_y ? (tile_bytes - 2) - b : b;
            u8 data1 = vram[tile_off + use_b];
            u8 data2 = vram[tile_off + use_b + 1];

            if (mirror_x) {
                data1 = hflip_lut[data1];
                data2 = hflip_lut[data2];
            }

            int mask8 = (data1 | data2) & clip;
            if (behind) {
                const u8 *ob = bg_opacity + row_y * 21;
                int bg = ob[oidx + 1];

                if (oidx >= 0) {
                    bg |= ob[oidx] << 8;
                }
                mask8 &= ~(bg >> (8 - osh));
            }
            if (!mask8) {
                continue;
            }

            // whole row at once: 16 packed color bits and the opacity
            // mask, merged into at most 3 buffer bytes
            int idx_hi = (data1 & 0xf0) | (data2 >> 4);
            int idx_lo = ((data1 & 0x0f) << 4) | (data2 & 0x0f);
            u32 bits = (lut[idx_hi] << 8) | lut[idx_lo];
            u32 mask = mask_expand[mask8];

            int pos = pos0;
            if (pos < 0) {
                bits <<= -pos * 2;
                mask <<= -pos * 2;
                pos = 0;
            }

            u8 *p = pixels + row_y * 42 + (pos >> 2);
            int sh = 8 - (pos & 3) * 2;
            u32 wbits = bits << sh;
            u32 wmask = mask << sh;
            u8 m;

            m = (u8) (wmask >> 16);
            if (m) {
                p[0] = (p[0] & ~m) | ((u8) (wbits >> 16) & m);
            }
            m = (u8) (wmask >> 8);
            if (m) {
                p[1] = (p[1] & ~m) | ((u8) (wbits >> 8) & m);
            }
            m = (u8) wmask;
            if (m) {
                p[2] = (p[2] & ~m) | ((u8) wbits & m);
            }
        }
    }
}