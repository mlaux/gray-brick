#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "compiler.h"
#include "emitters.h"
#include "interop.h"
#include "flags.h"
#include "timing.h"

#define LINE_CYCLES  456
#define FRAME_CYCLES 70224

// fast-forward the countdown to 0 (the wake deadline) and exit at
// next_pc so the wait re-checks
static void emit_wake_skip(struct code_block *block, int next_pc)
{
#ifdef GB6_PROFILING
    emit_add_l_dn_disp_an(block, REG_68K_D_CYCLE_COUNT, JIT_CTX_SKIPPED,
            REG_68K_A_CTX);
#endif
    emit_moveq_dn(block, REG_68K_D_CYCLE_COUNT, 0);
    // move.l #next_pc, d3
    emit_move_l_dn(block, REG_68K_D_NEXT_PC, next_pc);
    emit_rts(block);
}

// tail for the LY wait paths: D2 holds the PPU-cycle distance to the
// target line. converts to CPU cycles in double speed, then clamps to
// wake_limit
static void emit_ly_wait_clamp(struct code_block *block, uint16_t loop_pc)
{
    // double D2 if effective double speed is active (CPU cycles = 2x PPU cycles)
    emit_tst_b_disp_an(block, JIT_CTX_EFF_DOUBLE_SPEED, REG_68K_A_CTX);
    size_t single_speed = block->length;
    emit_beq_b(block, 0);
    emit_add_l_dn_dn(block, REG_68K_D_CYCLE_COUNT, REG_68K_D_CYCLE_COUNT);
    patch_branch_b(block, single_speed);

    // d1 = wake_limit - dist: the countdown value at the wait's end.
    // borrow means the target is past the wake deadline
    emit_move_l_disp_an_dn(block, JIT_CTX_WAKE_LIMIT, REG_68K_A_CTX,
            REG_68K_D_SCRATCH_1);
    emit_sub_l_dn_dn(block, REG_68K_D_CYCLE_COUNT, REG_68K_D_SCRATCH_1);
    size_t in_reach = block->length;
    emit_bcc_s(block, 0);

#ifdef GB6_PROFILING
    emit_addq_l_disp_an(block, 1, JIT_CTX_LY_SKIPS, REG_68K_A_CTX);
#endif
    emit_moveq_dn(block, REG_68K_D_CYCLE_COUNT, 0);
    emit_move_l_dn(block, REG_68K_D_NEXT_PC, loop_pc);
    emit_rts(block);

    patch_branch_b(block, in_reach);
    emit_move_l_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_CYCLE_COUNT);
}

// the loop condition is already false, fall through
static void emit_ly_exit_now(
    struct code_block *block,
    int a_imm,
    uint8_t fc_reg,
    uint8_t flags,
    int exit_cycles,
    uint16_t next_pc
) {
    if (a_imm >= 0) {
        emit_moveq_dn(block, REG_68K_D_A, (int8_t) a_imm);
    } else {
        emit_divu_w_imm_dn(block, LINE_CYCLES, fc_reg);
        emit_move_l_dn_dn(block, fc_reg, REG_68K_D_A);
    }
    emit_moveq_dn(block, REG_68K_D_FLAGS, flags);
    emit_add_cycles(block, exit_cycles);
    emit_move_l_dn(block, REG_68K_D_NEXT_PC, next_pc);
    emit_rts(block);
}

// a completed wait, A and D7 are compile-time known
static void emit_ly_wait_done(
    struct code_block *block,
    int a_imm,
    uint8_t flags,
    uint16_t next_pc
) {
    emit_moveq_dn(block, REG_68K_D_A, (int8_t) a_imm);
    emit_moveq_dn(block, REG_68K_D_FLAGS, flags);
    emit_move_l_dn(block, REG_68K_D_NEXT_PC, next_pc);
    emit_rts(block);
}

