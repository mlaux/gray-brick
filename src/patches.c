#include <string.h>
#include "patches.h"

// Pokemon Red/Blue: fix terrible frame rate in menus
// make HandleMenuInput and WaitForTextScrollButtonPress halt instead of spin
static const u8 pokemon_red_patch0[] = { 0xd5 };  /* jr $3ad9 -> jr $3ad6 */
static const u8 pokemon_red_patch1[] = { 0xd7, 0x3d };  /* call $3e6d -> call $3dd7 */

static const struct rom_patch pokemon_red_patches[] = {
    { 0x3b00, 1, NULL, pokemon_red_patch0 },
    { 0x3889, 2, NULL, pokemon_red_patch1 },
};

static const struct rom_patch_list all_patches[] = {
    { "POKEMON RED",  pokemon_red_patches,  2 },
    { "POKEMON BLUE", pokemon_red_patches,  2 },
};

#define PATCH_LIST_COUNT (sizeof(all_patches) / sizeof(all_patches[0]))

const struct rom_patch_list *patches_find(const char *title)
{
    int k;

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
