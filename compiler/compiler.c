#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "emitters.h"
#include "branches.h"
#include "flags.h"
#include "interop.h"
#include "cb_prefix.h"
#include "reg_loads.h"
#include "mem_loads.h"
#include "alu.h"
#include "stack.h"
#include "instructions.h"
#include "timing.h"

// helper for reading GB memory during compilation
#define READ_BYTE(off) (ctx->read(ctx->dmg, src_address + (off)))

uint16_t m68k_offsets[256];

int pending_cycles;

// GB offsets that are targets of backward jumps within the block; pending
// cycles need to be flushed before these so loop heads sit at pending == 0
uint8_t flush_at[256];

// SM83 instruction lengths for the branch-target pre-scan (CB handled as 2)
const uint8_t insn_length[256] = {
    1, 3, 1, 1, 1, 1, 2, 1, 3, 1, 1, 1, 1, 1, 2, 1, // 0x00
    2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1, // 0x10
    2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1, // 0x20
    2, 3, 1, 1, 1, 1, 2, 1, 2, 1, 1, 1, 1, 1, 2, 1, // 0x30
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 0x40
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 0x50
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 0x60
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 0x70
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 0x80
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 0x90
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 0xa0
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // 0xb0
    1, 1, 3, 3, 3, 1, 2, 1, 1, 1, 3, 2, 3, 3, 2, 1, // 0xc0
    1, 1, 3, 1, 3, 1, 2, 1, 1, 1, 3, 1, 3, 1, 2, 1, // 0xd0
    2, 1, 1, 1, 1, 1, 2, 1, 2, 1, 3, 1, 1, 1, 2, 1, // 0xe0
    2, 1, 1, 1, 1, 1, 2, 1, 2, 1, 3, 1, 1, 1, 2, 1, // 0xf0
};

void defer_cycles(int n)
{
    pending_cycles += n;
}

void flush_cycles(struct code_block *block)
{
    emit_add_cycles(block, pending_cycles);
    pending_cycles = 0;
}

// mark targets of backward jr within the block so the main loop flushes
// pending cycles there. spurious marks (offsets the main loop never lands
// on, or code past an exit) are harmless
static void scan_branch_targets(
    uint16_t src_address,
    struct compile_ctx *ctx
) {
    uint16_t off = 0;

    while (off < 256) {
        uint8_t op = READ_BYTE(off);
        uint16_t next = off + insn_length[op];

        if (op == 0x18 || op == 0x20 || op == 0x28
                || op == 0x30 || op == 0x38) {
            int16_t target = (int16_t) next + (int8_t) READ_BYTE(off + 1);
            if (target >= 0 && target < off) {
                flush_at[target] = 1;
            }
        }
        off = next;
    }
}

// Reconstruct BC from split format (0x00BB00CC) into D1.w as 0xBBCC
void compile_join_bc(struct code_block *block, int dreg)
{
    emit_move_l_dn_dn(block, REG_68K_D_BC, dreg);  // D1 = 0x00BB00CC
    emit_lsr_l_imm_dn(block, 8, dreg);             // D1 = 0x0000BB00
    emit_move_b_dn_dn(block, REG_68K_D_BC, dreg);  // D1 = 0x0000BBCC
}

// Reconstruct DE from split format (0x00DD00EE) into D1.w as 0xDDEE
void compile_join_de(struct code_block *block, int dreg)
{
    emit_move_l_dn_dn(block, REG_68K_D_DE, dreg);  // D1 = 0x00DD00EE
    emit_lsr_l_imm_dn(block, 8, dreg);             // D1 = 0x0000DD00
    emit_move_b_dn_dn(block, REG_68K_D_DE, dreg);  // D1 = 0x0000DDEE
}

