#include "cpu_cache.h"
#include "dispatcher_asm.h"
#include "settings.h"

// Offset of the FlushCodeCache trap in patch_helper code
#define CACHEFLUSH_OFFSET 102

// compiled blocks JMP here instead of RTS. This routine:
// 1. Checks if accumulated cycles in D2 >= cycles_per_exit, if so, RTS to C
// 2. Determines which cache to use based on PC in D3
// 3. Looks up block in appropriate cache, if found -> JMP to it
// 4. Otherwise -> RTS to C to compile the block
// context offsets in jit.h
static void dispatcher_code_asm(void)
{
    asm volatile(
        "\t"
        "tst.b 16(%%a4)\n\t" // check trace_enabled
        "bne.s .Ldisp_exit\n\t" // if set, always return to C
        "cmp.l %[cycles], %%d2\n\t"
        "bcc.s .Ldisp_exit\n\t"

        "cmpi.w #0x4000, %%d3\n\t"
        "bcs.s .Ldisp_bank0\n\t"

        "cmpi.w #0x8000, %%d3\n\t"
        "bcs.s .Ldisp_banked\n\t"
        "\n"

    ".Ldisp_upper:\n\t"
        "movea.l 28(%%a4), %%a0\n\t"
        "moveq #0, %%d0\n\t"
        "move.w %%d3, %%d0\n\t"
        "subi.w #0x8000, %%d0\n\t"
        "lsl.l #2, %%d0\n\t"
        "movea.l (%%a0,%%d0.l), %%a0\n\t"
        "cmpa.w #0, %%a0\n\t"
        "beq.s .Ldisp_exit\n\t"
        "jmp (%%a0)\n\t"
        "\n"

    ".Ldisp_bank0:\n\t"
        "movea.l 20(%%a4), %%a0\n\t"
        "moveq #0, %%d0\n\t"
        "move.w %%d3, %%d0\n\t"
        "lsl.l #2, %%d0\n\t"
        "movea.l (%%a0,%%d0.l), %%a0\n\t"
        "cmpa.w #0, %%a0\n\t"
        "beq.s .Ldisp_exit\n\t"
        "jmp (%%a0)\n\t"
        "\n"

    ".Ldisp_banked:\n\t"
        "movea.l 24(%%a4), %%a0\n\t"
        "moveq #0, %%d0\n\t"
        "move.b 17(%%a4), %%d0\n\t"
        "lsl.l #2, %%d0\n\t"
        "movea.l (%%a0,%%d0.l), %%a0\n\t"
        "cmpa.w #0, %%a0\n\t"
        "beq.s .Ldisp_exit\n\t"
        "moveq #0, %%d0\n\t"
        "move.w %%d3, %%d0\n\t"
        "subi.w #0x4000, %%d0\n\t"
        "lsl.l #2, %%d0\n\t"
        "movea.l (%%a0,%%d0.l), %%a0\n\t"
        "cmpa.w #0, %%a0\n\t"
        "beq.s .Ldisp_exit\n\t"

        "jmp (%%a0)\n\t"
        "\n"

    ".Ldisp_exit:\n\t"
        "rts\n\t"

        : // no outputs
        : [cycles] "m" (cycles_per_exit)
        : "d0", "a0", "cc", "memory"
    );
}

