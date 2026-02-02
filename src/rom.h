#ifndef _ROM_H
#define _ROM_H

#include "types.h"
#include "mbc.h"

struct rom {
    u32 length;
    u8 *data;
    struct mbc *mbc;
    u8 cgb_flag;  // ROM header byte 0x143: 0x80 = CGB enhanced, 0xC0 = CGB only
};

int rom_load(struct rom *rom, const char *filename);

void rom_free(struct rom *rom);

// Extract game title from ROM header. buf should be at least 17 bytes.
// Returns pointer to buf.
char *rom_get_title(struct rom *rom, char *buf);

#endif
