#include <stdint.h>
#include "compiler.h"
#include "branches.h"
#include "emitters.h"

// helper for reading GB memory during compilation
#define READ_BYTE(off) (ctx->read(ctx->dmg, src_address + (off)))

// Map GB conditional branch opcode to 68k condition code
// Returns COND_NONE if not a conditional branch
int get_branch_condition(uint8_t opcode)
{
    switch (opcode) {
    case 0x20: return COND_NE; // jr nz
    case 0x28: return COND_EQ; // jr z
    case 0x30: return COND_CC; // jr nc
    case 0x38: return COND_CS; // jr c
    case 0xc0: return COND_NE; // ret nz
    case 0xc2: return COND_NE; // jp nz
    case 0xc8: return COND_EQ; // ret z
    case 0xca: return COND_EQ; // jp z
    case 0xd0: return COND_CC; // ret nc
    case 0xd2: return COND_CC; // jp nc
    case 0xd8: return COND_CS; // ret c
    case 0xda: return COND_CS; // jp c
    case 0xc4: return COND_NE; // call nz
    case 0xcc: return COND_EQ; // call z
    case 0xd4: return COND_CC; // call nc
    case 0xdc: return COND_CS; // call c
    default:   return COND_NONE;
    }
}

// Invert condition for "skip if NOT taken" branches
static int invert_cond(int cond)
{
    switch (cond) {
    case COND_EQ: return COND_NE;
    case COND_NE: return COND_EQ;
    case COND_CS: return COND_CC;
    case COND_CC: return COND_CS;
    default:      return cond;
    }
}

// returns 1 if jr ended the block, 0 if it's a backward jump within block
int compile_jr(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t *src_ptr,
    uint16_t src_address
) {
    int8_t disp;
    int16_t target_gb_offset;
    uint16_t target_m68k, target_gb_pc;
    int16_t m68k_disp;

    disp = (int8_t) READ_BYTE(*src_ptr);
    (*src_ptr)++;

    // jr displacement is relative to PC after the jr instruction
    // *src_ptr now points to the byte after jr, so target = *src_ptr + disp
    target_gb_offset = (int16_t) *src_ptr + disp;

    // Check if this is a backward jump to a location we've already compiled
    if (target_gb_offset >= 0 && target_gb_offset < (int16_t) (*src_ptr - 2)) {
        // Backward jump within block
        target_gb_pc = src_address + target_gb_offset;
        target_m68k = block->m68k_offsets[target_gb_offset];
        m68k_disp = (int16_t) target_m68k - (int16_t) (block->length + 2);

        // Register mid-block entry point for this branch target
        if (ctx->cache_store) {
            void *code_ptr = (void *) (block->code + target_m68k);
            ctx->cache_store(target_gb_pc, ctx->current_bank, code_ptr);
        }

        // Tiny loops (disp >= -3, e.g. "dec a; jr nz") are pure computation
        // (no room for memory access + flag-setting instruction).
        // Skip interrupt check to avoid overhead killing performance.
        if (disp >= -3) {
            emit_bra_w(block, m68k_disp);
            return 0;
        }

        // Larger loop - check cycle count, exit to dispatcher if >= scanline
        // cmpi.l #cycles_per_exit, d2
        emit_cmpi_l_imm_dn(block, cycles_per_exit, REG_68K_D_CYCLE_COUNT);

        // bcs.w over exit sequence to bra.w (skip moveq(2) + move.w(4) + patchable_exit(16) = 22, plus 2 = 24)
        // bcs = branch if carry set = branch if cycles < cycles_per_exit
        emit_bcs_w(block, 24);
        // Exit to dispatcher with target PC
        emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
        emit_move_w_dn(block, REG_68K_D_NEXT_PC, target_gb_pc);
        emit_patchable_exit(block);

        // Native branch (cycles < scanline boundary)
        // Must recompute displacement since block->length changed
        m68k_disp = (int16_t) target_m68k - (int16_t) (block->length + 2);
        emit_bra_w(block, m68k_disp);
        return 0;
    }

    // Forward jump or outside block - go through patchable exit
    target_gb_pc = src_address + target_gb_offset;
    emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
    emit_move_w_dn(block, REG_68K_D_NEXT_PC, target_gb_pc);
    emit_patchable_exit(block);
    return 1;
}

