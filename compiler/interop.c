#include "compiler.h"
#include "emitters.h"
#include "interop.h"

// Retro68 uses D0-D2 as scratch so I have to push cycle count before calling
// back into C. i'm not sure if this is a mac calling convention or specific
// to this gcc port.
// also interestingly, it doesn't appear to use the "A5 world" or A6, so i can
// use those registers while in the JIT world. calling back into C won't mess
// them up

// look up the page table entry for the GB address in addr_dreg (clobbered)
// and leave it in dest_areg. table entries are biased so that
// entry + (s16)gb_address = host pointer (see PAGE_BIAS in dmg.h), which
// lets the full address in D1.w index the page directly with (An,Dn.w)
// sign-extended addressing - no low-byte masking needed.
void compile_page_lookup(
    struct code_block *block,
    uint8_t table_areg,
    uint8_t addr_dreg,
    uint8_t dest_areg
) {
    if (compiler_68020) {
        // lsr.w #8, addr
        emit_lsr_w_imm_dn(block, 8, addr_dreg);
        // movea.l (table,addr.w*4), dest
        emit_movea_l_idx_scale4_an_an(block, table_areg, addr_dreg, dest_areg);
    } else {
        // fold the entry scaling into the shift:
        // (addr >> 8) * 4 == (addr >> 6) & 0x3fc
        emit_lsr_w_imm_dn(block, 6, addr_dreg);
        emit_andi_w_dn(block, addr_dreg, 0x03fc);
        // movea.l (table,addr.w), dest
        emit_movea_l_idx_an_an(block, 0, table_areg, addr_dreg, dest_areg);
    }
}

// addr in D1, val_reg specifies value register
void compile_slow_dmg_write(struct code_block *block, uint8_t val_reg)
{
    // D2 has to be exact for lazy register evaluation. no-op when reached
    // through the inline wrapper, which already flushed before the split
    flush_cycles(block);
    // store current cycle count for lazy register evaluation
    emit_move_l_dn_disp_an(block, REG_68K_D_CYCLE_COUNT, JIT_CTX_READ_CYCLES, REG_68K_A_CTX);
    // and push so retro68 doesn't erase
    emit_push_l_dn(block, REG_68K_D_CYCLE_COUNT); // 2
    emit_push_b_dn(block, val_reg); // 2
    emit_push_w_dn(block, REG_68K_D_SCRATCH_1); // 2
    emit_push_l_disp_an(block, JIT_CTX_DMG, REG_68K_A_CTX); // 4
    emit_movea_l_disp_an_an(block, JIT_CTX_WRITE, REG_68K_A_CTX, REG_68K_A_SCRATCH_1); // 4
    emit_jsr_ind_an(block, REG_68K_A_SCRATCH_1); // 2
    emit_addq_l_an(block, 7, 8); // 2
    emit_pop_l_dn(block, REG_68K_D_CYCLE_COUNT); // 2
}

// inline dmg_write with page table fast path - addr in D1, value in val_reg
static void compile_inline_dmg_write(struct code_block *block, uint8_t val_reg)
{
    size_t unmapped, done;

    // flush before the fast/slow split so both paths see the same D2
    flush_cycles(block);
    // Fast path: check write page table. entries are biased, so the full
    // address in D1.w indexes the page directly
    // move.w d1, d3
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_NEXT_PC);
    // a0 = write_page[addr >> 8]
    compile_page_lookup(block, REG_68K_A_WRITE_PAGE, REG_68K_D_NEXT_PC, REG_68K_A_SCRATCH_1);
    // move.l a0, d3 - sets Z, d3 is dead
    emit_move_l_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_NEXT_PC);
    unmapped = block->length;
    emit_beq_b(block, 0);

    // Page hit:
    // move.b val_reg, (a0,d1.w)
    emit_move_b_dn_idx_an(block, val_reg, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_1);
    done = block->length;
    emit_bra_b(block, 0);

    patch_branch_b(block, unmapped);
    compile_slow_dmg_write(block, val_reg);
    patch_branch_b(block, done);
}

