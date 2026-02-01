#include <stdio.h>
#include <string.h>

#include "rom.h"
#include "lcd.h"
#include "dmg.h"
#include "mbc.h"
#include "cgb.h"
#include "types.h"
#include "audio.h"
#include "../system6/jit.h"
#include "../system6/audio_mac.h"
#include "../system6/settings.h"

#define INT_VBLANK  (1 << 0)
#define INT_LCDSTAT (1 << 1)
#define INT_TIMER   (1 << 2)
#define INT_SERIAL  (1 << 3)
#define INT_JOYPAD  (1 << 4)
#define NUM_INTERRUPTS 5

#define CYCLES_PER_FRAME 70224
#define CYCLES_PER_LINE 456
#define CYCLES_MIDDLE (CYCLES_LINE_144 / 2)
#define CYCLES_LINE_144 (CYCLES_PER_FRAME - (10 * CYCLES_PER_LINE))

// TAC clock select -> cycles per TIMA increment
static const u16 timer_divisors[] = { 1024, 16, 64, 256 };

void dmg_new(struct dmg *dmg, struct rom *rom, struct lcd *lcd)
{
    dmg->rom = rom;
    dmg->lcd = lcd;

    dmg->joypad = 0xf; // nothing pressed
    dmg->action_buttons = 0xf;

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
        dmg->read_page[k] = &dmg->rom->data[k << 8];
    }

    // ROM bank 1: 0x4000-0x7fff (pages 0x40-0x7f)
    // MBC will update this when switching banks
    for (k = 0x40; k <= 0x7f; k++) {
        dmg->read_page[k] = &dmg->rom->data[k << 8];
    }

    // video RAM: 0x8000-0x9fff (pages 0x80-0x9f)
    // Uses bank 0 initially, cgb_update_vram_bank() can switch for CGB
    for (k = 0x80; k <= 0x9f; k++) {
        int offset = (k - 0x80) << 8;
        dmg->read_page[k] = &dmg->video_ram[offset];
        dmg->write_page[k] = &dmg->video_ram[offset];
    }

    // external RAM: 0xa000-0xbfff (pages 0xa0-0xbf)
    // leave NULL - MBC handles this

    // work RAM bank 0: 0xc000-0xcfff (pages 0xc0-0xcf) - always fixed
    for (k = 0xc0; k <= 0xcf; k++) {
        int offset = (k - 0xc0) << 8;
        dmg->read_page[k] = &dmg->main_ram[offset];
        dmg->write_page[k] = &dmg->main_ram[offset];
    }

    // work RAM bank 1: 0xd000-0xdfff (pages 0xd0-0xdf) - switchable in CGB
    // Initially points to bank 1 (offset 0x1000)
    for (k = 0xd0; k <= 0xdf; k++) {
        int offset = 0x1000 + ((k - 0xd0) << 8);
        dmg->read_page[k] = &dmg->main_ram[offset];
        dmg->write_page[k] = &dmg->main_ram[offset];
    }

    // echo RAM: 0xe000-0xfdff (pages 0xe0-0xfd)
    // Mirrors 0xc000-0xddff (bank 0 + current switchable bank)
    for (k = 0xe0; k <= 0xef; k++) {
        // Echo of 0xc000-0xcfff (bank 0)
        int offset = (k - 0xe0) << 8;
        dmg->read_page[k] = &dmg->main_ram[offset];
        dmg->write_page[k] = &dmg->main_ram[offset];
    }
    for (k = 0xf0; k <= 0xfd; k++) {
        // Echo of 0xd000-0xddff (switchable bank)
        int offset = 0x1000 + ((k - 0xf0) << 8);
        dmg->read_page[k] = &dmg->main_ram[offset];
        dmg->write_page[k] = &dmg->main_ram[offset];
    }

    // pages 0xfe and 0xff stay NULL for special handling
}

void dmg_update_rom_bank(struct dmg *dmg, int bank)
{
    int k;
    u8 *bank_base = &dmg->rom->data[bank * 0x4000];
    for (k = 0x40; k <= 0x7f; k++) {
        dmg->read_page[k] = &bank_base[(k - 0x40) << 8];
    }

    // Notify JIT of bank switch
    if (dmg->rom_bank_switch_hook) {
        dmg->rom_bank_switch_hook(bank);
    }
}

void dmg_update_ram_bank(struct dmg *dmg, u8 *ram_base)
{
    int k;
    for (k = 0xa0; k <= 0xbf; k++) {
        if (ram_base) {
            int offset = (k - 0xa0) << 8;
            dmg->read_page[k] = &ram_base[offset];
            dmg->write_page[k] = &ram_base[offset];
        } else {
            dmg->read_page[k] = NULL;
            dmg->write_page[k] = NULL;
        }
    }
}

