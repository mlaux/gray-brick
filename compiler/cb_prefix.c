#include <stdint.h>

#include "cb_prefix.h"
#include "compiler.h"
#include "emitters.h"
#include "flags.h"
#include "interop.h"

// todo remove these
static void compile_shift_flags(struct code_block *block)
{
    emit_move_sr_dn(block, REG_68K_D_FLAGS);
}

static void compile_swap_flags(struct code_block *block)
{
    emit_move_sr_dn(block, REG_68K_D_FLAGS);
    emit_andi_b_dn(block, REG_68K_D_FLAGS, 0xfe);
}

static void compile_bit_flags(struct code_block *block)
{
    // Metroid II (and probably others) depend on bit only affecting Z and not C
    emit_scc(block, COND_EQ, REG_68K_D_SCRATCH_0);
    emit_andi_b_dn(block, REG_68K_D_SCRATCH_0, 0x04);
    emit_andi_b_dn(block, REG_68K_D_FLAGS, 0xfb);
    emit_or_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_FLAGS);
}

// Get a GB register's value into D1 for modification
// Returns the register to use for the actual operation
static int get_reg_for_op(struct code_block *block, int gb_reg)
{
    switch (gb_reg) {
    case 0: // B - high byte of D5, need to extract
        emit_move_l_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_1);
        emit_swap(block, REG_68K_D_SCRATCH_1);  // B now in low byte
        return REG_68K_D_SCRATCH_1;
    case 1: // C - low byte of D5
        emit_move_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_1);
        return REG_68K_D_SCRATCH_1;
    case 2: // D - high byte of D6
        emit_move_l_dn_dn(block, REG_68K_D_DE, REG_68K_D_SCRATCH_1);
        emit_swap(block, REG_68K_D_SCRATCH_1);
        return REG_68K_D_SCRATCH_1;
    case 3: // E - low byte of D6
        emit_move_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_SCRATCH_1);
        return REG_68K_D_SCRATCH_1;
    case 4: // H - high byte of HL (A2)
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);  // H now in low byte
        return REG_68K_D_SCRATCH_1;
    case 5: // L - low byte of HL (A2)
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        return REG_68K_D_SCRATCH_1;
    case 6: // (HL) - memory indirect
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);  // result in D0
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);
        return REG_68K_D_SCRATCH_1;
    case 7: // A - directly accessible
        return REG_68K_D_A;
    }
    return REG_68K_D_SCRATCH_1;
}

// Write the result from D1 (or A for reg 7) back to the GB register
static void put_reg_result(struct code_block *block, int gb_reg)
{
    switch (gb_reg) {
    case 0: // B - write back to high byte of D5
        emit_swap(block, REG_68K_D_BC);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_BC);
        emit_swap(block, REG_68K_D_BC);
        break;
    case 1: // C - write back to low byte of D5
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_BC);
        break;
    case 2: // D - write back to high byte of D6
        emit_swap(block, REG_68K_D_DE);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_DE);
        emit_swap(block, REG_68K_D_DE);
        break;
    case 3: // E - write back to low byte of D6
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_DE);
        break;
    case 4: // H - write back to high byte of A2
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_0);  // D0.w = HL
        emit_ror_w_8(block, REG_68K_D_SCRATCH_1);  // D1.w = 0xHH00
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);  // D1.w = 0xHHLL
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        break;
    case 5: // L - write back to low byte of A2
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_0);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_0, REG_68K_A_HL);
        break;
    case 6: // (HL) - write back to memory
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_write_d0(block);
        break;
    case 7: // A - already in place
        break;
    }
}

// RLC - rotate left circular, bit 7 -> C and bit 0
static void compile_rlc_reg(struct code_block *block, int gb_reg)
{
    int op_reg = get_reg_for_op(block, gb_reg);
    emit_rol_b_imm(block, 1, op_reg);  // rotate left, C gets old bit 7, Z set from result
    compile_shift_flags(block);
    put_reg_result(block, gb_reg);
}

// RRC - rotate right circular, bit 0 -> C and bit 7
static void compile_rrc_reg(struct code_block *block, int gb_reg)
{
    int op_reg = get_reg_for_op(block, gb_reg);
    emit_ror_b_imm(block, 1, op_reg);  // rotate right, C gets old bit 0, Z set from result
    compile_shift_flags(block);
    put_reg_result(block, gb_reg);
}

