#include "alu.h"
#include "emitters.h"
#include "flags.h"
#include "interop.h"
#include "branches.h"

// helper for reading GB memory during compilation
#define READ_BYTE(off) (ctx->read(ctx->dmg, src_address + (off)))

// Try to fuse with a following conditional branch using live CCR flags.
// allow_carry: if true, allow C conditions (for cp/sub); if false, only Z (for and/or/tst)
// Returns 1 if fused, 0 if caller should save flags normally.
static int try_fuse_branch(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t *src_ptr,
    uint16_t src_address,
    int allow_carry
) {
    uint8_t next_op = READ_BYTE(*src_ptr);
    int cond = get_branch_condition(next_op);

    if (cond == COND_NONE)
        return 0;

    // For and/or/tst, only fuse Z conditions
    if (!allow_carry && (cond == COND_CS || cond == COND_CC))
        return 0;

    // Record m68k offset for branch instruction and consume opcode
    block->m68k_offsets[*src_ptr] = block->length;
    (*src_ptr)++;

    // Emit fused branch based on opcode type
    switch (next_op) {
    case 0x20: case 0x28: case 0x30: case 0x38:  // jr nz/z/nc/c
        compile_jr_cond_fused(block, ctx, src_ptr, src_address, cond);
        break;
    case 0xc2: case 0xca: case 0xd2: case 0xda:  // jp nz/z/nc/c
        compile_jp_cond_fused(block, ctx, src_ptr, src_address, cond);
        break;
    case 0xc4: case 0xcc: case 0xd4: case 0xdc:  // call nz/z/nc/c
        compile_call_cond_fused(block, ctx, src_ptr, src_address, cond);
        break;
    default:  // ret nz/z/nc/c
        compile_ret_cond_fused(block, cond);
        break;
    }
    return 1;
}

// DAA tracking: save old_A and set N flag before ALU ops that affect A
// These are needed for DAA to compute the half-carry (H) and know add vs sub
static void compile_daa_track_add(struct code_block *block)
{
    // Save old_A to context for H flag computation
    emit_move_b_dn_disp_an(block, REG_68K_D_A, JIT_CTX_DAA_STATE, REG_68K_A_CTX);
    // Set N=0 (addition)
    emit_moveq_dn(block, REG_68K_D_SCRATCH_0, 0);
    emit_move_b_dn_disp_an(block, REG_68K_D_SCRATCH_0, JIT_CTX_DAA_STATE + 1, REG_68K_A_CTX);
}

static void compile_daa_track_sub(struct code_block *block)
{
    // Save old_A to context for H flag computation
    emit_move_b_dn_disp_an(block, REG_68K_D_A, JIT_CTX_DAA_STATE, REG_68K_A_CTX);
    // Set N=1 (subtraction)
    emit_moveq_dn(block, REG_68K_D_SCRATCH_0, 1);
    emit_move_b_dn_disp_an(block, REG_68K_D_SCRATCH_0, JIT_CTX_DAA_STATE + 1, REG_68K_A_CTX);
}

