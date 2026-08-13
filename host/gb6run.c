// gb6run - host-side GB ROM runner: real src/ hardware code + real JIT
// compiler, executed under Musashi. see PLAN.md phase 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <sys/stat.h>

#include "types.h"
#include "rom.h"
#include "lcd.h"
#include "dmg.h"
#include "cgb.h"
#include "audio.h"
#include "mbc.h"
#include "rom_patches.h"
#include "compiler.h"

#include "../system6/jit.h"
#include "../system6/settings.h"
#include "host.h"

static struct rom rom;
static struct lcd lcd;
static struct audio audio;
static struct cgb_state cgb;
static struct dmg *dmg;

static int opt_serial;
static char serial_buf[65536];
static size_t serial_len;

void host_serial_byte(u8 byte)
{
    if (serial_len < sizeof serial_buf - 1) {
        serial_buf[serial_len++] = byte;
        serial_buf[serial_len] = 0;
    }
    if (opt_serial) {
        putchar(byte);
        fflush(stdout);
    }
}

// ---- frame capture ----------------------------------------------------
// lcd.c renders into a 168-wide packed 2bpp buffer (42 bytes/row); each
// row's visible 160 pixels start at row_scx[y], exactly as the Mac
// blitters read it

static const char *opt_dump_dir;
static int opt_hash_frames;
static int opt_dump_state;
static int opt_log_raster;
static int opt_scx_stats;
static int opt_dirty_stats;
static int opt_mac_sim;
static int opt_exit_stats;
static int opt_half_res;
static const char *opt_insn_log;

// mirror of the 1x B&W dither cell in system6/lcd_mac.c so --half-res
// dumps show what the Mac actually puts on a 1-bit screen
static int half_dither_bit(int shade, int row, int col)
{
    if (shade == 3) {
        return 1;
    }
    if (shade == 2) {
        return row == col;
    }
    if (shade == 1) {
        return row && col;
    }
    return 0;
}

// 160x144, gray (1 byte/px) for DMG or RGB (3 bytes/px) for CGB
static u8 frame_out[160 * 144 * 3];

static size_t extract_frame(struct lcd *l)
{
    int cgb_mode = dmg->cgb && dmg->cgb->mode;
    int voff = l->row_voff;
    size_t n = 0;
    int x, y;

    for (y = 0; y < 144; y++) {
        // half-res only fills even rows; the odd one is its dither partner
        // screen row y sits at buffer row y + voff
        int sy = (opt_half_res ? (y & ~1) : y) + voff;
        const u8 *row = l->pixels + sy * 42;
        int scx_off = l->row_scx[sy];
        for (x = 0; x < 160; x++) {
            int px = x + scx_off;
            int shade = (row[px >> 2] >> (6 - 2 * (px & 3))) & 3;

            if (opt_half_res) {
                frame_out[n++] =
                    half_dither_bit(shade, y & 1, px & 1) ? 0 : 255;
                continue;
            }

            if (!cgb_mode) {
                frame_out[n++] = (u8) ((3 - shade) * 85);
                continue;
            }

            // CGB: attr picks the palette (bit 4 = sprite), pixel value
            // indexes into it; palette ram is RGB555 little-endian
            u8 attr = l->attrs[(y + voff) * 168 + px];
            const u8 *ram = (attr & 0x10) ? l->obj_palette_ram
                                          : l->bg_palette_ram;
            int ci = ((attr & 7) * 4 + shade) * 2;
            u16 rgb555 = ram[ci] | (ram[ci + 1] << 8);
            u8 r5 = rgb555 & 0x1f;
            u8 g5 = (rgb555 >> 5) & 0x1f;
            u8 b5 = (rgb555 >> 10) & 0x1f;
            frame_out[n++] = (u8) ((r5 << 3) | (r5 >> 2));
            frame_out[n++] = (u8) ((g5 << 3) | (g5 >> 2));
            frame_out[n++] = (u8) ((b5 << 3) | (b5 >> 2));
        }
    }
    return n;
}