// RL - rotate left through carry, old C -> bit 0, bit 7 -> new C
static void compile_rl_reg(struct code_block *block, int gb_reg)
{
    int op_reg = get_reg_for_op(block, gb_reg);

    // Save old carry (bit 0 of D7) - already in position for OR into bit 0
    emit_move_b_dn_dn(block, REG_68K_D_FLAGS, REG_68K_D_SCRATCH_0);
    emit_andi_b_dn(block, REG_68K_D_SCRATCH_0, 0x01);

    // Shift left - bit 7 goes to 68k C flag, 0 goes to bit 0
    emit_lsl_b_imm_dn(block, 1, op_reg);

    // Capture the new C flag (bit 0 of CCR)
    emit_move_sr_dn(block, REG_68K_D_FLAGS);
    emit_andi_b_dn(block, REG_68K_D_FLAGS, 0x01);  // keep only C

    // OR old carry into bit 0 of result
    emit_or_b_dn_dn(block, REG_68K_D_SCRATCH_0, op_reg);

    // Set Z flag based on final result
    emit_scc(block, 0x07, REG_68K_D_SCRATCH_0);  // seq for Z
    emit_andi_b_dn(block, REG_68K_D_SCRATCH_0, 0x04);  // mask to bit 2
    emit_or_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_FLAGS);

    put_reg_result(block, gb_reg);
}

// RR - rotate right through carry, old C -> bit 7, bit 0 -> new C
static void compile_rr_reg(struct code_block *block, int gb_reg)
{
    int op_reg = get_reg_for_op(block, gb_reg);

    // Save old carry (bit 0 of D7), shift to bit 7 position
    emit_move_b_dn_dn(block, REG_68K_D_FLAGS, REG_68K_D_SCRATCH_0);
    emit_andi_b_dn(block, REG_68K_D_SCRATCH_0, 0x01);
    emit_ror_b_imm(block, 1, REG_68K_D_SCRATCH_0);  // 0x01 -> 0x80

    // Shift right - bit 0 goes to 68k C flag, 0 goes to bit 7
    emit_lsr_b_imm_dn(block, 1, op_reg);

    // Capture the new C flag (bit 0 of CCR)
    emit_move_sr_dn(block, REG_68K_D_FLAGS);
    emit_andi_b_dn(block, REG_68K_D_FLAGS, 0x01);  // keep only C

    // OR old carry into bit 7 of result
    emit_or_b_dn_dn(block, REG_68K_D_SCRATCH_0, op_reg);

    // Set Z flag based on final result
    emit_scc(block, 0x07, REG_68K_D_SCRATCH_0);  // seq for Z
    emit_andi_b_dn(block, REG_68K_D_SCRATCH_0, 0x04);  // mask to bit 2
    emit_or_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_FLAGS);

    put_reg_result(block, gb_reg);
}

// SLA - shift left arithmetic, 0 -> bit 0, bit 7 -> C
static void compile_sla_reg(struct code_block *block, int gb_reg)
{
    int op_reg = get_reg_for_op(block, gb_reg);
    emit_lsl_b_imm_dn(block, 1, op_reg);
    compile_shift_flags(block);
    put_reg_result(block, gb_reg);
}

// SRA - shift right arithmetic, bit 7 preserved, bit 0 -> C
static void compile_sra_reg(struct code_block *block, int gb_reg)
{
    int op_reg = get_reg_for_op(block, gb_reg);
    emit_asr_b_imm_dn(block, 1, op_reg);
    compile_shift_flags(block);
    put_reg_result(block, gb_reg);
}

// SWAP - swap nibbles
static void compile_swap_reg(struct code_block *block, int gb_reg)
{
    int op_reg = get_reg_for_op(block, gb_reg);
    emit_ror_b_imm(block, 4, op_reg);  // rotate by 4 swaps nibbles
    compile_swap_flags(block);
    put_reg_result(block, gb_reg);
}

// SRL - shift right logical, 0 -> bit 7, bit 0 -> C
static void compile_srl_reg(struct code_block *block, int gb_reg)
{
    int op_reg = get_reg_for_op(block, gb_reg);
    emit_lsr_b_imm_dn(block, 1, op_reg);
    compile_shift_flags(block);
    put_reg_result(block, gb_reg);
}

