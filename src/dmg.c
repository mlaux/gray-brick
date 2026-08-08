#include <stdio.h>
#include <string.h>

#include "rom.h"
#include "lcd.h"
#include "dmg.h"
#include "mbc.h"
#include "cgb.h"
#include "types.h"
#include "audio.h"
#include "prof.h"
#include "../system6/jit.h"
#include "../system6/audio_mac.h"
#include "../system6/settings.h"
#include "../system6/cache.h"

#define INT_VBLANK  (1 << 0)
#define INT_LCDSTAT (1 << 1)
#define INT_TIMER   (1 << 2)
#define INT_SERIAL  (1 << 3)
#define INT_JOYPAD  (1 << 4)
#define NUM_INTERRUPTS 5

// TAC clock select -> cycles per TIMA increment
static const u16 timer_divisors[] = { 1024, 16, 64, 256 };

static void update_joyp(struct dmg *dmg);

static int dmg_double_speed(struct dmg *dmg)
{
    return dmg->cgb && dmg->cgb->double_speed && !ignore_double_speed;
}

// the CPU-cycle clock, including cycles currently in progress in the JIT
// DIV and TIMA run on this
static u32 dmg_now_cpu(struct dmg *dmg)
{
    return dmg->total_cycles + jit_ctx.read_cycles;
}

// the frame relative "beam" position
static u32 dmg_now_ppu(struct dmg *dmg)
{
    u32 read = jit_ctx.read_cycles;
    if (dmg_double_speed(dmg)) {
        read >>= 1;
    }
    return dmg->frame_cycles + read;
}

// handle the case where a write just moved a deadline, but the running
// dispatch snapshotted wake_limit before it
static void dmg_budget_update(struct dmg *dmg)
{
    u32 dist = dmg_cycles_to_next_event(dmg);

    if (dmg->interrupt_enable
            && (dmg->interrupt_request_mask & dmg->zero_page[0x7f] & 0x1f)) {
        dist = 0;
    }

    if (dmg_double_speed(dmg)) {
        dist <<= 1;
    }
    if (dist < jit_ctx.wake_limit) {
        jit_ctx.wake_limit = dist;
    }
}

// single-band state when nothing changed mid-frame
static void raster_live_regs(struct lcd *lcd, struct raster_regs *regs)
{
    regs->lcdc = lcd_read(lcd, REG_LCDC);
    regs->scx = lcd_read(lcd, REG_SCX);
    regs->scy = lcd_read(lcd, REG_SCY);
    regs->wy = lcd_read(lcd, REG_WY);
    regs->wx = lcd_read(lcd, REG_WX);
    regs->bgp = lcd_read(lcd, REG_BGP);
    regs->obp0 = lcd_read(lcd, REG_OBP0);
    regs->obp1 = lcd_read(lcd, REG_OBP1);
}

// start a new raster frame - line 0 state from current regs
// and clear the log
static void raster_frame_reset(struct dmg *dmg)
{
    raster_live_regs(dmg->lcd, &dmg->lcd->frame_regs);
    dmg->lcd->raster_count = 0;
    dmg->lcd->raster_overflow = 0;
}

// a raster register changed mid-frame, log it for the band replay
static void raster_record(
    struct dmg *dmg,
    u16 address,
    u8 data,
    u32 ly,
    u32 pos)
{
    struct lcd *lcd = dmg->lcd;
    u32 line = pos >= 252 ? ly + 1 : ly;
    struct raster_log_entry *e;

    if (!(lcd_read(lcd, REG_LCDC) & LCDC_ENABLE)) {
        return;
    }
    if (line >= 144) {
        // affects next frame only
        return;
    }
    if (lcd->raster_count == RASTER_LOG_SIZE) {
        lcd->raster_overflow = 1;
        return;
    }

    e = &lcd->raster_log[lcd->raster_count++];
    e->line = line;
    e->reg = address - REG_LCD_BASE;
    e->value = data;
}

void dmg_new(struct dmg *dmg, struct rom *rom, struct lcd *lcd)
{
    dmg->rom = rom;
    dmg->lcd = lcd;

    dmg->joypad = 0xf; // nothing pressed
    dmg->action_buttons = 0xf;
    update_joyp(dmg);

    // hardware state as the boot ROM hands it to the cartridge
    lcd_write(lcd, REG_LCDC, 0x91);
    lcd_write(lcd, REG_BGP, 0xfc);
    dmg->interrupt_request_mask = 0xe1;

    dmg->event_deadline[EV_STAT] = EV_NONE;
    dmg->event_deadline[EV_VBLANK] = CYCLES_LINE_144;
    dmg->event_deadline[EV_RENDER] = CYCLES_LINE_144;
    dmg->event_deadline[EV_TIMA] = EV_NONE;
    dmg->event_deadline[EV_SERIAL] = EV_NONE;
    dmg->event_deadline[EV_WRAP] = CYCLES_PER_FRAME;

    raster_frame_reset(dmg);
    dmg_init_pages(dmg);
}

