#include "compiler.h"
#include "emitters.h"
#include "interop.h"

// Retro68 uses D0-D2 as scratch so I have to push cycle count before calling
// back into C. i'm not sure if this is a mac calling convention or specific
// to this gcc port.
// also interestingly, it doesn't appear to use the "A5 world" or A6, so i can
// use those registers while in the JIT world. calling back into C won't mess
// them up

// addr in D1, val_reg specifies value register
void compile_slow_dmg_write(struct code_block *block, uint8_t val_reg)
{
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
    // Fast path: check write page table
    // move.w d1, d0                     ; 2 bytes [0-1]
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // lsr.w #8, d0                      ; 2 bytes [2-3]
    emit_lsr_w_imm_dn(block, 8, REG_68K_D_SCRATCH_0);
    // lsl.w #2, d0                      ; 2 bytes [4-5]
    emit_lsl_w_imm_dn(block, 2, REG_68K_D_SCRATCH_0);
    // movea.l (a6,d0.w), a0             ; 4 bytes [6-9]
    emit_movea_l_idx_an_an(block, 0, REG_68K_A_WRITE_PAGE, REG_68K_D_SCRATCH_0, REG_68K_A_SCRATCH_1);
    // cmpa.w #0, a0                     ; 4 bytes [10-13]
    emit_cmpa_w_imm_an(block, 0, REG_68K_A_SCRATCH_1);
    // beq.s slow_path (+12)             ; 2 bytes [14-15] -> offset 28
    emit_beq_b(block, 12);

    // Page hit (offset 16):
    // move.w d1, d0                     ; 2 bytes [16-17]
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // andi.w #$ff, d0                   ; 4 bytes [18-21]
    emit_andi_w_dn(block, REG_68K_D_SCRATCH_0, 0x00ff);
    // move.b val_reg, (a0,d0.w)         ; 4 bytes [22-25]
    emit_move_b_dn_idx_an(block, val_reg, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // bra.s done (+24)                  ; 2 bytes [26-27] -> offset 52
    emit_bra_b(block, 24);

    // slow_path: (offset 28)
    compile_slow_dmg_write(block, val_reg);
    // falls through to done (offset 52)
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
    emit_move_b_dn(block, 3, val);
    compile_inline_dmg_write(block, 3);
    // emit_move_b_dn(block, 0, val);
    // compile_slow_dmg_write(block, 0);
}

// Call dmg_write(dmg, addr, val) - addr in D1, val in D0
void compile_call_dmg_write_d0(struct code_block *block)
{
    // uses d0 as scratch so need to move to d3, but it's so long that
    // what's one more instruction...
    emit_move_b_dn_dn(block, 0, 3);
    compile_inline_dmg_write(block, 3);
    // compile_slow_dmg_write(block, 0);
}

// Emit slow path call to dmg_read - expects address in D1, returns in D0
void compile_slow_dmg_read(struct code_block *block)
{
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
    // Fast path: check page table
    // move.w d1, d0                     ; 2 bytes [0-1]
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // lsr.w #8, d0                      ; 2 bytes [2-3]
    emit_lsr_w_imm_dn(block, 8, REG_68K_D_SCRATCH_0);
    // lsl.w #2, d0                      ; 2 bytes [4-5]
    emit_lsl_w_imm_dn(block, 2, REG_68K_D_SCRATCH_0);
    // movea.l (a5,d0.w), a0             ; 4 bytes [6-9]
    emit_movea_l_idx_an_an(block, 0, REG_68K_A_READ_PAGE, REG_68K_D_SCRATCH_0, REG_68K_A_SCRATCH_1);
    // cmpa.w #0, a0                     ; 4 bytes [10-13]
    emit_cmpa_w_imm_an(block, 0, REG_68K_A_SCRATCH_1);
    // beq.s slow_path (+12)             ; 2 bytes [14-15] -> offset 28
    emit_beq_b(block, 12);

    // Page hit (offset 16):
    // move.w d1, d0                     ; 2 bytes [16-17]
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // andi.w #$ff, d0                   ; 4 bytes [18-21]
    emit_andi_w_dn(block, REG_68K_D_SCRATCH_0, 0x00ff);
    // move.b (a0,d0.w), d0              ; 4 bytes [22-25]
    emit_move_b_idx_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_0);
    // bra.s done (+22)                  ; 2 bytes [26-27] -> offset 50
    emit_bra_b(block, 22);

    // slow_path: (offset 28)
    compile_slow_dmg_read(block);
    // falls through to done (offset 50)
}

// Call dmg_read(dmg, addr) - addr in D1, result goes to D4 (A register)
void compile_call_dmg_read_a(struct code_block *block)
{
    compile_call_dmg_read(block);
    emit_move_b_dn_dn(block, 0, REG_68K_D_A);
}