// row_scx uniformity statistics (--scx-stats): how often the single-blit
// fast path applies, and the shape of the frames that miss it
static u32 scx_stat_frames;
static u32 scx_stat_uniform;
static u32 scx_stat_one_row;
static u32 scx_stat_runs_hist[8];

static void scx_stats_frame(struct lcd *l)
{
    int counts[8] = { 0 };
    int voff = l->row_voff;
    int runs = 1;
    int most = 0;
    int k;

    counts[l->row_scx[voff] & 7]++;
    for (k = 1; k < 144; k++) {
        if (l->row_scx[voff + k] != l->row_scx[voff + k - 1]) {
            runs++;
        }
        counts[l->row_scx[voff + k] & 7]++;
    }

    scx_stat_frames++;
    if (l->row_scx_uniform) {
        scx_stat_uniform++;
    }

    for (k = 1; k < 8; k++) {
        if (counts[k] > counts[most]) {
            most = k;
        }
    }
    if (counts[most] == 143) {
        scx_stat_one_row++;
    }

    scx_stat_runs_hist[runs < 8 ? runs : 7]++;
}

static void scx_stats_summary(FILE *out)
{
    int k;

    fprintf(out, "scx-stats: %u frames, %u uniform (%.1f%%), "
            "%u with a single odd row\n",
            scx_stat_frames, scx_stat_uniform,
            scx_stat_frames
                ? 100.0 * scx_stat_uniform / scx_stat_frames : 0.0,
            scx_stat_one_row);
    fprintf(out, "scx-stats: offset runs/frame:");
    for (k = 1; k < 8; k++) {
        if (scx_stat_runs_hist[k]) {
            fprintf(out, " %d%s:%u", k, k == 7 ? "+" : "",
                    scx_stat_runs_hist[k]);
        }
    }
    fprintf(out, "\n");
}

// dirty-row statistics (--dirty-stats): how much work row-skipping
// blitters save, plus an assertion that rows marked clean really did
// produce identical output to the previous frame
static u32 dr_frames, dr_clean_frames, dr_rows_sum, dr_content_sum;
static u32 dr_mismatches;
static u8 dr_prev_frame[160 * 144 * 3];
static int dr_prev_valid;

static void dirty_stats_frame(struct lcd *l, size_t n)
{
    size_t row_bytes = n / 144;
    int dirty_rows = 0, content_rows = 0;
    int y;

    for (y = 0; y < 144; y++) {
        u8 dirty = l->row_dirty[y + l->row_voff];

        if (dirty) {
            dirty_rows++;
        }
        if (dirty & ROW_DIRTY_CONTENT) {
            content_rows++;
        }
        if (dr_prev_valid && !dirty
                && memcmp(frame_out + y * row_bytes,
                          dr_prev_frame + y * row_bytes, row_bytes)) {
            fprintf(stderr, "dirty-stats: MISMATCH frame %u row %d\n",
                    host_frames_drawn, y);
            dr_mismatches++;
        }
    }

    dr_frames++;
    if (!dirty_rows) {
        dr_clean_frames++;
    }
    dr_rows_sum += dirty_rows;
    dr_content_sum += content_rows;

    memcpy(dr_prev_frame, frame_out, n);
    dr_prev_valid = 1;
}

static void dirty_stats_summary(FILE *out)
{
    fprintf(out, "dirty-stats: %u frames, %u clean (%.1f%%), "
            "avg dirty rows %.1f, avg content rows %.1f, mismatches %u\n",
            dr_frames, dr_clean_frames,
            dr_frames ? 100.0 * dr_clean_frames / dr_frames : 0.0,
            dr_frames ? (double) dr_rows_sum / dr_frames : 0.0,
            dr_frames ? (double) dr_content_sum / dr_frames : 0.0,
            dr_mismatches);
}

// mac blitter simulation (--mac-sim): replay lcd_mac_cgb.c exactly
static u16 ms_lut[16][4];
static u16 ms_off[168 * LCD_BUF_ROWS];
static u16 ms_screen[160 * 144];
static int ms_valid;
static u32 ms_bad_frames, ms_bad_rows;

