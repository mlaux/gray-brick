#include "compiler.h"
#include "emitters.h"

// how to do the flags properly:
// 8-bit and, or, xor -> set Z for result, set C=0
// 8-bit add, adc, sub, sbc, cp -> set both Z and C for result
// 16 bit add -> set C for result, leave Z alone
// 8-bit incs/decs -> set Z for result, leave C alone
// 16-bit incs/decs -> leave both alone
// non-cb rotates -> set C for result, set Z=0
// cb rotates/shifts -> set both Z and C for result
// swap -> set Z for result, set C=0
// bit -> set Z for result, leave C alone

void compile_set_zc_flags(struct code_block *block)
{
    emit_move_sr_dn(block, REG_68K_D_FLAGS);
}

void compile_set_z_flag(struct code_block *block)
{
    // inc/dec/bit must set Z from result but preserve C from previous flags.
    // Save new CCR to D3, extract Z, merge with old C from D7.
    // Then load merged flags into CCR so fused branches see correct Z.
    emit_move_sr_dn(block, REG_68K_D_NEXT_PC);
    emit_andi_b_dn(block, REG_68K_D_NEXT_PC, 0x04);
    emit_andi_b_dn(block, REG_68K_D_FLAGS, 0x01);
    emit_or_b_dn_dn(block, REG_68K_D_NEXT_PC, REG_68K_D_FLAGS);
    emit_move_dn_ccr(block, REG_68K_D_FLAGS);
}

void compile_set_c_flag(struct code_block *block)
{
    // add hl,rr sets C from result but preserves Z from previous flags.
    // Save new CCR to D3, extract C, merge with old Z from D7.
    emit_move_sr_dn(block, REG_68K_D_NEXT_PC);
    emit_andi_b_dn(block, REG_68K_D_NEXT_PC, 0x01);
    emit_andi_b_dn(block, REG_68K_D_FLAGS, 0x04);
    emit_or_b_dn_dn(block, REG_68K_D_NEXT_PC, REG_68K_D_FLAGS);
}