// Compile conditional relative jump (jr nz, jr z, jr nc, jr c)
// flag_bit: which bit in D7 to test (2=Z, 0=C)
// branch_if_set: if true, branch when flag is set; if false, branch when clear
void compile_jr_cond(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t *src_ptr,
    uint16_t src_address,
    uint8_t flag_bit,
    int branch_if_set
) {
    int8_t disp;
    int16_t target_gb_offset;
    uint16_t target_m68k, target_gb_pc;
    int16_t m68k_disp;

    disp = (int8_t) READ_BYTE(*src_ptr);
    (*src_ptr)++;

    target_gb_offset = (int16_t) *src_ptr + disp;

    // Test the flag bit in D7
    // btst sets 68k Z=1 if tested bit is 0, Z=0 if tested bit is 1
    emit_btst_imm_dn(block, flag_bit, REG_68K_D_FLAGS);

    // Check if this is a backward jump within block
    if (target_gb_offset >= 0 && target_gb_offset < (int16_t) (*src_ptr - 2)) {
        // Backward jump - check condition, then maybe interrupt flag
        target_gb_pc = src_address + target_gb_offset;
        target_m68k = block->m68k_offsets[target_gb_offset];
        m68k_disp = (int16_t) target_m68k - (int16_t) (block->length + 2);

        // Register mid-block entry point for this branch target
        if (ctx->cache_store) {
            void *code_ptr = (void *) (block->code + target_m68k);
            ctx->cache_store(target_gb_pc, ctx->current_bank, code_ptr);
        }

        // Tiny loops (disp >= -3): skip interrupt check, just branch
        if (disp >= -3) {
            // bxx.w displacement needs adjustment for where we emit it
            int16_t cond_disp = (int16_t) target_m68k - (int16_t) (block->length + 2);
            if (branch_if_set) {
                emit_bne_w(block, cond_disp);
            } else {
                emit_beq_w(block, cond_disp);
            }
            return;
        }

        // Larger loop - check condition, then cycle count
        // Structure:
        //   btst #flag_bit, d7           ; already emitted above
        //   bne/beq .check_cycles        ; if condition met, check cycles
        //   bra.w .fall_through          ; condition not met, skip all
        // .check_cycles:
        //   cmpi.l #cycles_per_exit, d2
        //   bcs.w loop_target            ; cycles < cycles_per_exit, do native branch
        //   moveq #0, d0                 ; cycles >= cycles_per_exit, exit
        //   move.w #target, d0
        //   patchable_exit
        // .fall_through:

        // Sizes: bne/beq(4) + bra.w(4) + cmpi.l(6) + bcs.w(4) + moveq(2) + move.w(4) + patchable_exit(16) = 40
        // .check_cycles is at +8 from first branch
        // .fall_through is at +40 from first branch

        if (branch_if_set) {
            // Branch if flag is set: btst gives Z=0 when bit=1, so use bne
            emit_bne_w(block, 6);  // skip to .check_cycles (+8, but PC is at +2)
        } else {
            // Branch if flag is clear: btst gives Z=1 when bit=0, so use beq
            emit_beq_w(block, 6);
        }

        // bra.w to .fall_through (cmpi.l(6) + bcs.w(4) + moveq(2) + move.w(4) + patchable_exit(16) = 32, plus 2 for PC = 34)
        emit_bra_w(block, 34);

        // .check_cycles:
        emit_cmpi_l_imm_dn(block, cycles_per_exit, REG_68K_D_CYCLE_COUNT);

        // bcs.w to native loop target (cycles < cycles_per_exit)
        m68k_disp = (int16_t) target_m68k - (int16_t) (block->length + 2);
        emit_bcs_w(block, m68k_disp);

        // Exit to dispatcher (cycles >= cycles_per_exit)
        emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
        emit_move_w_dn(block, REG_68K_D_NEXT_PC, target_gb_pc);
        emit_patchable_exit(block);

        // .fall_through: block continues
        return;
    }

    // Forward/external jump - conditionally exit via patchable exit
    // If condition NOT met, skip the exit sequence
    target_gb_pc = src_address + target_gb_offset;

    if (branch_if_set) {
        // Skip exit if flag is clear (btst Z=1 when bit=0)
        emit_beq_w(block, 24);  // skip: moveq(2) + move.w(4) + patchable_exit(16) = 22, plus 2 = 24
    } else {
        // Skip exit if flag is set (btst Z=0 when bit=1)
        emit_bne_w(block, 24);
    }

    emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
    emit_move_w_dn(block, REG_68K_D_NEXT_PC, target_gb_pc);
    emit_patchable_exit(block);
}

