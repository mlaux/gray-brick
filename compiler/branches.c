#include <stdint.h>
#include "compiler.h"
#include "branches.h"
#include "emitters.h"
#include "stack.h"

// helper for reading GB memory during compilation
#define READ_BYTE(off) (ctx->read(ctx->dmg, src_address + (off)))

void compile_jr(
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
        target_m68k = m68k_offsets[target_gb_offset];
        m68k_disp = (int16_t) target_m68k - (int16_t) (block->length + 2);

        // Register mid-block entry point for this branch target
        if (ctx->cache_store) {
            void *code_ptr = (void *) (block->code + target_m68k);
            ctx->cache_store(target_gb_pc, ctx->current_bank, code_ptr);
        }

        flush_cycles(block);

        // Tiny loops (disp >= -3, e.g. "dec a; jr nz") are pure computation
        // (no room for memory access + flag-setting instruction).
        // Skip interrupt check to avoid overhead killing performance.
        if (disp >= -3) {
            m68k_disp = (int16_t) target_m68k - (int16_t) (block->length + 2);
            emit_bra_w(block, m68k_disp);
            return;
        }

        // Larger loop - the flush above subtracted from D2 and set the
        // flags: budget remains while D2 > 0 (pending >= 12 at a jr, so
        // the flush always emits)
        size_t skip = block->length;
        emit_bgt_b(block, 0);
        // Exit to dispatcher with target PC
        emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
        emit_move_w_dn(block, REG_68K_D_NEXT_PC, target_gb_pc);
        emit_block_exit(block, target_gb_pc);

        // Native branch (cycles < scanline boundary)
        // Recompute displacement since block->length changed
        patch_branch_b(block, skip);
        m68k_disp = (int16_t) target_m68k - (int16_t) (block->length + 2);
        emit_bra_w(block, m68k_disp);
        return;
    }

    // Forward jump or outside block - go through patchable exit
    target_gb_pc = src_address + target_gb_offset;
    flush_cycles(block);
    emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
    emit_move_w_dn(block, REG_68K_D_NEXT_PC, target_gb_pc);
    emit_block_exit(block, target_gb_pc);
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
        target_m68k = m68k_offsets[target_gb_offset];

        // Register mid-block entry point for this branch target
        if (ctx->cache_store) {
            void *code_ptr = (void *) (block->code + target_m68k);
            ctx->cache_store(target_gb_pc, ctx->current_bank, code_ptr);
        }

        // Tiny loops (disp >= -3): skip interrupt check, just branch
        if (disp >= -3) {
            // Skip if NOT taken
            size_t skip = block->length;
            if (branch_if_set) {
                emit_beq_b(block, 0);
            } else {
                emit_bne_b(block, 0);
            }
            // taken path: materialize pending + taken extra, loop head is
            // at pending == 0. fall-through keeps deferring
            emit_add_cycles(block, pending_cycles + 4);
            m68k_disp = (int16_t) target_m68k - (int16_t) (block->length + 2);
            emit_bra_w(block, m68k_disp);
            patch_branch_b(block, skip);
            return;
        }

        // Larger loop - check condition, then cycle count
        // Structure:
        //   btst #flag_bit, d7           ; already emitted above
        //   bne/beq .check_cycles        ; if condition met, check cycles
        //   bra.b .fall_through          ; condition not met, skip all
        // .check_cycles:
        //   add pending + 4 to d2
        //   cmp.l JIT_CTX_WAKE_LIMIT(a4), d2
        //   bcs.w loop_target            ; cycles < exit budget, do native branch
        //   moveq #0, d0                 ; cycles >= exit budget, exit
        //   move.w #target, d0
        //   patchable_exit
        // .fall_through:

        size_t cond = block->length;
        if (branch_if_set) {
            // Branch if flag is set: btst gives Z=0 when bit=1, so use bne
            emit_bne_b(block, 0);  // skip the bra.b to .check_cycles
        } else {
            // Branch if flag is clear: btst gives Z=1 when bit=0, so use beq
            emit_beq_b(block, 0);
        }

        // bra.b to .fall_through
        size_t fall = block->length;
        emit_bra_b(block, 0);

        // .check_cycles:
        patch_branch_b(block, cond);
        // the charge subtracts from D2 and sets the flags: budget
        // remains while D2 > 0
        emit_add_cycles(block, pending_cycles + 4);

        // bgt.w to native loop target (budget remains)
        m68k_disp = (int16_t) target_m68k - (int16_t) (block->length + 2);
        emit_bgt_w(block, m68k_disp);

        // Exit to dispatcher (cycles >= exit budget)
        emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
        emit_move_w_dn(block, REG_68K_D_NEXT_PC, target_gb_pc);
        emit_block_exit(block, target_gb_pc);

        // .fall_through: block continues deferring
        patch_branch_b(block, fall);
        return;
    }

    // Forward/external jump - conditionally exit via patchable exit
    // If condition NOT met, skip the exit sequence
    target_gb_pc = src_address + target_gb_offset;

    size_t skip = block->length;
    if (branch_if_set) {
        // Skip exit if flag is clear (btst Z=1 when bit=0)
        emit_beq_b(block, 0);
    } else {
        // Skip exit if flag is set (btst Z=0 when bit=1)
        emit_bne_b(block, 0);
    }

    emit_add_cycles(block, pending_cycles + 4);  // pending + taken extra
    emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
    emit_move_w_dn(block, REG_68K_D_NEXT_PC, target_gb_pc);
    emit_block_exit(block, target_gb_pc);

    patch_branch_b(block, skip);
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
    size_t skip;
    *src_ptr += 2;

    // Test the flag bit in D7
    emit_btst_imm_dn(block, flag_bit, REG_68K_D_FLAGS);

    // If condition NOT met, skip the exit sequence
    skip = block->length;
    if (branch_if_set) {
        // Skip exit if flag is clear (btst Z=1 when bit=0)
        emit_beq_b(block, 0);
    } else {
        // Skip exit if flag is set (btst Z=0 when bit=1)
        emit_bne_b(block, 0);
    }

    emit_add_cycles(block, pending_cycles + 4);  // pending + taken extra
    emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
    emit_move_w_dn(block, REG_68K_D_NEXT_PC, target);
    emit_block_exit(block, target);

    patch_branch_b(block, skip);
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

    flush_cycles(block);
    compile_push_imm16(block, ret_addr);

    // jump to target
    emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
    emit_move_w_dn(block, REG_68K_D_NEXT_PC, target);
    emit_block_exit(block, target);
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
    size_t skip;
    int saved;
    *src_ptr += 2;

    // Test the flag bit in D7
    emit_btst_imm_dn(block, flag_bit, REG_68K_D_FLAGS);

    // If condition NOT met, skip the call sequence
    skip = block->length;
    if (branch_if_set) {
        emit_beq_w(block, 0);
    } else {
        emit_bne_w(block, 0);
    }

    // taken path: materialize pending + extra; restore for the fall-through
    saved = pending_cycles;
    emit_add_cycles(block, pending_cycles + 12);
    pending_cycles = 0;
    compile_push_imm16(block, ret_addr);

    // Jump to target
    emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
    emit_move_w_dn(block, REG_68K_D_NEXT_PC, target);
    emit_block_exit(block, target);
    pending_cycles = saved;

    patch_branch_w(block, skip);
}