void dmg_init_pages(struct dmg *dmg)
{
    int k;

    // start with everything as slow path
    for (k = 0; k < 256; k++) {
        dmg->read_page[k] = NULL;
        dmg->write_page[k] = NULL;
    }

    // ROM bank 0: 0x0000-0x3fff (pages 0x00-0x3f)
    for (k = 0x00; k <= 0x3f; k++) {
        dmg->read_page[k] = PAGE_BIAS(&dmg->rom->data[k << 8], k);
    }

    // ROM bank 1: 0x4000-0x7fff (pages 0x40-0x7f)
    // MBC will update this when switching banks
    for (k = 0x40; k <= 0x7f; k++) {
        dmg->read_page[k] = PAGE_BIAS(&dmg->rom->data[k << 8], k);
    }
    dmg->current_rom_bank = 1;

    // video RAM: 0x8000-0x9fff (pages 0x80-0x9f)
    // Uses bank 0 initially, cgb_update_vram_bank() can switch for CGB
    for (k = 0x80; k <= 0x9f; k++) {
        u8 *page = &dmg->video_ram[(k - 0x80) << 8];
        dmg->read_page[k] = PAGE_BIAS(page, k);
        dmg->write_page[k] = PAGE_BIAS(page, k);
    }

    // external RAM: 0xa000-0xbfff (pages 0xa0-0xbf)
    // leave NULL - MBC handles this

    // work RAM bank 0: 0xc000-0xcfff (pages 0xc0-0xcf) - always fixed
    for (k = 0xc0; k <= 0xcf; k++) {
        u8 *page = &dmg->main_ram[(k - 0xc0) << 8];
        dmg->read_page[k] = PAGE_BIAS(page, k);
        dmg->write_page[k] = PAGE_BIAS(page, k);
    }

    // work RAM bank 1: 0xd000-0xdfff (pages 0xd0-0xdf) - switchable in CGB
    // Initially points to bank 1 (offset 0x1000)
    for (k = 0xd0; k <= 0xdf; k++) {
        u8 *page = &dmg->main_ram[0x1000 + ((k - 0xd0) << 8)];
        dmg->read_page[k] = PAGE_BIAS(page, k);
        dmg->write_page[k] = PAGE_BIAS(page, k);
    }

    // echo RAM: 0xe000-0xfdff (pages 0xe0-0xfd)
    // Mirrors 0xc000-0xddff (bank 0 + current switchable bank)
    for (k = 0xe0; k <= 0xef; k++) {
        // Echo of 0xc000-0xcfff (bank 0)
        u8 *page = &dmg->main_ram[(k - 0xe0) << 8];
        dmg->read_page[k] = PAGE_BIAS(page, k);
        dmg->write_page[k] = PAGE_BIAS(page, k);
    }
    for (k = 0xf0; k <= 0xfd; k++) {
        // Echo of 0xd000-0xddff (switchable bank)
        u8 *page = &dmg->main_ram[0x1000 + ((k - 0xf0) << 8)];
        dmg->read_page[k] = PAGE_BIAS(page, k);
        dmg->write_page[k] = PAGE_BIAS(page, k);
    }

    // pages 0xfe and 0xff stay NULL for special handling
}

void dmg_update_rom_bank(struct dmg *dmg, int bank)
{
    int k;
    u8 *biased;

    if (dmg->current_rom_bank == bank) {
        return;
    }
    dmg->current_rom_bank = bank;

    biased = PAGE_BIAS(&dmg->rom->data[bank * 0x4000], 0x40);
    for (k = 0x40; k <= 0x7f; k += 4) {
        dmg->read_page[k] = biased;
        dmg->read_page[k + 1] = biased;
        dmg->read_page[k + 2] = biased;
        dmg->read_page[k + 3] = biased;
    }

    // Notify JIT of bank switch
    if (dmg->rom_bank_switch_hook) {
        dmg->rom_bank_switch_hook(bank);
    }
}

void dmg_update_ram_bank(struct dmg *dmg, u8 *ram_base)
{
    u8 *biased = ram_base ? PAGE_BIAS(ram_base, 0xa0) : NULL;
    int k;

    for (k = 0xa0; k <= 0xbf; k += 2) {
        dmg->read_page[k] = biased;
        dmg->write_page[k] = biased;
        dmg->read_page[k + 1] = biased;
        dmg->write_page[k + 1] = biased;
    }
}

static void dmg_request_interrupt(struct dmg *dmg, int nr)
{
    dmg->interrupt_request_mask |= nr;
}

static void update_joyp(struct dmg *dmg)
{
    // each selected group pulls its pressed bits low
    u8 ret = 0xcf;

    if (dmg->action_selected) {
        ret &= 0xf0 | dmg->action_buttons;
    } else {
        ret |= 0x20;
    }

    if (dmg->joypad_selected) {
        ret &= 0xf0 | dmg->joypad;
    } else {
        ret |= 0x10;
    }

    dmg->joyp = ret;
}