// synthesize wait for LY to reach target value
// detects ldh a, [$44]; cp N; jr cc, back (and the and/or-a cp-0 form)
//
// jr nz (0x20): loop while LY != N, exit when LY == N -> wait for N
// jr z  (0x28): loop while LY == N, exit when LY != N -> wait for N+1
// jr c  (0x38): loop while LY < N, exit when LY >= N  -> wait for N
void compile_ly_wait(
    struct code_block *block,
    uint8_t target_ly,
    uint8_t jr_opcode,
    uint16_t next_pc,
    uint16_t loop_pc,
    int tail_cycles
) {
    uint32_t lo = target_ly * LINE_CYCLES;
    uint32_t hi = lo + LINE_CYCLES;

    int exit_cycles = pending_cycles + tail_cycles;
    // the wait paths overwrite D2
    pending_cycles = 0;

    if (target_ly >= 154) {
        if (jr_opcode == 0x28) {
            // LY can never equal N: the loop exits on the first pass
            emit_movea_l_disp_an_an(block, JIT_CTX_FRAME_CYCLES_PTR,
                    REG_68K_A_CTX, REG_68K_A_SCRATCH_1);
            emit_move_l_ind_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0);
            emit_ly_exit_now(block, -1, REG_68K_D_SCRATCH_0, 0x01,
                    exit_cycles, next_pc);
        } else {
            // LY can never reach N: idle at deadline pace forever
            emit_wake_skip(block, loop_pc);
        }
        return;
    }

    // d0 = frame_cycles
    emit_movea_l_disp_an_an(block, JIT_CTX_FRAME_CYCLES_PTR, REG_68K_A_CTX,
            REG_68K_A_SCRATCH_1);
    emit_move_l_ind_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0);

    switch (jr_opcode) {
    case 0x20: {
        emit_cmpi_l_imm_dn(block, lo, REG_68K_D_SCRATCH_0);
        size_t below = block->length;
        emit_bcs_b(block, 0);
        emit_cmpi_l_imm_dn(block, hi, REG_68K_D_SCRATCH_0);
        size_t inside = block->length;
        emit_bcs_b(block, 0);

        // past line N
        emit_wake_skip(block, loop_pc);

        // on line N right now, the loop exits on this pass
        patch_branch_b(block, inside);
        emit_ly_exit_now(block, target_ly, 0, 0x04, exit_cycles, next_pc);

        // before line N, wait for its start
        patch_branch_b(block, below);
        emit_move_l_dn(block, REG_68K_D_CYCLE_COUNT, lo);
        emit_sub_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_CYCLE_COUNT);
        emit_ly_wait_clamp(block, loop_pc);
        emit_ly_wait_done(block, target_ly, 0x04, next_pc);
        break;
    }
    case 0x28: {
        emit_cmpi_l_imm_dn(block, lo, REG_68K_D_SCRATCH_0);
        size_t below = block->length;
        emit_bcs_b(block, 0);
        emit_cmpi_l_imm_dn(block, hi, REG_68K_D_SCRATCH_0);
        size_t above = block->length;
        emit_bcc_s(block, 0);

        // on line N, wait for the next line (N=153 wraps at the frame end)
        emit_move_l_dn(block, REG_68K_D_CYCLE_COUNT, hi);
        emit_sub_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_CYCLE_COUNT);
        emit_ly_wait_clamp(block, loop_pc);
        emit_ly_wait_done(block, (target_ly + 1) % 154,
                target_ly == 153 ? 0x01 : 0x00, next_pc);

        // not on line N, the loop exits on this pass, A = actual LY
        patch_branch_b(block, below);
        emit_ly_exit_now(block, -1, REG_68K_D_SCRATCH_0, 0x01,
                exit_cycles, next_pc);
        patch_branch_b(block, above);
        emit_ly_exit_now(block, -1, REG_68K_D_SCRATCH_0, 0x00,
                exit_cycles, next_pc);
        break;
    }
    case 0x38: {
        emit_cmpi_l_imm_dn(block, lo, REG_68K_D_SCRATCH_0);
        size_t reached = block->length;
        emit_bcc_s(block, 0);

        // before line N, wait for its start
        emit_move_l_dn(block, REG_68K_D_CYCLE_COUNT, lo);
        emit_sub_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_CYCLE_COUNT);
        emit_ly_wait_clamp(block, loop_pc);
        emit_ly_wait_done(block, target_ly, 0x04, next_pc);

        // at or past line N, the loop exits on this pass
        patch_branch_b(block, reached);
        emit_cmpi_l_imm_dn(block, hi, REG_68K_D_SCRATCH_0);
        size_t exactly = block->length;
        emit_bcs_b(block, 0);
        emit_ly_exit_now(block, -1, REG_68K_D_SCRATCH_0, 0x00,
                exit_cycles, next_pc);
        patch_branch_b(block, exactly);
        emit_ly_exit_now(block, target_ly, 0, 0x04, exit_cycles, next_pc);
        break;
    }
    }
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
        compile_call_dmg_read_hl(block);
        break;
    default:
        printf("invalid register for compile_get_gb_reg_d0\n");
        exit(1);
    }

    emit_andi_w_dn(block, REG_68K_D_SCRATCH_0, 0xff);
}