// Compile conditional absolute jump (jp nz, jp z, jp nc, jp c)
// flag_bit: which bit in D7 to test (2=Z, 0=C)
// branch_if_set: if true, branch when flag is set; if false, branch when clear
void compile_jp_cond(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t *src_ptr,
    uint16_t src_address,
    uint8_t flag_bit,
    int branch_if_set
) {
    uint16_t target = READ_BYTE(*src_ptr) | (READ_BYTE(*src_ptr + 1) << 8);
    *src_ptr += 2;

    // Test the flag bit in D7
    emit_btst_imm_dn(block, flag_bit, REG_68K_D_FLAGS);

    // If condition NOT met, skip the exit sequence
    if (branch_if_set) {
        // Skip exit if flag is clear (btst Z=1 when bit=0)
        emit_beq_w(block, 24);  // skip: moveq(2) + move.w(4) + patchable_exit(16) = 22, plus 2 = 24
    } else {
        // Skip exit if flag is set (btst Z=0 when bit=1)
        emit_bne_w(block, 24);
    }

    emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
    emit_move_w_dn(block, REG_68K_D_NEXT_PC, target);
    emit_patchable_exit(block);
}

void compile_call_imm16(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t *src_ptr,
    uint16_t src_address
) {
    uint16_t target = READ_BYTE(*src_ptr) | (READ_BYTE(*src_ptr + 1) << 8);
    uint16_t ret_addr = src_address + *src_ptr + 2;  // address after call
    *src_ptr += 2;

    // push return address (A3 = virtual address)
    emit_subq_w_an(block, REG_68K_A_SP, 2);
    emit_move_w_dn(block, REG_68K_D_SCRATCH_1, ret_addr);
    emit_move_b_dn_ind_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_SP);
    emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
    emit_move_b_dn_disp_an(block, REG_68K_D_SCRATCH_1, 1, REG_68K_A_SP);

    // jump to target
    emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
    emit_move_w_dn(block, REG_68K_D_NEXT_PC, target);
    emit_patchable_exit(block);
}

// Compile conditional call (call nz, call z, call nc, call c)
// flag_bit: which bit in D7 to test (2=Z, 0=C)
// branch_if_set: if true, call when flag is set; if false, call when clear
void compile_call_cond(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t *src_ptr,
    uint16_t src_address,
    uint8_t flag_bit,
    int branch_if_set
) {
    uint16_t target = READ_BYTE(*src_ptr) | (READ_BYTE(*src_ptr + 1) << 8);
    uint16_t ret_addr = src_address + *src_ptr + 2;  // address after call
    *src_ptr += 2;

    // Test the flag bit in D7
    emit_btst_imm_dn(block, flag_bit, REG_68K_D_FLAGS);

    // If condition NOT met, skip the call sequence
    // call sequence: subq(2) + move.w(4) + move.b(2) + rol(2) + move.b(4) +
    //                moveq(2) + move.w(4) + patchable_exit(16) = 36, +2 = 38
    if (branch_if_set) {
        emit_beq_w(block, 38);
    } else {
        emit_bne_w(block, 38);
    }

    // Push return address
    emit_subq_w_an(block, REG_68K_A_SP, 2);
    emit_move_w_dn(block, REG_68K_D_SCRATCH_1, ret_addr);
    emit_move_b_dn_ind_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_SP);
    emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
    emit_move_b_dn_disp_an(block, REG_68K_D_SCRATCH_1, 1, REG_68K_A_SP);

    // Jump to target
    emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
    emit_move_w_dn(block, REG_68K_D_NEXT_PC, target);
    emit_patchable_exit(block);
}