void compile_ret(struct code_block *block)
{
    compile_pop_pc(block);
    emit_dispatch_jump(block);
}

// Compile conditional return (ret nz, ret z, ret nc, ret c)
// flag_bit: which bit in D7 to test (2=Z, 0=C)
// branch_if_set: if true, return when flag is set; if false, return when clear
void compile_ret_cond(struct code_block *block, uint8_t flag_bit, int branch_if_set)
{
    size_t skip;
    int saved;

    // Test the flag bit in D7
    emit_btst_imm_dn(block, flag_bit, REG_68K_D_FLAGS);

    // If condition NOT met, skip the return sequence
    skip = block->length;
    if (branch_if_set) {
        emit_beq_w(block, 0);
    } else {
        emit_bne_w(block, 0);
    }

    // taken path: materialize pending + extra; restore for the fall-through
    saved = pending_cycles;
    emit_add_cycles(block, pending_cycles + 12);
    pending_cycles = 0;
    compile_pop_pc(block);
    emit_dispatch_jump(block);
    pending_cycles = saved;

    patch_branch_w(block, skip);
}

void compile_rst_n(struct code_block *block, uint8_t target, uint16_t ret_addr)
{
    compile_push_imm16(block, ret_addr);

    // jump to target (0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38)
    emit_moveq_dn(block, REG_68K_D_NEXT_PC, target);
    emit_block_exit(block, target);
}
