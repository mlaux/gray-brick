#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "rom.h"
#include "types.h"

// this was only used by the imgui version, can remove
int rom_load(struct rom *rom, const char *filename)
{
    FILE *fp;
    int len;

    fp = fopen(filename, "r");
    if (!fp) {
        return 0;
    }

    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    rewind(fp);

    rom->data = malloc(len);
    rom->length = len;
    if (fread(rom->data, 1, len, fp) < len) {
        return 0;
    }
    rom->mbc = mbc_new(rom->data[0x147]);
    if (!rom->mbc) {
        return 0;
    }

    return 1;
}

void rom_free(struct rom *rom)
{
    free(rom->data);
}

char *rom_get_title(struct rom *rom, char *buf)
{
    int k, len;

    // title is at 0x134-0x143 (16 bytes)
    memcpy(buf, &rom->data[0x134], 16);
    buf[16] = '\0';
    len = strlen(buf);

    if ((unsigned char) buf[15] & 0xc0) {
        // CGB game or GB compatible CGB game
        buf[11] = '\0';
        len = strlen(buf);
    }

    // trim trailing spaces
    for (k = len - 1; k >= 0 && buf[k] == ' '; k--) {
        buf[k] = '\0';
    }

    return buf;
}