// synthesize wait for LY to reach target value from a register
// detects ldh a, [$44]; cp <reg>; jr cc, back
// same structure as compile_ly_wait with the target resolved at run time
void compile_ly_wait_reg(
    struct code_block *block,
    int gb_reg,
    uint8_t jr_opcode,
    uint16_t next_pc,
    uint16_t loop_pc,
    int tail_cycles
) {
    // may emit a dmg_read call for cp (hl), which flushes pending cycles
    compile_get_gb_reg_d0(block, gb_reg);

    int exit_cycles = pending_cycles + tail_cycles;
    // the wait paths overwrite D2, subsuming any pending cycles
    pending_cycles = 0;

    // stash the target in A: every fall-through leaves A derived from it,
    // and a loop-head exit re-runs the loop, which reloads A anyway
    emit_move_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_A);

    // d0 = target line start
    emit_mulu_w_imm_dn(block, LINE_CYCLES, REG_68K_D_SCRATCH_0);

    if (jr_opcode != 0x28) {
        // target > 153: LY can never reach it, idle at deadline pace
        emit_cmpi_l_imm_dn(block, FRAME_CYCLES, REG_68K_D_SCRATCH_0);
        size_t reachable = block->length;
        emit_bcs_b(block, 0);
        emit_wake_skip(block, loop_pc);
        patch_branch_b(block, reachable);
    }
    // (for jr z an out-of-range target just tests as "not on line N")

    // d1 = frame_cycles
    emit_movea_l_disp_an_an(block, JIT_CTX_FRAME_CYCLES_PTR, REG_68K_A_CTX,
            REG_68K_A_SCRATCH_1);
    emit_move_l_ind_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_1);

    switch (jr_opcode) {
    case 0x20: {
        emit_cmp_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);
        size_t below = block->length;
        emit_bcs_b(block, 0);
        emit_addi_l_dn(block, REG_68K_D_SCRATCH_0, LINE_CYCLES);
        emit_cmp_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);
        size_t inside = block->length;
        emit_bcs_b(block, 0);

        // past line N
        emit_wake_skip(block, loop_pc);

        // on line N right now: exit with A = N already in place
        patch_branch_b(block, inside);
        emit_moveq_dn(block, REG_68K_D_FLAGS, 0x04);
        emit_add_cycles(block, exit_cycles);
        emit_move_l_dn(block, REG_68K_D_NEXT_PC, next_pc);
        emit_rts(block);

        // before line N: wait for its start (d0 still holds it here)
        patch_branch_b(block, below);
        emit_move_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_CYCLE_COUNT);
        emit_sub_l_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_CYCLE_COUNT);
        emit_ly_wait_clamp(block, loop_pc);
        emit_moveq_dn(block, REG_68K_D_FLAGS, 0x04);
        emit_move_l_dn(block, REG_68K_D_NEXT_PC, next_pc);
        emit_rts(block);
        break;
    }
    case 0x28: {
        emit_cmp_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);
        size_t below = block->length;
        emit_bcs_b(block, 0);
        emit_addi_l_dn(block, REG_68K_D_SCRATCH_0, LINE_CYCLES);
        emit_cmp_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);
        size_t above = block->length;
        emit_bcc_s(block, 0);

        // on line N: wait for the next line (d0 = its start)
        emit_move_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_CYCLE_COUNT);
        emit_sub_l_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_CYCLE_COUNT);
        emit_ly_wait_clamp(block, loop_pc);
        // A = N+1, wrapping 154 -> 0 at the frame end (C set: 0 < N)
        emit_addq_w_dn(block, REG_68K_D_A, 1);
        emit_cmpi_w_imm_dn(block, 154, REG_68K_D_A);
        size_t no_wrap = block->length;
        emit_bne_b(block, 0);
        emit_moveq_dn(block, REG_68K_D_A, 0);
        emit_moveq_dn(block, REG_68K_D_FLAGS, 0x01);
        size_t flags_done = block->length;
        emit_bra_b(block, 0);
        patch_branch_b(block, no_wrap);
        emit_moveq_dn(block, REG_68K_D_FLAGS, 0x00);
        patch_branch_b(block, flags_done);
        emit_move_l_dn(block, REG_68K_D_NEXT_PC, next_pc);
        emit_rts(block);

        // not on line N: the loop exits on this pass, A = actual LY
        patch_branch_b(block, below);
        emit_ly_exit_now(block, -1, REG_68K_D_SCRATCH_1, 0x01,
                exit_cycles, next_pc);
        patch_branch_b(block, above);
        emit_ly_exit_now(block, -1, REG_68K_D_SCRATCH_1, 0x00,
                exit_cycles, next_pc);
        break;
    }
    case 0x38: {
        emit_cmp_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);
        size_t reached = block->length;
        emit_bcc_s(block, 0);

        // before line N: wait for its start
        emit_move_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_CYCLE_COUNT);
        emit_sub_l_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_CYCLE_COUNT);
        emit_ly_wait_clamp(block, loop_pc);
        emit_moveq_dn(block, REG_68K_D_FLAGS, 0x04);
        emit_move_l_dn(block, REG_68K_D_NEXT_PC, next_pc);
        emit_rts(block);

        // at or past line N: the loop exits on this pass
        patch_branch_b(block, reached);
        emit_addi_l_dn(block, REG_68K_D_SCRATCH_0, LINE_CYCLES);
        emit_cmp_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);
        size_t exactly = block->length;
        emit_bcs_b(block, 0);
        emit_ly_exit_now(block, -1, REG_68K_D_SCRATCH_1, 0x00,
                exit_cycles, next_pc);
        patch_branch_b(block, exactly);
        emit_moveq_dn(block, REG_68K_D_FLAGS, 0x04);
        emit_add_cycles(block, exit_cycles);
        emit_move_l_dn(block, REG_68K_D_NEXT_PC, next_pc);
        emit_rts(block);
        break;
    }
    }
}

