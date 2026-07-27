#include "compiler.h"
#include "emitters.h"
#include "interop.h"

// Retro68 uses D0-D2 as scratch, so the cycle countdown is stashed in
// jit_ctx.read_cycles across C calls and reloaded after. The C side
// doubles as a reader (lazy DIV/LY need the exact clock) and a writer
// (budget_touch shrinks it in tandem with wake_limit).
// also interestingly, it doesn't appear to use the "A5 world" or A6, so i can
// use those registers while in the JIT world. calling back into C won't mess
// them up

// look up the page table entry for the GB address in addr_dreg (clobbered)
// and leave it in dest_areg. table entries are biased so that
// entry + (s16)gb_address = host pointer (see PAGE_BIAS in dmg.h), which
// lets the full address in D1.w index the page directly with (An,Dn.w)
// sign-extended addressing
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

// shared helper entry addresses, set by compile_emit_helpers
struct jit_helpers jit_helpers;

// scratch the helpers are emitted into
static struct code_block helper_block;

// C-call sequence for dmg_read - addr in D1.w, result in D0.b
static void emit_c_read_call(struct code_block *block)
{
    // stash the countdown for lazy DIV/LY evaluation
    emit_move_l_dn_disp_an(block, REG_68K_D_CYCLE_COUNT, JIT_CTX_READ_CYCLES, REG_68K_A_CTX); // 4
    emit_push_w_dn(block, REG_68K_D_SCRATCH_1); // 2
    emit_push_l_disp_an(block, JIT_CTX_DMG, REG_68K_A_CTX); // 4
    emit_movea_l_disp_an_an(block, JIT_CTX_READ, REG_68K_A_CTX, REG_68K_A_SCRATCH_1); // 4
    emit_jsr_ind_an(block, REG_68K_A_SCRATCH_1); // 2
    emit_addq_l_an(block, 7, 6); // 2
    emit_move_l_disp_an_dn(block, JIT_CTX_READ_CYCLES, REG_68K_A_CTX, REG_68K_D_CYCLE_COUNT);
}

// C-call sequence for dmg_write - addr in D1.w, value in D0.b
static void emit_c_write_call(struct code_block *block)
{
    emit_move_l_dn_disp_an(block, REG_68K_D_CYCLE_COUNT, JIT_CTX_READ_CYCLES, REG_68K_A_CTX);
    emit_push_b_dn(block, REG_68K_D_SCRATCH_0); // 2
    emit_push_w_dn(block, REG_68K_D_SCRATCH_1); // 2
    emit_push_l_disp_an(block, JIT_CTX_DMG, REG_68K_A_CTX); // 4
    emit_movea_l_disp_an_an(block, JIT_CTX_WRITE, REG_68K_A_CTX, REG_68K_A_SCRATCH_1); // 4
    emit_jsr_ind_an(block, REG_68K_A_SCRATCH_1); // 2
    emit_addq_l_an(block, 7, 8); // 2
    emit_move_l_disp_an_dn(block, JIT_CTX_READ_CYCLES, REG_68K_A_CTX, REG_68K_D_CYCLE_COUNT);
}

// start the next entry on a 16-byte I-cache line (base is 16-aligned)
static void pad_align16(struct code_block *block)
{
    while (block->length & 15) {
        emit_word(block, 0x4e71); // nop
    }
}

