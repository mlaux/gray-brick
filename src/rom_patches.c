#include <string.h>
#include "rom_patches.h"

// Pokemon Red/Blue: fix terrible frame rate in menus
// make HandleMenuInput and WaitForTextScrollButtonPress halt instead of spin
static const u8 pokemon_red_patch0[] = { 0xd5 };  /* jr $3ad9 -> jr $3ad6 */
static const u8 pokemon_red_patch1[] = { 0xd7, 0x3d };  /* call $3e6d -> call $3dd7 */

static const struct rom_patch pokemon_red_patches[] = {
    { 0x3b00, 1, NULL, pokemon_red_patch0 },
    { 0x3889, 2, NULL, pokemon_red_patch1 },
};

// see patches/kaeru.asm
static const u8 kaeru_vector_orig[] = { 0xc3, 0x65, 0x03 }; // jp $0365
// jp $3c13, empty space in original rom
static const u8 kaeru_vector[] = { 0xc3, 0x13, 0x3c };       

static const u8 kaeru_handler[] = {
    0xf5, 0xe5, 0xf0, 0xbc, 0xa7, 0x20, 0x0c, 0xf0, 0x41, 0xe6,
    0x08, 0x28, 0x15, 0x3e, 0x40, 0xe0, 0x41, 0x18, 0x0f, 0xf0,
    0x41, 0xe6, 0x08, 0x20, 0x04, 0x3e, 0x48, 0xe0, 0x41, 0xf0,
    0xbc, 0xc3, 0x69, 0x03, 0xf0, 0x44, 0x21, 0xbe, 0xff, 0xbe,
    0x38, 0x30, 0x23, 0xbe, 0x30, 0x16, 0x3e, 0xe4, 0xe0, 0x47,
    0xf0, 0xc0, 0xe0, 0x42, 0xaf, 0xe0, 0x43, 0x3e, 0xe1, 0xe0,
    0x40, 0xf0, 0xbf, 0xe0, 0x45, 0xe1, 0xf1, 0xd9, 0xf0, 0xa6,
    0xe0, 0x47, 0xf0, 0xaa, 0xe0, 0x42, 0xf0, 0xa9, 0xe0, 0x43,
    0x3e, 0xe3, 0xe0, 0x40, 0xaf, 0xe0, 0x45, 0xe1, 0xf1, 0xd9,
    0x21, 0x71, 0xdf, 0xf0, 0x44, 0x85, 0x6f, 0xf0, 0xa9, 0x86,
    0xe0, 0x43, 0xf0, 0xbe, 0xe0, 0x45, 0xe1, 0xf1, 0xd9,
};

static const struct rom_patch kaeru_patches[] = {
    { 0x0048, 3, kaeru_vector_orig, kaeru_vector },
    { 0x3c13, sizeof(kaeru_handler), NULL, kaeru_handler },
};

static const struct rom_patch_list all_patches[] = {
    { "POKEMON RED",   pokemon_red_patches, 2 },
    { "POKEMON BLUE",  pokemon_red_patches, 2 },
    { "KAERUNOTAMENI", kaeru_patches,       2 },
};

#define PATCH_LIST_COUNT (sizeof(all_patches) / sizeof(all_patches[0]))

const struct rom_patch_list *patches_find(const char *title)
{
    size_t k;

    for (k = 0; k < PATCH_LIST_COUNT; k++) {
        if (strcmp(title, all_patches[k].game_title) == 0) {
            return &all_patches[k];
        }
    }
    return NULL;
}

int patches_apply(u8 *rom_data, u32 rom_length, const struct rom_patch_list *list)
{
    int k, applied = 0;
    const struct rom_patch *p;

    for (k = 0; k < list->patch_count; k++) {
        p = &list->patches[k];

        if (p->address + p->length > rom_length) {
            continue;
        }

        if (p->original != NULL) {
            if (memcmp(rom_data + p->address, p->original, p->length) != 0) {
                continue;
            }
        }

        memcpy(rom_data + p->address, p->replacement, p->length);
        applied++;
    }

    return applied;
}
