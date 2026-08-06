// A/B equivalence tests for the vertically-aligned packed buffer.
//
// Every case renders the same register/VRAM/OAM state twice: once packed
// at row_voff = 0 (the legacy layout, identical to the pre-voff renderer)
// and once at row_voff = scy & 7. Both are composed down to the visible
// 160x144 screen (buffer row y + voff, shifted by row_scx) and must match
// byte for byte. A final set checks the point of the whole exercise: a
// fine vertical scroll diffs as offset-only, with no content-dirty rows.
//
// Build with ASan so any render outside the 152-row buffers also fails.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"
#include "lcd.h"
#include "dmg.h"
#include "cgb.h"

static struct dmg d;
static struct lcd l;
static struct cgb_state cgb_state;

static int cases_run;
static int failures;

// xorshift32, fixed seed for reproducible cases
static u32 rng_state = 0x12345678;

static u32 rnd(void)
{
    u32 x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

struct band {
    int start;
    struct raster_regs regs;
};

static void randomize_vram(void)
{
    int k;
    for (k = 0; k < 0x4000; k++) {
        d.video_ram[k] = (u8) rnd();
    }
}

// sprite positions biased toward clip/selection edges
static void randomize_oam(void)
{
    static const u8 edge_y[] = { 0, 1, 8, 9, 15, 16, 17, 152, 153, 159, 160 };
    static const u8 edge_x[] = { 0, 1, 2, 7, 8, 9, 159, 160, 166, 167, 168 };
    int k;

    for (k = 0; k < 40; k++) {
        u32 r = rnd();
        l.oam[k * 4] = (r & 1) ? edge_y[(r >> 8) % sizeof(edge_y)] : (u8) (r >> 16);
        r = rnd();
        l.oam[k * 4 + 1] = (r & 1) ? edge_x[(r >> 8) % sizeof(edge_x)] : (u8) (r >> 16);
        l.oam[k * 4 + 2] = (u8) rnd();
        l.oam[k * 4 + 3] = (u8) rnd();
    }
}

// mirror of render_band_pass + the render_frame band replay for one frame
static void render_frame_bands(const struct band *bands, int nbands, int voff)
{
    int cgb_mode = d.cgb && d.cgb->mode;
    int k;

    l.row_voff = (u8) voff;
    l.window_line = 0;

    for (k = 0; k < nbands; k++) {
        int sy_start = bands[k].start;
        int sy_end = (k + 1 < nbands) ? bands[k + 1].start : 144;
        const struct raster_regs *regs = &bands[k].regs;

        memset(&l.row_scx[sy_start + voff], regs->scx & 7, sy_end - sy_start);

        if (regs->lcdc & LCDC_ENABLE_BG) {
            if (cgb_mode) {
                lcd_cgb_render_band(&d, sy_start, sy_end, regs);
            } else {
                lcd_update_palette_lut(regs->bgp);
                lcd_render_band(&d, sy_start, sy_end, regs);
            }
        } else if (!cgb_mode) {
            lcd_render_blank_band(&d, sy_start, sy_end, regs);
        }
        if (regs->lcdc & LCDC_ENABLE_OBJ) {
            if (cgb_mode) {
                lcd_cgb_render_objs_band(&d, sy_start, sy_end, regs);
            } else {
                lcd_render_objs_band(&d, sy_start, sy_end, regs);
            }
        }
    }

    if (l.row_stride == 2) {
        for (k = voff; k < voff + 144; k += 2) {
            l.row_scx[k + 1] = l.row_scx[k];
        }
    }
}

// what the blitters show: buffer row y + voff shifted by its row_scx
static void compose(u8 *out_shade, u8 *out_attr)
{
    int voff = l.row_voff;
    int y, x;

    for (y = 0; y < 144; y++) {
        int sy = (l.row_stride == 2 ? (y & ~1) : y) + voff;
        const u8 *row = l.pixels + sy * 42;
        int off = l.row_scx[sy];

        for (x = 0; x < 160; x++) {
            int px = x + off;
            out_shade[y * 160 + x] = (row[px >> 2] >> (6 - 2 * (px & 3))) & 3;
            if (out_attr) {
                out_attr[y * 160 + x] = l.attrs[sy * 168 + px];
            }
        }
    }
}

static void check_case(const struct band *bands, int nbands, const char *name)
{
    static u8 shade_a[160 * 144], shade_b[160 * 144];
    static u8 attr_a[160 * 144], attr_b[160 * 144];
    int cgb_mode = d.cgb && d.cgb->mode;
    int voff = bands[0].regs.scy & 7;

    if (l.row_stride == 2) {
        voff &= ~1;
    }

    // same fill before both renders so rows a config legitimately leaves
    // untouched compare equal
    memset(l.pixels, 0xaa, 42 * LCD_BUF_ROWS);
    memset(l.attrs, 0, 168 * LCD_BUF_ROWS);
    memset(l.row_scx, 0, LCD_BUF_ROWS);
    render_frame_bands(bands, nbands, 0);
    compose(shade_a, cgb_mode ? attr_a : NULL);

    memset(l.pixels, 0xaa, 42 * LCD_BUF_ROWS);
    memset(l.attrs, 0, 168 * LCD_BUF_ROWS);
    memset(l.row_scx, 0, LCD_BUF_ROWS);
    render_frame_bands(bands, nbands, voff);
    compose(shade_b, cgb_mode ? attr_b : NULL);

    cases_run++;
    if (memcmp(shade_a, shade_b, sizeof(shade_a))
            || (cgb_mode && memcmp(attr_a, attr_b, sizeof(attr_a)))) {
        const struct raster_regs *r = &bands[0].regs;
        failures++;
        fprintf(stderr,
                "FAIL %s: scy=%d scx=%d lcdc=%02x wy=%d wx=%d "
                "bands=%d stride=%d voff=%d\n",
                name, r->scy, r->scx, r->lcdc, r->wy, r->wx,
                nbands, l.row_stride, voff);
    }
}

static void base_regs(struct raster_regs *r)
{
    memset(r, 0, sizeof(*r));
    r->lcdc = LCDC_ENABLE | LCDC_ENABLE_BG;
    r->bgp = 0xe4;
    r->obp0 = 0xd2;
    r->obp1 = 0x93;
    r->wx = 200; // window off screen unless a test moves it
    r->wy = 200;
}

// scx values: every fine offset, both sides of the coarse wrap, and the
// high end where scx + 160 wraps the map
static const u8 scx_list[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 96,
    247, 248, 249, 250, 253, 254, 255
};
#define N_SCX ((int) sizeof(scx_list))

static void test_bg_scroll_sweep(void)
{
    static const u8 lcdc_bits[] = {
        0,
        LCDC_BG_TILE_DATA,
        LCDC_BG_TILE_MAP,
        LCDC_BG_TILE_DATA | LCDC_BG_TILE_MAP,
    };
    struct band b;
    int cfg, scy, k;

    for (cfg = 0; cfg < 4; cfg++) {
        randomize_vram();
        randomize_oam();
        for (scy = 0; scy < 256; scy++) {
            for (k = 0; k < N_SCX; k++) {
                base_regs(&b.regs);
                b.start = 0;
                b.regs.lcdc |= lcdc_bits[cfg] | LCDC_ENABLE_OBJ;
                if (cfg & 1) {
                    b.regs.lcdc |= LCDC_OBJ_SIZE;
                }
                b.regs.scy = (u8) scy;
                b.regs.scx = scx_list[k];
                check_case(&b, 1, "bg+obj scroll");
            }
        }
    }
}

static void test_window_sweep(void)
{
    static const u8 wx_list[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 80,
        158, 159, 160, 165, 166, 167, 200
    };
    struct band b;
    int wy, k;

    randomize_vram();
    randomize_oam();

    for (wy = 0; wy <= 146; wy++) {
        for (k = 0; k < (int) sizeof(wx_list); k++) {
            base_regs(&b.regs);
            b.start = 0;
            b.regs.lcdc |= LCDC_ENABLE_WINDOW | LCDC_ENABLE_OBJ
                    | LCDC_WINDOW_TILE_MAP;
            b.regs.wy = (u8) wy;
            b.regs.wx = wx_list[k];
            b.regs.scy = (u8) rnd();
            b.regs.scx = (u8) rnd();
            check_case(&b, 1, "window");
        }
    }
}

static void test_blank_band(void)
{
    struct band b;
    int scy;

    randomize_vram();
    randomize_oam();

    for (scy = 0; scy < 256; scy += 3) {
        base_regs(&b.regs);
        b.start = 0;
        b.regs.lcdc &= ~LCDC_ENABLE_BG;
        b.regs.lcdc |= LCDC_ENABLE_OBJ;
        b.regs.scy = (u8) scy;
        b.regs.bgp = (u8) rnd();
        check_case(&b, 1, "blank+obj");
    }
}

// mid-frame changes to everything except scy: bands must land on the
// same rows in both layouts
static void test_multi_band(void)
{
    struct band bands[4];
    int c, k;

    for (c = 0; c < 2000; c++) {
        int nbands = 2 + (rnd() % 3);
        int scy = (u8) rnd();
        int line = 0;

        if ((c & 0xff) == 0) {
            randomize_vram();
            randomize_oam();
        }

        for (k = 0; k < nbands; k++) {
            line = k ? line + 1 + (rnd() % (140 / nbands)) : 0;
            bands[k].start = line;
            base_regs(&bands[k].regs);
            bands[k].regs.scy = (u8) scy;
            bands[k].regs.scx = (u8) rnd();
            bands[k].regs.lcdc |= (rnd() & 1 ? LCDC_ENABLE_OBJ : 0)
                    | (rnd() & 1 ? LCDC_BG_TILE_DATA : 0);
            if (rnd() & 1) {
                bands[k].regs.lcdc |= LCDC_ENABLE_WINDOW;
                bands[k].regs.wy = (u8) (rnd() % 150);
                bands[k].regs.wx = (u8) (rnd() % 170);
            }
        }
        check_case(bands, nbands, "multi-band");
    }
}

static void test_half_res(void)
{
    struct band b;
    int scy, k;

    l.row_stride = 2;
    randomize_vram();
    randomize_oam();

    for (scy = 0; scy < 256; scy++) {
        for (k = 0; k < N_SCX; k += 2) {
            base_regs(&b.regs);
            b.start = 0;
            b.regs.lcdc |= LCDC_ENABLE_OBJ;
            b.regs.scy = (u8) scy;
            b.regs.scx = scx_list[k];
            if (scy & 1) {
                b.regs.lcdc |= LCDC_ENABLE_WINDOW;
                b.regs.wy = (u8) (scy % 150);
                b.regs.wx = (u8) (scy % 170);
            }
            check_case(&b, 1, "half-res");
        }
    }
    l.row_stride = 1;
}

static void test_cgb(void)
{
    struct band b;
    int scy, k;

    d.cgb = &cgb_state;
    cgb_state.mode = 1;
    randomize_vram();
    randomize_oam();

    for (scy = 0; scy < 256; scy++) {
        for (k = 0; k < N_SCX; k++) {
            base_regs(&b.regs);
            b.start = 0;
            b.regs.lcdc |= LCDC_ENABLE_OBJ;
            b.regs.scy = (u8) scy;
            b.regs.scx = scx_list[k];
            if (scy & 2) {
                b.regs.lcdc |= LCDC_ENABLE_WINDOW;
                b.regs.wy = (u8) (scy % 150);
                b.regs.wx = (u8) ((scy * 3) % 170);
            }
            check_case(&b, 1, "cgb");
        }
    }
    d.cgb = NULL;
}

// the payoff property: scrolling within a tile is offset-only, crossing
// a tile boundary is content-dirty
static void test_fine_scroll_diff(void)
{
    struct band b;
    int scy, y;

    lcd_new(&l); // reset diff state
    memset(l.oam, 0, sizeof(l.oam));
    randomize_vram();

    base_regs(&b.regs);
    b.start = 0;

    for (scy = 16; scy <= 25; scy++) {
        int voff = scy & 7;
        int content = 0, offset = 0;

        b.regs.scy = (u8) scy;
        render_frame_bands(&b, 1, voff);
        lcd_diff_rows(&l, 0);

        for (y = voff; y < voff + 144; y++) {
            content += (l.row_dirty[y] & ROW_DIRTY_CONTENT) != 0;
            offset += (l.row_dirty[y] & ROW_DIRTY_OFFSET) != 0;
        }
        cases_run++;

        if (scy == 16) {
            continue; // first frame diffs against garbage, all dirty
        }
        if (scy < 24) {
            // fine scroll: only the row entering at the bottom edge can
            // be content-dirty, every row re-blits at the new offset
            if (content > 1 || offset != 144) {
                failures++;
                fprintf(stderr, "FAIL fine-scroll scy=%d: "
                        "content=%d offset=%d (want <=1/144)\n",
                        scy, content, offset);
            }
        } else if (scy == 24) {
            // coarse step: content moved one tile row through the buffer
            if (content < 100) {
                failures++;
                fprintf(stderr, "FAIL coarse-step scy=%d: content=%d "
                        "(want ~144)\n", scy, content);
            }
        }
    }
}

int main(void)
{
    lcd_init_lut();
    lcd_cgb_init_lut();

    memset(&d, 0, sizeof(d));
    lcd_new(&l);
    d.lcd = &l;

    test_bg_scroll_sweep();
    test_window_sweep();
    test_blank_band();
    test_multi_band();
    test_half_res();
    test_cgb();
    test_fine_scroll_diff();

    printf("test_lcd: %d cases, %d failures\n", cases_run, failures);
    return failures != 0;
}