const struct code_block *compile_emit_helpers(uint32_t base, void *hram_base)
{
    struct code_block *b = &helper_block;
    size_t unmapped, lo, ie;

    b->length = 0;
    lo = ie = 0;

    // read chain - page lookup falls through to the hit and its rts
    jit_helpers.read8_hl = base + b->length;
    emit_move_w_an_dn(b, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
    jit_helpers.read8 = base + b->length;
    emit_move_w_dn_dn(b, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    compile_page_lookup(b, REG_68K_A_READ_PAGE, REG_68K_D_SCRATCH_0,
            REG_68K_A_SCRATCH_1);
    emit_move_l_an_dn(b, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_0);
    unmapped = b->length;
    emit_beq_b(b, 0);
    emit_move_b_idx_an_dn(b, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_1,
            REG_68K_D_SCRATCH_0);
    emit_rts(b);

    patch_branch_b(b, unmapped);
    if (hram_base) {
        // HRAM and the IE mirror at $ffff,
        // bias the base by +$80 (-$10000+$80)
        emit_cmpi_w_imm_dn(b, 0xff80, REG_68K_D_SCRATCH_1);
        lo = b->length;
        emit_bcs_b(b, 0);
        emit_movea_l_imm32(b, REG_68K_A_SCRATCH_1,
                (uint32_t) (uintptr_t) hram_base + 0x80);
        emit_move_b_idx_an_dn(b, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_1,
                REG_68K_D_SCRATCH_0);
        emit_rts(b);
        patch_branch_b(b, lo);
    }
    jit_helpers.read8_slow = base + b->length;
    emit_c_read_call(b);
    emit_rts(b);

    pad_align16(b);

    // write chain
    jit_helpers.write8_a = base + b->length;
    emit_move_b_dn_dn(b, REG_68K_D_A, REG_68K_D_SCRATCH_0);
    unmapped = b->length;
    emit_bra_b(b, 0);
    jit_helpers.write8_hl_a = base + b->length;
    emit_move_b_dn_dn(b, REG_68K_D_A, REG_68K_D_SCRATCH_0);
    jit_helpers.write8_hl = base + b->length;
    emit_move_w_an_dn(b, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
    patch_branch_b(b, unmapped);
    jit_helpers.write8 = base + b->length;
    // D0 holds the value, so the lookup scratches through D3
    emit_move_w_dn_dn(b, REG_68K_D_SCRATCH_1, REG_68K_D_NEXT_PC);
    compile_page_lookup(b, REG_68K_A_WRITE_PAGE, REG_68K_D_NEXT_PC,
            REG_68K_A_SCRATCH_1);
    emit_move_l_an_dn(b, REG_68K_A_SCRATCH_1, REG_68K_D_NEXT_PC);
    unmapped = b->length;
    emit_beq_b(b, 0);
    emit_move_b_dn_idx_an(b, REG_68K_D_SCRATCH_0, REG_68K_A_SCRATCH_1,
            REG_68K_D_SCRATCH_1);
    emit_rts(b);

    if (hram_base) {
        patch_branch_b(b, unmapped);
        // $ffff is IE, so need to go to C
        emit_cmpi_w_imm_dn(b, 0xff80, REG_68K_D_SCRATCH_1);
        lo = b->length;
        emit_bcs_b(b, 0);
        emit_cmpi_w_imm_dn(b, 0xffff, REG_68K_D_SCRATCH_1);
        ie = b->length;
        emit_beq_b(b, 0);
        emit_movea_l_imm32(b, REG_68K_A_SCRATCH_1,
                (uint32_t) (uintptr_t) hram_base + 0x80);
        emit_move_b_dn_idx_an(b, REG_68K_D_SCRATCH_0, REG_68K_A_SCRATCH_1,
                REG_68K_D_SCRATCH_1);
        emit_rts(b);
    }
    jit_helpers.write8_slow_a = base + b->length;
    emit_move_b_dn_dn(b, REG_68K_D_A, REG_68K_D_SCRATCH_0);
    jit_helpers.write8_slow = base + b->length;
    if (hram_base) {
        patch_branch_b(b, lo);
        patch_branch_b(b, ie);
    } else {
        patch_branch_b(b, unmapped);
    }
    emit_c_write_call(b);
    emit_rts(b);

    if (b->length > JIT_HELPERS_SIZE) {
        return NULL;
    }
    return b;
}

// addr in D1, val_reg specifies value register
void compile_slow_dmg_write(struct code_block *block, uint8_t val_reg)
{
    // D2 has to be exact for lazy register evaluation
    flush_cycles(block);
    if (val_reg == REG_68K_D_A) {
        emit_jsr_abs_l(block, jit_helpers.write8_slow_a);
        return;
    }
    if (val_reg != REG_68K_D_SCRATCH_0) {
        emit_move_b_dn_dn(block, val_reg, REG_68K_D_SCRATCH_0);
    }
    emit_jsr_abs_l(block, jit_helpers.write8_slow);
}

// Call dmg_write(dmg, addr, val) - addr in D1, val in D4 (A register)
void compile_call_dmg_write_a(struct code_block *block)
{
    flush_cycles(block);
    emit_jsr_abs_l(block, jit_helpers.write8_a);
}

// Call dmg_write(dmg, addr, val) - addr in D1, val is immediate
void compile_call_dmg_write_imm(struct code_block *block, uint8_t val)
{
    emit_move_b_dn(block, 0, val);
    flush_cycles(block);
    emit_jsr_abs_l(block, jit_helpers.write8);
}

// Call dmg_write(dmg, addr, val) - addr in D1, val in D0
void compile_call_dmg_write_d0(struct code_block *block)
{
    flush_cycles(block);
    emit_jsr_abs_l(block, jit_helpers.write8);
}

// Call dmg_write(dmg, HL, val) - val in D0, D1 loaded inside the helper
void compile_call_dmg_write_hl_d0(struct code_block *block)
{
    flush_cycles(block);
    emit_jsr_abs_l(block, jit_helpers.write8_hl);
}

// Call dmg_write(dmg, HL, A)
void compile_call_dmg_write_hl_a(struct code_block *block)
{
    flush_cycles(block);
    emit_jsr_abs_l(block, jit_helpers.write8_hl_a);
}

// Call dmg_write(dmg, HL, val) - val is immediate
void compile_call_dmg_write_hl_imm(struct code_block *block, uint8_t val)
{
    emit_move_b_dn(block, 0, val);
    flush_cycles(block);
    emit_jsr_abs_l(block, jit_helpers.write8_hl);
}

// Emit slow path call to dmg_read - expects address in D1, returns in D0
void compile_slow_dmg_read(struct code_block *block)
{
    // D2 has to be exact for lazy DIV/LY evaluation; the helper stashes
    // it around the C call
    flush_cycles(block);
    emit_jsr_abs_l(block, jit_helpers.read8_slow);
}

// Call dmg_read(dmg, addr) - addr in D1, result stays in D0
void compile_call_dmg_read(struct code_block *block)
{
    flush_cycles(block);
    emit_jsr_abs_l(block, jit_helpers.read8);
}

// Call dmg_read(dmg, HL) - D1 loaded inside the helper, result in D0
void compile_call_dmg_read_hl(struct code_block *block)
{
    flush_cycles(block);
    emit_jsr_abs_l(block, jit_helpers.read8_hl);
}

// Call dmg_read(dmg, addr) - addr in D1, result goes to D4 (A register)
void compile_call_dmg_read_a(struct code_block *block)
{
    compile_call_dmg_read(block);
    emit_move_b_dn_dn(block, 0, REG_68K_D_A);
}

// Call dmg_read(dmg, HL) - result goes to D4 (A register)
void compile_call_dmg_read_hl_a(struct code_block *block)
{
    compile_call_dmg_read_hl(block);
    emit_move_b_dn_dn(block, 0, REG_68K_D_A);
}

void compile_call_ei_di(struct code_block *block, int enabled)
{
    flush_cycles(block);
    // save cycle count
    emit_move_l_dn_disp_an(block, REG_68K_D_CYCLE_COUNT, JIT_CTX_READ_CYCLES, REG_68K_A_CTX);
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
    emit_move_l_disp_an_dn(block, JIT_CTX_READ_CYCLES, REG_68K_A_CTX, REG_68K_D_CYCLE_COUNT);
}

// Slow path for dmg_read16 - addr in D1.w, result in D0.w
void compile_slow_dmg_read16(struct code_block *block)
{
    flush_cycles(block);
    emit_move_l_dn_disp_an(block, REG_68K_D_CYCLE_COUNT, JIT_CTX_READ_CYCLES, REG_68K_A_CTX);
    emit_push_w_dn(block, REG_68K_D_SCRATCH_1);
    emit_push_l_disp_an(block, JIT_CTX_DMG, REG_68K_A_CTX);
    emit_movea_l_disp_an_an(block, JIT_CTX_READ16, REG_68K_A_CTX, REG_68K_A_SCRATCH_1);
    emit_jsr_ind_an(block, REG_68K_A_SCRATCH_1);
    emit_addq_l_an(block, 7, 6);
    emit_move_l_disp_an_dn(block, JIT_CTX_READ_CYCLES, REG_68K_A_CTX, REG_68K_D_CYCLE_COUNT);
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
    emit_push_w_dn(block, REG_68K_D_SCRATCH_0);
    emit_push_w_dn(block, REG_68K_D_SCRATCH_1);
    emit_push_l_disp_an(block, JIT_CTX_DMG, REG_68K_A_CTX);
    emit_movea_l_disp_an_an(block, JIT_CTX_WRITE16, REG_68K_A_CTX, REG_68K_A_SCRATCH_1);
    emit_jsr_ind_an(block, REG_68K_A_SCRATCH_1);
    emit_addq_l_an(block, 7, 8);
    emit_move_l_disp_an_dn(block, JIT_CTX_READ_CYCLES, REG_68K_A_CTX, REG_68K_D_CYCLE_COUNT);
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
    emit_move_l_dn_disp_an(block, REG_68K_D_CYCLE_COUNT, JIT_CTX_READ_CYCLES, REG_68K_A_CTX);
    // push dmg pointer
    emit_push_l_disp_an(block, JIT_CTX_DMG, REG_68K_A_CTX);  // 4
    // load stop_func address
    emit_movea_l_disp_an_an(block, JIT_CTX_STOP_FUNC, REG_68K_A_CTX, REG_68K_A_SCRATCH_1);  // 4
    // call stop_func
    emit_jsr_ind_an(block, REG_68K_A_SCRATCH_1);  // 2
    // clean up stack
    emit_addq_l_an(block, 7, 4);  // 2
    emit_move_l_disp_an_dn(block, JIT_CTX_READ_CYCLES, REG_68K_A_CTX, REG_68K_D_CYCLE_COUNT);

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