static void compile_ld_imm16_split(
    struct code_block *block,
    uint8_t reg,
    struct compile_ctx *ctx,
    uint16_t src_address,
    uint16_t *src_ptr
) {
    uint8_t lobyte = READ_BYTE(*src_ptr);
    uint8_t hibyte = READ_BYTE(*src_ptr + 1);
    uint32_t split = ((uint32_t) hibyte << 16) | lobyte;
    *src_ptr += 2;
    if (split < 0x80) {
        emit_moveq_dn(block, reg, split);
    } else {
        emit_move_l_dn(block, reg, split);
    }
}

static void compile_ld_imm16_contiguous(
    struct code_block *block,
    uint8_t reg,
    struct compile_ctx *ctx,
    uint16_t src_address,
    uint16_t *src_ptr
) {
    uint8_t lobyte = READ_BYTE(*src_ptr);
    uint8_t hibyte = READ_BYTE(*src_ptr + 1);
    *src_ptr += 2;
    emit_movea_w_imm16(block, reg, hibyte << 8 | lobyte);
}

struct code_block *compile_block(uint16_t src_address, struct compile_ctx *ctx)
{
    struct code_block *block;
    uint16_t src_ptr = 0;
    uint8_t op;
    int done = 0;
    size_t k;

#ifdef DEBUG_COMPILE
    printf("compile_block: src_address=0x%04x\n", src_address);
    printf("  first bytes: %02x %02x %02x\n",
           READ_BYTE(0), READ_BYTE(1), READ_BYTE(2));
#endif

    if (ctx->alloc) {
        block = ctx->alloc(sizeof *block);
    } else {
        block = malloc(sizeof *block);
    }
    if (!block) {
        return NULL;
    }

    block->length = 0;
    block->count = 0;
    block->src_address = src_address;
    block->error = 0;
    block->failed_opcode = 0;
    block->failed_address = 0;
    memset(m68k_offsets, 0, sizeof m68k_offsets);

    pending_cycles = 0;
    memset(flush_at, 0, sizeof flush_at);
    scan_branch_targets(src_address, ctx);

    // set everything to illegal instruction so it's easy to catch weird branches
    for (k = 0; k < sizeof block->code; k += 2) {
      block->code[k] = 0x4a;
      block->code[k + 1] = 0xfc;
    }