// DAA - Decimal Adjust Accumulator
// Adjusts A for BCD arithmetic based on N, H, C flags from prior ALU op
static void compile_daa(struct code_block *block)
{
    size_t branch_to_sub, branch_add_h_done;
    size_t branch_to_finish, branch_to_finish2;

    // Save original A lower nibble into D1 for H computation later
    // (before any DAA adjustments modify A)
    emit_move_b_dn_dn(block, REG_68K_D_A, REG_68K_D_SCRATCH_1);
    emit_andi_b_dn(block, REG_68K_D_SCRATCH_1, 0x0F);  // D1 = original A & 0xF

    // Load N flag into D0 and test it (we'll reload old_A later in each path)
    emit_move_b_disp_an_dn(block, JIT_CTX_DAA_STATE + 1, REG_68K_A_CTX, REG_68K_D_SCRATCH_0);
    emit_tst_b_dn(block, REG_68K_D_SCRATCH_0);
    branch_to_sub = block->length;
    emit_bne_w(block, 0);  // branch to subtraction path if N=1

    // === Addition path (N=0) ===
    // First check C || A > 0x99 -> add 0x60
    emit_btst_imm_dn(block, 0, REG_68K_D_FLAGS);  // test C flag (4 bytes)
    emit_bne_b(block, 6);  // if C set, skip compare and jump to add 0x60 (2 bytes)
    emit_cmp_b_imm_dn(block, REG_68K_D_A, 0x99);  // (4 bytes)
    emit_bls_b(block, 8);  // if A <= 0x99, skip add 0x60 and ori (2 bytes)
    // add 0x60 and set C
    emit_addi_b_dn(block, REG_68K_D_A, 0x60);  // (4 bytes)
    emit_ori_b_dn(block, REG_68K_D_FLAGS, 0x01);  // set C (4 bytes)

    // Now check H || (A & 0x0F) > 9 -> add 0x06
    // Load old_A into D0 for H computation
    emit_move_b_disp_an_dn(block, JIT_CTX_DAA_STATE, REG_68K_A_CTX, REG_68K_D_SCRATCH_0);
    // D0 = old_A, D1 = original A & 0xF
    // Compute H: D1 < (D0 & 0xF)?
    emit_andi_b_dn(block, REG_68K_D_SCRATCH_0, 0x0F);  // D0 = old_A & 0xF
    emit_cmp_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);  // cmp D0, D1
    emit_bcc_s(block, 8);  // if D1 >= D0 (carry clear), H=0, check nibble value
    // H=1, add 0x06
    emit_addi_b_dn(block, REG_68K_D_A, 0x06);
    branch_add_h_done = block->length;
    emit_bra_w(block, 0);  // skip to finish

    // H=0, check if original (A & 0x0F) > 9 (D1 still has this value)
    emit_cmp_b_imm_dn(block, REG_68K_D_SCRATCH_1, 0x09);
    emit_bls_b(block, 4);  // if <= 9, skip
    emit_addi_b_dn(block, REG_68K_D_A, 0x06);

    branch_to_finish = block->length;
    emit_bra_w(block, 0);  // jump to finish

    // === Subtraction path (N=1) ===
    // Patch branch_to_sub
    block->code[branch_to_sub + 2] = (block->length - branch_to_sub - 2) >> 8;
    block->code[branch_to_sub + 3] = (block->length - branch_to_sub - 2) & 0xff;

    // Check C -> sub 0x60
    emit_btst_imm_dn(block, 0, REG_68K_D_FLAGS);
    emit_beq_b(block, 4);  // if C clear, skip
    emit_subi_b_dn(block, REG_68K_D_A, 0x60);

    // Compute H: D1 > (old_A & 0xF) for subtraction
    // Load old_A & 0xF into D0
    emit_move_b_disp_an_dn(block, JIT_CTX_DAA_STATE, REG_68K_A_CTX, REG_68K_D_SCRATCH_0);
    emit_andi_b_dn(block, REG_68K_D_SCRATCH_0, 0x0F);  // D0 = old_A & 0xF
    emit_cmp_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);  // cmp D0, D1
    emit_bls_b(block, 4);  // if D1 <= D0 (lower or same), no H, skip
    emit_subi_b_dn(block, REG_68K_D_A, 0x06);

    branch_to_finish2 = block->length;
    emit_bra_w(block, 0);

    // === Finish: set Z flag ===
    // Patch forward branches
    block->code[branch_add_h_done + 2] = (block->length - branch_add_h_done - 2) >> 8;
    block->code[branch_add_h_done + 3] = (block->length - branch_add_h_done - 2) & 0xff;
    block->code[branch_to_finish + 2] = (block->length - branch_to_finish - 2) >> 8;
    block->code[branch_to_finish + 3] = (block->length - branch_to_finish - 2) & 0xff;
    block->code[branch_to_finish2 + 2] = (block->length - branch_to_finish2 - 2) >> 8;
    block->code[branch_to_finish2 + 3] = (block->length - branch_to_finish2 - 2) & 0xff;

    // Set Z flag based on A, preserve C
    emit_andi_b_dn(block, REG_68K_D_FLAGS, 0x01);  // keep only C
    emit_tst_b_dn(block, REG_68K_D_A);
    emit_bne_b(block, 4);  // if not zero, skip setting Z
    emit_ori_b_dn(block, REG_68K_D_FLAGS, 0x04);  // set Z
}

