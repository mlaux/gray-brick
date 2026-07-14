#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "compiler.h"
#include "emitters.h"
#include "interop.h"
#include "flags.h"
#include "timing.h"

// synthesize wait for LY to reach target value
// detects ldh a, [$44]; cp N; jr cc, back
void compile_ly_wait(
    struct code_block *block,
    uint8_t target_ly,
    uint8_t jr_opcode,
    uint16_t next_pc
) {
    // jr nz (0x20): loop while LY != N, exit when LY == N -> wait for N
    // jr z  (0x28): loop while LY == N, exit when LY != N -> wait for N+1
    // jr c  (0x38): loop while LY < N, exit when LY >= N  -> wait for N
    uint8_t wait_ly = target_ly;
    if (jr_opcode == 0x28) {
        wait_ly = (target_ly + 1) % 154;
    }

    uint32_t target_cycles = wait_ly * 456;

    // the wait synthesis overwrites D2, subsuming any pending cycles
    pending_cycles = 0;

    // load frame_cycles pointer
    emit_movea_l_disp_an_an(block, JIT_CTX_FRAME_CYCLES_PTR, REG_68K_A_CTX, REG_68K_A_SCRATCH_1);
    // load frame_cycles into d0
    emit_move_l_ind_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0);

    // compare frame_cycles to target
    emit_cmpi_l_imm_dn(block, target_cycles, REG_68K_D_SCRATCH_0);
    size_t next_frame = block->length;
    emit_bcc_s(block, 0);  // if frame_cycles >= target, wait until next frame

    // same frame: d2 = target - frame_cycles
    emit_move_l_dn(block, REG_68K_D_CYCLE_COUNT, target_cycles);
    emit_sub_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_CYCLE_COUNT);
    size_t have_d2 = block->length;
    emit_bra_b(block, 0);

    // next frame: d2 = (70224 + target) - frame_cycles
    patch_branch_b(block, next_frame);
    emit_move_l_dn(block, REG_68K_D_CYCLE_COUNT, 70224 + target_cycles);
    emit_sub_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_CYCLE_COUNT);
    patch_branch_b(block, have_d2);

    // double D2 if effective double speed is active (CPU cycles = 2x PPU cycles)
    emit_tst_b_disp_an(block, JIT_CTX_EFF_DOUBLE_SPEED, REG_68K_A_CTX);
    size_t single_speed = block->length;
    emit_beq_b(block, 0);
    emit_add_l_dn_dn(block, REG_68K_D_CYCLE_COUNT, REG_68K_D_CYCLE_COUNT);
    patch_branch_b(block, single_speed);

    // set A to the LY value we waited for
    emit_moveq_dn(block, REG_68K_D_A, wait_ly);

    // exit to C
    emit_move_l_dn(block, REG_68K_D_NEXT_PC, next_pc);
    emit_rts(block);
}

// get GB register value into D0, zero-extended to word
void compile_get_gb_reg_d0(struct code_block *block, int gb_reg)
{
    switch (gb_reg) {
    case GB_REG_B:
        emit_move_l_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_0);
        emit_swap(block, REG_68K_D_SCRATCH_0);
        break;
    case GB_REG_C:
        emit_move_l_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_0);
        break;
    case GB_REG_D:
        emit_move_l_dn_dn(block, REG_68K_D_DE, REG_68K_D_SCRATCH_0);
        emit_swap(block, REG_68K_D_SCRATCH_0);
        break;
    case GB_REG_E:
        emit_move_l_dn_dn(block, REG_68K_D_DE, REG_68K_D_SCRATCH_0);
        break;
    case GB_REG_H:
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_0);
        emit_lsr_w_imm_dn(block, 8, REG_68K_D_SCRATCH_0);
        break;
    case GB_REG_L:
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_0);
        break;
    case GB_REG_HL:
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);
        break;
    default:
        printf("invalid register for compile_get_gb_reg_d0\n");
        exit(1);
    }

    emit_andi_w_dn(block, REG_68K_D_SCRATCH_0, 0xff);
}