static void mac_sim_frame(struct lcd *l)
{
    int all = !ms_valid;
    int bad = 0;
    int y, x, k;

    if (all || l->frame_dirty) {
        u32 bg_dirty = l->bg_palette_dirty;
        u32 obj_dirty = l->obj_palette_dirty;

        if (bg_dirty || obj_dirty) {
            for (k = 0; k < 32; k++) {
                if (bg_dirty & (1UL << k)) {
                    ms_lut[k >> 2][k & 3] = l->bg_palette_ram[k * 2]
                            | (l->bg_palette_ram[k * 2 + 1] << 8);
                }
                if (obj_dirty & (1UL << k)) {
                    ms_lut[8 + (k >> 2)][k & 3] = l->obj_palette_ram[k * 2]
                            | (l->obj_palette_ram[k * 2 + 1] << 8);
                }
            }
            l->bg_palette_dirty = 0;
            l->obj_palette_dirty = 0;
        }

        // convert in buffer space, dirty rows only, like lcd_mac_cgb.c
        for (y = 0; y < LCD_BUF_ROWS; y++) {
            if (!all && !(l->row_dirty[y] & ROW_DIRTY_CONTENT)) {
                continue;
            }
            for (x = 0; x < 168; x++) {
                int shade = (l->pixels[y * 42 + (x >> 2)]
                        >> (6 - 2 * (x & 3))) & 3;
                u8 attr = l->attrs[y * 168 + x];
                int lut = ((attr >> 1) & 0x08) | (attr & 0x07);
                ms_off[y * 168 + x] = ms_lut[lut][shade];
            }
        }

        for (y = 0; y < 144; y++) {
            int by = y + l->row_voff;

            if (!all && !l->row_dirty[by]) {
                continue;
            }
            for (x = 0; x < 160; x++) {
                ms_screen[y * 160 + x] =
                        ms_off[by * 168 + l->row_scx[by] + x];
            }
        }
    }
    ms_valid = 1;

    // reference: fresh conversion straight from current state, like
    // extract_frame but kept as rgb555
    for (y = 0; y < 144; y++) {
        int by = y + l->row_voff;
        int row_bad = 0;
        for (x = 0; x < 160; x++) {
            int px = x + l->row_scx[by];
            int shade = (l->pixels[by * 42 + (px >> 2)]
                    >> (6 - 2 * (px & 3))) & 3;
            u8 attr = l->attrs[by * 168 + px];
            const u8 *ram = (attr & ATTR_IS_SPRITE) ? l->obj_palette_ram
                                                    : l->bg_palette_ram;
            int ci = ((attr & 7) * 4 + shade) * 2;
            u16 want = ram[ci] | (ram[ci + 1] << 8);

            if (ms_screen[y * 160 + x] != want) {
                if (!bad && !row_bad) {
                    fprintf(stderr, "mac-sim: frame %u row %d x %d "
                            "got %04x want %04x (attr %02x shade %d "
                            "dirty %02x)\n",
                            host_frames_drawn, y, x,
                            ms_screen[y * 160 + x], want, attr, shade,
                            l->row_dirty[by]);
                }
                row_bad = 1;
            }
        }
        if (row_bad) {
            ms_bad_rows++;
            bad = 1;
        }
    }
    if (bad) {
        ms_bad_frames++;
    }
}