// patch_helper: called via JSR from patchable block exits
// On entry: return address on stack points to after the JSR (the exit: rts)
// D3 = target GB PC
// A4 = context pointer
//
// This routine:
// 1. Checks if target is in banked region (skip patching if so)
// 2. Looks up target in cache
// 3. If found: patches the JSR into JMP.L and jumps to target
// 4. If not found: jumps to exit which RTSs to C
static void patch_helper_code_asm(void)
{
    asm volatile(
        "\t"
        "move.l (%%sp)+, %%a1\n\t"

        // Determine region
        "cmpi.w #0x4000, %%d3\n\t"
        "bcs.s .Lpatch_bank0\n\t"

        "cmpi.w #0x8000, %%d3\n\t"
        "bcc.s .Lpatch_upper\n\t"

        // .banked: - lookup banked_cache[current_bank][d3 - 0x4000]

        // TODO: this scenario can occur, but i think it's saved by the fact that
        // 'jp hl' always goes through the dispatcher, and bank0 code with a
        // hardcoded jp/call $5xxx where the intended target varies by bank is very rare:

        // 1. block A is at address 0x1000 (bank 0, always visible)
        // 2. block A has a patchable exit to address 0x5000 (banked region)
        // 3. with bank 1 active, block A runs, patch_helper finds bank 1's code
        //      at 0x5000, patches block A with JMP.L bank1_code
        // 4. later, bank 2 is switched in
        // 5. some other code jumps to 0x1000 - block A is found in bank0_cache and runs
        // 6. block A's patched JMP goes directly to bank 1's code
        // "bra.s .Lpatch_no_patch\n\t" // cacheflush_offset is 66 if using this and commenting out below
        "movea.l 24(%%a4), %%a0\n\t"         // banked_cache
        "moveq #0, %%d0\n\t"
        "move.b 17(%%a4), %%d0\n\t"          // current_rom_bank
        "lsl.l #2, %%d0\n\t"
        "movea.l (%%a0,%%d0.l), %%a0\n\t"    // banked_cache[bank]
        "cmpa.w #0, %%a0\n\t"
        "beq.s .Lpatch_no_patch\n\t"
        "moveq #0, %%d0\n\t"
        "move.w %%d3, %%d0\n\t"
        "subi.w #0x4000, %%d0\n\t"
        "lsl.l #2, %%d0\n\t"
        "movea.l (%%a0,%%d0.l), %%a0\n\t"
        "bra.s .Lpatch_check_found\n\t"
        "\n"

    ".Lpatch_bank0:\n\t"
        "movea.l 20(%%a4), %%a0\n\t"         // bank0_cache
        "moveq #0, %%d0\n\t"
        "move.w %%d3, %%d0\n\t"
        "lsl.l #2, %%d0\n\t"
        "movea.l (%%a0,%%d0.l), %%a0\n\t"
        "bra.s .Lpatch_check_found\n\t"
        "\n"

    ".Lpatch_upper:\n\t"
        "movea.l 28(%%a4), %%a0\n\t"         // upper_cache
        "moveq #0, %%d0\n\t"
        "move.w %%d3, %%d0\n\t"
        "subi.w #0x8000, %%d0\n\t"
        "lsl.l #2, %%d0\n\t"
        "movea.l (%%a0,%%d0.l), %%a0\n\t"
        "\n"

    ".Lpatch_check_found:\n\t"
        "cmpa.w #0, %%a0\n\t"
        "beq.s .Lpatch_no_patch\n\t"

        // .do_patch:
        "lea -6(%%a1), %%a1\n\t"
        "move.w #0x4ef9, (%%a1)+\n\t"        // JMP.L opcode
        "move.l %%a0, (%%a1)\n\t"

        // don't need to worry about A0 and A1 here (from Inside Macintosh):
        // The trap dispatcher first saves registers D0, D1, D2, A1, and, if bit 8 is 0, A0.
        // The Operating System routine may alter any of the registers D0-D2 and A0-A2,
        // but it must preserve registers D3-D7 and A3-A6. The Operating System routine
        // may return information in register D0 (and A0 if bit 8 is set). To return to
        // the trap dispatcher, the Operating System routine executes the RTS
        // (return from subroutine) instruction.
        // When the trap dispatcher resumes control, first it restores the value of registers
        // D1, D2, A1, A2, and, if bit 8 is 0, A0. The values in registers D0 and,
        // if bit 8 is 1, in A0 are not restored.
        ".short 0xa0bd\n\t" // patched to NOP on 68000
        "jmp (%%a0)\n\t"
        "\n"

    ".Lpatch_no_patch:\n\t"
        "jmp (%%a1)\n\t"

        ::: "d0", "a0", "a1", "cc", "memory"
    );
}

void *get_dispatcher_code(void)
{
    return dispatcher_code_asm;
}

void *get_patch_helper_code(void)
{
    unsigned char *code = (unsigned char *)patch_helper_code_asm;

    if (!TrapAvailable(_CacheFlush)) {
        // replace _CacheFlush with a nop
        code[CACHEFLUSH_OFFSET + 0] = 0x4e;
        code[CACHEFLUSH_OFFSET + 1] = 0x71;
    }
    return code;
}
