#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "rom.h"
#include "types.h"

char *rom_get_title(struct rom *rom, char *buf)
{
    int k, len;

    // title is at 0x134-0x143 (16 bytes)
    memcpy(buf, &rom->data[0x134], 16);
    buf[16] = '\0';
    len = strlen(buf);

    if ((unsigned char) buf[15] == 0x80 || (unsigned char) buf[15] == 0xc0) {
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
