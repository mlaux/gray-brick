#ifndef _CGB_H
#define _CGB_H

#include "types.h"

// CGB HDMA registers
#define REG_HDMA1 0xFF51   // Source high byte
#define REG_HDMA2 0xFF52   // Source low byte (bits 0-3 ignored)
#define REG_HDMA3 0xFF53   // Dest high byte (forced to $80-$9F)
#define REG_HDMA4 0xFF54   // Dest low byte (bits 0-3 ignored)
#define REG_HDMA5 0xFF55   // Mode (bit 7) + length-1 (bits 0-6)

// CGB speed switch register
#define REG_KEY1 0xFF4D    // Bit 7 = current speed, bit 0 = switch armed

// CGB VRAM/WRAM bank registers
#define REG_VBK  0xFF4F    // VRAM bank (bit 0)
#define REG_SVBK 0xFF70    // WRAM bank (bits 0-2)

// CGB palette registers
#define REG_BCPS 0xFF68    // BG palette index + auto-increment
#define REG_BCPD 0xFF69    // BG palette data
#define REG_OCPS 0xFF6A    // OBJ palette index + auto-increment
#define REG_OCPD 0xFF6B    // OBJ palette data

// CGB tile attribute bits (from VRAM bank 1 tile map)
#define CGB_ATTR_PALETTE    0x07  // bits 0-2: palette number (0-7)
#define CGB_ATTR_VRAM_BANK  (1 << 3)  // bit 3: tile data VRAM bank
#define CGB_ATTR_HFLIP      (1 << 5)  // bit 5: horizontal flip
#define CGB_ATTR_VFLIP      (1 << 6)  // bit 6: vertical flip
#define CGB_ATTR_PRIORITY   (1 << 7)  // bit 7: BG priority over sprites

// CGB OAM attribute bits
#define OAM_ATTR_CGB_PALETTE   0x07       // bits 0-2: CGB palette number (0-7)
#define OAM_ATTR_CGB_VRAM_BANK (1 << 3)   // bit 3: tile data VRAM bank

// Per-pixel attribute buffer format (1 byte per pixel)
// bit 0-2: palette number (0-7)
// bit 3: BG priority flag
// bit 4: is_sprite flag (1 = sprite pixel, 0 = BG pixel)
#define ATTR_PALETTE_MASK   0x07
#define ATTR_PRIORITY       (1 << 3)
#define ATTR_IS_SPRITE      (1 << 4)

struct cgb_state {
    u8 mode;               // 1 if CGB mode active
    u8 double_speed;       // 1 if running at 8MHz
    u8 speed_switch_armed; // KEY1 bit 0
    u8 vram_bank;          // VBK register - VRAM bank (0-1)
    u8 wram_bank;          // SVBK register - WRAM bank (1-7, 0 treated as 1)

    // HDMA state
    u8 hdma_active;        // 1 if HDMA transfer is in progress
    u8 hdma_remaining;     // Blocks remaining - 1 (0 = 1 block left)
    u16 hdma_source;       // Current source address (updated during transfer)
    u16 hdma_dest;         // Current destination address (updated during transfer)
    u8 hdma_last_ly;       // Last LY that triggered HDMA (0xFF = none this frame)
};

struct dmg;
struct lcd;

// Initialize CGB state based on ROM header flag
void cgb_init(struct cgb_state *cgb, int cgb_flag);

// CGB register I/O - returns 1 if address was handled, 0 otherwise
int cgb_read_reg(struct cgb_state *cgb, struct lcd *lcd, u16 address, u8 *out);
int cgb_write_reg(struct cgb_state *cgb, struct dmg *dmg, u16 address, u8 data);

// Page table updates for bank switching
void cgb_update_vram_bank(struct cgb_state *cgb, struct dmg *dmg);
void cgb_update_wram_bank(struct cgb_state *cgb, struct dmg *dmg);

// Speed switch - called by STOP instruction
// Returns 1 if speed was switched, 0 if should halt
int cgb_speed_switch(struct cgb_state *cgb);

// HDMA functions
// Called when HBlank starts (LY < 144) to transfer 16 bytes if HDMA is active
// Returns number of M-cycles the CPU should be halted (8 in normal speed, 16 in double speed)
int cgb_hdma_hblank(struct cgb_state *cgb, struct dmg *dmg, u8 ly);

#endif