static void dmg_request_interrupt(struct dmg *dmg, int nr)
{
    dmg->interrupt_request_mask |= nr;
}

void dmg_set_button(struct dmg *dmg, int field, int button, int pressed)
{
    u8 *mod;
    if (field == FIELD_JOY) {
        mod = &dmg->joypad;
    } else if (field == FIELD_ACTION) {
        mod = &dmg->action_buttons;
    } else {
        printf("setting invalid button state\n");
        return;
    }

    if (pressed) {
        *mod &= ~button;
    } else {
        *mod |= button;
    }
}

static u8 get_button_state(struct dmg *dmg)
{
    u8 ret = 0xf0;
    if (dmg->action_selected) {
        ret |= dmg->action_buttons;
    }
    if (dmg->joypad_selected) {
        ret |= dmg->joypad;
    }
    return ret;
}

u8 dmg_read_slow(struct dmg *dmg, u16 address)
{
    if (address == REG_LY) {
        // O(1) LY calculation - each scanline is 456 cycles, 154 lines per frame
        u32 current = dmg->frame_cycles + jit_ctx.read_cycles;
        if (current >= 70224) {
            current -= 70224;
        }
        return (current / 456) % 154;
    }

    if (address == REG_STAT) {
        // just cycle through the modes, the game gets the one it needs
        u8 stat = lcd_read(dmg->lcd, REG_STAT);
        stat = (stat & 0xfc) | (((stat & 3) + 1) & 3);
        lcd_write(dmg->lcd, REG_STAT, stat);
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
        return get_button_state(dmg);
    }
    if (address == REG_TIMER_DIV) {
        // compute based on total cycles + in-flight cycles from JIT
        u32 current = dmg->total_cycles + jit_ctx.read_cycles;
        u32 div_val = current - dmg->div_reset_cycle;
        return (div_val >> 8) & 0xff;
    }
    if (address == REG_TIMER_COUNT) {
        return dmg->timer_count;
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
        val = page[address & 0xff];
    } else {
        val = dmg_read_slow(dmg, address);
    }
    return val;
}