void compile_ret(struct code_block *block)
{
    // pop return address from stack (A3 = virtual address)
    emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
    emit_move_b_disp_an_dn(block, 1, REG_68K_A_SP, REG_68K_D_NEXT_PC);
    emit_rol_w_8(block, REG_68K_D_NEXT_PC);
    emit_move_b_ind_an_dn(block, REG_68K_A_SP, REG_68K_D_NEXT_PC);
    emit_addq_w_an(block, REG_68K_A_SP, 2);
    emit_dispatch_jump(block);
}

// Compile conditional return (ret nz, ret z, ret nc, ret c)
// flag_bit: which bit in D7 to test (2=Z, 0=C)
// branch_if_set: if true, return when flag is set; if false, return when clear
void compile_ret_cond(struct code_block *block, uint8_t flag_bit, int branch_if_set)
{
    // Test the flag bit in D7
    emit_btst_imm_dn(block, flag_bit, REG_68K_D_FLAGS);

    // If condition NOT met, skip the return sequence
    // ret sequence: moveq(2) + move.b(4) + rol(2) + move.b(2) + addq(2) +
    //               dispatch_jump(6) = 18, +2 = 20
    if (branch_if_set) {
        emit_beq_w(block, 20);
    } else {
        emit_bne_w(block, 20);
    }

    // Pop return address and dispatch
    emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
    emit_move_b_disp_an_dn(block, 1, REG_68K_A_SP, REG_68K_D_NEXT_PC);
    emit_rol_w_8(block, REG_68K_D_NEXT_PC);
    emit_move_b_ind_an_dn(block, REG_68K_A_SP, REG_68K_D_NEXT_PC);
    emit_addq_w_an(block, REG_68K_A_SP, 2);
    emit_dispatch_jump(block);
}

void compile_rst_n(struct code_block *block, uint8_t target, uint16_t ret_addr)
{
    // push return address
    emit_subq_w_an(block, REG_68K_A_SP, 2);
    emit_move_w_dn(block, REG_68K_D_SCRATCH_1, ret_addr);
    emit_move_b_dn_ind_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_SP);
    emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
    emit_move_b_dn_disp_an(block, REG_68K_D_SCRATCH_1, 1, REG_68K_A_SP);

    // jump to target (0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38)
    emit_moveq_dn(block, REG_68K_D_NEXT_PC, target);
    emit_patchable_exit(block);
}

// fused branches start here. all these avoid is a "btst", but it doesn't really
// increase the complexity of the compiler, so i think they can stay...

// Fused jr cond - uses live CCR flags from preceding ALU op
// cond is 68k condition code (COND_EQ, COND_NE, COND_CS, COND_CC)
int compile_jr_cond_fused(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t *src_ptr,
    uint16_t src_address,
    int cond
) {
    int8_t disp;
    int16_t target_gb_offset;
    uint16_t target_m68k, target_gb_pc;
    int16_t m68k_disp;

    disp = (int8_t) READ_BYTE(*src_ptr);
    (*src_ptr)++;

    target_gb_offset = (int16_t) *src_ptr + disp;

    // Check if this is a backward jump within block
    if (target_gb_offset >= 0 && target_gb_offset < (int16_t) (*src_ptr - 2)) {
        target_gb_pc = src_address + target_gb_offset;
        target_m68k = block->m68k_offsets[target_gb_offset];

        // Register mid-block entry point for this branch target
        if (ctx->cache_store) {
            void *code_ptr = (void *) (block->code + target_m68k);
            ctx->cache_store(target_gb_pc, ctx->current_bank, code_ptr);
        }

        // Tiny loops: skip cycle check
        if (disp >= -3) {
            m68k_disp = (int16_t) target_m68k - (int16_t) (block->length + 2);
            emit_bcc_opcode_w(block, cond, m68k_disp);
            return 0;
        }

        // Larger loop - check condition, then cycle count
        // Structure:
        //   bcc.w .check_cycles      ; if condition met
        //   bra.w .fall_through      ; condition not met
        // .check_cycles:
        //   cmpi.l #cycles_per_exit, d2
        //   bcs.w loop_target        ; cycles < cycles_per_exit
        //   <exit via patchable_exit>
        // .fall_through:

        // Branch to check_cycles if condition met
        emit_bcc_opcode_w(block, cond, 6);

        // bra.w to .fall_through (cmpi.l(6) + bcs.w(4) + exit(22) = 32, plus 2 = 34)
        emit_bra_w(block, 34);

        // .check_cycles:
        emit_cmpi_l_imm_dn(block, cycles_per_exit, REG_68K_D_CYCLE_COUNT);

        // bcs.w to native loop target (cycles < cycles_per_exit)
        m68k_disp = (int16_t) target_m68k - (int16_t) (block->length + 2);
        emit_bcs_w(block, m68k_disp);

        // Exit via patchable exit (cycles >= cycles_per_exit)
        emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
        emit_move_w_dn(block, REG_68K_D_NEXT_PC, target_gb_pc);
        emit_patchable_exit(block);

        return 0;
    }

    // Forward/external jump - conditionally exit via patchable exit
    target_gb_pc = src_address + target_gb_offset;

    // Skip exit if condition NOT met (skip: moveq(2) + move.w(4) + patchable_exit(16) = 22, +2 = 24)
    emit_bcc_opcode_w(block, invert_cond(cond), 24);

    emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
    emit_move_w_dn(block, REG_68K_D_NEXT_PC, target_gb_pc);
    emit_patchable_exit(block);
    return 0;  // doesn't end block - fall through continues
}

