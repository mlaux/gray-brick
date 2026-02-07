#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "compiler.h"
#include "emitters.h"
#include "interop.h"
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

    // load frame_cycles pointer
    emit_movea_l_disp_an_an(block, JIT_CTX_FRAME_CYCLES_PTR, REG_68K_A_CTX, REG_68K_A_SCRATCH_1);
    // load frame_cycles into d0
    emit_move_l_ind_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0);

    // compare frame_cycles to target
    emit_cmpi_l_imm_dn(block, target_cycles, REG_68K_D_SCRATCH_0);
    emit_bcc_s(block, 10);  // if frame_cycles >= target, wait until next frame

    // same frame: d2 = target - frame_cycles
    emit_move_l_dn(block, REG_68K_D_CYCLE_COUNT, target_cycles);
    emit_sub_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_CYCLE_COUNT);
    emit_bra_b(block, 8);

    // next frame: d2 = (70224 + target) - frame_cycles
    emit_move_l_dn(block, REG_68K_D_CYCLE_COUNT, 70224 + target_cycles);
    emit_sub_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_CYCLE_COUNT);

    // double D2 if effective double speed is active (CPU cycles = 2x PPU cycles)
    emit_tst_b_disp_an(block, JIT_CTX_EFF_DOUBLE_SPEED, REG_68K_A_CTX);
    emit_beq_b(block, 2);
    emit_add_l_dn_dn(block, REG_68K_D_CYCLE_COUNT, REG_68K_D_CYCLE_COUNT);

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
    // Get the target LY value into D0
    compile_get_gb_reg_d0(block, gb_reg);

    // jr nz (0x20): loop while LY != N, exit when LY == N -> wait for N
    // jr z  (0x28): loop while LY == N, exit when LY != N -> wait for N+1
    // jr c  (0x38): loop while LY < N, exit when LY >= N  -> wait for N
    if (jr_opcode == 0x28) {
        // wait_ly = (target + 1) % 154
        emit_addq_w_dn(block, REG_68K_D_SCRATCH_0, 1);
        emit_cmpi_w_imm_dn(block, 154, REG_68K_D_SCRATCH_0);
        emit_bcs_b(block, 2);  // skip the clear if D0 < 154 
        emit_moveq_dn(block, REG_68K_D_SCRATCH_0, 0);
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
    emit_bcc_s(block, 6);  // if frame_cycles >= target_cycles, skip to next_frame

    // same frame: d2 = target_cycles - frame_cycles = d0 - d1
    emit_move_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_CYCLE_COUNT);
    emit_sub_l_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_CYCLE_COUNT);
    emit_bra_b(block, 10);

    // next frame: d2 = (70224 + target_cycles) - frame_cycles
    emit_move_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_CYCLE_COUNT);
    emit_addi_l_dn(block, REG_68K_D_CYCLE_COUNT, 70224);
    emit_sub_l_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_CYCLE_COUNT);

    // double D2 if effective double speed is active (CPU cycles = 2x PPU cycles)
    emit_tst_b_disp_an(block, JIT_CTX_EFF_DOUBLE_SPEED, REG_68K_A_CTX);
    emit_beq_b(block, 2);
    emit_add_l_dn_dn(block, REG_68K_D_CYCLE_COUNT, REG_68K_D_CYCLE_COUNT);

    // restore wait_ly from stack into A register
    emit_pop_l_dn(block, REG_68K_D_A);

    // exit to C
    emit_move_l_dn(block, REG_68K_D_NEXT_PC, next_pc);
    emit_rts(block);
}

// only good for vblank interrupts for now...
void compile_halt(struct code_block *block, int next_pc)
{
    //   movea.l JIT_CTX_FRAME_CYCLES_PTR(a4), a0 ; 4 bytes
    //   move.l (a0), d0                          ; 2 bytes
    //   cmpi.l #65664, d0                        ; 6 bytes
    //   bcc.s _frame_end                         ; 2 bytes
    //   move.l #65664, d2                        ; 6 bytes
    //   sub.l d0, d2                             ; 2 bytes
    //   bra.s _exit                              ; 2 bytes
    // _frame_end
    //   move.l #70224, d2                        ; 6 bytes
    //   sub.l d0, d2                             ; 2 bytes
    // _exit
    //   move.l #next_pc, d3                      ; 6 bytes
    //   rts                                      ; 2 bytes

    // load frame_cycles pointer
    emit_movea_l_disp_an_an(block, JIT_CTX_FRAME_CYCLES_PTR, REG_68K_A_CTX, REG_68K_A_SCRATCH_1);
    emit_move_l_ind_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0);

    // see if already in vblank
    emit_cmpi_l_imm_dn(block, 65664, REG_68K_D_SCRATCH_0);
    emit_bcc_s(block, 10);

    // before vblank: d2 = 65664 - frame_cycles
    emit_move_l_dn(block, REG_68K_D_CYCLE_COUNT, 65664);
    emit_sub_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_CYCLE_COUNT);
    emit_bra_b(block, 8);

    // in vblank: d2 = (70224 - frame_cycles) + 65664
    emit_move_l_dn(block, REG_68K_D_CYCLE_COUNT, 70224 + 65664);
    emit_sub_l_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_CYCLE_COUNT);

    // double D2 if effective double speed is active (CPU cycles = 2x PPU cycles)
    emit_tst_b_disp_an(block, JIT_CTX_EFF_DOUBLE_SPEED, REG_68K_A_CTX);
    emit_beq_b(block, 2);
    emit_add_l_dn_dn(block, REG_68K_D_CYCLE_COUNT, REG_68K_D_CYCLE_COUNT);

    // exit to C
    emit_move_l_dn(block, REG_68K_D_NEXT_PC, next_pc);
    emit_rts(block);
}