void dmg_set_button(struct dmg *dmg, int field, int button, int pressed)
{
    u8 *mod;
    int selected;
    if (field == FIELD_JOY) {
        mod = &dmg->joypad;
        selected = dmg->joypad_selected;
    } else if (field == FIELD_ACTION) {
        mod = &dmg->action_buttons;
        selected = dmg->action_selected;
    } else {
        printf("setting invalid button state\n");
        return;
    }

    if (pressed) {
        *mod &= ~button;
        // high-to-low transition on a selected line raises the joypad
        // interrupt (games that halt waiting for input need this)
        if (selected) {
            dmg_request_interrupt(dmg, INT_JOYPAD);
        }
    } else {
        *mod |= button;
    }
    update_joyp(dmg);
}

// advance the lazy LY counter to the current "beam" position
static u8 dmg_current_ly(struct dmg *dmg, u32 *line_pos)
{
    u32 current;

    current = dmg_now_ppu(dmg);
    if (current >= 70224) {
        current -= 70224;
    }

    // handle frame wrap-around
    if (current < dmg->ly_read_cycle) {
        dmg->ly_read_cycle = 0;
        dmg->lazy_ly = 0;
    }

    // advance through scanlines until we reach current cycle
    while (dmg->ly_read_cycle + 456 <= current) {
        dmg->lazy_ly++;
        if (dmg->lazy_ly == 154) {
            dmg->lazy_ly = 0;
        }
        dmg->ly_read_cycle += 456;
    }

    if (line_pos) {
        *line_pos = current - dmg->ly_read_cycle;
    }
    return dmg->lazy_ly;
}

// earliest enabled STAT interrupt event at frame cycle >= from, where
// from_line = from / 456. returns 0xffffffff if none left this frame and
// stores the event's line in *line_out. events: LYC match at the start of
// its line, hblank at line start + 252 (lines 0-143), OAM scan at line
// start (lines 0-143), and vblank start
static u32 stat_event_from(
    struct dmg *dmg,
    u32 from,
    u32 from_line,
    u32 *line_out
) {
    u8 stat = lcd_read(dmg->lcd, REG_STAT);
    u32 best = 0xffffffff;
    u32 best_line = 0;
    u32 c, line;

    // per-line STAT interrupts cost a dispatch each
    // the user can turn them off for games (GSC) that enable them
    // but rarely use them
    if (!stat_ints_enabled
            || !(lcd_read(dmg->lcd, REG_LCDC) & LCDC_ENABLE)) {
        return best;
    }

    if (stat & STAT_INTR_SOURCE_HBLANK) {
        line = from_line;
        c = line * CYCLES_PER_LINE + 252;
        if (c < from) {
            line++;
            c += CYCLES_PER_LINE;
        }
        if (line <= 143 && c < best) {
            best = c;
            best_line = line;
        }
    }

    if (stat & STAT_INTR_SOURCE_MODE2) {
        line = from_line;
        c = line * CYCLES_PER_LINE;
        if (c < from) {
            line++;
            c += CYCLES_PER_LINE;
        }
        if (line <= 143 && c < best) {
            best = c;
            best_line = line;
        }
    }

    if (stat & STAT_INTR_SOURCE_VBLANK) {
        if (CYCLES_LINE_144 >= from && CYCLES_LINE_144 < best) {
            best = CYCLES_LINE_144;
            best_line = 144;
        }
    }

    if (stat & STAT_INTR_SOURCE_MATCH) {
        line = lcd_read(dmg->lcd, REG_LYC);
        c = line * CYCLES_PER_LINE;
        if (line <= 153 && c >= from && c < best) {
            best = c;
            best_line = line;
        }
    }

    if (line_out) {
        *line_out = best_line;
    }
    return best;
}

static void dmg_stat_recompute(struct dmg *dmg)
{
    u32 pos, line;
    u32 ly = dmg_current_ly(dmg, &pos);
    u32 cur = ly * CYCLES_PER_LINE + pos;

    if (dmg_now_ppu(dmg) >= CYCLES_PER_FRAME) {
        // cur wrapped to a next-frame position
        dmg->event_deadline[EV_STAT] = EV_NONE;
        return;
    }

    dmg->event_deadline[EV_STAT] = stat_event_from(dmg, cur, ly, &line);
    dmg->stat_event_line = line;
}

static void dmg_stat_touch(struct dmg *dmg)
{
    if (dmg_now_ppu(dmg) >= dmg->event_deadline[EV_STAT]) {
        dmg_request_interrupt(dmg, INT_LCDSTAT);
    }
    dmg_stat_recompute(dmg);
}

static void dmg_update_frame_events(struct dmg *dmg)
{
    int on = lcd_read(dmg->lcd, REG_LCDC) & LCDC_ENABLE;

    dmg->event_deadline[EV_VBLANK] =
        (on && !dmg->sent_vblank_start) ? CYCLES_LINE_144 : EV_NONE;
    dmg->event_deadline[EV_RENDER] =
        (on && !dmg->rendered_this_frame) ? CYCLES_LINE_144 : EV_NONE;
}