void compile_call_ei_di(struct code_block *block, int enabled)
{
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
    // Check if both bytes on same page (addr & 0xff != 0xff)
    // If low byte is 0xff, second byte would cross to next page
    // move.w d1, d0                     ; 2 bytes [0-1]
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // andi.w #$00ff, d0                 ; 4 bytes [2-5]
    emit_andi_w_dn(block, REG_68K_D_SCRATCH_0, 0x00ff);
    // cmpi.w #$00ff, d0                 ; 4 bytes [6-9]
    emit_cmpi_w_imm_dn(block, 0x00ff, REG_68K_D_SCRATCH_0);
    // beq.b slow_path (+38)             ; 2 bytes [10-11] -> offset 50
    emit_beq_b(block, 38);

    // Page table lookup
    // move.w d1, d0                     ; 2 bytes [12-13]
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // lsr.w #8, d0                      ; 2 bytes [14-15]
    emit_lsr_w_imm_dn(block, 8, REG_68K_D_SCRATCH_0);
    // lsl.w #2, d0                      ; 2 bytes [16-17]
    emit_lsl_w_imm_dn(block, 2, REG_68K_D_SCRATCH_0);
    // movea.l (a5,d0.w), a1             ; 4 bytes [18-21]
    emit_movea_l_idx_an_an(block, 0, REG_68K_A_READ_PAGE, REG_68K_D_SCRATCH_0, REG_68K_A_SCRATCH_1);
    // cmpa.w #0, a1                     ; 4 bytes [22-25]
    emit_cmpa_w_imm_an(block, 0, REG_68K_A_SCRATCH_1);
    // beq.b slow_path (+22)             ; 2 bytes [26-27] -> offset 50
    emit_beq_b(block, 22);

    // Fast read from page - read low byte, then high byte, combine
    // move.w d1, d0                     ; 2 bytes [28-29]
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // andi.w #$ff, d0                   ; 4 bytes [30-33]
    emit_andi_w_dn(block, REG_68K_D_SCRATCH_0, 0x00ff);
    // move.b (a1,d0.w), d3              ; 4 bytes [34-37] - low byte -> d3
    emit_move_b_idx_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0, REG_68K_D_NEXT_PC);
    // addq.w #1, d0                     ; 2 bytes [38-39]
    emit_addq_w_dn(block, REG_68K_D_SCRATCH_0, 1);
    // move.b (a1,d0.w), d0              ; 4 bytes [40-43] - high byte -> d0.b
    emit_move_b_idx_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_0);
    // lsl.w #8, d0                      ; 2 bytes [44-45] - shift high byte up
    emit_lsl_w_imm_dn(block, 8, REG_68K_D_SCRATCH_0);
    // move.b d3, d0                     ; 2 bytes [46-47] - combine low byte
    emit_move_b_dn_dn(block, REG_68K_D_NEXT_PC, REG_68K_D_SCRATCH_0);
    // bra.b done (+22)                  ; 2 bytes [48-49] -> offset 72
    emit_bra_b(block, 22);

    // slow_path: (offset 50)
    compile_slow_dmg_read16(block);
    // falls through to done (offset 72)
}

// Slow path for dmg_write16 - addr in D1.w, data in D0.w
void compile_slow_dmg_write16(struct code_block *block)
{
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
    // beq.b slow_path (+36)             ; 2 bytes [12-13] -> offset 50
    emit_beq_b(block, 36);

    // Page table lookup
    // move.w d1, d0                     ; 2 bytes [14-15]
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // lsr.w #8, d0                      ; 2 bytes [16-17]
    emit_lsr_w_imm_dn(block, 8, REG_68K_D_SCRATCH_0);
    // lsl.w #2, d0                      ; 2 bytes [18-19]
    emit_lsl_w_imm_dn(block, 2, REG_68K_D_SCRATCH_0);
    // movea.l (a6,d0.w), a1             ; 4 bytes [20-23]
    emit_movea_l_idx_an_an(block, 0, REG_68K_A_WRITE_PAGE, REG_68K_D_SCRATCH_0, REG_68K_A_SCRATCH_1);
    // cmpa.w #0, a1                     ; 4 bytes [24-27]
    emit_cmpa_w_imm_an(block, 0, REG_68K_A_SCRATCH_1);
    // beq.b slow_path (+20)             ; 2 bytes [28-29] -> offset 50
    emit_beq_b(block, 20);

    // Fast write to page - write low byte, then high byte
    // move.w d1, d0                     ; 2 bytes [30-31]
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // andi.w #$ff, d0                   ; 4 bytes [32-35]
    emit_andi_w_dn(block, REG_68K_D_SCRATCH_0, 0x00ff);
    // move.b d3, (a1,d0.w)              ; 4 bytes [36-39] - write low byte
    emit_move_b_dn_idx_an(block, REG_68K_D_NEXT_PC, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // lsr.w #8, d3                      ; 2 bytes [40-41] - shift high byte down
    emit_lsr_w_imm_dn(block, 8, REG_68K_D_NEXT_PC);
    // addq.w #1, d0                     ; 2 bytes [42-43]
    emit_addq_w_dn(block, REG_68K_D_SCRATCH_0, 1);
    // move.b d3, (a1,d0.w)              ; 4 bytes [44-47] - write high byte
    emit_move_b_dn_idx_an(block, REG_68K_D_NEXT_PC, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // bra.b done (+26)                  ; 2 bytes [48-49] -> offset 76
    emit_bra_b(block, 26);

    // slow_path: (offset 50)
    // Restore data from D3 to D0 for slow path
    // move.w d3, d0                     ; 2 bytes [50-51]
    emit_move_w_dn_dn(block, REG_68K_D_NEXT_PC, REG_68K_D_SCRATCH_0);
    compile_slow_dmg_write16(block);
    // falls through to done (offset 76)
}

// Call stop_func(dmg) - returns 0 to continue, non-zero to halt
// If returns 0, execution continues after STOP
// If returns non-zero, we set HALT_SENTINEL and exit
void compile_call_stop(struct code_block *block, int next_pc)
{
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
