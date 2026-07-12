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
// lcd.c renders into a 168-wide packed 2bpp buffer (42 bytes/row); the
// visible 160 pixels start at SCX & 7, exactly as the Mac blitters read it

static const char *opt_dump_dir;
static int opt_hash_frames;
static int opt_dump_state;

// 160x144, gray (1 byte/px) for DMG or RGB (3 bytes/px) for CGB
static u8 frame_out[160 * 144 * 3];

static size_t extract_frame(struct lcd *l)
{
    int cgb_mode = dmg->cgb && dmg->cgb->mode;
    int scx_off = lcd_read(l, REG_SCX) & 7;
    size_t n = 0;
    int x, y;

    for (y = 0; y < 144; y++) {
        const u8 *row = l->pixels + y * 42;
        for (x = 0; x < 160; x++) {
            int px = x + scx_off;
            int shade = (row[px >> 2] >> (6 - 2 * (px & 3))) & 3;

            if (!cgb_mode) {
                frame_out[n++] = (u8) ((3 - shade) * 85);
                continue;
            }

            // CGB: attr picks the palette (bit 4 = sprite), pixel value
            // indexes into it; palette ram is RGB555 little-endian
            u8 attr = l->attrs[y * 168 + px];
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

static void frame_hook(struct lcd *l)
{
    size_t n = extract_frame(l);

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
        "  --chain              chain cached blocks like the Mac dispatcher\n"
        "  --cpu 68000|68020    codegen + emulated cpu (default 68020)\n"
        "  --cycles-per-exit N  dispatcher exit budget (default 70224)\n"
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

    cycles_per_exit = 70224;
    compiler_68020 = 1;

    for (k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "--cpu") && k + 1 < argc) {
            k++;
            if (!strcmp(argv[k], "68000")) {
                compiler_68020 = 0;
            } else if (!strcmp(argv[k], "68020")) {
                compiler_68020 = 1;
            } else {
                usage();
                return 1;
            }
        } else if (!strcmp(argv[k], "--cycles-per-exit") && k + 1 < argc) {
            cycles_per_exit = atoi(argv[++k]);
        } else if (!strcmp(argv[k], "--frames") && k + 1 < argc) {
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

    if (sizeof(struct dmg) > MBC_ADDR - DMG_ADDR
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

    dmg = (struct dmg *) &m68k_mem[DMG_ADDR];
    memset(dmg, 0, sizeof *dmg);
    dmg_new(dmg, &rom, &lcd);

    if (gbc_enabled && (rom.cgb_flag == 0x80 || rom.cgb_flag == 0xc0)) {
        cgb_init(&cgb, rom.cgb_flag);
        dmg->cgb = &cgb;
    }

    audio_init(&audio);
    dmg->audio = &audio;

    if (opt_dump_dir) {
        if (mkdir(opt_dump_dir, 0755) && errno != EEXIST) {
            fprintf(stderr, "gb6run: cannot create %s\n", opt_dump_dir);
            return 2;
        }
    }
    if (opt_dump_dir || opt_hash_frames) {
        host_lcd_draw_hook = frame_hook;
    }

    host_jit_init(dmg);

    fprintf(stderr, "gb6run: \"%s\" mbc $%02x%s, %s%s, budget %d\n",
            title, rom.data[0x147], dmg->cgb ? ", cgb mode" : "",
            compiler_68020 ? "68020" : "68000",
            host_chain ? ", chain" : "", cycles_per_exit);

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

    fprintf(stderr,
            "gb6run: ran %u frames, %u dispatches, %zu serial bytes\n",
            host_frames(), host_dispatches, serial_len);

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
