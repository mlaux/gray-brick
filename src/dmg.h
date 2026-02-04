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

struct dmg {
    u8 zero_page[0x80];
    // page table for fast memory access (256 pages of 256 bytes each)
    u8 *read_page[256];
    u8 *write_page[256];

    struct rom *rom;
    struct lcd *lcd;
    struct audio *audio;
    struct cgb_state *cgb;  // NULL in DMG mode

    u8 main_ram[0x8000];   // 32KB WRAM for CGB (8 x 4KB banks), only 8KB used in DMG
    u8 video_ram[0x4000];  // 16KB VRAM for CGB (2 x 8KB banks), only 8KB used in DMG
    u32 frames_rendered;
    int joypad_selected;
    int action_selected;
    u8 interrupt_enable;
    u8 interrupt_request_mask;
    void (*rom_bank_switch_hook)(int new_bank);

    u8 joypad;
    u8 action_buttons;
    u16 timer_div;
    u8 timer_count;
    u8 timer_mod;
    u8 timer_control;

    u32 frame_cycles;
    u8 sent_ly_interrupt;
    u8 sent_vblank_start;
    u8 rendered_this_frame;
    u16 lazy_ly;
    u32 ly_read_cycle;

    // for DIV evaluation from cycles
    u32 total_cycles;
    u32 div_reset_cycle;

    // for TIMA timer
    u32 timer_cycles;

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

// page table management
void dmg_init_pages(struct dmg *dmg);
void dmg_update_rom_bank(struct dmg *dmg, int bank);
void dmg_update_ram_bank(struct dmg *dmg, u8 *ram_base);

void dmg_ei_di(void *dmg, u16 enabled);

#endif
