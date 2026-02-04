#ifndef _PATCHES_H
#define _PATCHES_H

#include "types.h"

struct rom_patch {
    u16 address;
    u16 length;
    const u8 *original;    /* NULL = don't verify, just apply */
    const u8 *replacement;
};

struct rom_patch_list {
    const char *game_title;
    const struct rom_patch *patches;
    int patch_count;
};

/* Find patches for a game by title. Returns NULL if no patches found. */
const struct rom_patch_list *patches_find(const char *title);

/* Apply all patches from a patch list to ROM data. Returns number of patches applied. */
int patches_apply(u8 *rom_data, u32 rom_length, const struct rom_patch_list *list);

#endif