// Call dmg_write(dmg, addr, val) - addr in D1, val in D4 (A register)
void compile_call_dmg_write_a(struct code_block *block)
{
    compile_inline_dmg_write(block, REG_68K_D_A);
    // compile_slow_dmg_write(block, REG_68K_D_A);
}

// Call dmg_write(dmg, addr, val) - addr in D1, val is immediate
void compile_call_dmg_write_imm(struct code_block *block, uint8_t val)
{
    emit_move_b_dn(block, 0, val);
    compile_inline_dmg_write(block, 0);
}

// Call dmg_write(dmg, addr, val) - addr in D1, val in D0
void compile_call_dmg_write_d0(struct code_block *block)
{
    compile_inline_dmg_write(block, 0);
}

// Emit slow path call to dmg_read - expects address in D1, returns in D0
void compile_slow_dmg_read(struct code_block *block)
{
    // D2 has to be exact for lazy DIV/LY evaluation. no-op when reached
    // through the inline wrapper, which already flushed before the split
    flush_cycles(block);
    // store current cycle count for DIV/LY evaluation
    emit_move_l_dn_disp_an(block, REG_68K_D_CYCLE_COUNT, JIT_CTX_READ_CYCLES, REG_68K_A_CTX); // 4
    emit_push_l_dn(block, REG_68K_D_CYCLE_COUNT); // 2
    emit_push_w_dn(block, REG_68K_D_SCRATCH_1); // 2
    emit_push_l_disp_an(block, JIT_CTX_DMG, REG_68K_A_CTX); // 4
    emit_movea_l_disp_an_an(block, JIT_CTX_READ, REG_68K_A_CTX, REG_68K_A_SCRATCH_1); // 4
    emit_jsr_ind_an(block, REG_68K_A_SCRATCH_1); // 2
    emit_addq_l_an(block, 7, 6); // 2
    emit_pop_l_dn(block, REG_68K_D_CYCLE_COUNT); // 2
}

// Call dmg_read(dmg, addr) - addr in D1, result stays in D0
// Page table fast path, falls back to slow path for unmapped pages
void compile_call_dmg_read(struct code_block *block)
{
    size_t unmapped, done;

    // flush before the fast/slow split so both paths see the same D2
    flush_cycles(block);
    // Fast path: check page table. entries are biased, so the full
    // address in D1.w indexes the page directly
    // move.w d1, d0
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // a0 = read_page[addr >> 8]
    compile_page_lookup(block, REG_68K_A_READ_PAGE, REG_68K_D_SCRATCH_0, REG_68K_A_SCRATCH_1);
    // move.l a0, d0 - sets Z, d0 is dead
    emit_move_l_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0);
    unmapped = block->length;
    emit_beq_b(block, 0);

    // Page hit:
    // move.b (a0,d1.w), d0
    emit_move_b_idx_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    done = block->length;
    emit_bra_b(block, 0);

    patch_branch_b(block, unmapped);
    compile_slow_dmg_read(block);
    patch_branch_b(block, done);
}

// Call dmg_read(dmg, addr) - addr in D1, result goes to D4 (A register)
void compile_call_dmg_read_a(struct code_block *block)
{
    compile_call_dmg_read(block);
    emit_move_b_dn_dn(block, 0, REG_68K_D_A);
}

void compile_call_ei_di(struct code_block *block, int enabled)
{
    flush_cycles(block);
    // push enabled
    emit_moveq_dn(block, REG_68K_D_SCRATCH_1, (int8_t) enabled);
    // i actually have this as a 16-bit int for some reason
    emit_push_w_dn(block, REG_68K_D_SCRATCH_1);
    // push dmg pointer
    emit_push_l_disp_an(block, JIT_CTX_DMG, REG_68K_A_CTX);
    // load address of function
    emit_movea_l_disp_an_an(block, JIT_CTX_EI_DI, REG_68K_A_CTX, REG_68K_A_SCRATCH_1);
    // call dmg_ei_di
    emit_jsr_ind_an(block, REG_68K_A_SCRATCH_1);
    // clean up stack
    emit_addq_l_an(block, 7, 6);
}