static void frame_hook(struct lcd *l)
{
    size_t n = extract_frame(l);

    if (opt_scx_stats) {
        scx_stats_frame(l);
    }

    if (opt_dirty_stats) {
        dirty_stats_frame(l, n);
    }

    if (opt_mac_sim) {
        mac_sim_frame(l);
    }

    // where in the frame the snapshot render actually fired, and the reg
    // state it sampled (frame_cycles is the end-of-sync beam position, so
    // in chain mode this shows how late the render ran)
    if (opt_log_raster) {
        u32 at = dmg->frame_cycles;
        printf("render f=%u at=%u ly=%u.%u LCDC=%02x STAT=%02x SCY=%02x "
               "SCX=%02x WY=%02x WX=%02x BGP=%02x IE=%02x IF=%02x IME=%d\n",
               host_frames_drawn, at, at / 456, at % 456,
               lcd_read(l, REG_LCDC), lcd_read(l, REG_STAT),
               lcd_read(l, REG_SCY), lcd_read(l, REG_SCX),
               lcd_read(l, REG_WY), lcd_read(l, REG_WX),
               lcd_read(l, REG_BGP), dmg->zero_page[0x7f],
               dmg->interrupt_request_mask, dmg->interrupt_enable);
    }

    if (opt_hash_frames) {
        u64 h = 0xcbf29ce484222325ull;
        size_t k;
        for (k = 0; k < n; k++) {
            h = (h ^ frame_out[k]) * 0x100000001b3ull;
        }
        printf("frame %u %016llx\n", host_frames_drawn,
               (unsigned long long) h);
    }

    if (opt_dump_dir) {
        char path[1024];
        FILE *fp;
        snprintf(path, sizeof path, "%s/frame_%05u.ppm",
                 opt_dump_dir, host_frames_drawn);
        fp = fopen(path, "wb");
        if (!fp) {
            fprintf(stderr, "gb6run: cannot write %s\n", path);
            exit(2);
        }
        fprintf(fp, "P%c\n160 144\n255\n", n == 160 * 144 ? '5' : '6');
        fwrite(frame_out, 1, n, fp);
        fclose(fp);
    }
}

// ---- raster write observation -------------------------------------------
// --log-raster streams writes to the registers a band-splitting renderer
// would replay (SCX/SCY/WX/WY, BGP/OBP0/OBP1, LCDC, OAM DMA), tagged with
// the beam position, then prints a per-register summary at exit. this is
// the PLAN.md phase 3 instrument: measure who would actually use mid-frame
// bands before any replay code exists

struct raster_stat {
    u32 off;                // lcd disabled at write time
    u32 vbl;                // ly >= 144
    u32 vis_same;           // visible area, value unchanged
    u32 vis_chg;            // visible area, value changed
    u32 oam, draw, hbl;     // mode split of vis_chg
    u8 line_min, line_max;  // ly range of vis_chg
};
static struct raster_stat raster_stats[16];
static u32 raster_writes;

// distinct changed lines per frame = the bands a replay would render.
// DMA is kept out of this: replay uses an OAM snapshot, not bands
static u32 rl_frame = 0xffffffff;
static u8 rl_line_seen[144];
static u32 rl_distinct;
static u32 rl_frames_split, rl_bands_sum, rl_bands_max;

static const char *const raster_names[16] = {
    "LCDC", "?", "SCY", "SCX", "?", "?", "DMA", "BGP",
    "OBP0", "OBP1", "WY", "WX", "BCPS", "BCPD", "OCPS", "OCPD"
};

// $ff40-$ff4b map to 0-11, CGB $ff68-$ff6b to 12-15
static int raster_reg_index(u16 addr)
{
    if (addr <= REG_WX) {
        return addr - REG_LCD_BASE;
    }
    return 12 + (addr - 0xff68);
}

static void raster_fold_frame(void)
{
    if (!rl_distinct) {
        return;
    }
    rl_frames_split++;
    rl_bands_sum += rl_distinct;
    if (rl_distinct > rl_bands_max) {
        rl_bands_max = rl_distinct;
    }
    memset(rl_line_seen, 0, sizeof rl_line_seen);
    rl_distinct = 0;
}