void dmg_write_slow(struct dmg *dmg, u16 address, u8 data)
{
    // ROM region writes go to MBC for bank switching
    if (address < 0x8000) {
        mbc_write(dmg->rom->mbc, dmg, address, data);
        return;
    }

    // external RAM not enabled, or RTC register selected
    if (address >= 0xa000 && address < 0xc000) {
        mbc_ram_write(dmg->rom->mbc, address, data);
        return;
    }

    // OAM DMA
    if (address == 0xff46) {
        u16 src = data << 8;
        int k = 0;
        for (u16 addr = src; addr < src + 0xa0; addr++) {
            dmg->lcd->oam[k++] = dmg_read(dmg, addr);
        }
        return;
    }

    // BGP write - update the palette LUT
    if (address == REG_BGP) {
        lcd_update_palette_lut(data);
        lcd_write(dmg->lcd, address, data);
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
        return;
    }

    // I/O registers
    if (address == 0xff00) {
        dmg->joypad_selected = !(data & (1 << 4));
        dmg->action_selected = !(data & (1 << 5));
        return;
    }
    if (address == REG_TIMER_DIV) {
        // writing any value resets DIV to 0 at this cycle
        dmg->div_reset_cycle = dmg->total_cycles + jit_ctx.read_cycles;
        return;
    }
    if (address == REG_TIMER_COUNT) {
        dmg->timer_count = data;
        return;
    }
    if (address == REG_TIMER_MOD) {
        dmg->timer_mod = data;
        return;
    }
    if (address == REG_TIMER_CONTROL) {
        dmg->timer_control = data;
        return;
    }
    if (address >= 0xff10 && address <= 0xff3f) {
        audio_write(dmg->audio, address, data);
        return;
    }
    if (address == 0xff0f) {
        dmg->interrupt_request_mask = data;
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
        page[address & 0xff] = data;
        return;
    }
    dmg_write_slow(dmg, address, data);
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

static void lcd_sync(struct dmg *dmg)
{
    int lcdc = lcd_read(dmg->lcd, REG_LCDC);
    int lyc_cycles = lcd_read(dmg->lcd, REG_LYC) * CYCLES_PER_LINE;

    // Process HBlank DMA if active (CGB mode only)
    if (dmg->cgb && dmg->cgb->mode) {
        int current_line = dmg->frame_cycles / CYCLES_PER_LINE;
        cgb_process_hdma(dmg->cgb, dmg, current_line);
    }

    if (dmg->frame_cycles >= lyc_cycles && !dmg->sent_ly_interrupt) {
        lcd_set_bit(dmg->lcd, REG_STAT, STAT_FLAG_MATCH);
        if (lcd_isset(dmg->lcd, REG_STAT, STAT_INTR_SOURCE_MATCH)) {
            dmg_request_interrupt(dmg, INT_LCDSTAT);
        }
        dmg->sent_ly_interrupt = 1;
    } else {
        lcd_clear_bit(dmg->lcd, REG_STAT, STAT_FLAG_MATCH);
    }

    if (dmg->frame_cycles >= CYCLES_MIDDLE && !dmg->rendered_this_frame) {
        // MAYBE todo: render per-tile-row? might be a good compromise - when 
        // drawing so inaccurately, there is no "best" choice for where to do
        // this, so just do it in the middle of the screen. this could maybe avoid
        // games turning off sprites for text boxes, messing with the scroll for
        // status bars, etc

        // frame skip setting is 0-4
        if (dmg->frames_rendered % (frame_skip + 1) == 0) {
            if (lcdc & LCDC_ENABLE_BG) {
                if (dmg->cgb && dmg->cgb->mode) {
                    lcd_cgb_render_background(dmg, lcdc, lcdc & LCDC_ENABLE_WINDOW);
                } else {
                    lcd_render_background(dmg, lcdc, lcdc & LCDC_ENABLE_WINDOW);
                }
            }
            if (lcdc & LCDC_ENABLE_OBJ) {
                if (dmg->cgb && dmg->cgb->mode) {
                    lcd_cgb_render_objs(dmg);
                } else {
                    lcd_render_objs(dmg);
                }
            }
            lcd_draw(dmg->lcd);
        }

        dmg->frames_rendered++;
        dmg->rendered_this_frame = 1;
    }

    if (dmg->frame_cycles >= CYCLES_LINE_144 && !dmg->sent_vblank_start) {
        // fire VBLANK once per frame
        dmg_request_interrupt(dmg, INT_VBLANK);
        if (lcd_isset(dmg->lcd, REG_STAT, STAT_INTR_SOURCE_VBLANK)) {
            dmg_request_interrupt(dmg, INT_LCDSTAT);
        }
        dmg->sent_vblank_start = 1;
    }
}

// TIMA timer
static void timer_sync(struct dmg *dmg, int cycles)
{
    u16 divisor = timer_divisors[dmg->timer_control & 3];
    dmg->timer_cycles += cycles;
    while (dmg->timer_cycles >= divisor) {
        dmg->timer_cycles -= divisor;
        dmg->timer_count++;
        if (dmg->timer_count == 0) {
            // overflow: reload from TMA and request interrupt
            dmg->timer_count = dmg->timer_mod;
            dmg_request_interrupt(dmg, INT_TIMER);
        }
    }
}

// not accurate at all, but not going for accuracy. i'm FINALLY happy with the
// logic here. supports arbitrary cycles_per_exit, currently user configurable
// between every line, every 16 lines, and every 1 frame
// note that the user configurable setting is less relevant for games that use
// HALT, because HALT will skip directly to line 144
void dmg_sync_hw(struct dmg *dmg, int cycles)
{
    int ppu_cycles;

    dmg->total_cycles += cycles;

    // In double speed mode, PPU/APU run at normal speed (half the CPU cycles)
    ppu_cycles = (dmg->cgb && dmg->cgb->double_speed) ? (cycles >> 1) : cycles;
    dmg->frame_cycles += ppu_cycles;

    audio_mac_sync(ppu_cycles);

    if (lcd_read(dmg->lcd, REG_LCDC) & LCDC_ENABLE) {
        lcd_sync(dmg);
    }

    if (dmg->timer_control & TIMER_CONTROL_ENABLED) {
        timer_sync(dmg, cycles);
    }

    if (dmg->frame_cycles >= CYCLES_PER_FRAME) {
        dmg->frame_cycles %= CYCLES_PER_FRAME;
        dmg->sent_vblank_start = 0;
        dmg->sent_ly_interrupt = 0;
        dmg->rendered_this_frame = 0;
        // Reset HDMA line counter for CGB
        if (dmg->cgb) {
            dmg->cgb->hdma_last_line = 0;
        }
    }
}

void dmg_ei_di(void *_dmg, u16 enabled)
{
    struct dmg *dmg = (struct dmg *) _dmg;
    dmg->interrupt_enable = enabled ? 1 : 0;
}