// Fused jp cond - uses live CCR flags
void compile_jp_cond_fused(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t *src_ptr,
    uint16_t src_address,
    int cond
) {
    uint16_t target = READ_BYTE(*src_ptr) | (READ_BYTE(*src_ptr + 1) << 8);
    *src_ptr += 2;

    // Skip exit if condition NOT met (skip: moveq(2) + move.w(4) + patchable_exit(16) = 22, +2 = 24)
    emit_bcc_opcode_w(block, invert_cond(cond), 24);

    emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
    emit_move_w_dn(block, REG_68K_D_NEXT_PC, target);
    emit_patchable_exit(block);
}

// Fused ret cond - uses live CCR flags
void compile_ret_cond_fused(struct code_block *block, int cond)
{
    // Skip return if condition NOT met
    // ret sequence: moveq(2) + move.b(4) + rol(2) + move.b(2) + addq(2) +
    //               dispatch_jump(6) = 18, +2 = 20
    emit_bcc_opcode_w(block, invert_cond(cond), 20);

    // Pop return address and dispatch
    emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
    emit_move_b_disp_an_dn(block, 1, REG_68K_A_SP, REG_68K_D_NEXT_PC);
    emit_rol_w_8(block, REG_68K_D_NEXT_PC);
    emit_move_b_ind_an_dn(block, REG_68K_A_SP, REG_68K_D_NEXT_PC);
    emit_addq_w_an(block, REG_68K_A_SP, 2);
    emit_dispatch_jump(block);
}

// Fused call cond - uses live CCR flags
void compile_call_cond_fused(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t *src_ptr,
    uint16_t src_address,
    int cond
) {
    uint16_t target = READ_BYTE(*src_ptr) | (READ_BYTE(*src_ptr + 1) << 8);
    uint16_t ret_addr = src_address + *src_ptr + 2;
    *src_ptr += 2;

    // Skip call if condition NOT met
    // call sequence: subq(2) + move.w(4) + move.b(2) + rol(2) + move.b(4) +
    //                moveq(2) + move.w(4) + patchable_exit(16) = 36, +2 = 38
    emit_bcc_opcode_w(block, invert_cond(cond), 38);

    // Push return address
    emit_subq_w_an(block, REG_68K_A_SP, 2);
    emit_move_w_dn(block, REG_68K_D_SCRATCH_1, ret_addr);
    emit_move_b_dn_ind_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_SP);
    emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
    emit_move_b_dn_disp_an(block, REG_68K_D_SCRATCH_1, 1, REG_68K_A_SP);

    // Jump to target
    emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
    emit_move_w_dn(block, REG_68K_D_NEXT_PC, target);
    emit_patchable_exit(block);
}