static void raster_hook(u16 addr, u8 old, u8 val, u32 ly, u32 pos)
{
    int reg = raster_reg_index(addr);
    struct raster_stat *st = &raster_stats[reg];
    u32 frame = host_frames();
    int on = (addr == REG_LCDC ? old : lcd_read(&lcd, REG_LCDC))
            & LCDC_ENABLE;
    // rewriting the DMA source page still rewrites OAM, so always counts
    // as a change
    int changed = addr == REG_DMA || old != val;
    const char *where;

    if (frame != rl_frame) {
        raster_fold_frame();
        rl_frame = frame;
    }
    raster_writes++;

    if (!on) {
        st->off++;
        where = "off";
    } else if (ly >= 144) {
        st->vbl++;
        where = "vbl";
    } else {
        where = pos < 80 ? "oam" : pos < 252 ? "draw" : "hbl";
        if (!changed) {
            st->vis_same++;
        } else {
            if (!st->vis_chg || ly < st->line_min) {
                st->line_min = ly;
            }
            if (!st->vis_chg || ly > st->line_max) {
                st->line_max = ly;
            }
            st->vis_chg++;
            if (pos < 80) st->oam++;
            else if (pos < 252) st->draw++;
            else st->hbl++;

            // index-only writes (DMA source, BCPS/OCPS) are not bands
            if (addr != REG_DMA && addr != 0xff68 && addr != 0xff6a
                    && !rl_line_seen[ly]) {
                rl_line_seen[ly] = 1;
                rl_distinct++;
            }
        }
    }

    printf("raster f=%u ly=%u %s %s %02x->%02x\n",
           frame, (unsigned) ly, where, raster_names[reg], old, val);
}

static void raster_summary(FILE *fp)
{
    int k;

    raster_fold_frame();
    fprintf(fp, "raster: %u writes over %u frames\n",
            raster_writes, host_frames());
    fprintf(fp,
            "raster: frames needing bands: %u (avg %.1f split lines, max %u)\n",
            rl_frames_split,
            rl_frames_split ? (double) rl_bands_sum / rl_frames_split : 0.0,
            rl_bands_max);
    fprintf(fp,
            "raster: reg     off    vbl   vis=  vis!=   oam  draw   hbl  lines\n");
    for (k = 0; k < 16; k++) {
        struct raster_stat *st = &raster_stats[k];
        if (!(st->off | st->vbl | st->vis_same | st->vis_chg)) {
            continue;
        }
        fprintf(fp, "raster: %-5s %6u %6u %6u %6u %5u %5u %5u",
                raster_names[k], st->off, st->vbl, st->vis_same,
                st->vis_chg, st->oam, st->draw, st->hbl);
        if (st->vis_chg) {
            fprintf(fp, "  %u-%u", st->line_min, st->line_max);
        }
        fputc('\n', fp);
    }
}

// ---- input scripting ----------------------------------------------------
// script lines are "frame:buttons" where buttons is a comma-separated list
// (Up Down Left Right A B Select Start, case-insensitive) giving the FULL
// pad state from that frame on; an empty list releases everything

struct input_event {
    u32 frame;
    u8 joy;     // dpad bits, BUTTON_RIGHT..BUTTON_DOWN
    u8 action;  // BUTTON_A/B/SELECT/START
};

#define MAX_INPUT_EVENTS 256
static struct input_event input_events[MAX_INPUT_EVENTS];
static int input_count;
static int input_next;
static u8 cur_joy, cur_action;

static int parse_button(const char *name, u8 *joy, u8 *action)
{
    if (!strcasecmp(name, "right")) *joy |= BUTTON_RIGHT;
    else if (!strcasecmp(name, "left")) *joy |= BUTTON_LEFT;
    else if (!strcasecmp(name, "up")) *joy |= BUTTON_UP;
    else if (!strcasecmp(name, "down")) *joy |= BUTTON_DOWN;
    else if (!strcasecmp(name, "a")) *action |= BUTTON_A;
    else if (!strcasecmp(name, "b")) *action |= BUTTON_B;
    else if (!strcasecmp(name, "select")) *action |= BUTTON_SELECT;
    else if (!strcasecmp(name, "start")) *action |= BUTTON_START;
    else return 0;
    return 1;
}