// ADC core: expects operand already in D1.b
// Does A = A + D1 + carry using 16-bit arithmetic
static void compile_adc_core(struct code_block *block)
{
    // Track for DAA: save old_A and set N=0 (addition)
    compile_daa_track_add(block);

    // Zero-extend operand: andi.w #0xff, D1
    emit_andi_w_dn(block, REG_68K_D_SCRATCH_1, 0x00ff);

    // Zero-extend A into D0: moveq #0, D0; move.b D4, D0
    emit_moveq_dn(block, REG_68K_D_SCRATCH_0, 0);
    emit_move_b_dn_dn(block, REG_68K_D_A, REG_68K_D_SCRATCH_0);

    // Test old carry and conditionally add 1
    emit_btst_imm_dn(block, 0, REG_68K_D_FLAGS);
    emit_beq_b(block, 2);
    emit_addq_w_dn(block, REG_68K_D_SCRATCH_0, 1);

    // Add operand: add.w D1, D0
    emit_add_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);

    // Store result: move.b D0, D4
    emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_A);

    // Set flags 
    // Z from 8-bit result, C from bit 8 of 16-bit result
    emit_tst_b_dn(block, REG_68K_D_A);
    emit_move_sr_dn(block, REG_68K_D_FLAGS);     // save Z
    emit_andi_b_dn(block, REG_68K_D_FLAGS, 0xfe);  // clear C
    emit_lsr_w_imm_dn(block, 8, REG_68K_D_SCRATCH_0);  // D0.b = carry out
    emit_or_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_FLAGS);
}

// SBC core: expects operand already in D1.b
// Does A = A - D1 - carry using 16-bit arithmetic
static void compile_sbc_core(struct code_block *block)
{
    // Track for DAA: save old_A and set N=1 (subtraction)
    compile_daa_track_sub(block);

    // Zero-extend operand: andi.w #0xff, D1
    emit_andi_w_dn(block, REG_68K_D_SCRATCH_1, 0x00ff);

    // Zero-extend A into D0: moveq #0, D0; move.b D4, D0
    emit_moveq_dn(block, REG_68K_D_SCRATCH_0, 0);
    emit_move_b_dn_dn(block, REG_68K_D_A, REG_68K_D_SCRATCH_0);

    // Add old borrow to subtrahend
    emit_btst_imm_dn(block, 0, REG_68K_D_FLAGS);
    emit_beq_b(block, 2);
    emit_addq_w_dn(block, REG_68K_D_SCRATCH_1, 1);

    // Subtract: D0 = A - (operand + borrow_in), sets CCR.C if borrow
    emit_sub_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);

    // Save borrow before we clobber CCR
    emit_scc(block, 0x05, REG_68K_D_SCRATCH_1);  // scs D1: D1 = $ff if borrow

    // Store result: move.b D0, D4
    emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_A);

    // Set flags
    emit_tst_b_dn(block, REG_68K_D_A);
    emit_move_sr_dn(block, REG_68K_D_FLAGS); // save Z
    emit_andi_b_dn(block, REG_68K_D_FLAGS, 0xfe); // clear C
    emit_andi_b_dn(block, REG_68K_D_SCRATCH_1, 0x01); // D1 = 0 or 1
    emit_or_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_FLAGS);
}

