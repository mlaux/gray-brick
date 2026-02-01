#ifndef _ROM_H
#define _ROM_H

#include "types.h"
#include "mbc.h"

struct rom {
    u32 length;
    u8 *data;
    struct mbc *mbc;
};

// Extract game title from ROM header. buf should be at least 17 bytes.
// Returns pointer to buf.
char *rom_get_title(struct rom *rom, char *buf);

#endif