// enabling the LCD restarts the PPU frame at line 0
static void dmg_lcd_frame_restart(struct dmg *dmg)
{
    u32 partial = dmg->frame_cycles;

    if (!partial) {
        return;
    }

    dmg->frame_cycles = 0;
    dmg->sent_vblank_start = 0;
    dmg->rendered_this_frame = 0;
    dmg->lazy_ly = 0;
    dmg->ly_read_cycle = 0;
    raster_frame_reset(dmg);

    // need to move these back because frame_cycles was still going even with
    // the lcd off...
    if (dmg->event_deadline[EV_TIMA] != EV_NONE) {
        dmg->event_deadline[EV_TIMA] =
            dmg->event_deadline[EV_TIMA] > partial
                ? dmg->event_deadline[EV_TIMA] - partial : 0;
    }
    if (dmg->event_deadline[EV_SERIAL] != EV_NONE) {
        dmg->event_deadline[EV_SERIAL] =
            dmg->event_deadline[EV_SERIAL] > partial
                ? dmg->event_deadline[EV_SERIAL] - partial : 0;
    }

    if (dmg->cgb) {
        dmg->cgb->hdma_last_ly = 0xff;
        dmg->cgb->hdma_completed = 0;
    }
}

static u32 tima_divisor(struct dmg *dmg)
{
    return timer_divisors[dmg->timer_control & 3];
}

// get live TIMA value derived from the base pair
static u8 dmg_tima_read(struct dmg *dmg)
{
    u32 elapsed, count;

    if (!(dmg->timer_control & TIMER_CONTROL_ENABLED)) {
        return dmg->timer_count;
    }

    elapsed = dmg_now_cpu(dmg) - dmg->tima_base_cycle;
    count = dmg->timer_count + elapsed / tima_divisor(dmg);
    if (count <= 0xff) {
        return count;
    }

    // EV_TIMA hasn't fired yet, but the counter has already been reloaded from TMA
    return dmg->timer_mod + (count - 256) % (256 - dmg->timer_mod);
}

// rearm the overflow deadline from the current base state
static void dmg_tima_recompute(struct dmg *dmg)
{
    u32 dist;

    if (!(dmg->timer_control & TIMER_CONTROL_ENABLED)) {
        dmg->event_deadline[EV_TIMA] = EV_NONE;
        return;
    }

    dist = (256 - dmg->timer_count) * tima_divisor(dmg)
         - (dmg_now_cpu(dmg) - dmg->tima_base_cycle);
    if (dmg_double_speed(dmg)) {
        dist >>= 1;
    }
    dmg->event_deadline[EV_TIMA] = dmg_now_ppu(dmg) + dist;
    dmg_budget_update(dmg);
}

static void dmg_tima_touch(struct dmg *dmg, int keep_phase)
{
    u32 now = dmg_now_cpu(dmg);

    if (dmg_now_ppu(dmg) >= dmg->event_deadline[EV_TIMA]) {
        dmg_request_interrupt(dmg, INT_TIMER);
    }

    if (dmg->timer_control & TIMER_CONTROL_ENABLED) {
        u32 phase = keep_phase
            ? (now - dmg->tima_base_cycle) & (tima_divisor(dmg) - 1)
            : 0;
        dmg->timer_count = dmg_tima_read(dmg);
        dmg->tima_base_cycle = now - phase;
    }
}

void dmg_speed_changed(struct dmg *dmg)
{
    dmg_tima_touch(dmg, 1);
    dmg_tima_recompute(dmg);
}

u8 dmg_read_slow(struct dmg *dmg, u16 address)
{
    if (address == REG_LY) {
        if (!(lcd_read(dmg->lcd, REG_LCDC) & LCDC_ENABLE)) {
            return 0;
        }

        // the compiler detects "ldh a, [$44]; cp N; jr cc" which is the most
        // common case, and skips to that line, so this actually doesn't run
        // that much
        return dmg_current_ly(dmg, NULL);
    }

    if (address == REG_STAT) {
        // compute the real mode and match flag from the beam position
        u8 stat = lcd_read(dmg->lcd, REG_STAT);
        u32 line_pos;
        u8 ly = dmg_current_ly(dmg, &line_pos);
        int mode;

        if (ly >= 144) {
            // vblank
            mode = 1;
        } else if (line_pos < 80) {
            // OAM scan
            mode = 2;
        } else if (line_pos < 252) {
            // active area, 160 visible pixels + 12 extra
            // https://gbdev.io/pandocs/Rendering.html#first12
            mode = 3;
        } else {
            // hblank
            mode = 0;
        }

        stat = (stat & 0xf8) | mode;
        if (ly == lcd_read(dmg->lcd, REG_LYC)) {
            stat |= STAT_FLAG_MATCH;
        }
        return stat;
    }

    // OAM and LCD registers
    if (lcd_is_valid_addr(address)) {
        return lcd_read(dmg->lcd, address);
    }

    // high RAM
    if (address >= 0xff80) {
        return dmg->zero_page[address - 0xff80];
    }

    // I/O registers
    if (address == 0xff00) {
        return dmg->joyp;
    }
    if (address == REG_TIMER_DIV) {
        u32 div_val = dmg_now_cpu(dmg) - dmg->div_reset_cycle;
        return (div_val >> 8) & 0xff;
    }
    if (address == REG_TIMER_COUNT) {
        return dmg_tima_read(dmg);
    }
    if (address == REG_TIMER_MOD) {
        return dmg->timer_mod;
    }
    if (address == REG_TIMER_CONTROL) {
        return dmg->timer_control;
    }
    if (address >= 0xff10 && address <= 0xff3f) {
        return audio_read(dmg->audio, address);
    }
    if (address == 0xff0f) {
        return dmg->interrupt_request_mask;
    }
    if (address == 0xff01) {
        return dmg->reg_sb;
    }
    if (address == 0xff02) {
        // unused bits read 1; bit 7 clears when the transfer completes
        return dmg->reg_sc | 0x7e;
    }

    // CGB registers
    if (dmg->cgb && dmg->cgb->mode) {
        u8 cgb_val;
        if (cgb_read_reg(dmg->cgb, dmg->lcd, address, &cgb_val)) {
            return cgb_val;
        }
    }

    // external RAM not enabled, or RTC register selected
    if (address >= 0xa000 && address < 0xc000) {
        u8 val;
        if (mbc_ram_read(dmg->rom->mbc, address, &val)) {
            return val;
        }
        return 0xff;
    }

    return 0xff;
}

