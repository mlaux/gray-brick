#include "compiler.h"
#include "emitters.h"
#include "interop.h"

// Retro68 uses D0-D2 as scratch so I have to push cycle count before calling
// back into C. i'm not sure if this is a mac calling convention or specific
// to this gcc port.
// also interestingly, it doesn't appear to use the "A5 world" or A6, so i can
// use those registers while in the JIT world. calling back into C won't mess
// them up

// look up the page table entry for the page index in idx_dreg (address >> 8)
// and leave it in dest_areg. table entries are biased so that
// entry + (s16)gb_address = host pointer (see PAGE_BIAS in dmg.h), which
// lets the full address in D1.w index the page directly with (An,Dn.w)
// sign-extended addressing - no low-byte masking needed.
// emits 4 bytes on 68020+, 6 bytes on 68000 (clobbers idx_dreg there)
void compile_page_lookup(
    struct code_block *block,
    uint8_t table_areg,
    uint8_t idx_dreg,
    uint8_t dest_areg
) {
    if (compiler_68020) {
        // movea.l (table,idx.w*4), dest
        emit_movea_l_idx_scale4_an_an(block, table_areg, idx_dreg, dest_areg);
    } else {
        // lsl.w #2, idx
        emit_lsl_w_imm_dn(block, 2, idx_dreg);
        // movea.l (table,idx.w), dest
        emit_movea_l_idx_an_an(block, 0, table_areg, idx_dreg, dest_areg);
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
    // flush before the fast/slow split so both paths see the same D2
    flush_cycles(block);
    // Fast path: check write page table. entries are biased, so the full
    // address in D1.w indexes the page directly
    // move.w d1, d3                     ; 2 bytes [0-1]
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_NEXT_PC);
    // lsr.w #8, d3                      ; 2 bytes [2-3]
    emit_lsr_w_imm_dn(block, 8, REG_68K_D_NEXT_PC);
    // movea.l (a6,d3.w*4), a0           ; 4 bytes [4-7] (6 on 68000)
    compile_page_lookup(block, REG_68K_A_WRITE_PAGE, REG_68K_D_NEXT_PC, REG_68K_A_SCRATCH_1);
    // move.l a0, d3 - sets Z, d3 is dead ; 2 bytes [8-9]
    emit_move_l_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_NEXT_PC);
    // beq.s slow_path (+6)              ; 2 bytes [10-11] -> offset 18
    emit_beq_b(block, 6);

    // Page hit:
    // move.b val_reg, (a0,d1.w)         ; 4 bytes [12-15]
    emit_move_b_dn_idx_an(block, val_reg, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_1);
    // bra.s done (+24)                  ; 2 bytes [16-17] -> offset 42
    emit_bra_b(block, 24);

    // slow_path: (offset 18)
    compile_slow_dmg_write(block, val_reg);
    // falls through to done (offset 42)
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
    // flush before the fast/slow split so both paths see the same D2
    flush_cycles(block);
    // Fast path: check page table. entries are biased, so the full
    // address in D1.w indexes the page directly
    // move.w d1, d0                     ; 2 bytes [0-1]
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // lsr.w #8, d0                      ; 2 bytes [2-3]
    emit_lsr_w_imm_dn(block, 8, REG_68K_D_SCRATCH_0);
    // movea.l (a5,d0.w*4), a0           ; 4 bytes [4-7] (6 on 68000)
    compile_page_lookup(block, REG_68K_A_READ_PAGE, REG_68K_D_SCRATCH_0, REG_68K_A_SCRATCH_1);
    // move.l a0, d0 - sets Z, d0 is dead ; 2 bytes [8-9]
    emit_move_l_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // beq.s slow_path (+6)              ; 2 bytes [10-11] -> offset 18
    emit_beq_b(block, 6);

    // Page hit:
    // move.b (a0,d1.w), d0              ; 4 bytes [12-15]
    emit_move_b_idx_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // bra.s done (+22)                  ; 2 bytes [16-17] -> offset 40
    emit_bra_b(block, 22);

    // slow_path: (offset 18)
    compile_slow_dmg_read(block);
    // falls through to done (offset 40)
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
    // the 68000 page lookup is 2 bytes longer than the 68020 one, and the
    // first slow-path branch jumps over it
    int pad = compiler_68020 ? 0 : 2;

    // flush before the fast/slow split so both paths see the same D2
    flush_cycles(block);

    // Check if both bytes on same page (addr & 0xff != 0xff)
    // If low byte is 0xff, second byte would cross to next page
    // move.w d1, d0                     ; 2 bytes [0-1]
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // andi.w #$00ff, d0                 ; 4 bytes [2-5]
    emit_andi_w_dn(block, REG_68K_D_SCRATCH_0, 0x00ff);
    // cmpi.w #$00ff, d0                 ; 4 bytes [6-9]
    emit_cmpi_w_imm_dn(block, 0x00ff, REG_68K_D_SCRATCH_0);
    // beq.b slow_path (+30/+32)         ; 2 bytes [10-11] -> offset 42
    emit_beq_b(block, 30 + pad);

    // Page table lookup
    // move.w d1, d0                     ; 2 bytes [12-13]
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // lsr.w #8, d0                      ; 2 bytes [14-15]
    emit_lsr_w_imm_dn(block, 8, REG_68K_D_SCRATCH_0);
    // movea.l (a5,d0.w*4), a0           ; 4 bytes [16-19] (6 on 68000)
    compile_page_lookup(block, REG_68K_A_READ_PAGE, REG_68K_D_SCRATCH_0, REG_68K_A_SCRATCH_1);
    // move.l a0, d0 - sets Z, d0 is dead ; 2 bytes [20-21]
    emit_move_l_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // beq.b slow_path (+18)             ; 2 bytes [22-23] -> offset 42
    emit_beq_b(block, 18);

    // Fast read - low byte at (a0,d1.w), high byte one address later
    // (no page cross possible: low byte of the address is not 0xff)
    // move.b (a0,d1.w), d3              ; 4 bytes [24-27] - low byte -> d3
    emit_move_b_idx_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_1, REG_68K_D_NEXT_PC);
    // move.w d1, d0                     ; 2 bytes [28-29]
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // addq.w #1, d0                     ; 2 bytes [30-31]
    emit_addq_w_dn(block, REG_68K_D_SCRATCH_0, 1);
    // move.b (a0,d0.w), d0              ; 4 bytes [32-35] - high byte -> d0.b
    emit_move_b_idx_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_0);
    // lsl.w #8, d0                      ; 2 bytes [36-37] - shift high byte up
    emit_lsl_w_imm_dn(block, 8, REG_68K_D_SCRATCH_0);
    // move.b d3, d0                     ; 2 bytes [38-39] - combine low byte
    emit_move_b_dn_dn(block, REG_68K_D_NEXT_PC, REG_68K_D_SCRATCH_0);
    // bra.b done (+22)                  ; 2 bytes [40-41] -> offset 64
    emit_bra_b(block, 22);

    // slow_path: (offset 42)
    compile_slow_dmg_read16(block);
    // falls through to done (offset 64)
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
    // the 68000 page lookup is 2 bytes longer than the 68020 one, and the
    // first slow-path branch jumps over it
    int pad = compiler_68020 ? 0 : 2;

    // flush before the fast/slow split so both paths see the same D2
    flush_cycles(block);

    // Save data to D3 before we use D0 as scratch
    // move.w d0, d3                     ; 2 bytes [0-1]
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_NEXT_PC);

    // Check if both bytes on same page (addr & 0xff != 0xff)
    // move.w d1, d0                     ; 2 bytes [2-3]
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // andi.w #$00ff, d0                 ; 4 bytes [4-7]
    emit_andi_w_dn(block, REG_68K_D_SCRATCH_0, 0x00ff);
    // cmpi.w #$00ff, d0                 ; 4 bytes [8-11]
    emit_cmpi_w_imm_dn(block, 0x00ff, REG_68K_D_SCRATCH_0);
    // beq.b slow_path (+28/+30)         ; 2 bytes [12-13] -> offset 42
    emit_beq_b(block, 28 + pad);

    // Page table lookup
    // move.w d1, d0                     ; 2 bytes [14-15]
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // lsr.w #8, d0                      ; 2 bytes [16-17]
    emit_lsr_w_imm_dn(block, 8, REG_68K_D_SCRATCH_0);
    // movea.l (a6,d0.w*4), a0           ; 4 bytes [18-21] (6 on 68000)
    compile_page_lookup(block, REG_68K_A_WRITE_PAGE, REG_68K_D_SCRATCH_0, REG_68K_A_SCRATCH_1);
    // move.l a0, d0 - sets Z, d0 is dead ; 2 bytes [22-23]
    emit_move_l_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // beq.b slow_path (+16)             ; 2 bytes [24-25] -> offset 42
    emit_beq_b(block, 16);

    // Fast write - low byte at (a0,d1.w), high byte one address later
    // (no page cross possible: low byte of the address is not 0xff)
    // move.b d3, (a0,d1.w)              ; 4 bytes [26-29] - write low byte
    emit_move_b_dn_idx_an(block, REG_68K_D_NEXT_PC, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_1);
    // lsr.w #8, d3                      ; 2 bytes [30-31] - shift high byte down
    emit_lsr_w_imm_dn(block, 8, REG_68K_D_NEXT_PC);
    // move.w d1, d0                     ; 2 bytes [32-33]
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // addq.w #1, d0                     ; 2 bytes [34-35]
    emit_addq_w_dn(block, REG_68K_D_SCRATCH_0, 1);
    // move.b d3, (a0,d0.w)              ; 4 bytes [36-39] - write high byte
    emit_move_b_dn_idx_an(block, REG_68K_D_NEXT_PC, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // bra.b done (+26)                  ; 2 bytes [40-41] -> offset 68
    emit_bra_b(block, 26);

    // slow_path: (offset 42)
    // Restore data from D3 to D0 for slow path
    // move.w d3, d0                     ; 2 bytes [42-43]
    emit_move_w_dn_dn(block, REG_68K_D_NEXT_PC, REG_68K_D_SCRATCH_0);
    compile_slow_dmg_write16(block);
    // falls through to done (offset 68)
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
    emit_beq_b(block, 12);  // if 0, skip halt sequence (+12 to skip: 2 + move.l(6) + jmp(6) = 14? no wait...)

    // Halt: set HALT_SENTINEL and dispatch
    emit_move_l_dn(block, REG_68K_D_NEXT_PC, 0xffffffff);  // 6
    emit_dispatch_jump(block);  // 6

    // Continue: set next PC and dispatch
    emit_move_l_dn(block, REG_68K_D_NEXT_PC, next_pc);  // 6
    emit_dispatch_jump(block);  // 6
}
