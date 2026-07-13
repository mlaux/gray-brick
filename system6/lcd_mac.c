/* Game Boy emulator for 68k Macs
   lcd_mac.c - LCD rendering with 1x/2x scaling */

#include <Quickdraw.h>
#include <Windows.h>
#include <Palettes.h>

#include "../src/lcd.h"
#include "../src/dmg.h"
#include "../src/cgb.h"
#include "emulator.h"
#include "lcd_mac.h"
#include "settings.h"

#include "gb_palettes.h"
int current_palette = 0;

// set for window updates, palette/scale/mode etc
static int force_all = 1;

void lcd_mac_invalidate(void)
{
  force_all = 1;
}

// packed byte bits:  [7-6]  [5-4]  [3-2]  [1-0]
// GB pixel:           p0     p1     p2     p3

// lookup tables for 2x dithered rendering
// index = 4 packed GB pixels (2 bits each), output = 8 screen pixels (1bpp)
static unsigned char dither_row0[256];
static unsigned char dither_row1[256];

// lookup tables for 1x threshold rendering
// thresh_hi: 4 GB pixels -> 4 bits in high nibble
// thresh_lo: 4 GB pixels -> 4 bits in low nibble
static unsigned char thresh_hi[256];
static unsigned char thresh_lo[256];

// LUTs for indexed color modes - map packed byte directly to screen pixels
// 1x: 4 pixels per packed byte
static unsigned long color_lut_1x[256];
// 2x: 8 pixels per packed byte (each GB pixel doubled), split into two 32-bit values
static unsigned long color_lut_2x_lo[256];  // pixels 0,0,1,1
static unsigned long color_lut_2x_hi[256];  // pixels 2,2,3,3

void init_dither_lut(void)
{
  // 2x2 dither patterns for each GB color (0-3)
  // stored in high 2 bits: 00=white, 01=right black, 10=left black, 11=both black
  // color 0 (white):      top=00 bot=00
  // color 1 (light gray): top=00 bot=01 (one black pixel, bottom-right)
  // color 2 (dark gray):  top=10 bot=01 (checkerboard)
  // color 3 (black):      top=11 bot=11
  static const unsigned char pat_top[4] = { 0x00, 0x00, 0x80, 0xc0 };
  static const unsigned char pat_bot[4] = { 0x00, 0x40, 0x40, 0xc0 };
  int idx;

  for (idx = 0; idx < 256; idx++) {
    int p0 = (idx >> 6) & 3;
    int p1 = (idx >> 4) & 3;
    int p2 = (idx >> 2) & 3;
    int p3 = idx & 3;

    dither_row0[idx] = pat_top[p0] | (pat_top[p1] >> 2) |
                       (pat_top[p2] >> 4) | (pat_top[p3] >> 6);
    dither_row1[idx] = pat_bot[p0] | (pat_bot[p1] >> 2) |
                       (pat_bot[p2] >> 4) | (pat_bot[p3] >> 6);

    // threshold LUTs for 1x mode: color >= 2 is black
    // thresh_hi produces high nibble, thresh_lo produces low nibble
    thresh_hi[idx] = ((p0 >= 2) ? 0x80 : 0) | ((p1 >= 2) ? 0x40 : 0) |
                     ((p2 >= 2) ? 0x20 : 0) | ((p3 >= 2) ? 0x10 : 0);
    thresh_lo[idx] = ((p0 >= 2) ? 0x08 : 0) | ((p1 >= 2) ? 0x04 : 0) |
                     ((p2 >= 2) ? 0x02 : 0) | ((p3 >= 2) ? 0x01 : 0);
  }
}

void init_indexed_lut(WindowPtr wp)
{
  int k;
  PaletteHandle pal;
  unsigned char gray_index[4];

  pal = NewPalette(4, nil, pmTolerant, 0);

  for (k = 0; k < 4; k++) {
    SetEntryColor(pal, k, &gb_palettes[current_palette].colors[k]);
  }

  SetPalette(wp, pal, true);
  ActivatePalette(wp);

  // get mapped color indices for current screen CLUT
  for (k = 0; k < 4; k++) {
    gray_index[k] = Entry2Index(k);
  }

  // build color LUTs for fast indexed rendering
  for (k = 0; k < 256; k++) {
    unsigned char c0 = gray_index[(k >> 6) & 3];
    unsigned char c1 = gray_index[(k >> 4) & 3];
    unsigned char c2 = gray_index[(k >> 2) & 3];
    unsigned char c3 = gray_index[k & 3];

    // 1x: 4 consecutive pixels
    color_lut_1x[k] = ((unsigned long)c0 << 24) | ((unsigned long)c1 << 16) |
                      ((unsigned long)c2 << 8) | c3;

    // 2x: each pixel doubled horizontally
    color_lut_2x_lo[k] = ((unsigned long)c0 << 24) | ((unsigned long)c0 << 16) |
                         ((unsigned long)c1 << 8) | c1;
    color_lut_2x_hi[k] = ((unsigned long)c2 << 24) | ((unsigned long)c2 << 16) |
                         ((unsigned long)c3 << 8) | c3;
  }
}