static int load_input_script(const char *path)
{
    char line[256];
    FILE *fp = fopen(path, "r");

    if (!fp) {
        fprintf(stderr, "gb6run: cannot open input script %s\n", path);
        return 0;
    }

    while (fgets(line, sizeof line, fp)) {
        char *colon, *tok;
        struct input_event *ev;

        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }
        colon = strchr(line, ':');
        if (!colon || input_count == MAX_INPUT_EVENTS) {
            fprintf(stderr, "gb6run: bad input script line: %s", line);
            fclose(fp);
            return 0;
        }
        *colon = 0;

        ev = &input_events[input_count++];
        ev->frame = (u32) atol(line);
        ev->joy = 0;
        ev->action = 0;
        for (tok = strtok(colon + 1, ", \t\r\n"); tok;
                tok = strtok(NULL, ", \t\r\n")) {
            if (!parse_button(tok, &ev->joy, &ev->action)) {
                fprintf(stderr, "gb6run: unknown button \"%s\"\n", tok);
                fclose(fp);
                return 0;
            }
        }
    }
    fclose(fp);
    return 1;
}

// apply only transitions: dmg_set_button raises the joypad interrupt on
// every press of a selected line, so re-asserting held buttons would
// re-trigger it
static void apply_input(const struct input_event *ev)
{
    int k;

    for (k = 0; k < 4; k++) {
        u8 bit = 1 << k;
        if ((ev->joy ^ cur_joy) & bit) {
            dmg_set_button(dmg, FIELD_JOY, bit, ev->joy & bit);
        }
        if ((ev->action ^ cur_action) & bit) {
            dmg_set_button(dmg, FIELD_ACTION, bit, ev->action & bit);
        }
    }
    cur_joy = ev->joy;
    cur_action = ev->action;
}

// like rom_load, but places the image inside the emulated address space
// so compiled fast paths can read it directly
static int host_rom_load(const char *path)
{
    FILE *fp;
    long len;

    fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "gb6run: cannot open %s\n", path);
        return 0;
    }

    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    rewind(fp);

    if (len < 0x150 || len > ROM_MAX) {
        fprintf(stderr, "gb6run: bad rom size %ld\n", len);
        fclose(fp);
        return 0;
    }
    if (fread(&m68k_mem[ROM_ADDR], 1, len, fp) < (size_t) len) {
        fprintf(stderr, "gb6run: short read on %s\n", path);
        fclose(fp);
        return 0;
    }
    fclose(fp);

    rom.data = &m68k_mem[ROM_ADDR];
    rom.length = len;
    rom.cgb_flag = rom.data[0x143];

    // relocate the mbc (and its embedded cart ram) into emulated memory
    // so ram banks can be mapped into the page tables
    struct mbc *m = mbc_new(rom.data[0x147]);
    if (!m) {
        fprintf(stderr, "gb6run: unsupported mbc type $%02x\n",
                rom.data[0x147]);
        return 0;
    }
    rom.mbc = (struct mbc *) &m68k_mem[MBC_ADDR];
    memcpy(rom.mbc, m, sizeof *m);

    return 1;
}

static void usage(void)
{
    fprintf(stderr,
        "usage: gb6run <rom> [options]\n"
        "  --frames N           stop after N frames (default 600)\n"
        "  --until-serial STR   stop once serial output contains STR\n"
        "  --serial             print serial bytes to stdout as they arrive\n"
        "  --dump-frames DIR    write each rendered frame as DIR/frame_N.ppm\n"
        "  --hash-frames        print a hash line per rendered frame\n"
        "  --dump-state         print final registers + hw state at exit\n"
        "  --input FILE         scripted joypad input (\"frame:Start,A\" lines)\n"
        "  --log-raster         log raster-relevant register writes + summary\n"
        "  --scx-stats          row_scx uniformity summary to stderr\n"
        "  --dirty-stats        row-diff savings summary + clean-row assertion\n"
        "  --exit-stats         exit budget causes + interrupt deliveries\n"
        "  --half-res           render 160x72 and dither to 1-bit like 1x mac B&W\n"
        "  --insn-log FILE      log every executed 68k instruction (- for stdout)\n"
        "  --no-stat-ints       drop STAT events from the scheduler (Mac menu toggle)\n"
        "  --chain              chain cached blocks like the Mac dispatcher\n"
        "  --trace              per-dispatch state line to stderr\n"
        "  --status             print status bar messages to stderr\n");
}

