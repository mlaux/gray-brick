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

// register state used to render a band
struct raster_regs {
    u8 lcdc;
    u8 scx, scy;
    u8 wy, wx;
    u8 bgp;
    u8 obp0, obp1;
};

// one mid-frame LCD register change
struct raster_log_entry {
    u8 line;   // first line the value applies to
    u8 reg;    // address - REG_LCD_BASE
    u8 value;
};

// overflow falls back to a final-state single-band render
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

    // scx&7 alignment each row of the packed buffer was rendered at
    u8 row_scx[144];
    u8 row_scx_uniform;

    // 1 = every line rendered, 2 = half vertical resolution
    u8 row_stride;

    // window internal line counter 
    // advances only on lines the window renders
    u8 window_line;

    // what changed since the previous rendered frame (lcd_diff_rows),
    // so blitters can skip clean rows. frame_dirty ORs all rows
    u8 row_dirty[144];
    u8 frame_dirty;

    // CGB color palettes (64 bytes each = 8 palettes x 4 colors x 2 bytes RGB555)
    u8 bg_palette_ram[64];
    u8 obj_palette_ram[64];
    u8 bcps;  // BG palette index + auto-increment (bit 7)
    u8 ocps;  // Sprite palette index + auto-increment (bit 7)

    // CGB palette dirty tracking (32 bits = 32 colors), owned by the
    // mac blitter's color cache which clears it after rebuilding
    u32 bg_palette_dirty;
    u32 obj_palette_dirty;

    // set when a palette write actually changes a byte
    u8 palette_frame_dirty;
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
// against the previous rendered frame, cgb mode also compares attrs
void lcd_diff_rows(struct lcd *lcd, int cgb);

// output the pixels to the screen
void lcd_draw(struct lcd *lcd);

struct dmg;

// render `count` aligned bg tile rows (2 packed bytes + 1 opacity byte
// each), one 32-tile map row from tile_col with wraparound
// tile_base = vram + tile data base + row_in_tile * 2
// tile_col 0-31
// count <= 21
void lcd_render_bg_tiles(
    const u8 *map_row,
    const u8 *tile_base,
    const u8 *lut,
    u8 *row,
    u8 *opac,
    int tile_col,
    int count,
    int unsigned_mode);

void lcd_render_band(
    struct dmg *dmg,
    int sy_start,
    int sy_end,
    const struct raster_regs *regs);

// dmg band with lcdc bit 0 off: fill with bgp color 0
void lcd_render_blank_band(
    struct dmg *dmg,
    int sy_start,
    int sy_end,
    const struct raster_regs *regs);
void lcd_render_objs_band(
    struct dmg *dmg,
    int sy_start,
    int sy_end,
    const struct raster_regs *regs);

// oam scan: first ten sprites per scanline in oam order
void lcd_select_objs(
    const u8 *oam,
    int tall,
    int sy_start,
    int sy_end,
    u16 *sel);

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