// one CopyBits per contiguous run of rows sharing a scroll offset and dirty status
void lcd_blit_color_bands(struct lcd *lcd_ptr, int scale, int all)
{
  CGrafPtr port;
  Rect src_rect, dst_rect;
  int y0 = 0;

  SetPort(g_wp);
  port = (CGrafPtr) g_wp;

  while (y0 < 144) {
    int off = lcd_ptr->row_scx[y0];
    int dirty = all || lcd_ptr->row_dirty[y0] != 0;
    int y1 = y0 + 1;

    while (y1 < 144 && lcd_ptr->row_scx[y1] == off
           && (all || (lcd_ptr->row_dirty[y1] != 0) == dirty)) {
      y1++;
    }

    if (dirty) {
      src_rect.top = y0 * scale;
      src_rect.bottom = y1 * scale;
      src_rect.left = off * scale;
      src_rect.right = (off + 160) * scale;

      dst_rect.top = y0 * scale;
      dst_rect.bottom = y1 * scale;
      dst_rect.left = 0;
      dst_rect.right = 160 * scale;

      CopyBits(
          (BitMap *) &offscreen_pixmap,
          (BitMap *) *port->portPixMap,
          &src_rect, &dst_rect, srcCopy, NULL
      );
    }

    y0 = y1;
  }
}

// same but for BW
static void lcd_blit_bw_bands(struct lcd *lcd_ptr, int scale, int all)
{
  Rect src_rect, dst_rect;
  int y0 = 0;

  SetPort(g_wp);

  while (y0 < 144) {
    int off = lcd_ptr->row_scx[y0];
    int dirty = all || lcd_ptr->row_dirty[y0] != 0;
    int y1 = y0 + 1;

    while (y1 < 144 && lcd_ptr->row_scx[y1] == off
           && (all || (lcd_ptr->row_dirty[y1] != 0) == dirty)) {
      y1++;
    }

    if (dirty) {
      src_rect.top = y0 * scale;
      src_rect.bottom = y1 * scale;
      src_rect.left = off * scale;
      src_rect.right = (off + 160) * scale;

      dst_rect.top = y0 * scale;
      dst_rect.bottom = y1 * scale;
      dst_rect.left = 0;
      dst_rect.right = 160 * scale;

      CopyBits(&offscreen_bmp, &g_wp->portBits,
               &src_rect, &dst_rect, srcCopy, NULL);
    }

    y0 = y1;
  }
}

// 1x rendering, >= 2 is black, doesn't look great but it's fine
static void lcd_draw_1x_copybits(struct lcd *lcd_ptr)
{
  int gy;
  unsigned char *src = lcd_ptr->pixels;
  unsigned char *dst = (unsigned char *) offscreen_buf;

  if (!force_all && !lcd_ptr->frame_dirty) {
    return;
  }

  for (gy = 0; gy < 144; gy++) {
    // not ROW_DIRTY_OFFSET bc the actual pixels are the same for that
    if (force_all || (lcd_ptr->row_dirty[gy] & ROW_DIRTY_CONTENT)) {
      int gx;
      for (gx = 0; gx < 21; gx++) {
        // 2 packed bytes = 8 pixels -> 1 output byte
        dst[gx] = thresh_hi[src[gx * 2]] | thresh_lo[src[gx * 2 + 1]];
      }
    }
    src += 42;
    dst += 21;
  }

  lcd_blit_bw_bands(lcd_ptr, 1, force_all);
}

static void lcd_draw_1x_direct(struct lcd *lcd_ptr)
{
  // TODO REMOVE
}

