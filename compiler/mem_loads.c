#include <stdint.h>

#include "mem_loads.h"
#include "compiler.h"
#include "emitters.h"
#include "interop.h"
#include "timing.h"

#define READ_BYTE(off) (ctx->read(ctx->dmg, src_address + (off)))

void compile_ldh_u8_a(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint8_t addr
) {
    if (addr >= 0x80 && addr != 0xff) {
        emit_move_b_dn_abs32(block, REG_68K_D_A,
                (uint32_t) (uintptr_t) ctx->hram_base + (addr - 0x80));
    } else {
        emit_move_w_dn(block, REG_68K_D_SCRATCH_1, 0xff00 + addr);
        compile_slow_dmg_write(block, REG_68K_D_A);
    }
}

static void compile_ldh_a_u8_direct(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint8_t addr
) {
    if (addr == 0x00 && ctx && ctx->joyp_ptr) {
        emit_move_b_abs32_dn(block,
                (uint32_t) (uintptr_t) ctx->joyp_ptr, REG_68K_D_A);
        return;
    }
    if (addr >= 0x80) {
        // HRAM
        emit_move_b_abs32_dn(block,
                (uint32_t) (uintptr_t) ctx->hram_base + (addr - 0x80),
                REG_68K_D_A);
    } else {
        // not hram so it has to be I/O, go directly to C
        emit_move_w_dn(block, REG_68K_D_SCRATCH_1, 0xff00 + addr);
        compile_slow_dmg_read(block);
        emit_move_b_dn_dn(block, 0, REG_68K_D_A);
    }
}

int compile_ldh_a_u8(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t src_address,
    uint16_t *src_ptr
) {
    uint8_t addr = READ_BYTE((*src_ptr)++);

    // check for LY polling loop
    if (addr == 0x44) {
        uint8_t next0 = READ_BYTE(*src_ptr);

        // check for cp imm8 pattern
        if (next0 == 0xfe) {
            uint8_t target_ly = READ_BYTE(*src_ptr + 1);
            uint8_t jr_op = READ_BYTE(*src_ptr + 2);
            int8_t offset = (int8_t) READ_BYTE(*src_ptr + 3);

            if ((jr_op == 0x20 || jr_op == 0x28 || jr_op == 0x38) &&
                offset == -6) {
                // detected polling loop - synthesize wait
                uint16_t next_pc = src_address + *src_ptr + 4;
                uint16_t loop_pc = src_address + *src_ptr - 2;
                compile_ly_wait(block, target_ly, jr_op,
                        next_pc, loop_pc, 16);
                *src_ptr += 4;
                return 1;
            }
        }

        // check for cp r pattern (0xb8-0xbe = cp B,C,D,E,H,L,(HL))
        if (next0 >= 0xb8 && next0 <= 0xbe) {
            int gb_reg = next0 - 0xb8;
            uint8_t jr_op = READ_BYTE(*src_ptr + 1);
            int8_t offset = (int8_t) READ_BYTE(*src_ptr + 2);

            if ((jr_op == 0x20 || jr_op == 0x28 || jr_op == 0x38) &&
                offset == -5) {
                // detected polling loop with register compare
                uint16_t next_pc = src_address + *src_ptr + 3;
                uint16_t loop_pc = src_address + *src_ptr - 2;
                compile_ly_wait_reg(block, gb_reg, jr_op, next_pc,
                        loop_pc, next0 == 0xbe ? 16 : 12);
                *src_ptr += 3;
                return 1;
            }
        }

        // check for and a / or a pattern, equivalent to cp 0:
        // jr nz waits for LY to wrap to 0, jr z waits for LY to leave 0
        // no jr c variant since and/or always clear C
        if (next0 == 0xa7 || next0 == 0xb7) {
            uint8_t jr_op = READ_BYTE(*src_ptr + 1);
            int8_t offset = (int8_t) READ_BYTE(*src_ptr + 2);

            if ((jr_op == 0x20 || jr_op == 0x28) && offset == -5) {
                uint16_t next_pc = src_address + *src_ptr + 3;
                uint16_t loop_pc = src_address + *src_ptr - 2;
                compile_ly_wait(block, 0, jr_op, next_pc, loop_pc, 12);
                *src_ptr += 3;
                return 1;
            }
        }
    }

    // check for HRAM flag polling loop, the usual way games
    // without HALT wait for vblank:
    //   ldh a, (nn); and a / or a; jr z/nz back to the ldh
    if (addr >= 0x80) {
        uint8_t next0 = READ_BYTE(*src_ptr);

        if (next0 == 0xa7 || next0 == 0xb7) {
            uint8_t jr_op = READ_BYTE(*src_ptr + 1);
            int8_t offset = (int8_t) READ_BYTE(*src_ptr + 2);

            if ((jr_op == 0x20 || jr_op == 0x28) && offset == -5) {
                uint16_t loop_pc = src_address + *src_ptr - 2;
                compile_hram_idle_wait(block, ctx->hram_base, addr, jr_op,
                        loop_pc);
                *src_ptr += 3;
                return 0;
            }
        }
    }

    compile_ldh_a_u8_direct(block, ctx, addr);
    return 0;
}

