#ifndef _DMG_H
#define _DMG_H

#include "types.h"

struct cgb_state;

#define FIELD_JOY 1
#define FIELD_ACTION 2

#define BUTTON_RIGHT (1 << 0)
#define BUTTON_LEFT (1 << 1)
#define BUTTON_UP (1 << 2)
#define BUTTON_DOWN (1 << 3)
#define BUTTON_A (1 << 0)
#define BUTTON_B (1 << 1)
#define BUTTON_SELECT (1 << 2)
#define BUTTON_START (1 << 3)

#define REG_TIMER_DIV 0xFF04
#define REG_TIMER_COUNT 0xFF05
#define REG_TIMER_MOD 0xFF06
#define REG_TIMER_CONTROL 0xFF07

#define TIMER_CONTROL_ENABLED (1 << 2)

struct rom;
struct lcd;
struct audio;

#define CYCLES_PER_FRAME 70224
#define CYCLES_PER_LINE 456
#define CYCLES_LINE_144 (CYCLES_PER_FRAME - (10 * CYCLES_PER_LINE))

enum {
    EV_STAT,
    EV_VBLANK,
    EV_RENDER,
    EV_TIMA,
    EV_SERIAL,
    EV_WRAP, // always active to provide a fallback
    EV_COUNT
};

#define EV_NONE 0xffffffff

// (An,Dn.w) sign extends the GB address
// pages >= 8 get 0x10000 added to cancel the sign extension
#define PAGE_BIAS(ptr, page) \
    ((u8 *)(ptr) - (((u32)(page)) << 12) + (((page) >= 8) ? 0x10000 : 0))

struct dmg {
    u8 zero_page[0x80];
    // page table for fast memory access (16 pages of 4KB each).
    // page 0xf is never mapped - 0xf000-0xfdff echo, OAM, I/O and HRAM
    // all take the slow path (HRAM is mostly resolved at compile time)
    u8 *read_page[16];
    u8 *write_page[16];
    // original write_page entries for upper pages unmapped because they
    // hold compiled code (self-modifying code detection), NULL otherwise
    u8 *saved_write_page[8];

    struct rom *rom;
    struct lcd *lcd;
    struct audio *audio;
    struct cgb_state *cgb;  // NULL in DMG mode

    u8 *main_ram;
    u8 *video_ram;

    u32 frames_rendered;
    int joypad_selected;
    int action_selected;
    u8 interrupt_enable;
    u8 interrupt_request_mask;
    void (*rom_bank_switch_hook)(int new_bank);
    // observation hook for raster-relevant register writes, with the beam
    // position they landed on. NULL outside the host harness
    void (*raster_write_hook)(u16 address, u8 old_val, u8 new_val,
            u32 ly, u32 line_pos);

    u8 joypad;
    u8 action_buttons;
    // computed FF00 value, kept current on select/button changes so
    // emitted code can read it with one move.b
    u8 joyp;
    // serial port: writing SC with bit 7 + internal clock arms EV_SERIAL
    // 4096 CPU cycles out; completion reads back the no-partner value
    u8 reg_sb;
    u8 reg_sc;

    // lazy TIMA: timer_count is the counter value as of tima_base_cycle
    // (total_cycles clock); reads derive the live value the way DIV does,
    // EV_TIMA fires the overflow. frozen while the timer is disabled
    u8 timer_count;
    u8 timer_mod;
    u8 timer_control;

    u32 frame_cycles;
    u8 sent_vblank_start;
    u8 rendered_this_frame;
    u16 lazy_ly;
    u32 ly_read_cycle;

    // deadline table (see the EV_ enum)
    u32 event_deadline[EV_COUNT];
    u16 stat_event_line;

    // for DIV evaluation from cycles
    u32 total_cycles;
    u32 div_reset_cycle;

    // CPU cycle (total_cycles clock) at which timer_count was current
    u32 tima_base_cycle;

    // current ROM bank (to avoid redundant page table updates)
    int current_rom_bank;
};

void dmg_new(struct dmg *dmg, struct rom *rom, struct lcd *lcd);
void dmg_set_button(struct dmg *dmg, int field, int button, int pressed);

u8 dmg_read(void *dmg, u16 address);
void dmg_write(void *dmg, u16 address, u8 data);
u16 dmg_read16(void *_dmg, u16 address);
void dmg_write16(void *_dmg, u16 address, u16 data);

u8 dmg_read_slow(struct dmg *dmg, u16 address);
void dmg_write_slow(struct dmg *dmg, u16 address, u8 data);

void dmg_sync_hw(struct dmg *dmg, int cycles);

u32 dmg_cycles_to_next_event(struct dmg *dmg);

// CGB speed switch changed the CPU<->PPU cycle ratio; rearm CPU-clock
// deadlines
void dmg_speed_changed(struct dmg *dmg);

void hdma_sync(struct dmg *dmg);

// page table management
void dmg_init_pages(struct dmg *dmg);
void dmg_update_rom_bank(struct dmg *dmg, int bank);
void dmg_update_ram_bank(struct dmg *dmg, u8 *ram_base);

void dmg_ei_di(void *dmg, u16 enabled);

#endif
