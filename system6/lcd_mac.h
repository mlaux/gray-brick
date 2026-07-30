#ifndef _LCD_MAC_H
#define _LCD_MAC_H

#include <Quickdraw.h>

typedef struct {
    const char *name;
    RGBColor colors[4];
} GBPalette;

extern int current_palette;

void init_dither_lut(void);
void init_video_luts(WindowPtr wp);

struct lcd;
void lcd_draw(struct lcd *lcd_ptr);
void lcd_blit_color_bands(PixMap *src, struct lcd *lcd_ptr, int scale, int all);
PixMap *lcd_active_pixmap(void);

void lcd_mac_invalidate(void);

#endif