// synthesize wait for LY to reach target value from a register
// detects ldh a, [$44]; cp <reg>; jr cc, back
void compile_ly_wait_reg(
    struct code_block *block,
    int gb_reg,
    uint8_t jr_opcode,
    uint16_t next_pc
) {
    // the wait synthesis overwrites D2, subsuming any pending cycles
    pending_cycles = 0;

    // Get the target LY value into D0
    compile_get_gb_reg_d0(block, gb_reg);

    // jr nz (0x20): loop while LY != N, exit when LY == N -> wait for N
    // jr z  (0x28): loop while LY == N, exit when LY != N -> wait for N+1
    // jr c  (0x38): loop while LY < N, exit when LY >= N  -> wait for N
    if (jr_opcode == 0x28) {
        // wait_ly = (target + 1) % 154
        emit_addq_w_dn(block, REG_68K_D_SCRATCH_0, 1);
        emit_cmpi_w_imm_dn(block, 154, REG_68K_D_SCRATCH_0);
        size_t no_wrap = block->length;
        emit_bcs_b(block, 0);  // skip the clear if D0 < 154
        emit_moveq_dn(block, REG_68K_D_SCRATCH_0, 0);
        patch_branch_b(block, no_wrap);
    }

    // D0 = wait_ly; save to stack for later
    emit_push_l_dn(block, REG_68K_D_SCRATCH_0);

    // D0 = target_cycles = wait_ly * 456
    emit_mulu_w_imm_dn(block, 456, REG_68K_D_SCRATCH_0);

    // load frame_cycles pointer into A0
    emit_movea_l_disp_an_an(block, JIT_CTX_FRAME_CYCLES_PTR, REG_68K_A_CTX, REG_68K_A_SCRATCH_1);
    // load frame_cycles into D1
    emit_move_l_ind_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_1);

    // compare frame_cycles (D1) to target_cycles (D0)
    emit_cmp_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);
    size_t next_frame = block->length;
    emit_bcc_s(block, 0);  // if frame_cycles >= target_cycles, skip to next_frame

    // same frame: d2 = target_cycles - frame_cycles = d0 - d1
    emit_move_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_CYCLE_COUNT);
    emit_sub_l_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_CYCLE_COUNT);
    size_t have_d2 = block->length;
    emit_bra_b(block, 0);

    // next frame: d2 = (70224 + target_cycles) - frame_cycles
    patch_branch_b(block, next_frame);
    emit_move_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_CYCLE_COUNT);
    emit_addi_l_dn(block, REG_68K_D_CYCLE_COUNT, 70224);
    emit_sub_l_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_CYCLE_COUNT);
    patch_branch_b(block, have_d2);

    // double D2 if effective double speed is active (CPU cycles = 2x PPU cycles)
    emit_tst_b_disp_an(block, JIT_CTX_EFF_DOUBLE_SPEED, REG_68K_A_CTX);
    size_t single_speed = block->length;
    emit_beq_b(block, 0);
    emit_add_l_dn_dn(block, REG_68K_D_CYCLE_COUNT, REG_68K_D_CYCLE_COUNT);
    patch_branch_b(block, single_speed);

    // restore wait_ly from stack into A register
    emit_pop_l_dn(block, REG_68K_D_A);

    // exit to C
    emit_move_l_dn(block, REG_68K_D_NEXT_PC, next_pc);
    emit_rts(block);
}

// fast-forward D2 to jit_ctx.wake_limit (CPU cycles to the next deadline)
// and exit at next_pc so the wait re-checks
static void emit_wake_skip(struct code_block *block, int next_pc)
{
    // move.l JIT_CTX_WAKE_LIMIT(a4), d2
    emit_move_l_disp_an_dn(block, JIT_CTX_WAKE_LIMIT, REG_68K_A_CTX, REG_68K_D_CYCLE_COUNT);
    // move.l #next_pc, d3
    emit_move_l_dn(block, REG_68K_D_NEXT_PC, next_pc);
    emit_rts(block);
}

void compile_halt(struct code_block *block, int next_pc)
{
    // the wake skip overwrites D2, subsuming any pending cycles
    pending_cycles = 0;
    emit_wake_skip(block, next_pc);
}

// synthesize wait for an interrupt handler to change a flag byte in HRAM
// detects ldh a, [nn]; and a / or a; jr z/nz back to the ldh.
// instead of spinning until the cycle limit forces an exit, check the flag
// once, and if the loop would repeat, skip straight to the next wake
// deadline the same way HALT does. the interrupt handler runs, sets the
// flag, and control returns to loop_pc where the flag is checked again.
void compile_hram_idle_wait(
    struct code_block *block,
    uint8_t addr_lo,
    uint8_t jr_opcode,
    uint16_t loop_pc
) {
    // cycles for the and + untaken jr (the ldh was already counted).
    // the repeat path overwrites D2 with the wake skip, so pending stays
    // deferred for the fall-through
    defer_cycles(12);

    // A = HRAM flag
    emit_movea_l_ind_an_an(block, REG_68K_A_CTX, REG_68K_A_SCRATCH_1);
    emit_move_b_disp_an_dn(block, addr_lo - 0x80, REG_68K_A_SCRATCH_1, REG_68K_D_A);

    // and a / or a: Z from A, C=0
    emit_tst_b_dn(block, REG_68K_D_A);
    compile_set_zc_flags(block);

    // if the loop would exit, skip over the wake skip and continue
    size_t skip = block->length;
    if (jr_opcode == 0x28) {
        // jr z: loop repeats while A == 0
        emit_bne_b(block, 0);
    } else {
        // jr nz: loop repeats while A != 0
        emit_beq_b(block, 0);
    }

    emit_wake_skip(block, loop_pc);
    // fall through: loop exits, block continues at the next instruction
    patch_branch_b(block, skip);
}