u8 dmg_read(void *_dmg, u16 address)
{
    u8 val;
    struct dmg *dmg = (struct dmg *) _dmg;
    u8 *page = dmg->read_page[address >> 8];
    if (page) {
        // entries are biased, index with the sign-extended address
        val = page[(s16) address];
    } else {
        val = dmg_read_slow(dmg, address);
    }
    return val;
}

void dmg_write_slow(struct dmg *dmg, u16 address, u8 data)
{
    // ROM region writes go to MBC for bank switching
    if (address < 0x8000) {
        dmg->rom->mbc->write(dmg, address, data);
        return;
    }

    // pages holding compiled code have their fast write mapping removed
    if (address >= 0x8000) {
        u8 pidx = (u8) ((address >> 8) - 0x80);
        u8 *saved = dmg->saved_write_page[pidx];

        // a write that leaves the byte unchanged can't invalidate anything
        // the compiler derived from it
        if (cache_upper_range_hit(address)
                && (!saved || saved[(s16) address] != data)) {
            // self-modifying code - drop this page's compiled blocks
            cache_invalidate_upper_page((u8) (address >> 8));
            if (saved) {
                dmg->write_page[address >> 8] = saved;
                dmg->saved_write_page[pidx] = NULL;
                saved[(s16) address] = data;
                return;
            }
            // no fast mapping to restore, handle the write normally below
        } else if (saved) {
            // slow only because compiled code lives elsewhere in the page
            saved[(s16) address] = data;
            return;
        }
    }

    // external RAM not enabled, or RTC register selected
    if (address >= 0xa000 && address < 0xc000) {
        mbc_ram_write(dmg->rom->mbc, address, data);
        return;
    }

    // log changes to replay on render
    if (address >= REG_LCDC && address <= REG_WX
            && ((0xfcd >> (address - REG_LCDC)) & 1)) {
        u8 old = lcd_read(dmg->lcd, address);

        if (old != data || address == REG_DMA || dmg->raster_write_hook) {
            u32 pos;
            u8 ly = dmg_current_ly(dmg, &pos);

            if (address != REG_DMA && old != data) {
                raster_record(dmg, address, data, ly, pos);
            }
            if (dmg->raster_write_hook) {
                dmg->raster_write_hook(address, old, data, ly, pos);
            }
        }
    } else if (dmg->raster_write_hook
            && address >= 0xff68 && address <= 0xff6b) {
        struct lcd *lcd = dmg->lcd;
        u32 pos;
        u8 ly = dmg_current_ly(dmg, &pos);
        u8 old;

        if (address == 0xff68) {
            old = lcd->bcps;
        } else if (address == 0xff69) {
            old = lcd->bg_palette_ram[lcd->bcps & 0x3f];
        } else if (address == 0xff6a) {
            old = lcd->ocps;
        } else {
            old = lcd->obj_palette_ram[lcd->ocps & 0x3f];
        }
        dmg->raster_write_hook(address, old, data, ly, pos);
    }

    // OAM DMA
    if (address == 0xff46) {
        u16 src = data << 8;
        u8 *page = dmg->read_page[data];
        if (page) {
            memcpy(dmg->lcd->oam, page + (s16) src, 0xa0);
        } else {
            int k = 0;
            for (u16 addr = src; addr < src + 0xa0; addr++) {
                dmg->lcd->oam[k++] = dmg_read(dmg, addr);
            }
        }

        return;
    }

    if (address == REG_STAT || address == REG_LYC || address == REG_LCDC) {
        u8 was_on = lcd_read(dmg->lcd, REG_LCDC) & LCDC_ENABLE;

        lcd_write(dmg->lcd, address, data);
        if (address == REG_LCDC && !was_on && (data & LCDC_ENABLE)) {
            dmg_lcd_frame_restart(dmg);
        }
        dmg_stat_touch(dmg);
        if (address == REG_LCDC) {
            dmg_update_frame_events(dmg);
        }
        dmg_budget_update(dmg);
        return;
    }

    // OAM and LCD registers
    if (lcd_is_valid_addr(address)) {
        lcd_write(dmg->lcd, address, data);
        return;
    }

    // high RAM
    if (address >= 0xff80) {
        dmg->zero_page[address - 0xff80] = data;
        // IE gates which deadlines can exit/wake
        if (address == 0xffff) {
            dmg_budget_update(dmg);
        }
        return;
    }

    // I/O registers
    if (address == 0xff00) {
        dmg->joypad_selected = !(data & (1 << 4));
        dmg->action_selected = !(data & (1 << 5));
        update_joyp(dmg);
        return;
    }
    if (address == REG_TIMER_DIV) {
        // writing any value resets DIV to 0 at this cycle. the timer
        // prescaler runs off the same counter on hardware, so its phase
        // resets too
        dmg->div_reset_cycle = dmg_now_cpu(dmg);
        dmg_tima_touch(dmg, 0);
        dmg_tima_recompute(dmg);
        return;
    }
    if (address == REG_TIMER_COUNT) {
        // the write replaces the counter but not the prescaler phase
        dmg_tima_touch(dmg, 1);
        dmg->timer_count = data;
        dmg_tima_recompute(dmg);
        return;
    }
    if (address == REG_TIMER_MOD) {
        // only read back at the next reload; the armed deadline stands
        dmg->timer_mod = data;
        return;
    }
    if (address == REG_TIMER_CONTROL) {
        dmg_tima_touch(dmg, 0);
        dmg->timer_control = data;
        dmg->tima_base_cycle = dmg_now_cpu(dmg);
        dmg_tima_recompute(dmg);
        return;
    }
    if (address >= 0xff10 && address <= 0xff3f) {
        audio_mac_write(dmg->audio, address, data);
        return;
    }
    if (address == 0xff0f) {
        dmg->interrupt_request_mask = data;
        dmg_budget_update(dmg);
        return;
    }
    if (address == 0xff01) {
        dmg->reg_sb = data;
        return;
    }
    if (address == 0xff02) {
        // there is never a link partner, so an internal-clock transfer
        // completes 4096 CPU cycles out (8 bits at 8192 Hz) and an
        // external-clock one never does, exactly like an unplugged cable.
        // clearing bit 7 aborts a transfer
        u32 dist;
        dmg->reg_sc = data & 0x81;
        if ((data & 0x81) == 0x81) {
            dist = 4096;
            if (dmg_double_speed(dmg)) {
                dist >>= 1;
            }
            dmg->event_deadline[EV_SERIAL] = dmg_now_ppu(dmg) + dist;
        } else {
            dmg->event_deadline[EV_SERIAL] = EV_NONE;
        }
        dmg_budget_update(dmg);
        return;
    }

    // CGB registers
    if (dmg->cgb && dmg->cgb->mode) {
        if (cgb_write_reg(dmg->cgb, dmg, address, data)) {
            return;
        }
    }
}

