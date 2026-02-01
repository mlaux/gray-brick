#include <stdio.h>
#include <stdlib.h>

#include "compiler.h"
#include "emitters.h"
#include "branches.h"
#include "flags.h"
#include "interop.h"
#include "cb_prefix.h"
#include "reg_loads.h"
#include "alu.h"
#include "stack.h"
#include "instructions.h"
#include "timing.h"

// helper for reading GB memory during compilation
#define READ_BYTE(off) (ctx->read(ctx->dmg, src_address + (off)))

int cycles_per_exit;

void compiler_init(void)
{
    // nothing for now
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

static void compile_ldh_a_u8(struct code_block *block, uint8_t addr)
{
    if (addr >= 0x80) {
        // movea.l (a4), a0
        emit_movea_l_ind_an_an(block, 4, 0);
        // move.b addr(a0), d4
        emit_move_b_disp_an_dn(block, addr - 0x80, 0, 4);
    } else {
        // not hram so it has to be I/O, go directly to C
        emit_move_w_dn(block, REG_68K_D_SCRATCH_1, 0xff00 + addr);
        compile_slow_dmg_read(block);
        emit_move_b_dn_dn(block, 0, REG_68K_D_A);
    }
}

static void compile_ldh_u8_a(struct code_block *block, uint8_t addr)
{
    if (addr >= 0x80) {
        emit_movea_l_ind_an_an(block, 4, 0);
        emit_move_b_dn_disp_an(block, 4, addr - 0x80, 0);
    } else {
        emit_move_w_dn(block, REG_68K_D_SCRATCH_1, 0xff00 + addr);
        compile_slow_dmg_write(block, REG_68K_D_A);
    }
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
    *src_ptr += 2;
    emit_move_l_dn(block, reg, hibyte << 16);
    emit_move_w_dn(block, reg, lobyte);
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
    int k;

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

    // set everything to illegal instruction so it's easy to catch weird branches
    for (k = 0; k < sizeof block->code; k += 2) {
      block->code[k] = 0x4a;
      block->code[k + 1] = 0xfc;
    }

    while (!done) {
        size_t before = block->length;
        // detect overflow of code block and chain to next block
        // longest instruction is 178 bytes, exit sequence is 22 bytes
        // also, a block of all NOPs (Link's Awakening DX has this) overflows
        // the m68k_offsets array, and i don't want to make it bigger, so
        // just chain to another block. worst case: 253 nops then a fused compare/branch
        if (block->length > sizeof(block->code) - 200
                || block->count > 254) {
            emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
            emit_move_w_dn(block, REG_68K_D_NEXT_PC, src_address + src_ptr);
            emit_patchable_exit(block);
            break;
        }

        block->m68k_offsets[src_ptr] = block->length;
        block->count++;
        op = READ_BYTE(src_ptr);
        src_ptr++;

        if (op != 0xcb) {
            emit_add_cycles(block, instructions[op].cycles);
        }

        switch (op) {
        case 0x00: // nop
            // emit nothing
            break;

        case 0x02: // ld (bc), a
            compile_join_bc(block, 1);  // BC -> D1.w
            compile_call_dmg_write_a(block);  // dmg_write(dmg, D1.w, A)
            break;

        case 0x0a: // ld a, (bc)
            compile_join_bc(block, 1);  // BC -> D1.w
            compile_call_dmg_read_a(block);  // A = dmg_read(dmg, D1.w)
            break;

        case 0x12: // ld (de), a
            compile_join_de(block, 1);  // DE -> D1.w
            compile_call_dmg_write_a(block);  // dmg_write(dmg, D1.w, A)
            break;

        case 0x1a: // ld a, (de)
            compile_join_de(block, 1);  // DE -> D1.w
            compile_call_dmg_read_a(block);  // A = dmg_read(dmg, D1.w)
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
            emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
            emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1); // D1.w = HL
            compile_call_dmg_write_a(block); // dmg_write(dmg, D1.w, A)
            emit_subq_w_an(block, REG_68K_A_HL, 1); // HL--
            break;

        case 0x23: // inc hl
            emit_addq_w_an(block, REG_68K_A_HL, 1);
            break;

        case 0x2a: // ld a, (hl+)
            emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
            emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1); // D1.w = HL
            compile_call_dmg_read_a(block); // dmg_read(dmg, D1.w) into A
            emit_addq_w_an(block, REG_68K_A_HL, 1); // HL++
            break;

        case 0x3a: // ld a, (hl-)
            emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
            emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1); // D1.w = HL
            compile_call_dmg_read_a(block); // dmg_read(dmg, D1.w) into A
            emit_subq_w_an(block, REG_68K_A_HL, 1); // HL--
            break;

        case 0x2b: // dec hl
            emit_subq_w_an(block, REG_68K_A_HL, 1);
            break;

        case 0x3b: // dec sp
            emit_subq_l_an(block, REG_68K_A_SP, 1);
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
            emit_move_w_an_dn(block, REG_68K_A_SP, REG_68K_D_SCRATCH_1);  // D1.w = SP
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

        case 0x10: // stop - end of execution
            emit_move_l_dn(block, REG_68K_D_NEXT_PC, 0xffffffff);
            emit_dispatch_jump(block);
            done = 1;
            break;

        case 0x18: // jr disp8
            done = compile_jr(block, ctx, &src_ptr, src_address);
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
            emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
            emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
            compile_call_dmg_write_a(block);
            emit_addq_w_an(block, REG_68K_A_HL, 1);
            break;

        case 0xcb: // CB prefix
            {
                uint8_t cb_op = READ_BYTE(src_ptr++);
                emit_add_cycles(block, instructions[0x100 + cb_op].cycles);
                if (!compile_cb_insn(block, cb_op)) {
                    block->error = 1;
                    block->failed_opcode = 0xcb00 | cb_op;
                    block->failed_address = src_address + src_ptr - 2;
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
                emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
                emit_move_w_dn(block, REG_68K_D_NEXT_PC, target);
                emit_patchable_exit(block);
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
            compile_ldh_u8_a(block, READ_BYTE(src_ptr++));
            break;

        case 0xe9: // jp (hl)
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
            emit_move_l_dn(block, REG_68K_D_SCRATCH_1, 0x0000ff00);
            emit_or_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_1);
            compile_call_dmg_write_a(block);
            break;

        case 0xf0: // ld a, ($ff00 + u8)
            {
                uint8_t addr = READ_BYTE(src_ptr++);

                // check for LY polling loop
                if (addr == 0x44) {
                    uint8_t next0 = READ_BYTE(src_ptr);

                    // check for cp imm8 pattern
                    if (next0 == 0xfe) {
                        uint8_t target_ly = READ_BYTE(src_ptr + 1);
                        uint8_t jr_op = READ_BYTE(src_ptr + 2);
                        int8_t offset = (int8_t) READ_BYTE(src_ptr + 3);

                        if ((jr_op == 0x20 || jr_op == 0x28 || jr_op == 0x38) &&
                            offset < 0) {
                            // detected polling loop - synthesize wait
                            uint16_t next_pc = src_address + src_ptr + 4;
                            compile_ly_wait(block, target_ly, jr_op, next_pc);
                            src_ptr += 4;
                            done = 1;
                            break;
                        }
                    }

                    // check for cp r pattern (0xb8-0xbe = cp B,C,D,E,H,L,(HL))
                    if (next0 >= 0xb8 && next0 <= 0xbe) {
                        int gb_reg = next0 - 0xb8;
                        uint8_t jr_op = READ_BYTE(src_ptr + 1);
                        int8_t offset = (int8_t) READ_BYTE(src_ptr + 2);

                        if ((jr_op == 0x20 || jr_op == 0x28 || jr_op == 0x38) &&
                            offset < 0) {
                            // detected polling loop with register compare
                            uint16_t next_pc = src_address + src_ptr + 3;
                            compile_ly_wait_reg(block, gb_reg, jr_op, next_pc);
                            src_ptr += 3;
                            done = 1;
                            break;
                        }
                    }
                }

                compile_ldh_a_u8(block, addr);
            }
            break;

        case 0xf2: // ld a, ($ff00 + c)
            emit_move_l_dn(block, REG_68K_D_SCRATCH_1, 0x0000ff00);
            emit_or_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_1);
            compile_call_dmg_read_a(block);
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
            {
                uint8_t val = READ_BYTE(src_ptr++);
                emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
                emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);  // D1.w = HL
                compile_call_dmg_write_imm(block, val);
            }
            break;

        case 0xea: // ld (u16), a
            {
                uint16_t addr = READ_BYTE(src_ptr) | (READ_BYTE(src_ptr + 1) << 8);
                src_ptr += 2;
                emit_move_l_dn(block, REG_68K_D_SCRATCH_1, addr);
                if (addr >= 0x8000 && addr < 0xfe00) {
                    emit_move_b_dn_idx_an(block, REG_68K_D_A, REG_68K_A_VIRT_BASE, REG_68K_D_SCRATCH_1);
                } else {
                    // ROM writes for MBC, or HRAM/IO
                    compile_call_dmg_write_a(block);
                }
            }
            break;

        case 0xfa: // ld a, (u16)
            {
                uint16_t addr = READ_BYTE(src_ptr) | (READ_BYTE(src_ptr + 1) << 8);
                src_ptr += 2;
                if (addr < 0x8000) {
                    uint8_t val = ctx->read(ctx->dmg, addr);
                    emit_moveq_dn(block, REG_68K_D_A, val);
                } else if (addr < 0xfe00) {
                    emit_move_l_dn(block, REG_68K_D_SCRATCH_1, addr);
                    emit_move_b_idx_an_dn(block, REG_68K_A_VIRT_BASE, REG_68K_D_SCRATCH_1, REG_68K_D_A);
                } else {
                    emit_move_l_dn(block, REG_68K_D_SCRATCH_1, addr);
                    compile_call_dmg_read_a(block);
                }
            }
            break;

        case 0x76:
            compile_halt(block, src_address + src_ptr);
            done = 1;
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
            emit_move_l_dn(block, REG_68K_D_NEXT_PC, 0xffffffff);
            emit_dispatch_jump(block);
            done = 1;
            break;
        }

        size_t emitted = block->length - before;
        if (emitted > 80) {
            printf("warning: instruction %02x emitted %zu bytes\n", op, emitted);
        }

        if (ctx->single_instruction && !done) {
            emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
            emit_move_w_dn(block, REG_68K_D_NEXT_PC, src_address + src_ptr);
            emit_patchable_exit(block);
            break;
        }
    }

    block->end_address = src_address + src_ptr;
    return block;
}

void block_free(struct code_block *block)
{
    free(block);
}