void compile_ldh_c_a(struct code_block *block)
{
    emit_move_w_dn(block, REG_68K_D_SCRATCH_1, 0xff00);
    emit_or_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_1);
    compile_call_dmg_write_a(block);
}

void compile_ldh_a_c(struct code_block *block)
{
    emit_move_w_dn(block, REG_68K_D_SCRATCH_1, 0xff00);
    emit_or_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_1);
    compile_call_dmg_read_a(block);
}

void compile_ld_u16_a(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t src_address,
    uint16_t *src_ptr
) {
    uint16_t addr = READ_BYTE(*src_ptr) | (READ_BYTE(*src_ptr + 1) << 8);
    *src_ptr += 2;

    if (addr >= 0xff80 && addr != 0xffff) {
        emit_move_b_dn_abs32(block, REG_68K_D_A,
                (uint32_t) (uintptr_t) ctx->hram_base + (addr - 0xff80));
    } else if (addr < 0x8000) {
        // MBC register write: straight to mbc_write_func. for
        // the bank select reg, skip the call when A already
        // holds the current effective bank
        size_t skip = 0;
        int check = ctx->bank_reg_hi
                && addr >= ctx->bank_reg_lo
                && addr <= ctx->bank_reg_hi;

        flush_cycles(block);
        if (check) {
            emit_cmp_b_disp_an_dn(block, JIT_CTX_ROM_BANK,
                    REG_68K_A_CTX, REG_68K_D_A);
            skip = block->length;
            emit_beq_b(block, 0);
        }
        emit_move_w_dn(block, REG_68K_D_SCRATCH_1, addr);
        compile_call_dmg_write_mbc_a(block);
        if (check) {
            patch_branch_b(block, skip);
        }
    } else {
        emit_move_w_dn(block, REG_68K_D_SCRATCH_1, addr);
        compile_call_dmg_write_a(block);
    }
}

void compile_ld_a_u16(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t src_address,
    uint16_t *src_ptr
) {
    uint16_t addr = READ_BYTE(*src_ptr) | (READ_BYTE(*src_ptr + 1) << 8);
    // banked reads are only folded from banked blocks
    int fold = addr < 0x4000
            || (addr < 0x8000 && src_address >= 0x4000 && src_address < 0x8000);
    *src_ptr += 2;

    if (fold) {
        uint8_t val = ctx->read(ctx->dmg, addr);
        emit_moveq_dn(block, REG_68K_D_A, val);
    } else if (addr == 0xff00 && ctx->joyp_ptr) {
        emit_move_b_abs32_dn(block,
                (uint32_t) (uintptr_t) ctx->joyp_ptr,
                REG_68K_D_A);
    } else if (addr >= 0xff80) {
        // HRAM/IE: fixed address
        emit_move_b_abs32_dn(block,
                (uint32_t) (uintptr_t) ctx->hram_base + (addr - 0xff80),
                REG_68K_D_A);
    } else if (addr >= 0xc000 && addr < 0xd000) {
        emit_move_b_abs32_dn(block,
                (uint32_t) (uintptr_t) ctx->wram_base + (addr - 0xc000),
                REG_68K_D_A);
    } else if ((addr >= 0x8000 && addr < 0xa000)
            || (addr >= 0xd000 && addr < 0xf000)) {
        // VRAM/banked WRAM/echo pages are always mapped
        // (possibly rebanked), so resolve through read_page
        // with no null check. 0xf000-0xfdff echo shares the
        // unmapped page 0xf and stays on the C path
        emit_move_w_dn(block, REG_68K_D_SCRATCH_0, (addr >> 12) * 4);
        emit_movea_l_idx_an_an(block, 0, REG_68K_A_READ_PAGE,
                REG_68K_D_SCRATCH_0, REG_68K_A_SCRATCH_1);
        emit_move_b_disp_an_dn(block, (int16_t) addr,
                REG_68K_A_SCRATCH_1, REG_68K_D_A);
    } else {
        emit_move_w_dn(block, REG_68K_D_SCRATCH_1, addr);
        compile_call_dmg_read_a(block);
    }
}