void dmg_write(void *_dmg, u16 address, u8 data)
{
    struct dmg *dmg = (struct dmg *) _dmg;
    u8 *page = dmg->write_page[address >> 8];
    if (page) {
        // entries are biased, index with the sign-extended address
        page[(s16) address] = data;
    } else {
        dmg_write_slow(dmg, address, data);
    }
}

u16 dmg_read16(void *_dmg, u16 address)
{
    return dmg_read(_dmg, address) | dmg_read(_dmg, address + 1) << 8;
}

void dmg_write16(void *_dmg, u16 address, u16 data)
{
    dmg_write(_dmg, address, data & 0xff);
    dmg_write(_dmg, address + 1, (data >> 8) & 0xff);
}

// HDMA sync - triggers HDMA transfers for any lines we've crossed
void hdma_sync(struct dmg *dmg)
{
    u8 current_ly;
    u8 start_ly;
    u8 ly;

    // Only applies to CGB mode with active HDMA
    if (!dmg->cgb || !dmg->cgb->mode || !dmg->cgb->hdma_active) {
        return;
    }

    // Calculate current LY from frame cycles
    current_ly = (dmg->frame_cycles / CYCLES_PER_LINE);
    if (current_ly > 153) current_ly = 153;

    // Determine starting line for HDMA catch-up
    if (dmg->cgb->hdma_last_ly == 0xff) {
        // First HDMA this frame - start from line 0
        start_ly = 0;
    } else {
        // Continue from next line after last HDMA
        start_ly = dmg->cgb->hdma_last_ly + 1;
    }

    // Trigger HDMA for any lines we've passed (only for LY 0-143)
    for (ly = start_ly; ly <= current_ly && ly < 144 && dmg->cgb->hdma_active; ly++) {
        cgb_hdma_hblank(dmg->cgb, dmg, ly);
        dmg->cgb->hdma_last_ly = ly;
    }

    // If we're in VBlank, just update the tracking
    if (current_ly >= 144 && dmg->cgb->hdma_last_ly < 144) {
        dmg->cgb->hdma_last_ly = 143;
    }
}

