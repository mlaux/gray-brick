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

// register state a band renders from: the live registers when nothing
// changed mid-frame, or one step of the replayed write log
struct raster_regs {
    u8 lcdc;
    u8 scx, scy;
    u8 wy, wx;
    u8 bgp;
    u8 obp0, obp1;
};

// a mid-frame raster register change, recorded against the beam position
// and replayed as a band boundary at render time
struct raster_log_entry {
    u8 line;   // first line the value applies to
    u8 reg;    // address - REG_LCD_BASE
    u8 value;
};

// max observed changed-writes/frame is ~18 (dmg-acid2); pokegold's
// per-line SCY intro peaks at ~35. overflow falls back to a final-state
// single-band render
#define RASTER_LOG_SIZE 64

// row_dirty flags from lcd_diff_rows
#define ROW_DIRTY_CONTENT 1  // packed pixel bytes changed
#define ROW_DIRTY_OFFSET 2   // row_scx changed

struct lcd {
    u8 oam[0xa0];
    u8 regs[0x0c];
    u8 *pixels; // 168x144 packed buffer (42 bytes/row) for scroll offset handling
    u8 *attrs;  // CGB: attribute buffer (168 bytes/row) with per-pixel palette/priority

    // raster write log: reg state at line 0 plus the mid-frame changes,
    // replayed as bands at render time (empty log = one band)
    struct raster_regs frame_regs;
    struct raster_log_entry raster_log[RASTER_LOG_SIZE];
    u8 raster_count;
    u8 raster_overflow;

    // scx&7 alignment each row of the packed buffer was rendered at; the
    // blitters start row y's visible 160 pixels at row_scx[y].
    // row_scx_uniform means every row matches row_scx[0], so a single
    // whole-frame blit offset still works
    u8 row_scx[144];
    u8 row_scx_uniform;

    // what changed since the previous rendered frame (lcd_diff_rows),
    // so blitters can skip clean rows. frame_dirty ORs all rows
    u8 row_dirty[144];
    u8 frame_dirty;

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

// fill row_dirty/frame_dirty by comparing the packed buffer and row_scx
// against the previous rendered frame
void lcd_diff_rows(struct lcd *lcd);

// output the pixels to the screen
void lcd_draw(struct lcd *lcd);

struct dmg;

void lcd_render_band(
    struct dmg *dmg,
    int sy_start,
    int sy_end,
    const struct raster_regs *regs);
void lcd_render_objs_band(
    struct dmg *dmg,
    int sy_start,
    int sy_end,
    const struct raster_regs *regs);

// CGB-specific rendering (in lcd_cgb.c)
void lcd_cgb_init_lut(void);
void lcd_cgb_render_band(
    struct dmg *dmg,
    int sy_start,
    int sy_end,
    const struct raster_regs *regs);
void lcd_cgb_render_objs_band(
    struct dmg *dmg,
    int sy_start,
    int sy_end,
    const struct raster_regs *regs);

// Horizontal flip LUT (initialized by lcd_init_lut)
extern u8 hflip_lut[256];

#endif