int compile_alu_op(
    struct code_block *block,
    uint8_t op,
    struct compile_ctx *ctx,
    uint16_t src_address,
    uint16_t *src_ptr
) {
    switch (op) {
    // inc/dec register ops (0xn4, 0xn5, 0xnc, 0xnd where n = 0-3)
    case 0x04: // inc b
        emit_swap(block, REG_68K_D_BC);
        emit_addq_b_dn(block, REG_68K_D_BC, 1);
        compile_set_z_flag(block);
        emit_swap(block, REG_68K_D_BC);
        return 1;

    case 0x05: // dec b
        emit_swap(block, REG_68K_D_BC);
        emit_subq_b_dn(block, REG_68K_D_BC, 1);
        compile_set_z_flag(block);
        emit_swap(block, REG_68K_D_BC);
        return 1;

    case 0x0c: // inc c
        emit_addq_b_dn(block, REG_68K_D_BC, 1);
        compile_set_z_flag(block);
        return 1;

    case 0x0d: // dec c
        emit_subq_b_dn(block, REG_68K_D_BC, 1);
        compile_set_z_flag(block);
        try_fuse_branch(block, ctx, src_ptr, src_address, 0);
        return 1;

    case 0x14: // inc d
        emit_swap(block, REG_68K_D_DE);
        emit_addq_b_dn(block, REG_68K_D_DE, 1);
        compile_set_z_flag(block);
        emit_swap(block, REG_68K_D_DE);
        return 1;

    case 0x15: // dec d
        emit_swap(block, REG_68K_D_DE);
        emit_subq_b_dn(block, REG_68K_D_DE, 1);
        compile_set_z_flag(block);
        emit_swap(block, REG_68K_D_DE);
        return 1;

    case 0x1c: // inc e
        emit_addq_b_dn(block, REG_68K_D_DE, 1);
        compile_set_z_flag(block);
        return 1;

    case 0x1d: // dec e
        emit_subq_b_dn(block, REG_68K_D_DE, 1);
        compile_set_z_flag(block);
        try_fuse_branch(block, ctx, src_ptr, src_address, 0);
        return 1;

    case 0x24: // inc h
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
        emit_addq_b_dn(block, REG_68K_D_SCRATCH_1, 1);
        compile_set_z_flag(block);
        emit_ror_w_8(block, REG_68K_D_SCRATCH_1);
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        return 1;

    case 0x25: // dec h
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
        emit_subq_b_dn(block, REG_68K_D_SCRATCH_1, 1);
        compile_set_z_flag(block);
        emit_ror_w_8(block, REG_68K_D_SCRATCH_1);
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        return 1;

    case 0x2c: // inc l
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_addq_b_dn(block, REG_68K_D_SCRATCH_1, 1);
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        compile_set_z_flag(block);
        return 1;

    case 0x2d: // dec l (movea doesn't affect CCR, so we can fuse)
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_subq_b_dn(block, REG_68K_D_SCRATCH_1, 1);
        emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        compile_set_z_flag(block);
        try_fuse_branch(block, ctx, src_ptr, src_address, 0);
        return 1;

    case 0x34: // inc (hl)
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);
        emit_addq_b_dn(block, 0, 1);
        compile_set_z_flag(block);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_write_d0(block);
        return 1;

    case 0x35: // dec (hl)
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);
        emit_subq_b_dn(block, 0, 1);
        compile_set_z_flag(block);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_write_d0(block);
        return 1;

    case 0x3c: // inc a
        compile_daa_track_add(block);  // Track for DAA (games use INC A + DAA)
        emit_addq_b_dn(block, REG_68K_D_A, 1);
        compile_set_z_flag(block);
        try_fuse_branch(block, ctx, src_ptr, src_address, 0);
        return 1;

    case 0x3d: // dec a
        compile_daa_track_sub(block);  // Track for DAA (games use DEC A + DAA)
        emit_subq_b_dn(block, REG_68K_D_A, 1);
        compile_set_z_flag(block);
        try_fuse_branch(block, ctx, src_ptr, src_address, 0);
        return 1;

    // misc ALU ops
    case 0x27: // daa - decimal adjust accumulator
        compile_daa(block);
        return 1;

    case 0x2f: // cpl - complement A
        emit_not_b_dn(block, REG_68K_D_A);
        return 1;

    case 0x37: // scf - set carry flag
        emit_ori_b_dn(block, REG_68K_D_FLAGS, 0x01);
        return 1;

    case 0x3f: // ccf - complement carry flag
        emit_eor_b_imm_dn(block, 0x01, REG_68K_D_FLAGS);
        return 1;

    // 8-bit ALU register ops (0x80-0xbf)
    case 0x80: // add a, b
        compile_daa_track_add(block);
        emit_swap(block, REG_68K_D_BC);
        emit_add_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_A);
        compile_set_zc_flags(block);  // capture before swap clobbers CCR
        emit_swap(block, REG_68K_D_BC);
        return 1;

    case 0x81: // add a, c
        compile_daa_track_add(block);
        emit_add_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0x82: // add a, d
        compile_daa_track_add(block);
        emit_swap(block, REG_68K_D_DE);
        emit_add_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_A);
        compile_set_zc_flags(block);  // capture before swap clobbers CCR
        emit_swap(block, REG_68K_D_DE);
        return 1;

    case 0x83: // add a, e
        compile_daa_track_add(block);
        emit_add_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0x84: // add a, h
        compile_daa_track_add(block);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
        emit_add_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0x85: // add a, l
        compile_daa_track_add(block);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_add_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0x86: // add a, (hl)
        compile_daa_track_add(block);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);
        emit_add_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0x87: // add a, a
        compile_daa_track_add(block);
        emit_add_b_dn_dn(block, REG_68K_D_A, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    // adc a, r (0x88-0x8f)
    case 0x88: // adc a, b
        emit_swap(block, REG_68K_D_BC);
        emit_move_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_1);
        emit_swap(block, REG_68K_D_BC);
        compile_adc_core(block);
        return 1;

    case 0x89: // adc a, c
        emit_move_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_1);
        compile_adc_core(block);
        return 1;

    case 0x8a: // adc a, d
        emit_swap(block, REG_68K_D_DE);
        emit_move_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_SCRATCH_1);
        emit_swap(block, REG_68K_D_DE);
        compile_adc_core(block);
        return 1;

    case 0x8b: // adc a, e
        emit_move_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_SCRATCH_1);
        compile_adc_core(block);
        return 1;

    case 0x8c: // adc a, h
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
        compile_adc_core(block);
        return 1;

    case 0x8d: // adc a, l
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_adc_core(block);
        return 1;

    case 0x8e: // adc a, (hl)
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);
        compile_adc_core(block);
        return 1;

    case 0x8f: // adc a, a
        emit_move_b_dn_dn(block, REG_68K_D_A, REG_68K_D_SCRATCH_1);
        compile_adc_core(block);
        return 1;

    case 0x90: // sub a, b
        compile_daa_track_sub(block);
        emit_swap(block, REG_68K_D_BC);
        emit_sub_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_A);
        compile_set_zc_flags(block);  // capture before swap clobbers CCR
        emit_swap(block, REG_68K_D_BC);
        return 1;

    case 0x91: // sub a, c
        compile_daa_track_sub(block);
        emit_sub_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0x92: // sub a, d
        compile_daa_track_sub(block);
        emit_swap(block, REG_68K_D_DE);
        emit_sub_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_A);
        compile_set_zc_flags(block);  // capture before swap clobbers CCR
        emit_swap(block, REG_68K_D_DE);
        return 1;

    case 0x93: // sub a, e
        compile_daa_track_sub(block);
        emit_sub_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0x94: // sub a, h
        compile_daa_track_sub(block);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
        emit_sub_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0x95: // sub a, l
        compile_daa_track_sub(block);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_sub_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0x96: // sub a, (hl)
        compile_daa_track_sub(block);
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);
        emit_sub_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0x97: // sub a, a - always results in 0
        compile_daa_track_sub(block);
        emit_moveq_dn(block, REG_68K_D_A, 0);
        compile_set_zc_flags(block);
        return 1;

    // sbc a, r (0x98-0x9f)
    case 0x98: // sbc a, b
        emit_swap(block, REG_68K_D_BC);
        emit_move_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_1);
        emit_swap(block, REG_68K_D_BC);
        compile_sbc_core(block);
        return 1;

    case 0x99: // sbc a, c
        emit_move_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_SCRATCH_1);
        compile_sbc_core(block);
        return 1;

    case 0x9a: // sbc a, d
        emit_swap(block, REG_68K_D_DE);
        emit_move_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_SCRATCH_1);
        emit_swap(block, REG_68K_D_DE);
        compile_sbc_core(block);
        return 1;

    case 0x9b: // sbc a, e
        emit_move_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_SCRATCH_1);
        compile_sbc_core(block);
        return 1;

    case 0x9c: // sbc a, h
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
        compile_sbc_core(block);
        return 1;

    case 0x9d: // sbc a, l
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_sbc_core(block);
        return 1;

    case 0x9e: // sbc a, (hl)
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);
        emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);
        compile_sbc_core(block);
        return 1;

    case 0x9f: // sbc a, a
        emit_move_b_dn_dn(block, REG_68K_D_A, REG_68K_D_SCRATCH_1);
        compile_sbc_core(block);
        return 1;

    case 0xa0: // and a, b
        emit_swap(block, REG_68K_D_BC);
        emit_and_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_A);
        compile_set_zc_flags(block);
        emit_swap(block, REG_68K_D_BC);
        return 1;

    case 0xa1: // and a, c
        emit_and_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0xa2: // and a, d
        emit_swap(block, REG_68K_D_DE);
        emit_and_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_A);
        compile_set_zc_flags(block);
        emit_swap(block, REG_68K_D_DE);
        return 1;

    case 0xa3: // and a, e
        emit_and_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0xa4: // and a, h
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
        emit_and_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0xa5: // and a, l
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_and_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0xa6: // and a, (hl)
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);
        emit_and_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0xa7: // and a, a - set flags based on A
        emit_tst_b_dn(block, REG_68K_D_A);
        compile_set_zc_flags(block);
        try_fuse_branch(block, ctx, src_ptr, src_address, 0);
        return 1;

    case 0xa8: // xor a, b
        emit_swap(block, REG_68K_D_BC);
        emit_eor_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_A);
        compile_set_zc_flags(block);
        emit_swap(block, REG_68K_D_BC);
        return 1;

    case 0xa9: // xor a, c
        emit_eor_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0xaa: // xor a, d
        emit_swap(block, REG_68K_D_DE);
        emit_eor_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_A);
        compile_set_zc_flags(block);
        emit_swap(block, REG_68K_D_DE);
        return 1;

    case 0xab: // xor a, e
        emit_eor_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0xac: // xor a, h
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
        emit_eor_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0xad: // xor a, l
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_eor_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0xae: // xor a, (hl)
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);
        emit_eor_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0xaf: // xor a, a - always results in 0, Z=1
        emit_moveq_dn(block, REG_68K_D_A, 0);
        compile_set_zc_flags(block);
        return 1;

    case 0xb0: // or a, b
        emit_swap(block, REG_68K_D_BC);
        emit_or_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_A);
        compile_set_zc_flags(block);
        emit_swap(block, REG_68K_D_BC);
        return 1;

    case 0xb1: // or a, c
        emit_or_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0xb2: // or a, d
        emit_swap(block, REG_68K_D_DE);
        emit_or_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_A);
        compile_set_zc_flags(block);
        emit_swap(block, REG_68K_D_DE);
        return 1;

    case 0xb3: // or a, e
        emit_or_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0xb4: // or a, h
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
        emit_or_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0xb5: // or a, l
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_or_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0xb6: // or a, (hl)
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);
        emit_or_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_A);
        compile_set_zc_flags(block);
        return 1;

    case 0xb7: // or a, a - set flags based on A
        emit_tst_b_dn(block, REG_68K_D_A);
        compile_set_zc_flags(block);
        try_fuse_branch(block, ctx, src_ptr, src_address, 0);
        return 1;

    case 0xb8: // cp a, b
        // cannot fuse branch because it needs to swap back BC :/
        emit_swap(block, REG_68K_D_BC);
        emit_cmp_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_A);
        compile_set_zc_flags(block);
        emit_swap(block, REG_68K_D_BC);
        return 1;

    case 0xb9: // cp a, c
        emit_cmp_b_dn_dn(block, REG_68K_D_BC, REG_68K_D_A);
        compile_set_zc_flags(block);
        try_fuse_branch(block, ctx, src_ptr, src_address, 1);
        return 1;

    case 0xba: // cp a, d 
        // can't fuse
        emit_swap(block, REG_68K_D_DE);
        emit_cmp_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_A);
        compile_set_zc_flags(block);
        emit_swap(block, REG_68K_D_DE);
        return 1;

    case 0xbb: // cp a, e
        emit_cmp_b_dn_dn(block, REG_68K_D_DE, REG_68K_D_A);
        compile_set_zc_flags(block);
        try_fuse_branch(block, ctx, src_ptr, src_address, 1);
        return 1;

    case 0xbc: // cp a, h
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
        emit_cmp_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_A);
        compile_set_zc_flags(block);
        try_fuse_branch(block, ctx, src_ptr, src_address, 1);
        return 1;

    case 0xbd: // cp a, l
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        emit_cmp_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_A);
        compile_set_zc_flags(block);
        try_fuse_branch(block, ctx, src_ptr, src_address, 1);
        return 1;

    case 0xbe: // cp a, (hl)
        emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
        compile_call_dmg_read(block);
        emit_cmp_b_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_A);
        compile_set_zc_flags(block);
        try_fuse_branch(block, ctx, src_ptr, src_address, 1);
        return 1;

    case 0xbf: // cp a, a - always Z=1, N=1, H=0, C=0
        emit_moveq_dn(block, REG_68K_D_FLAGS, 0x04);
        return 1;

    // immediate ALU ops (0xc6, 0xce, 0xd6, 0xde, 0xe6, 0xee, 0xf6, 0xfe)
    case 0xc6: // add a, #imm
        compile_daa_track_add(block);
        emit_addi_b_dn(block, REG_68K_D_A, READ_BYTE(*src_ptr));
        (*src_ptr)++;
        compile_set_zc_flags(block);
        return 1;

    case 0xce: // adc a, #imm
        emit_move_b_dn(block, REG_68K_D_SCRATCH_1, READ_BYTE(*src_ptr));
        (*src_ptr)++;
        compile_adc_core(block);
        return 1;

    case 0xd6: // sub a, #imm
        compile_daa_track_sub(block);
        emit_subi_b_dn(block, REG_68K_D_A, READ_BYTE(*src_ptr));
        (*src_ptr)++;
        compile_set_zc_flags(block);
        return 1;

    case 0xde: // sbc a, #imm
        emit_move_b_dn(block, REG_68K_D_SCRATCH_1, READ_BYTE(*src_ptr));
        (*src_ptr)++;
        compile_sbc_core(block);
        return 1;

    case 0xe6: // and a, #nn
        emit_andi_b_dn(block, REG_68K_D_A, READ_BYTE(*src_ptr));
        (*src_ptr)++;
        compile_set_zc_flags(block);
        return 1;

    case 0xee: // xor a, u8
        emit_eor_b_imm_dn(block, READ_BYTE(*src_ptr), REG_68K_D_A);
        (*src_ptr)++;
        compile_set_zc_flags(block);
        return 1;

    case 0xf6: // or a, #imm
        emit_ori_b_dn(block, REG_68K_D_A, READ_BYTE(*src_ptr));
        (*src_ptr)++;
        compile_set_zc_flags(block);
        return 1;

    case 0xfe: // cp a, imm8
        emit_cmp_b_imm_dn(block, REG_68K_D_A, READ_BYTE(*src_ptr));
        (*src_ptr)++;
        compile_set_zc_flags(block);
        try_fuse_branch(block, ctx, src_ptr, src_address, 1);
        return 1;

    default:
        return 0;
    }
}