// step one replayed write forward through the band register state
static void raster_apply(struct raster_regs *regs, u8 reg, u8 value)
{
    switch (reg + REG_LCD_BASE) {
    case REG_LCDC: regs->lcdc = value; break;
    case REG_SCY: regs->scy = value; break;
    case REG_SCX: regs->scx = value; break;
    case REG_BGP: regs->bgp = value; break;
    case REG_OBP0: regs->obp0 = value; break;
    case REG_OBP1: regs->obp1 = value; break;
    case REG_WY: regs->wy = value; break;
    case REG_WX: regs->wx = value; break;
    }
}

// render one band of the frame from one register state: background and
// window, then sprites clipped to the band. rows pack at the band's
// scx&7, and row_scx tells the blitters where each row's visible area
// starts
static void render_band_pass(
    struct dmg *dmg,
    int sy_start,
    int sy_end,
    const struct raster_regs *regs)
{
    int cgb = dmg->cgb && dmg->cgb->mode;

    memset(&dmg->lcd->row_scx[sy_start], regs->scx & 7, sy_end - sy_start);

    if (regs->lcdc & LCDC_ENABLE_BG) {
        if (cgb) {
            lcd_cgb_render_band(dmg, sy_start, sy_end, regs);
        } else {
            lcd_update_palette_lut(regs->bgp);
            lcd_render_band(dmg, sy_start, sy_end, regs);
        }
    } else if (!cgb) {
        lcd_render_blank_band(dmg, sy_start, sy_end, regs);
    }
    if (regs->lcdc & LCDC_ENABLE_OBJ) {
        if (cgb) {
            lcd_cgb_render_objs_band(dmg, sy_start, sy_end, regs);
        } else {
            lcd_render_objs_band(dmg, sy_start, sy_end, regs);
        }
    }
}

// one render per frame at vblank start - every visible line has
// been "scanned" by then, and the game's vblank handler hasn't run yet
static void render_frame(struct dmg *dmg)
{
    struct lcd *lcd = dmg->lcd;
    struct raster_regs regs;
    int replay, start, uniform;
    int k;

    // frame skip setting is 0-4
    if (dmg->frames_rendered++ % (frame_skip + 1) != 0) {
        return;
    }

    PROF_SET(PROF_RENDER);
    lcd->window_line = 0;
    replay = lcd->raster_count && !lcd->raster_overflow;
    if (replay) {
        regs = lcd->frame_regs;
    } else {
        // nothing changed mid-frame (or the log overflowed) 
        // render once from final register state
        raster_live_regs(lcd, &regs);
    }

    start = 0;
    for (k = 0; replay && k < lcd->raster_count; k++) {
        const struct raster_log_entry *e = &lcd->raster_log[k];

        if (e->line > start) {
            render_band_pass(dmg, start, e->line, &regs);
            start = e->line;
        }
        raster_apply(&regs, e->reg, e->value);
    }
    render_band_pass(dmg, start, 144, &regs);

    // half-res: each rendered line covers two screen rows, so the blit
    // bands and the diff have to see one offset across the pair
    if (lcd->row_stride == 2) {
        for (k = 0; k < 144; k += 2) {
            lcd->row_scx[k + 1] = lcd->row_scx[k];
        }
    }

    // single blit offset for the whole frame when every band packed its rows
    // at the same scx&7
    uniform = 1;
    for (k = 1; replay && k < 144; k++) {
        if (lcd->row_scx[k] != lcd->row_scx[0]) {
            uniform = 0;
            break;
        }
    }
    lcd->row_scx_uniform = uniform;

    // when palette ram changes, those frames redraw everything
    lcd_diff_rows(lcd, dmg->cgb && dmg->cgb->mode);
    if (lcd->palette_frame_dirty) {
        lcd->palette_frame_dirty = 0;
        for (k = 0; k < 144; k++) {
            lcd->row_dirty[k] |= ROW_DIRTY_CONTENT;
        }
        lcd->frame_dirty |= ROW_DIRTY_CONTENT;
    }

    lcd_draw(lcd);
    PROF_SET(PROF_SYNC);
}