// dec a/b; jr nz, -3 with an empty body (the classic OAM DMA delay)
// -> exit with correct cycles spent
void compile_delay_loop(
    struct code_block *block,
    uint8_t dec_op,
    uint16_t loop_pc,
    uint16_t next_pc
) {
    size_t past_wake, full, j_ok, done_exit;

    flush_cycles(block);

    // d0 = counter value, 0 meaning 256
    emit_moveq_dn(block, REG_68K_D_SCRATCH_0, 0);
    if (dec_op == 0x05) {
        emit_move_l_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_1);
        emit_swap(block, REG_68K_D_SCRATCH_1);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    } else {
        emit_move_b_dn_dn(block, REG_68K_D_A, REG_68K_D_SCRATCH_0);
    }
    emit_subq_w_dn(block, REG_68K_D_SCRATCH_0, 1);
    emit_andi_w_dn(block, REG_68K_D_SCRATCH_0, 0xff);
    emit_addq_w_dn(block, REG_68K_D_SCRATCH_0, 1);

    // d3 = N, d0 = 16N - 8: the whole loop is 16N - 4 and the dec's 4
    // was already deferred
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_NEXT_PC);
    emit_lsl_w_imm_dn(block, 4, REG_68K_D_SCRATCH_0);
    emit_subq_w_dn(block, REG_68K_D_SCRATCH_0, 8);

    // d1 = budget remaining (the countdown itself); <= 0 means the wake
    // deadline is already due
    emit_move_l_dn_dn(block, REG_68K_D_CYCLE_COUNT, REG_68K_D_SCRATCH_1);
    past_wake = block->length;
    emit_ble_b(block, 0);
    emit_cmp_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);
    full = block->length;
    emit_bcc_s(block, 0);

    // partial: j = (remaining + 4) / 16 iterations fit, at least 1
    emit_addq_l_dn(block, REG_68K_D_SCRATCH_1, 4);
    emit_lsr_l_imm_dn(block, 4, REG_68K_D_SCRATCH_1);
    j_ok = block->length;
    emit_bne_b(block, 0);
    patch_branch_b(block, past_wake);
    emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 1);
    patch_branch_b(block, j_ok);

    // charge 16j - 4, counter -= j, Z=0 C preserved
    emit_move_l_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    emit_lsl_w_imm_dn(block, 4, REG_68K_D_SCRATCH_1);
    emit_subq_w_dn(block, REG_68K_D_SCRATCH_1, 4);
    emit_sub_l_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_CYCLE_COUNT);
    emit_sub_w_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_NEXT_PC);
    if (dec_op == 0x05) {
        emit_swap(block, REG_68K_D_BC);
        emit_move_b_dn_dn(block, REG_68K_D_NEXT_PC, REG_68K_D_BC);
        emit_swap(block, REG_68K_D_BC);
    } else {
        emit_move_b_dn_dn(block, REG_68K_D_NEXT_PC, REG_68K_D_A);
    }
    emit_andi_b_dn(block, REG_68K_D_FLAGS, 0x01);
    emit_tst_b_dn(block, REG_68K_D_NEXT_PC);
    done_exit = block->length;
    emit_beq_b(block, 0);
    emit_move_l_dn(block, REG_68K_D_NEXT_PC, loop_pc);
    emit_rts(block);

    // forced j drained the counter: final jr untaken is 4 cheaper
    patch_branch_b(block, done_exit);
    emit_addq_l_dn(block, REG_68K_D_CYCLE_COUNT, 4);
    emit_ori_b_dn(block, REG_68K_D_FLAGS, 0x04);
    emit_move_l_dn(block, REG_68K_D_NEXT_PC, next_pc);
    emit_rts(block);

    // full: charge 16N - 8, counter = 0, Z=1, block continues
    patch_branch_b(block, full);
    emit_sub_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_CYCLE_COUNT);
    if (dec_op == 0x05) {
        emit_swap(block, REG_68K_D_BC);
        emit_andi_w_dn(block, REG_68K_D_BC, 0xff00);
        emit_swap(block, REG_68K_D_BC);
    } else {
        emit_moveq_dn(block, REG_68K_D_A, 0);
    }
    emit_andi_b_dn(block, REG_68K_D_FLAGS, 0x01);
    emit_ori_b_dn(block, REG_68K_D_FLAGS, 0x04);
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
    // cycles for the and + untaken jr, the ldh was already counted
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