// Slow path for dmg_read16 - addr in D1.w, result in D0.w
void compile_slow_dmg_read16(struct code_block *block)
{
    flush_cycles(block);
    emit_move_l_dn_disp_an(block, REG_68K_D_CYCLE_COUNT, JIT_CTX_READ_CYCLES, REG_68K_A_CTX);
    emit_push_l_dn(block, REG_68K_D_CYCLE_COUNT);
    emit_push_w_dn(block, REG_68K_D_SCRATCH_1);
    emit_push_l_disp_an(block, JIT_CTX_DMG, REG_68K_A_CTX);
    emit_movea_l_disp_an_an(block, JIT_CTX_READ16, REG_68K_A_CTX, REG_68K_A_SCRATCH_1);
    emit_jsr_ind_an(block, REG_68K_A_SCRATCH_1);
    emit_addq_l_an(block, 7, 6);
    emit_pop_l_dn(block, REG_68K_D_CYCLE_COUNT);
}

// Call dmg_read16(dmg, addr) - addr in D1.w, result in D0.w
// Inline fast path for page table hits when both bytes on same page
void compile_call_dmg_read16(struct code_block *block)
{
    size_t cross, unmapped, done;

    // flush before the fast/slow split so both paths see the same D2
    flush_cycles(block);

    // Check if both bytes on same page (addr & 0xff != 0xff)
    // If low byte is 0xff, second byte would cross to next page
    // move.w d1, d0
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // andi.w #$00ff, d0
    emit_andi_w_dn(block, REG_68K_D_SCRATCH_0, 0x00ff);
    // cmpi.w #$00ff, d0
    emit_cmpi_w_imm_dn(block, 0x00ff, REG_68K_D_SCRATCH_0);
    cross = block->length;
    emit_beq_b(block, 0);

    // Page table lookup
    // move.w d1, d0
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // a0 = read_page[addr >> 8]
    compile_page_lookup(block, REG_68K_A_READ_PAGE, REG_68K_D_SCRATCH_0, REG_68K_A_SCRATCH_1);
    // move.l a0, d0 - sets Z, d0 is dead
    emit_move_l_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0);
    unmapped = block->length;
    emit_beq_b(block, 0);

    // Fast read - low byte at (a0,d1.w), high byte one address later
    // (no page cross possible: low byte of the address is not 0xff)
    // move.b (a0,d1.w), d3 - low byte
    emit_move_b_idx_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_1, REG_68K_D_NEXT_PC);
    // move.w d1, d0
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // addq.w #1, d0
    emit_addq_w_dn(block, REG_68K_D_SCRATCH_0, 1);
    // move.b (a0,d0.w), d0 - high byte
    emit_move_b_idx_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_0);
    // lsl.w #8, d0 - shift high byte up
    emit_lsl_w_imm_dn(block, 8, REG_68K_D_SCRATCH_0);
    // move.b d3, d0 - combine low byte
    emit_move_b_dn_dn(block, REG_68K_D_NEXT_PC, REG_68K_D_SCRATCH_0);
    done = block->length;
    emit_bra_b(block, 0);

    patch_branch_b(block, cross);
    patch_branch_b(block, unmapped);
    compile_slow_dmg_read16(block);
    patch_branch_b(block, done);
}

// Slow path for dmg_write16 - addr in D1.w, data in D0.w
void compile_slow_dmg_write16(struct code_block *block)
{
    flush_cycles(block);
    emit_move_l_dn_disp_an(block, REG_68K_D_CYCLE_COUNT, JIT_CTX_READ_CYCLES, REG_68K_A_CTX);
    emit_push_l_dn(block, REG_68K_D_CYCLE_COUNT);
    emit_push_w_dn(block, REG_68K_D_SCRATCH_0);
    emit_push_w_dn(block, REG_68K_D_SCRATCH_1);
    emit_push_l_disp_an(block, JIT_CTX_DMG, REG_68K_A_CTX);
    emit_movea_l_disp_an_an(block, JIT_CTX_WRITE16, REG_68K_A_CTX, REG_68K_A_SCRATCH_1);
    emit_jsr_ind_an(block, REG_68K_A_SCRATCH_1);
    emit_addq_l_an(block, 7, 8);
    emit_pop_l_dn(block, REG_68K_D_CYCLE_COUNT);
}