static void dmg_event_fire(struct dmg *dmg, int ev)
{
    u32 line;

    switch (ev) {
    case EV_STAT:
        // several events crossed in one sync merge into a single IF bit
        dmg_request_interrupt(dmg, INT_LCDSTAT);
        dmg->event_deadline[EV_STAT] = stat_event_from(dmg,
                dmg->event_deadline[EV_STAT] + 1, dmg->stat_event_line,
                &line);
        dmg->stat_event_line = line;
        break;

    case EV_VBLANK:
        // vblank start, once per frame
        dmg_request_interrupt(dmg, INT_VBLANK);
        dmg->sent_vblank_start = 1;
        dmg->event_deadline[EV_VBLANK] = EV_NONE;
        break;

    case EV_RENDER:
        render_frame(dmg);
        dmg->rendered_this_frame = 1;
        dmg->event_deadline[EV_RENDER] = EV_NONE;
        break;

    case EV_SERIAL:
        // transfer done: the line reads all 1s with no partner
        dmg->reg_sb = 0xff;
        dmg->reg_sc &= 0x7f;
        dmg_request_interrupt(dmg, INT_SERIAL);
        dmg->event_deadline[EV_SERIAL] = EV_NONE;
        break;

    case EV_TIMA: {
        // overflow: reload from TMA at the exact overflow instant
        // and set the next one
        u32 overshoot = dmg->frame_cycles - dmg->event_deadline[EV_TIMA];
        u32 period = (256 - dmg->timer_mod) * tima_divisor(dmg);
        if (dmg_double_speed(dmg)) {
            overshoot <<= 1;
            period >>= 1;
        }

        dmg_request_interrupt(dmg, INT_TIMER);
        dmg->timer_count = dmg->timer_mod;
        dmg->tima_base_cycle = dmg->total_cycles - overshoot;
        dmg->event_deadline[EV_TIMA] += period;
        break;
    }

    case EV_WRAP:
        dmg->frame_cycles -= CYCLES_PER_FRAME;
        dmg->sent_vblank_start = 0;
        dmg->rendered_this_frame = 0;
        dmg->lazy_ly = 0;
        dmg->ly_read_cycle = 0;
        raster_frame_reset(dmg);

        dmg->event_deadline[EV_STAT] = stat_event_from(dmg, 0, 0, &line);
        dmg->stat_event_line = line;
        dmg_update_frame_events(dmg);

        // shift deadlines into the new frame
        // past the wrap point here)
        if (dmg->event_deadline[EV_TIMA] != EV_NONE) {
            dmg->event_deadline[EV_TIMA] -= CYCLES_PER_FRAME;
        }
        if (dmg->event_deadline[EV_SERIAL] != EV_NONE) {
            dmg->event_deadline[EV_SERIAL] -= CYCLES_PER_FRAME;
        }

        // reset HDMA line tracking for the new frame
        if (dmg->cgb) {
            dmg->cgb->hdma_last_ly = 0xff;
            dmg->cgb->hdma_completed = 0;
        }
        break;
    }
}

// PPU cycles until the next deadline the CPU must observe
u32 dmg_cycles_to_next_event(struct dmg *dmg)
{
    u8 ie = dmg->zero_page[0x7f];
    u32 best = dmg->event_deadline[EV_WRAP];
    u32 d;

    if (ie & INT_LCDSTAT) {
        d = dmg->event_deadline[EV_STAT];
        if (d < best) {
            best = d;
        }
    }
    if (ie & INT_TIMER) {
        d = dmg->event_deadline[EV_TIMA];
        if (d < best) {
            best = d;
        }
    }
    if (ie & INT_SERIAL) {
        d = dmg->event_deadline[EV_SERIAL];
        if (d < best) {
            best = d;
        }
    }
    d = dmg->event_deadline[EV_VBLANK];
    if (d < best) {
        best = d;
    }

    if (best <= dmg->frame_cycles) {
        // exit as soon as possible
        return 0;
    }
    return best - dmg->frame_cycles;
}

// absorb a block's cycles and fire every deadline they crossed
void dmg_sync_hw(struct dmg *dmg, int cycles)
{
    int ppu_cycles;

    dmg->total_cycles += cycles;

    // In double speed mode, PPU/APU run at normal speed (half the CPU cycles)
    // If ignore_double_speed is set, treat as normal speed for better performance
    ppu_cycles = dmg_double_speed(dmg) ? (cycles >> 1) : cycles;
    dmg->frame_cycles += ppu_cycles;

    audio_mac_sync(ppu_cycles);

    if (lcd_read(dmg->lcd, REG_LCDC) & LCDC_ENABLE) {
        hdma_sync(dmg);
    }

    // fire every deadline the counter has crossed, in chronological order.
    // this continues straight through a frame wrap
    for (;;) {
        u32 best = EV_NONE;
        int ev = -1;
        int k;

        for (k = 0; k < EV_COUNT; k++) {
            if (dmg->event_deadline[k] < best) {
                best = dmg->event_deadline[k];
                ev = k;
            }
        }
        if (ev < 0 || best > dmg->frame_cycles) {
            break;
        }

        dmg_event_fire(dmg, ev);
    }
}

void dmg_ei_di(void *_dmg, u16 enabled)
{
    struct dmg *dmg = (struct dmg *) _dmg;
    dmg->interrupt_enable = enabled ? 1 : 0;

    // EI/RETI with an interrupt already pending must deliver promptly
    if (dmg->interrupt_enable) {
        dmg_budget_update(dmg);
    }
}