    while (!done) {
        size_t before = block->length;
        // detect overflow of code block and chain to next block
        // longest instruction is around 150 bytes, exit sequence is 26 bytes
        if (block->length > sizeof(block->code) - 200 || src_ptr >= 256) {
            flush_cycles(block);
            emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
            emit_move_w_dn(block, REG_68K_D_NEXT_PC, src_address + src_ptr);
            emit_block_exit(block, (uint16_t) (src_address + src_ptr));
            break;
        }

        if (flush_at[src_ptr]) {
            flush_cycles(block);
        }
        m68k_offsets[src_ptr] = block->length;
        block->count++;
        op = READ_BYTE(src_ptr);
        src_ptr++;

        if (op != 0xcb) {
            defer_cycles(instructions[op].cycles);
        }

        switch (op) {
        case 0x00: // nop
            // emit nothing
            break;

        case 0x02: // ld (bc), a
            compile_join_bc(block, 1);
            compile_call_dmg_write_a(block);
            break;

        case 0x0a: // ld a, (bc)
            compile_join_bc(block, 1);
            compile_call_dmg_read_a(block);
            break;

        case 0x12: // ld (de), a
            compile_join_de(block, 1);
            compile_call_dmg_write_a(block);
            break;

        case 0x1a: // ld a, (de)
            compile_join_de(block, 1);
            compile_call_dmg_read_a(block);
            break;

        case 0x01: // ld bc, imm16
            compile_ld_imm16_split(block, REG_68K_D_BC, ctx, src_address, &src_ptr);
            break;
        case 0x11: // ld de, imm16
            compile_ld_imm16_split(block, REG_68K_D_DE, ctx, src_address, &src_ptr);
            break;
        case 0x21: // ld hl, imm16
            compile_ld_imm16_contiguous(block, REG_68K_A_HL, ctx, src_address, &src_ptr);
            break;
        case 0x31: // ld sp, imm16
            compile_ld_sp_imm16(ctx, block, src_address, &src_ptr);
            break;

        case 0x32: // ld (hl-), a
            compile_call_dmg_write_hl_a(block); // dmg_write(dmg, HL, A)
            emit_subq_w_an(block, REG_68K_A_HL, 1); // HL--
            break;

        case 0x23: // inc hl
            emit_addq_w_an(block, REG_68K_A_HL, 1);
            break;

        case 0x2a: // ld a, (hl+)
            compile_call_dmg_read_hl_a(block); // dmg_read(dmg, HL) into A
            emit_addq_w_an(block, REG_68K_A_HL, 1); // HL++
            break;

        case 0x3a: // ld a, (hl-)
            compile_call_dmg_read_hl_a(block); // dmg_read(dmg, HL) into A
            emit_subq_w_an(block, REG_68K_A_HL, 1); // HL--
            break;

        case 0x2b: // dec hl
            emit_subq_w_an(block, REG_68K_A_HL, 1);
            break;

        case 0x3b: // dec sp
            emit_subq_l_an(block, REG_68K_A_SP, 1);
            emit_subq_w_disp_an(block, 1, JIT_CTX_GB_SP, REG_68K_A_CTX);
            break;

        case 0x33: // inc sp
            emit_addq_l_an(block, REG_68K_A_SP, 1);
            emit_addq_w_disp_an(block, 1, JIT_CTX_GB_SP, REG_68K_A_CTX);
            break;

        case 0x03: // inc bc
            emit_ext_w_dn(block, REG_68K_D_BC);
            emit_addq_l_dn(block, REG_68K_D_BC, 1);
            break;


        case 0x06: // ld b, imm8
            emit_swap(block, REG_68K_D_BC);
            emit_move_b_dn(block, REG_68K_D_BC, READ_BYTE(src_ptr++));
            emit_swap(block, REG_68K_D_BC);
            break;

        case 0x07: // rlca - rotate A left, old bit 7 to carry and bit 0
            emit_rol_b_imm(block, 1, REG_68K_D_A);
            emit_move_sr_dn(block, REG_68K_D_FLAGS);
            emit_andi_b_dn(block, REG_68K_D_FLAGS, 0xfb); // Z always 0
            break;

        case 0x0b: // dec bc
            emit_ext_w_dn(block, REG_68K_D_BC);
            emit_subq_l_dn(block, REG_68K_D_BC, 1);
            break;

        case 0x0e: // ld c, imm8
            emit_move_b_dn(block, REG_68K_D_BC, READ_BYTE(src_ptr++));
            break;

        case 0x0f: // rrca - rotate A right, old bit 0 to carry and bit 7
            emit_ror_b_imm(block, 1, REG_68K_D_A);
            emit_move_sr_dn(block, REG_68K_D_FLAGS);
            emit_andi_b_dn(block, REG_68K_D_FLAGS, 0xfb); // Z always 0
            break;

        case 0x13: // inc de
            emit_ext_w_dn(block, REG_68K_D_DE);
            emit_addq_l_dn(block, REG_68K_D_DE, 1);
            break;

        case 0x1b: // dec de
            emit_ext_w_dn(block, REG_68K_D_DE);
            emit_subq_l_dn(block, REG_68K_D_DE, 1);
            break;

        case 0x16: // ld d, imm8
            emit_swap(block, REG_68K_D_DE);
            emit_move_b_dn(block, REG_68K_D_DE, READ_BYTE(src_ptr++));
            emit_swap(block, REG_68K_D_DE);
            break;

        case 0x09: // add hl, bc
        case 0x19: // add hl, de
            // use add.w so flags are set
            emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_0);
            if (op == 0x09) {
                compile_join_bc(block, 1);  // D1.w = BC
            } else {
                compile_join_de(block, 1);  // D1.w = DE
            }
            emit_add_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);  // D3 += D1, sets C
            emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_0, REG_68K_A_HL);  // HL = D3
            compile_set_c_flag(block);
            break;

        case 0x29: // add hl, hl
            emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_0);  // D3.w = HL
            emit_add_w_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_0);
            emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_0, REG_68K_A_HL);  // HL = D3
            compile_set_c_flag(block);
            break;

        case 0x39: // add hl, sp
            emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_0);  // D0.w = HL
            // read gb_sp from context, not A3 - might be a native pointer
            emit_move_w_disp_an_dn(block, JIT_CTX_GB_SP, REG_68K_A_CTX, REG_68K_D_SCRATCH_1);
            emit_add_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);  // D0 += D1, sets C
            emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_0, REG_68K_A_HL);  // HL = D0
            compile_set_c_flag(block);
            break;

        case 0x1e: // ld e, imm8
            emit_move_b_dn(block, REG_68K_D_DE, READ_BYTE(src_ptr++));
            break;

        case 0x26: // ld h, imm8
            // move.w a0, d5
            emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
             // rol.w #8, d5
            emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
            emit_move_b_dn(block, REG_68K_D_SCRATCH_1, READ_BYTE(src_ptr++));
            // ror.w #8, d5
            emit_ror_w_8(block, REG_68K_D_SCRATCH_1);
            // movea.w d5, a0
            emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
            break;


        case 0x2e: // ld l, imm8
            emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
            emit_move_b_dn(block, REG_68K_D_SCRATCH_1, READ_BYTE(src_ptr++));
            emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
            break;

        case 0x3e: // ld a, imm8
            emit_moveq_dn(block, REG_68K_D_A, READ_BYTE(src_ptr++));
            break;

        case 0x10: // stop - CGB speed switch or end of execution
            src_ptr++;  // skip the padding byte (0x00) after STOP opcode
            compile_call_stop(block, src_address + src_ptr);
            done = 1;
            break;

        case 0x18: // jr disp8
            compile_jr(block, ctx, &src_ptr, src_address);
            // always ends the block to avoid compiling following data
            // as code
            done = 1;
            break;

        case 0x20: // jr nz, disp8
            compile_jr_cond(block, ctx, &src_ptr, src_address, 2, 0);
            break;
        case 0x28: // jr z, disp8
            compile_jr_cond(block, ctx, &src_ptr, src_address, 2, 1);
            break;
        case 0x30: // jr nc, disp8
            compile_jr_cond(block, ctx, &src_ptr, src_address, 0, 0);
            break;
        case 0x38: // jr c, disp8
            compile_jr_cond(block, ctx, &src_ptr, src_address, 0, 1);
            break;

        case 0x22: // ld (hl+), a
            compile_call_dmg_write_hl_a(block);
            emit_addq_w_an(block, REG_68K_A_HL, 1);
            break;

        case 0xcb: // CB prefix
            {
                uint8_t cb_op = READ_BYTE(src_ptr++);
                defer_cycles(instructions[0x100 + cb_op].cycles);
                if (!compile_cb_insn(block, cb_op)) {
                    block->error = 1;
                    block->failed_opcode = 0xcb00 | cb_op;
                    block->failed_address = src_address + src_ptr - 2;
                    flush_cycles(block);
                    emit_move_l_dn(block, REG_68K_D_NEXT_PC, 0xffffffff);
                    emit_dispatch_jump(block);
                    done = 1;
                }
            }
            break;

        case 0xc0: // ret nz
            compile_ret_cond(block, 2, 0);
            break;

        case 0xc2: // jp nz, imm16
            compile_jp_cond(block, ctx, &src_ptr, src_address, 2, 0);
            break;

        case 0xc3: // jp imm16
            {
                uint16_t target = READ_BYTE(src_ptr) | (READ_BYTE(src_ptr + 1) << 8);
                src_ptr += 2;
                flush_cycles(block);
                emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
                emit_move_w_dn(block, REG_68K_D_NEXT_PC, target);
                emit_block_exit(block, target);
                done = 1;
            }
            break;

        case 0xc8: // ret z
            compile_ret_cond(block, 2, 1);
            break;

        case 0xc9: // ret
            compile_ret(block);
            done = 1;
            break;

        case 0xca: // jp z, imm16
            compile_jp_cond(block, ctx, &src_ptr, src_address, 2, 1);
            break;

        case 0xc4: // call nz, imm16
            compile_call_cond(block, ctx, &src_ptr, src_address, 2, 0);
            break;

        case 0xcc: // call z, imm16
            compile_call_cond(block, ctx, &src_ptr, src_address, 2, 1);
            break;

        case 0xcd: // call imm16
            compile_call_imm16(block, ctx, &src_ptr, src_address);
            done = 1;
            break;

        case 0xd4: // call nc, imm16
            compile_call_cond(block, ctx, &src_ptr, src_address, 0, 0);
            break;

        case 0xd0: // ret nc
            compile_ret_cond(block, 0, 0);
            break;

        case 0xd2: // jp nc, imm16
            compile_jp_cond(block, ctx, &src_ptr, src_address, 0, 0);
            break;

        case 0xd8: // ret c
            compile_ret_cond(block, 0, 1);
            break;

        case 0xda: // jp c, imm16
            compile_jp_cond(block, ctx, &src_ptr, src_address, 0, 1);
            break;

        case 0xdc: // call c, imm16
            compile_call_cond(block, ctx, &src_ptr, src_address, 0, 1);
            break;

        case 0xe0: // ld ($ff00 + u8), a
            compile_ldh_u8_a(block, ctx, READ_BYTE(src_ptr++));
            break;

        case 0xe9: // jp (hl)
            flush_cycles(block);
            // d3 can end up with a host pointer from an inlined memory access,
            // so clear upper 16 bits
            emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
            emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_NEXT_PC);
            emit_dispatch_jump(block);
            done = 1;
            break;

        case 0x17: // rla - rotate A left through carry
        {
            // Save old carry (bit 0 of D7) - already in position for bit 0 of A
            emit_move_b_dn_dn(block, REG_68K_D_FLAGS, REG_68K_D_SCRATCH_1);
            emit_andi_b_dn(block, REG_68K_D_SCRATCH_1, 0x01);  // isolate C flag

            // Shift left - bit 7 goes to 68k C flag, 0 goes to bit 0
            emit_lsl_b_imm_dn(block, 1, REG_68K_D_A);

            emit_move_sr_dn(block, REG_68K_D_FLAGS);
            emit_andi_b_dn(block, REG_68K_D_FLAGS, 0xfb); // Z always 0

            // OR old carry into bit 0
            emit_or_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_A);

            break;
        }

        case 0x1f: // rra
        {
            // Save old carry (bit 0 of D7), shift to bit 7 position
            emit_move_b_dn_dn(block, REG_68K_D_FLAGS, REG_68K_D_SCRATCH_1);
            emit_andi_b_dn(block, REG_68K_D_SCRATCH_1, 0x01);  // isolate C flag
            emit_ror_b_imm(block, 1, REG_68K_D_SCRATCH_1);  // 0x01 -> 0x80

            // Shift right - bit 0 goes to 68k C flag, 0 goes to bit 7
            emit_lsr_b_imm_dn(block, 1, REG_68K_D_A);

            emit_move_sr_dn(block, REG_68K_D_FLAGS);
            emit_andi_b_dn(block, REG_68K_D_FLAGS, 0xfb); // Z always 0

            // OR old carry into bit 7
            emit_or_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_A);

            break;
        }

        case 0xc7: // rst nn
            compile_rst_n(block, 0x00, src_address + src_ptr);
            done = 1;
            break;
        case 0xcf:
            compile_rst_n(block, 0x08, src_address + src_ptr);
            done = 1;
            break;
        case 0xd7:
            compile_rst_n(block, 0x10, src_address + src_ptr);
            done = 1;
            break;
        case 0xdf:
            compile_rst_n(block, 0x18, src_address + src_ptr);
            done = 1;
            break;
        case 0xe7:
            compile_rst_n(block, 0x20, src_address + src_ptr);
            done = 1;
            break;
        case 0xef:
            compile_rst_n(block, 0x28, src_address + src_ptr);
            done = 1;
            break;
        case 0xf7:
            compile_rst_n(block, 0x30, src_address + src_ptr);
            done = 1;
            break;
        case 0xff:
            compile_rst_n(block, 0x38, src_address + src_ptr);
            done = 1;
            break;

        case 0xe2: // ld ($ff00 + c), a
            compile_ldh_c_a(block);
            break;

        case 0xf0: // ld a, ($ff00 + u8)
            if (compile_ldh_a_u8(block, ctx, src_address, &src_ptr)) {
                done = 1;
            }
            break;

        case 0xf2: // ld a, ($ff00 + c)
            compile_ldh_a_c(block);
            break;

        case 0xd9: // reti
            compile_call_ei_di(block, 1);
            compile_ret(block);
            done = 1;
            break;

        case 0xf3: // di
            compile_call_ei_di(block, 0);
            break;

        case 0xfb: // ei
            compile_call_ei_di(block, 1);
            break;

        case 0x36: // ld (hl), u8
            compile_call_dmg_write_hl_imm(block, READ_BYTE(src_ptr++));
            break;

        case 0xea: // ld (u16), a
            compile_ld_u16_a(block, ctx, src_address, &src_ptr);
            break;

        case 0xfa: // ld a, (u16)
            compile_ld_a_u16(block, ctx, src_address, &src_ptr);
            break;

        case 0x76:
            compile_halt(block, src_address + src_ptr);
            done = 1;
            break;

        case 0x05: // dec b
        case 0x3d: // dec a
            // counted empty delay loop: dec r; jr nz, -3 (OAM DMA waits)
            if (READ_BYTE(src_ptr) == 0x20
                    && (int8_t) READ_BYTE(src_ptr + 1) == -3) {
                uint16_t loop_pc = src_address + src_ptr - 1;
                if (ctx->cache_store) {
                    ctx->cache_store(loop_pc, ctx->current_bank,
                            (void *) (block->code + m68k_offsets[src_ptr - 1]));
                }
                compile_delay_loop(block, op, loop_pc,
                        src_address + src_ptr + 2);
                src_ptr += 2;
                break;
            }
            compile_alu_op(block, op, ctx, src_address, &src_ptr);
            break;

        default:
            // 8-bit ALU ops
            if (compile_alu_op(block, op, ctx, src_address, &src_ptr)) {
                break;
            }
            // stack operations (push, pop, ld sp)
            if (compile_stack_op(block, op, ctx, src_address, &src_ptr)) {
                break;
            }
            // register loads: 0x40-0x7f (except 0x76 HALT)
            if (op >= 0x40 && op <= 0x7f) {
                if (compile_reg_load(block, op)) {
                    break;
                }
            }
            // unknown opcode - set error info and halt
            block->error = 1;
            block->failed_opcode = op;
            block->failed_address = src_address + src_ptr - 1;
            flush_cycles(block);
            emit_move_l_dn(block, REG_68K_D_NEXT_PC, 0xffffffff);
            emit_dispatch_jump(block);
            done = 1;
            break;
        }

        size_t emitted = block->length - before;
        // the fused register LY wait is the biggest expected sequence
        if (emitted > 176) {
            fprintf(stderr, "warning: instruction %02x emitted %zu bytes\n", op, emitted);
        }
    }

    block->end_address = src_address + src_ptr;
    return block;
}

void block_free(struct code_block *block)
{
    free(block);
}