int main(int argc, char *argv[])
{
    const char *rom_path = NULL;
    const char *until_serial = NULL;
    const struct rom_patch_list *patch_list;
    long max_frames = -1;
    size_t serial_checked = 0;
    int matched = 0;
    char title[17];
    int k;

    for (k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "--frames") && k + 1 < argc) {
            max_frames = atol(argv[++k]);
        } else if (!strcmp(argv[k], "--until-serial") && k + 1 < argc) {
            until_serial = argv[++k];
        } else if (!strcmp(argv[k], "--serial")) {
            opt_serial = 1;
        } else if (!strcmp(argv[k], "--dump-frames") && k + 1 < argc) {
            opt_dump_dir = argv[++k];
        } else if (!strcmp(argv[k], "--hash-frames")) {
            opt_hash_frames = 1;
        } else if (!strcmp(argv[k], "--dump-state")) {
            opt_dump_state = 1;
        } else if (!strcmp(argv[k], "--input") && k + 1 < argc) {
            if (!load_input_script(argv[++k])) {
                return 1;
            }
        } else if (!strcmp(argv[k], "--log-raster")) {
            opt_log_raster = 1;
        } else if (!strcmp(argv[k], "--scx-stats")) {
            opt_scx_stats = 1;
        } else if (!strcmp(argv[k], "--dirty-stats")) {
            opt_dirty_stats = 1;
        } else if (!strcmp(argv[k], "--mac-sim")) {
            opt_mac_sim = 1;
        } else if (!strcmp(argv[k], "--frame-skip") && k + 1 < argc) {
            extern int frame_skip;
            frame_skip = atoi(argv[++k]);
        } else if (!strcmp(argv[k], "--half-res")) {
            opt_half_res = 1;
        } else if (!strcmp(argv[k], "--exit-stats")) {
            opt_exit_stats = 1;
        } else if (!strcmp(argv[k], "--insn-log") && k + 1 < argc) {
            opt_insn_log = argv[++k];
        } else if (!strcmp(argv[k], "--no-stat-ints")) {
            stat_ints_enabled = 0;
        } else if (!strcmp(argv[k], "--chain")) {
            host_chain = 1;
        } else if (!strcmp(argv[k], "--trace")) {
            host_trace = 1;
        } else if (!strcmp(argv[k], "--status")) {
            host_show_status = 1;
        } else if (argv[k][0] == '-') {
            fprintf(stderr, "gb6run: unknown option %s\n", argv[k]);
            usage();
            return 1;
        } else if (!rom_path) {
            rom_path = argv[k];
        } else {
            usage();
            return 1;
        }
    }

    if (!rom_path) {
        usage();
        return 1;
    }
    if (max_frames < 0) {
        // --until-serial gets a generous timeout, plain runs a short one
        max_frames = until_serial ? 36000 : 600;
    }

    if (sizeof(struct dmg) > WRAM_ADDR - DMG_ADDR
            || WRAM_SIZE > VRAM_ADDR - WRAM_ADDR
            || VRAM_SIZE > HRAM_ADDR - VRAM_ADDR
            || HRAM_SIZE > MBC_ADDR - HRAM_ADDR
            || sizeof(struct mbc) > ROM_ADDR - MBC_ADDR) {
        fprintf(stderr, "gb6run: memory map too small for host structs\n");
        return 2;
    }

    if (!host_rom_load(rom_path)) {
        return 1;
    }
    rom_get_title(&rom, title);

    // same title-keyed game patches the Mac build applies
    patch_list = patches_find(title);
    if (patch_list) {
        int n = patches_apply(rom.data, rom.length, patch_list);
        fprintf(stderr, "gb6run: applied %d rom patches for \"%s\"\n",
                n, title);
    }

    lcd_init_lut();
    lcd_cgb_init_lut();
    lcd_new(&lcd);
    lcd.row_stride = opt_half_res ? 2 : 1;

    dmg = (struct dmg *) &m68k_mem[DMG_ADDR];
    memset(dmg, 0, sizeof *dmg);
    // compiled code can only reach memory inside m68k_mem
    dmg_new(dmg, &rom, &lcd, &m68k_mem[WRAM_ADDR], &m68k_mem[VRAM_ADDR],
            &m68k_mem[HRAM_ADDR]);

    if (gbc_enabled && (rom.cgb_flag & 0xc0)) {
        cgb_init(&cgb, rom.cgb_flag);
        dmg->cgb = &cgb;
    }

    audio_init(&audio);
    dmg->audio = &audio;

    if (opt_log_raster) {
        dmg->raster_write_hook = raster_hook;
    }

    if (opt_dump_dir) {
        if (mkdir(opt_dump_dir, 0755) && errno != EEXIST) {
            fprintf(stderr, "gb6run: cannot create %s\n", opt_dump_dir);
            return 2;
        }
    }
    if (opt_dump_dir || opt_hash_frames || opt_log_raster || opt_scx_stats
            || opt_dirty_stats) {
        host_lcd_draw_hook = frame_hook;
    }

    if (opt_insn_log) {
        host_insn_log = strcmp(opt_insn_log, "-") ? fopen(opt_insn_log, "w")
                                                  : stdout;
        if (!host_insn_log) {
            fprintf(stderr, "gb6run: cannot write %s\n", opt_insn_log);
            return 2;
        }
        setvbuf(host_insn_log, NULL, _IOFBF, 1 << 20);
    }

    host_jit_init(dmg);

    fprintf(stderr, "gb6run: \"%s\" mbc $%02x%s, %s\n",
            title, rom.data[0x147], dmg->cgb ? ", cgb mode" : "",
            host_chain ? "chain" : "");

    while (!jit_halted) {
        if (!host_jit_run()) {
            break;
        }
        while (input_next < input_count
                && host_frames() >= input_events[input_next].frame) {
            apply_input(&input_events[input_next]);
            input_next++;
        }
        if (host_frames() >= (u32) max_frames) {
            break;
        }
        if (until_serial && serial_len != serial_checked) {
            serial_checked = serial_len;
            if (strstr(serial_buf, until_serial)) {
                matched = 1;
                break;
            }
        }
    }

    if (opt_dump_state) {
        host_dump_state(stdout);
    }
    if (opt_log_raster) {
        raster_summary(stderr);
    }
    if (opt_scx_stats) {
        scx_stats_summary(stderr);
    }
    if (opt_dirty_stats) {
        dirty_stats_summary(stderr);
    }
    if (opt_mac_sim) {
        fprintf(stderr, "mac-sim: %u bad frames, %u bad rows\n",
                ms_bad_frames, ms_bad_rows);
    }

    fprintf(stderr,
            "gb6run: ran %u frames, %u dispatches, %zu serial bytes\n",
            host_frames(), host_dispatches, serial_len);
    if (opt_exit_stats) {
        fprintf(stderr,
                "exit-stats: budget bound by stat=%u vblank=%u tima=%u "
                "serial=%u wrap=%u\n",
                host_exit_cause[0], host_exit_cause[1], host_exit_cause[3],
                host_exit_cause[4], host_exit_cause[5]);
        fprintf(stderr,
                "exit-stats: delivered vblank=%u stat=%u timer=%u "
                "serial=%u joypad=%u\n",
                host_int_delivered[0], host_int_delivered[1],
                host_int_delivered[2], host_int_delivered[3],
                host_int_delivered[4]);
    }

    if (until_serial) {
        if (matched) {
            fprintf(stderr, "gb6run: serial matched \"%s\"\n", until_serial);
            return 0;
        }
        fprintf(stderr, "gb6run: serial did NOT match \"%s\"\n", until_serial);
        return 1;
    }
    return 0;
}