// BIT n, reg - test bit n of register
static void compile_bit_reg(struct code_block *block, int bit, int gb_reg)
{
    switch (gb_reg) {
    case 0: // B - high byte of D5 (bits 16-23)
        emit_btst_imm_dn(block, bit + 16, REG_68K_D_BC);
        break;
    case 1: // C - low byte of D5 (bits 0-7)
        emit_btst_imm_dn(block, bit, REG_68K_D_BC);
        break;
    case 2: // D - high byte of D6 (bits 16-23)
        emit_btst_imm_dn(block, bit + 16, REG_68K_D_DE);
        break;
    case 3: // E - low byte of D6 (bits 0-7)
        emit_btst_imm_dn(block, bit, REG_68K_D_DE);
        break;
    case 4: // H - high byte of HL
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_btst_imm_dn(block, bit + 8, REG_68K_D_SCRATCH_1);
        break;
    case 5: // L - low byte of HL
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_btst_imm_dn(block, bit, REG_68K_D_SCRATCH_1);
        break;
    case 6: // (HL) - memory indirect
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);  // result in D0
        emit_btst_imm_dn(block, bit, REG_68K_D_SCRATCH_0);
        break;
    case 7: // A
        emit_btst_imm_dn(block, bit, REG_68K_D_A);
        break;
    }
    compile_bit_flags(block);
}

// RES n, reg - clear bit n of register
static void compile_res_reg(struct code_block *block, int bit, int gb_reg)
{
    switch (gb_reg) {
    case 0: // B
        emit_bclr_imm_dn(block, bit + 16, REG_68K_D_BC);
        break;
    case 1: // C
        emit_bclr_imm_dn(block, bit, REG_68K_D_BC);
        break;
    case 2: // D
        emit_bclr_imm_dn(block, bit + 16, REG_68K_D_DE);
        break;
    case 3: // E
        emit_bclr_imm_dn(block, bit, REG_68K_D_DE);
        break;
    case 4: // H - need to move from A2, modify, move back
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_bclr_imm_dn(block, bit + 8, REG_68K_D_SCRATCH_1);
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        break;
    case 5: // L
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_bclr_imm_dn(block, bit, REG_68K_D_SCRATCH_1);
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        break;
    case 6: // (HL) - read, modify, write
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);  // D0 = memory[HL]
        emit_bclr_imm_dn(block, bit, REG_68K_D_SCRATCH_0);  // clear bit in D0
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);  // D1 = address
        compile_call_dmg_write_d0(block);
        break;
    case 7: // A
        emit_bclr_imm_dn(block, bit, REG_68K_D_A);
        break;
    }
}

// SET n, reg - set bit n of register
static void compile_set_reg(struct code_block *block, int bit, int gb_reg)
{
    switch (gb_reg) {
    case 0: // B
        emit_bset_imm_dn(block, bit + 16, REG_68K_D_BC);
        break;
    case 1: // C
        emit_bset_imm_dn(block, bit, REG_68K_D_BC);
        break;
    case 2: // D
        emit_bset_imm_dn(block, bit + 16, REG_68K_D_DE);
        break;
    case 3: // E
        emit_bset_imm_dn(block, bit, REG_68K_D_DE);
        break;
    case 4: // H
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_bset_imm_dn(block, bit + 8, REG_68K_D_SCRATCH_1);
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        break;
    case 5: // L
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_bset_imm_dn(block, bit, REG_68K_D_SCRATCH_1);
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        break;
    case 6: // (HL) - read, modify, write
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);  // D0 = memory[HL]
        emit_bset_imm_dn(block, bit, REG_68K_D_SCRATCH_0);  // set bit in D0
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);  // D1 = address
        compile_call_dmg_write_d0(block);  // write D0 to address D1
        break;
    case 7: // A
        emit_bset_imm_dn(block, bit, REG_68K_D_A);
        break;
    }
}

int compile_cb_insn(struct code_block *block, uint8_t cb_op)
{
    int op = cb_op >> 6;
    int bit_or_type = (cb_op >> 3) & 0x7;
    int reg = cb_op & 0x7;

    switch (op) {
    case 0: // rotates/shifts
        switch (bit_or_type) {
        case 0: compile_rlc_reg(block, reg); return 1;
        case 1: compile_rrc_reg(block, reg); return 1;
        case 2: compile_rl_reg(block, reg); return 1;
        case 3: compile_rr_reg(block, reg); return 1;
        case 4: compile_sla_reg(block, reg); return 1;
        case 5: compile_sra_reg(block, reg); return 1;
        case 6: compile_swap_reg(block, reg); return 1;
        case 7: compile_srl_reg(block, reg); return 1;
        }
        return 0;

    case 1: // BIT
        compile_bit_reg(block, bit_or_type, reg);
        return 1;

    case 2: // RES
        compile_res_reg(block, bit_or_type, reg);
        return 1;

    case 3: // SET
        compile_set_reg(block, bit_or_type, reg);
        return 1;
    }

    return 0;
}
