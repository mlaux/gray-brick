#include <Files.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "dmg.h"
#include "lcd.h"

static int debug_enabled = 1;

void debug_log_string(const char *str)
{
  short fref;
  char buf[128];
  long len;
  OSErr err;
  err = FSOpen("\pjit_log.txt", 0, &fref);
  if (err == fnfErr) {
      Create("\pjit_log.txt", 0, 'ttxt', 'TEXT');
      err = FSOpen("\pjit_log.txt", 0, &fref);
  }
  if (err != noErr) return;
  // Seek to end
  SetFPos(fref, fsFromLEOF, 0);
  len = strlen(str);
  FSWrite(fref, &len, str);
  buf[0] = '\n';
  len = 1;
  FSWrite(fref, &len, buf);

  FSClose(fref);
}

// this tries to avoid losing data on crash by opening the file every time,
// but it doesn't really work
void debug_log_block(struct code_block *block)
{
    short fref;
    char buf[128];
    long len;
    int k;
    OSErr err;

    if (!debug_enabled) return;

    err = FSOpen("\pjit_log.txt", 0, &fref);
    if (err == fnfErr) {
        Create("\pjit_log.txt", 0, 'ttxt', 'TEXT');
        err = FSOpen("\pjit_log.txt", 0, &fref);
    }
    if (err != noErr) return;

    // Seek to end
    SetFPos(fref, fsFromLEOF, 0);

    // Write block header
    sprintf(buf, "Block %04x->%04x (%d bytes):\n",
            block->src_address, block->end_address, (int) block->length);
    len = strlen(buf);
    FSWrite(fref, &len, buf);

    // Write hex dump of generated code
    for (k = 0; k < block->length; k++) {
        sprintf(buf, "%02x", block->code[k]);
        len = 2;
        FSWrite(fref, &len, buf);
        if ((k & 15) == 15 || k == block->length - 1) {
            buf[0] = '\n';
            len = 1;
            FSWrite(fref, &len, buf);
        } else {
            buf[0] = ' ';
            len = 1;
            FSWrite(fref, &len, buf);
        }
    }

    // Newline separator
    buf[0] = '\n';
    len = 1;
    FSWrite(fref, &len, buf);

    FSClose(fref);
}

void debug_dump_vram(struct dmg *dmg)
{
    short fref;
    long len;
    OSErr err;

    if (!dmg || !dmg->video_ram) return;

    // Dump VRAM bank 0 (first 8KB)
    err = FSOpen("\pvram_bank0.bin", 0, &fref);
    if (err == fnfErr) {
        Create("\pvram_bank0.bin", 0, 'BINA', 'VRAM');
        err = FSOpen("\pvram_bank0.bin", 0, &fref);
    }
    if (err == noErr) {
        SetFPos(fref, fsFromStart, 0);
        len = 0x2000;  // 8KB
        FSWrite(fref, &len, dmg->video_ram);
        SetEOF(fref, len);
        FSClose(fref);
    }

    // Dump VRAM bank 1 (second 8KB) - only for CGB
    err = FSOpen("\pvram_bank1.bin", 0, &fref);
    if (err == fnfErr) {
        Create("\pvram_bank1.bin", 0, 'BINA', 'VRAM');
        err = FSOpen("\pvram_bank1.bin", 0, &fref);
    }
    if (err == noErr) {
        SetFPos(fref, fsFromStart, 0);
        len = 0x2000;  // 8KB
        FSWrite(fref, &len, dmg->video_ram + 0x2000);
        SetEOF(fref, len);
        FSClose(fref);
    }

    // Dump OAM (160 bytes)
    err = FSOpen("\poam_dump.bin", 0, &fref);
    if (err == fnfErr) {
        Create("\poam_dump.bin", 0, 'BINA', 'OAM ');
        err = FSOpen("\poam_dump.bin", 0, &fref);
    }
    if (err == noErr) {
        SetFPos(fref, fsFromStart, 0);
        len = 0xA0;  // 160 bytes
        FSWrite(fref, &len, dmg->lcd->oam);
        SetEOF(fref, len);
        FSClose(fref);
    }
}
