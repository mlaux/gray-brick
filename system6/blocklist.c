/* Game Boy emulator for 68k Macs
   blocklist.c - save the (bank, pc) of every compiled ROM block at session
   end, then precompile the list on the next load

   File format (68k byte order):
     u16 version
     u32 crc32 of the ROM
     u16 count
     count * { u16 bank, u16 pc } */

#include <stdio.h>
#include <string.h>
#include <Files.h>

#include "types.h"
#include "dmg.h"
#include "rom.h"
#include "cache.h"
#include "jit.h"
#include "emulator.h"
#include "blocklist.h"

#include "crc32_table.h"

#define BLOCKLIST_VERSION 1

static u32 rom_crc32(const struct rom *rom)
{
    u32 crc = 0xffffffff;
    u32 k;

    for (k = 0; k < rom->length; k++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ rom->data[k]) & 0xff];
    }
    return crc ^ 0xffffffff;
}

static void build_filename(const char *title, char *out)
{
    sprintf(out, ":Caches:%s cache", title);
}

static u32 write_entries(FILE *fp, u32 limit)
{
    void **bank0;
    void ***banked;
    void **upper;
    u32 count = 0;
    int bank, k;
    u16 pair[2];

    cache_get_arrays(&bank0, &banked, &upper);
    if (!bank0 || !banked) {
        return 0;
    }

    for (k = 0; k < BANK0_CACHE_SIZE && count < limit; k++) {
        if (!bank0[k]) {
            continue;
        }
        if (fp) {
            pair[0] = 0;
            pair[1] = k;
            fwrite(pair, sizeof pair, 1, fp);
        }
        count++;
    }

    for (bank = 0; bank < MAX_ROM_BANKS && count < limit; bank++) {
        if (!banked[bank]) {
            continue;
        }
        for (k = 0; k < BANKED_CACHE_SIZE && count < limit; k++) {
            if (!banked[bank][k]) {
                continue;
            }
            if (fp) {
                pair[0] = bank;
                pair[1] = 0x4000 + k;
                fwrite(pair, sizeof pair, 1, fp);
            }
            count++;
        }
    }

    // skip upper code, it's RAM

    return count;
}

void blocklist_save(struct dmg *dmg, const char *title)
{
    char filename[40];
    Str63 pname;
    FInfo info;
    FILE *fp;
    int len;
    u16 version = BLOCKLIST_VERSION;
    u16 count16;
    u32 crc;
    u32 count = write_entries(NULL, 0xffff);

    if (!count) {
        return;
    }

    ensure_folder("\pCaches");
    build_filename(title, filename);
    fp = fopen(filename, "w");
    if (!fp) {
        return;
    }

    crc = rom_crc32(dmg->rom);
    count16 = count;
    fwrite(&version, sizeof version, 1, fp);
    fwrite(&crc, sizeof crc, 1, fp);
    fwrite(&count16, sizeof count16, 1, fp);
    write_entries(fp, count);
    fclose(fp);

    len = strlen(filename);
    pname[0] = len;
    memcpy(&pname[1], filename, len);
    if (GetFInfo(pname, 0, &info) == noErr) {
        info.fdType = 'BLST';
        info.fdCreator = 'MGBE';
        SetFInfo(pname, 0, &info);
    }
}

void blocklist_load(struct dmg *dmg, const char *title)
{
    char filename[40];
    FILE *fp;
    u16 version, count;
    u16 pair[2];
    u32 crc;
    u32 k;

    build_filename(title, filename);
    fp = fopen(filename, "r");
    if (!fp) {
        return;
    }

    if (fread(&version, sizeof version, 1, fp) != 1
            || version != BLOCKLIST_VERSION
            || fread(&crc, sizeof crc, 1, fp) != 1
            || fread(&count, sizeof count, 1, fp) != 1
            || !count
            || crc != rom_crc32(dmg->rom)) {
        fclose(fp);
        return;
    }

    set_status_bar("Compiling...");
    draw_progress_bar(0, count);

    for (k = 0; k < count; k++) {
        if (fread(pair, sizeof pair, 1, fp) != 1) {
            break;
        }
        if (pair[1] >= 0x8000) {
            continue;
        }
        if (pair[1] >= 0x4000
                && (pair[0] >= MAX_ROM_BANKS
                    || (u32) pair[0] * 0x4000 >= dmg->rom->length)) {
            continue;
        }
        if (!jit_precompile((u8) pair[0], pair[1])) {
            break;
        }
        draw_progress_bar(k + 1, count);
    }

    jit_precompile_finish();
    fclose(fp);
}