// Call dmg_write16(dmg, addr, data) - addr in D1.w, data in D0.w
// Inline fast path for page table hits when both bytes on same page
void compile_call_dmg_write16_d0(struct code_block *block)
{
    size_t cross, unmapped, done;

    // flush before the fast/slow split so both paths see the same D2
    flush_cycles(block);

    // Save data to D3 before we use D0 as scratch
    // move.w d0, d3
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_NEXT_PC);

    // Check if both bytes on same page (addr & 0xff != 0xff)
    // move.w d1, d0
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // andi.w #$00ff, d0
    emit_andi_w_dn(block, REG_68K_D_SCRATCH_0, 0x00ff);
    // cmpi.w #$00ff, d0
    emit_cmpi_w_imm_dn(block, 0x00ff, REG_68K_D_SCRATCH_0);
    cross = block->length;
    emit_beq_b(block, 0);

    // Page table lookup
    // move.w d1, d0
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // a0 = write_page[addr >> 8]
    compile_page_lookup(block, REG_68K_A_WRITE_PAGE, REG_68K_D_SCRATCH_0, REG_68K_A_SCRATCH_1);
    // move.l a0, d0 - sets Z, d0 is dead
    emit_move_l_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0);
    unmapped = block->length;
    emit_beq_b(block, 0);

    // Fast write - low byte at (a0,d1.w), high byte one address later
    // (no page cross possible: low byte of the address is not 0xff)
    // move.b d3, (a0,d1.w) - write low byte
    emit_move_b_dn_idx_an(block, REG_68K_D_NEXT_PC, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_1);
    // lsr.w #8, d3 - shift high byte down
    emit_lsr_w_imm_dn(block, 8, REG_68K_D_NEXT_PC);
    // move.w d1, d0
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // addq.w #1, d0
    emit_addq_w_dn(block, REG_68K_D_SCRATCH_0, 1);
    // move.b d3, (a0,d0.w) - write high byte
    emit_move_b_dn_idx_an(block, REG_68K_D_NEXT_PC, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0);
    done = block->length;
    emit_bra_b(block, 0);

    // Slow path: restore data from D3 to D0
    patch_branch_b(block, cross);
    patch_branch_b(block, unmapped);
    // move.w d3, d0
    emit_move_w_dn_dn(block, REG_68K_D_NEXT_PC, REG_68K_D_SCRATCH_0);
    compile_slow_dmg_write16(block);
    patch_branch_b(block, done);
}

// Call stop_func(dmg) - returns 0 to continue, non-zero to halt
// If returns 0, execution continues after STOP
// If returns non-zero, we set HALT_SENTINEL and exit
void compile_call_stop(struct code_block *block, int next_pc)
{
    flush_cycles(block);
    // push dmg pointer
    emit_push_l_disp_an(block, JIT_CTX_DMG, REG_68K_A_CTX);  // 4
    // load stop_func address
    emit_movea_l_disp_an_an(block, JIT_CTX_STOP_FUNC, REG_68K_A_CTX, REG_68K_A_SCRATCH_1);  // 4
    // call stop_func
    emit_jsr_ind_an(block, REG_68K_A_SCRATCH_1);  // 2
    // clean up stack
    emit_addq_l_an(block, 7, 4);  // 2

    // D0 = return value. If 0, continue execution. If non-zero, halt.
    emit_tst_b_dn(block, REG_68K_D_SCRATCH_0);  // 2
    size_t no_halt = block->length;
    emit_beq_b(block, 0);

    // Halt: set HALT_SENTINEL and dispatch
    emit_move_l_dn(block, REG_68K_D_NEXT_PC, 0xffffffff);  // 6
    emit_dispatch_jump(block);  // 6

    // Continue: set next PC and dispatch
    patch_branch_b(block, no_halt);
    emit_move_l_dn(block, REG_68K_D_NEXT_PC, next_pc);  // 6
    emit_dispatch_jump(block);  // 6
}
