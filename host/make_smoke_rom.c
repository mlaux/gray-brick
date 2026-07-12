// generates a minimal 32KB GB ROM that prints "Hi!\n" over the serial
// port (blargg protocol: byte to $ff01, $81 to $ff02) and then spins.
// used as the milestone 2 smoke test for the call gate

#include <stdio.h>
#include <string.h>

static unsigned char rom[0x8000];
static int pos = 0x150;

static void emit(int b)
{
    rom[pos++] = (unsigned char) b;
}

static void serial_char(char c)
{
    emit(0x3e); emit(c);    // ld a, c
    emit(0xe0); emit(0x01); // ldh ($01), a  ; SB
    emit(0x3e); emit(0x81); // ld a, $81
    emit(0xe0); emit(0x02); // ldh ($02), a  ; SC, start transfer
}

int main(int argc, char *argv[])
{
    const char *msg = "Hi!\n";
    const char *out = argc > 1 ? argv[1] : "smoke.gb";
    unsigned char csum = 0;
    FILE *fp;
    int k;

    // entry point: nop; jp $0150
    rom[0x100] = 0x00;
    rom[0x101] = 0xc3;
    rom[0x102] = 0x50;
    rom[0x103] = 0x01;

    memcpy(&rom[0x134], "SMOKE", 5);
    // 0x143 cgb flag, 0x147 mbc type, 0x148 rom size: all zero (DMG,
    // ROM only, 32KB)

    for (k = 0; msg[k]; k++) {
        serial_char(msg[k]);
    }
    emit(0x18); emit(0xfe); // jr *

    for (k = 0x134; k <= 0x14c; k++) {
        csum = csum - rom[k] - 1;
    }
    rom[0x14d] = csum;

    fp = fopen(out, "wb");
    if (!fp || fwrite(rom, 1, sizeof rom, fp) < sizeof rom) {
        fprintf(stderr, "failed to write %s\n", out);
        return 1;
    }
    fclose(fp);
    printf("wrote %s (%d code bytes)\n", out, pos - 0x150);
    return 0;
}