static void lcd_draw_1x_indexed(struct lcd *lcd_ptr)
{
  int gy;
  unsigned char *src = lcd_ptr->pixels;
  unsigned long *dst = (unsigned long *) offscreen_color_buf;

  if (screen_depth == 1) {
    return;
  }
  if (!force_all && !lcd_ptr->frame_dirty) {
    return;
  }

  for (gy = 0; gy < 144; gy++) {
    // not ROW_DIRTY_OFFSET bc the actual pixels are the same for that
    if (force_all || (lcd_ptr->row_dirty[gy] & ROW_DIRTY_CONTENT)) {
      int gx;
      for (gx = 0; gx < 42; gx++) {
        // LUT maps packed byte directly to 4 color pixels
        dst[gx] = color_lut_1x[src[gx]];
      }
    }
    src += 42;
    dst += 42;
  }

  lcd_blit_color_bands(lcd_ptr, 1, force_all);
}

static void lcd_draw_2x_copybits(struct lcd *lcd_ptr)
{
  int gy;
  unsigned char *src = lcd_ptr->pixels;
  unsigned char *dst = (unsigned char *) offscreen_buf;

  if (!force_all && !lcd_ptr->frame_dirty) {
    return;
  }

  for (gy = 0; gy < 144; gy++) {
    unsigned char *row0 = dst;
    unsigned char *row1 = dst + 42;
    int gx;

    if (!force_all && !(lcd_ptr->row_dirty[gy] & ROW_DIRTY_CONTENT)) {
      src += 42;
      dst += 84;
      continue;
    }

    for (gx = 0; gx < 42; gx++) {
      // packed byte is already the LUT index
      unsigned char idx = *src++;
      *row0++ = dither_row0[idx];
      *row1++ = dither_row1[idx];
    }

    // advance 2 screen rows at a time
    dst += 84;
  }

  lcd_blit_bw_bands(lcd_ptr, 2, force_all);
}

// faster on plus/SE etc
static void lcd_draw_2x_direct(struct lcd *lcd_ptr)
{
  // TODO REMOVE
}

static void lcd_draw_2x_indexed(struct lcd *lcd_ptr)
{
  int gy;
  unsigned char *src = lcd_ptr->pixels;
  unsigned long *dst = (unsigned long *) offscreen_color_buf;

  if (screen_depth == 1) {
    return;
  }
  if (!force_all && !lcd_ptr->frame_dirty) {
    return;
  }

  // convert all 168 buffer pixels per row; per-band scroll offsets are
  // handled by the band blit's source rects, so offset-only rows keep
  // their converted bytes
  for (gy = 0; gy < 144; gy++) {
    // row stride in longs: 336 bytes / 4 = 84 longs
    unsigned long *row0 = dst;
    unsigned long *row1 = dst + 84;
    int gx;

    if (!force_all && !(lcd_ptr->row_dirty[gy] & ROW_DIRTY_CONTENT)) {
      src += 42;
      dst += 168;
      continue;
    }

    for (gx = 0; gx < 42; gx++) {
      // LUT maps packed byte to 8 doubled pixels (two 32-bit values)
      unsigned char idx = *src++;
      unsigned long lo = color_lut_2x_lo[idx];
      unsigned long hi = color_lut_2x_hi[idx];

      // row0:  p0 p0  p1 p1  p2 p2  p3 p3
      // row1:  p0 p0  p1 p1  p2 p2  p3 p3
      row0[0] = lo; row0[1] = hi;
      row1[0] = lo; row1[1] = hi;

      row0 += 2;
      row1 += 2;
    }

    dst += 168;  // 2 rows * 84 longs per row
  }

  lcd_blit_color_bands(lcd_ptr, 2, force_all);
}

void (*draw_funcs[2][3])(struct lcd *) = {
  { lcd_draw_1x_copybits, lcd_draw_1x_direct, lcd_draw_1x_indexed },
  { lcd_draw_2x_copybits, lcd_draw_2x_direct, lcd_draw_2x_indexed },
};

// lcd_mac_cgb.c
void lcd_draw_cgb(struct lcd *lcd_ptr);

// called by dmg_step at vblank
void lcd_draw(struct lcd *lcd_ptr)
{
  if (dmg.cgb && dmg.cgb->mode && screen_depth > 1) {
    lcd_draw_cgb(lcd_ptr);
    force_all = 0;
    return;
  }

  // screen scale is 1 based, video mode is 0 based
  draw_funcs[screen_scale - 1][video_mode](lcd_ptr);
  force_all = 0;
}