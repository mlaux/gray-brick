#ifndef _LCD_H
#define _LCD_H

#include "types.h"

#define LCD_WIDTH 160
#define LCD_HEIGHT 144

#define REG_LCD_BASE 0xff40

#define REG_LCDC 0xff40
#define REG_STAT 0xff41
#define REG_SCY  0xff42
#define REG_SCX  0xff43
#define REG_LY   0xff44
#define REG_LYC  0xff45
#define REG_DMA  0xff46
#define REG_BGP  0xff47
#define REG_OBP0 0xff48
#define REG_OBP1 0xff49
#define REG_WY   0xff4a
#define REG_WX   0xff4b

#define REG_LCD_LAST REG_WX

#define STAT_FLAG_MATCH (1 << 2)
#define STAT_INTR_SOURCE_HBLANK (1 << 3)
#define STAT_INTR_SOURCE_VBLANK (1 << 4)
#define STAT_INTR_SOURCE_MODE2 (1 << 5)
#define STAT_INTR_SOURCE_MATCH (1 << 6)


#define LCDC_ENABLE_BG (1 << 0)
#define LCDC_ENABLE_OBJ (1 << 1)
#define LCDC_OBJ_SIZE (1 << 2)
#define LCDC_BG_TILE_MAP (1 << 3)
#define LCDC_BG_TILE_DATA (1 << 4)
#define LCDC_ENABLE_WINDOW (1 << 5)
#define LCDC_WINDOW_TILE_MAP (1 << 6)
#define LCDC_ENABLE (1 << 7)

#define OAM_ATTR_PALETTE (1 << 4)
#define OAM_ATTR_MIRROR_X (1 << 5)
#define OAM_ATTR_MIRROR_Y (1 << 6)
#define OAM_ATTR_BEHIND_BG (1 << 7)

struct lcd {
    u8 oam[0xa0];
    u8 regs[0x0c];
    u8 *pixels; // 168x144 packed buffer (42 bytes/row) for scroll offset handling
    u8 *attrs;  // CGB: attribute buffer (168 bytes/row) with per-pixel palette/priority

    // CGB color palettes (64 bytes each = 8 palettes x 4 colors x 2 bytes RGB555)
    u8 bg_palette_ram[64];
    u8 obj_palette_ram[64];
    u8 bcps;  // BG palette index + auto-increment (bit 7)
    u8 ocps;  // Sprite palette index + auto-increment (bit 7)

    // CGB palette dirty tracking (32 bits = 32 colors)
    u32 bg_palette_dirty;
    u32 obj_palette_dirty;
};

void lcd_new(struct lcd *lcd);
void lcd_init_lut(void);
void lcd_update_palette_lut(u8 palette);
u8 lcd_is_valid_addr(u16 addr);

static inline u8 lcd_read(struct lcd *lcd, u16 addr)
{
    if (addr >= 0xfe00 && addr < 0xfea0) {
        return lcd->oam[addr - 0xfe00];
    }
    return lcd->regs[addr - REG_LCD_BASE];
}

static inline void lcd_write(struct lcd *lcd, u16 addr, u8 value)
{
    if (addr >= 0xfe00 && addr < 0xfea0) {
        lcd->oam[addr - 0xfe00] = value;
    } else {
        lcd->regs[addr - REG_LCD_BASE] = value;
    }
}

static inline void lcd_set_bit(struct lcd *lcd, u16 addr, u8 bit)
{
    lcd_write(lcd, addr, lcd_read(lcd, addr) | bit);
}

static inline void lcd_clear_bit(struct lcd *lcd, u16 addr, u8 bit)
{
    lcd_write(lcd, addr, lcd_read(lcd, addr) & ~bit);
}

static inline int lcd_isset(struct lcd *lcd, u16 addr, u8 bit)
{
    u8 val = lcd_read(lcd, addr);
    return val & bit;
}

static inline void lcd_set_mode(struct lcd *lcd, int mode)
{
    u8 val = lcd_read(lcd, REG_STAT);
    lcd_write(lcd, REG_STAT, (val & 0xfc) | mode);
}

int lcd_step(struct lcd *lcd);

// output the pixels to the screen
void lcd_draw(struct lcd *lcd);

struct dmg;
void lcd_render_background(struct dmg *dmg, int lcdc, int window_enabled);
void lcd_render_objs(struct dmg *dmg);

// CGB-specific rendering (in lcd_cgb.c)
void lcd_cgb_init_lut(void);
void lcd_cgb_render_background(struct dmg *dmg, int lcdc, int window_enabled);
void lcd_cgb_render_objs(struct dmg *dmg);

// Horizontal flip LUT (initialized by lcd_init_lut)
extern u8 hflip_lut[256];

#endif