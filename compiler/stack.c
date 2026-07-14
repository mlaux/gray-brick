#include "stack.h"
#include "emitters.h"
#include "interop.h"
#include "compiler.h"

#define READ_BYTE(off) (ctx->read(ctx->dmg, src_address + (off)))

void compile_ld_sp_imm16(
    struct compile_ctx *ctx,
    struct code_block *block,
    uint16_t src_address,
    uint16_t *src_ptr
) {
    uint16_t gb_sp = READ_BYTE(*src_ptr) | (READ_BYTE(*src_ptr + 1) << 8);
    *src_ptr += 2;

    // always store gb_sp to context
    emit_move_w_dn(block, REG_68K_D_SCRATCH_1, gb_sp);
    emit_move_w_dn_disp_an(block, REG_68K_D_SCRATCH_1, JIT_CTX_GB_SP, REG_68K_A_CTX);

    // compile-time WRAM/HRAM detection. stacks less than 2 bytes above
    // the bottom of a region ($c000/$c001, $ff80/$ff81) stay in slow
    // mode: the first push writes at SP-2, below the region, and a
    // native A3 would walk out of the buffer
    if (ctx && ctx->wram_base && gb_sp >= 0xc002 && gb_sp < 0xd000) {
        // WRAM bank 0 ($C000-$CFFF): always fixed, use compile-time address
        uint32_t addr = (uint32_t) ctx->wram_base + (gb_sp - 0xc000);
        emit_movea_l_imm32(block, REG_68K_A_SP, addr);
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 1);
        emit_move_l_dn_disp_an(block, REG_68K_D_SCRATCH_1, JIT_CTX_STACK_IN_RAM, REG_68K_A_CTX);
    } else if (ctx && ctx->wram_base && gb_sp >= 0xd000 && gb_sp <= 0xe000) {
        // Switchable WRAM ($D000-$DFFF): use page table for correct bank.
        // resolve through the page of SP-1, the first byte a push writes:
        // the stack descends, so SP itself may sit one past the region.
        // SP = $e000 (top-of-WRAM stack) then resolves through page $df
        uint8_t page = (gb_sp - 1) >> 8;
        // D0 = page * 4 (index into page table)
        emit_move_w_dn(block, REG_68K_D_SCRATCH_0, (int16_t)(page * 4));
        // A3 = read_page[page] (biased entry, see PAGE_BIAS in dmg.h)
        emit_movea_l_idx_an_an(block, 0, REG_68K_A_READ_PAGE, REG_68K_D_SCRATCH_0, REG_68K_A_SP);
        // A3 += (s16)gb_sp to complete the biased address
        emit_lea_disp_an_an(block, (int16_t) gb_sp, REG_68K_A_SP, REG_68K_A_SP);
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 1);
        emit_move_l_dn_disp_an(block, REG_68K_D_SCRATCH_1, JIT_CTX_STACK_IN_RAM, REG_68K_A_CTX);
    } else if (ctx && ctx->hram_base && gb_sp >= 0xff82 && gb_sp <= 0xfffe) {
        // HRAM: A3 = hram_base + (gb_sp - 0xFF80)
        uint32_t addr = (uint32_t) ctx->hram_base + (gb_sp - 0xff80);
        emit_movea_l_imm32(block, REG_68K_A_SP, addr);
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 1);
        emit_move_l_dn_disp_an(block, REG_68K_D_SCRATCH_1, JIT_CTX_STACK_IN_RAM, REG_68K_A_CTX);
    } else {
        // slow mode: A3 holds GB SP value (not a valid pointer)
        emit_movea_w_imm16(block, REG_68K_A_SP, gb_sp);
        emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
        emit_move_l_dn_disp_an(block, REG_68K_D_SCRATCH_1, JIT_CTX_STACK_IN_RAM, REG_68K_A_CTX);
    }
}

// Slow path for pop: read 16-bit value via dmg_read16, result in D1.w
// Increments gb_sp by 2 in context. Clobbers D0, D1.
static void compile_slow_pop_to_d1(struct code_block *block)
{
    // D1 = gb_sp
    emit_move_w_disp_an_dn(block, JIT_CTX_GB_SP, REG_68K_A_CTX, REG_68K_D_SCRATCH_1);
    // call dmg_read16 - result in D0.w
    compile_call_dmg_read16(block);
    // increment gb_sp by 2
    emit_addq_w_disp_an(block, 2, JIT_CTX_GB_SP, REG_68K_A_CTX);
    // move result to D1
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);
}

// Slow path for push: write D0.w to stack via dmg_write16
// Decrements gb_sp by 2 in context first. Clobbers D0, D1.
static void compile_slow_push_d0(struct code_block *block)
{
    // decrement gb_sp by 2 first
    emit_subq_w_disp_an(block, 2, JIT_CTX_GB_SP, REG_68K_A_CTX);
    // D1 = gb_sp (new value)
    emit_move_w_disp_an_dn(block, JIT_CTX_GB_SP, REG_68K_A_CTX, REG_68K_D_SCRATCH_1);
    // call dmg_write16 - value in D0.w, addr in D1.w
    compile_call_dmg_write16_d0(block);
}

// Guarded push of a 16-bit constant (call/rst return addresses)
void compile_push_imm16(struct code_block *block, uint16_t value)
{
    size_t slow_push, done;

    // flush before the fast/slow split so both paths see the same D2
    flush_cycles(block);
    emit_tst_l_disp_an(block, JIT_CTX_STACK_IN_RAM, REG_68K_A_CTX);
    slow_push = block->length;
    emit_beq_b(block, 0);

    // Fast path: use A3 directly, store both bytes as immediates
    emit_subq_w_an(block, REG_68K_A_SP, 2);
    emit_subq_w_disp_an(block, 2, JIT_CTX_GB_SP, REG_68K_A_CTX);
    emit_move_b_imm_ind_an(block, value & 0xff, REG_68K_A_SP);
    emit_move_b_imm_disp_an(block, value >> 8, 1, REG_68K_A_SP);
    done = block->length;
    emit_bra_b(block, 0);

    // Slow path
    block->code[slow_push + 1] = block->length - slow_push - 2;
    emit_move_w_dn(block, REG_68K_D_SCRATCH_0, value);
    compile_slow_push_d0(block);

    block->code[done + 1] = block->length - done - 2;
}

// Guarded pop of the return address into D3, zero-extended (ret)
void compile_pop_pc(struct code_block *block)
{
    size_t slow_pop, done;

    // flush before the fast/slow split so both paths see the same D2
    flush_cycles(block);
    emit_tst_l_disp_an(block, JIT_CTX_STACK_IN_RAM, REG_68K_A_CTX);
    slow_pop = block->length;
    emit_beq_b(block, 0);

    // Fast path: use A3 directly
    emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
    emit_move_b_disp_an_dn(block, 1, REG_68K_A_SP, REG_68K_D_NEXT_PC);
    emit_rol_w_8(block, REG_68K_D_NEXT_PC);
    emit_move_b_ind_an_dn(block, REG_68K_A_SP, REG_68K_D_NEXT_PC);
    emit_addq_w_an(block, REG_68K_A_SP, 2);
    emit_addq_w_disp_an(block, 2, JIT_CTX_GB_SP, REG_68K_A_CTX);
    done = block->length;
    emit_bra_b(block, 0);

    // Slow path: dmg_read16 clobbers D3, so build it afterward
    block->code[slow_pop + 1] = block->length - slow_pop - 2;
    compile_slow_pop_to_d1(block);
    emit_moveq_dn(block, REG_68K_D_NEXT_PC, 0);
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_NEXT_PC);

    block->code[done + 1] = block->length - done - 2;
}

int compile_stack_op(
    struct code_block *block,
    uint8_t op,
    struct compile_ctx *ctx,
    uint16_t src_address,
    uint16_t *src_ptr
) {
    switch (op) {
    case 0x08: // ld (u16), sp
        {
            uint16_t addr = READ_BYTE(*src_ptr) | (READ_BYTE(*src_ptr + 1) << 8);
            *src_ptr += 2;

            // read gb_sp from context and write to memory
            emit_move_w_disp_an_dn(block, JIT_CTX_GB_SP, REG_68K_A_CTX, REG_68K_D_SCRATCH_0);
            emit_move_w_dn(block, REG_68K_D_SCRATCH_1, addr);
            compile_call_dmg_write16_d0(block);
        }
        return 1;

    case 0xc5: // push bc
        {
            size_t slow_push, done;

            // Check if sp_adjust is 0 (slow mode)
            // flush before the fast/slow split so both paths see the same D2
            flush_cycles(block);
            emit_tst_l_disp_an(block, JIT_CTX_STACK_IN_RAM, REG_68K_A_CTX);
            slow_push = block->length;
            emit_beq_b(block, 0);  // branch to slow path

            // Fast path: use A3 directly
            // SP -= 2 (both A3 and gb_sp)
            emit_subq_w_an(block, REG_68K_A_SP, 2);
            emit_subq_w_disp_an(block, 2, JIT_CTX_GB_SP, REG_68K_A_CTX);
            // bytes are already in split positions, store via swap
            // [SP+1] = high byte (B)
            emit_swap(block, REG_68K_D_BC);
            emit_move_b_dn_disp_an(block, REG_68K_D_BC, 1, REG_68K_A_SP);
            emit_swap(block, REG_68K_D_BC);
            // [SP] = low byte (C)
            emit_move_b_dn_ind_an(block, REG_68K_D_BC, REG_68K_A_SP);
            done = block->length;
            emit_bra_b(block, 0);

            // Slow path
            block->code[slow_push + 1] = block->length - slow_push - 2;
            compile_join_bc(block, REG_68K_D_SCRATCH_0);
            compile_slow_push_d0(block);

            // Patch done branch
            block->code[done + 1] = block->length - done - 2;
        }
        return 1;

    case 0xd5: // push de
        {
            size_t slow_push, done;

            // flush before the fast/slow split so both paths see the same D2
            flush_cycles(block);
            emit_tst_l_disp_an(block, JIT_CTX_STACK_IN_RAM, REG_68K_A_CTX);
            slow_push = block->length;
            emit_beq_b(block, 0);

            // Fast path: bytes already in split positions
            emit_subq_w_an(block, REG_68K_A_SP, 2);
            emit_subq_w_disp_an(block, 2, JIT_CTX_GB_SP, REG_68K_A_CTX);
            emit_swap(block, REG_68K_D_DE);
            emit_move_b_dn_disp_an(block, REG_68K_D_DE, 1, REG_68K_A_SP);
            emit_swap(block, REG_68K_D_DE);
            emit_move_b_dn_ind_an(block, REG_68K_D_DE, REG_68K_A_SP);
            done = block->length;
            emit_bra_b(block, 0);

            // Slow path
            block->code[slow_push + 1] = block->length - slow_push - 2;
            compile_join_de(block, REG_68K_D_SCRATCH_0);
            compile_slow_push_d0(block);

            block->code[done + 1] = block->length - done - 2;
        }
        return 1;

    case 0xe5: // push hl
        {
            size_t slow_push, done;

            // flush before the fast/slow split so both paths see the same D2
            flush_cycles(block);
            emit_tst_l_disp_an(block, JIT_CTX_STACK_IN_RAM, REG_68K_A_CTX);
            slow_push = block->length;
            emit_beq_b(block, 0);

            // Fast path
            emit_subq_w_an(block, REG_68K_A_SP, 2);
            emit_subq_w_disp_an(block, 2, JIT_CTX_GB_SP, REG_68K_A_CTX);
            emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
            emit_move_b_dn_ind_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_SP);
            emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
            emit_move_b_dn_disp_an(block, REG_68K_D_SCRATCH_1, 1, REG_68K_A_SP);
            done = block->length;
            emit_bra_b(block, 0);

            // Slow path
            block->code[slow_push + 1] = block->length - slow_push - 2;
            emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_0);
            compile_slow_push_d0(block);

            block->code[done + 1] = block->length - done - 2;
        }
        return 1;

    case 0xf5: // push af
        {
            size_t slow_push, done;

            // flush before the fast/slow split so both paths see the same D2
            flush_cycles(block);
            emit_tst_l_disp_an(block, JIT_CTX_STACK_IN_RAM, REG_68K_A_CTX);
            slow_push = block->length;
            emit_beq_b(block, 0);

            // Fast path
            emit_subq_w_an(block, REG_68K_A_SP, 2);
            emit_subq_w_disp_an(block, 2, JIT_CTX_GB_SP, REG_68K_A_CTX);
            // [SP] = F (low byte - flags)
            emit_move_b_dn_ind_an(block, REG_68K_D_FLAGS, REG_68K_A_SP);
            // [SP+1] = A (high byte)
            emit_move_b_dn_disp_an(block, REG_68K_D_A, 1, REG_68K_A_SP);
            done = block->length;
            emit_bra_b(block, 0);

            // Slow path: build AF in D0.w
            block->code[slow_push + 1] = block->length - slow_push - 2;
            emit_move_b_dn_dn(block, REG_68K_D_A, REG_68K_D_SCRATCH_0);
            emit_rol_w_8(block, REG_68K_D_SCRATCH_0);
            emit_move_b_dn_dn(block, REG_68K_D_FLAGS, REG_68K_D_SCRATCH_0);
            compile_slow_push_d0(block);

            block->code[done + 1] = block->length - done - 2;
        }
        return 1;

    case 0xc1: // pop bc
        {
            size_t slow_pop, done;

            // flush before the fast/slow split so both paths see the same D2
            flush_cycles(block);
            emit_tst_l_disp_an(block, JIT_CTX_STACK_IN_RAM, REG_68K_A_CTX);
            slow_pop = block->length;
            emit_beq_b(block, 0);

            // Fast path: load directly into split positions
            emit_swap(block, REG_68K_D_BC);
            emit_move_b_disp_an_dn(block, 1, REG_68K_A_SP, REG_68K_D_BC);  // B
            emit_swap(block, REG_68K_D_BC);
            emit_move_b_ind_an_dn(block, REG_68K_A_SP, REG_68K_D_BC);  // C
            emit_addq_w_an(block, REG_68K_A_SP, 2);
            emit_addq_w_disp_an(block, 2, JIT_CTX_GB_SP, REG_68K_A_CTX);
            done = block->length;
            emit_bra_b(block, 0);

            // Slow path: convert D1.w = 0xBBCC to 0x00BB00CC in BC
            block->code[slow_pop + 1] = block->length - slow_pop - 2;
            compile_slow_pop_to_d1(block);
            emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_BC);  // C = low byte
            emit_rol_w_8(block, REG_68K_D_SCRATCH_1);  // D1.b = B
            emit_swap(block, REG_68K_D_BC);
            emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_BC);  // B = high byte
            emit_swap(block, REG_68K_D_BC);

            // Patch done branch
            block->code[done + 1] = block->length - done - 2;
        }
        return 1;

    case 0xd1: // pop de
        {
            size_t slow_pop, done;

            // flush before the fast/slow split so both paths see the same D2
            flush_cycles(block);
            emit_tst_l_disp_an(block, JIT_CTX_STACK_IN_RAM, REG_68K_A_CTX);
            slow_pop = block->length;
            emit_beq_b(block, 0);

            // Fast path: load directly into split positions
            emit_swap(block, REG_68K_D_DE);
            emit_move_b_disp_an_dn(block, 1, REG_68K_A_SP, REG_68K_D_DE);  // D
            emit_swap(block, REG_68K_D_DE);
            emit_move_b_ind_an_dn(block, REG_68K_A_SP, REG_68K_D_DE);  // E
            emit_addq_w_an(block, REG_68K_A_SP, 2);
            emit_addq_w_disp_an(block, 2, JIT_CTX_GB_SP, REG_68K_A_CTX);
            done = block->length;
            emit_bra_b(block, 0);

            // Slow path: convert D1.w = 0xDDEE to 0x00DD00EE in DE
            block->code[slow_pop + 1] = block->length - slow_pop - 2;
            compile_slow_pop_to_d1(block);
            emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_DE);
            emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
            emit_swap(block, REG_68K_D_DE);
            emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_DE);
            emit_swap(block, REG_68K_D_DE);

            block->code[done + 1] = block->length - done - 2;
        }
        return 1;

    case 0xe1: // pop hl
        {
            size_t slow_pop, done;

            // flush before the fast/slow split so both paths see the same D2
            flush_cycles(block);
            emit_tst_l_disp_an(block, JIT_CTX_STACK_IN_RAM, REG_68K_A_CTX);
            slow_pop = block->length;
            emit_beq_b(block, 0);

            // Fast path
            emit_move_b_disp_an_dn(block, 1, REG_68K_A_SP, REG_68K_D_SCRATCH_1);
            emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
            emit_move_b_ind_an_dn(block, REG_68K_A_SP, REG_68K_D_SCRATCH_1);
            emit_addq_w_an(block, REG_68K_A_SP, 2);
            emit_addq_w_disp_an(block, 2, JIT_CTX_GB_SP, REG_68K_A_CTX);
            done = block->length;
            emit_bra_b(block, 0);

            // Slow path
            block->code[slow_pop + 1] = block->length - slow_pop - 2;
            compile_slow_pop_to_d1(block);

            block->code[done + 1] = block->length - done - 2;

            // HL = D1.w
            emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        }
        return 1;

    case 0xf1: // pop af
        {
            size_t slow_pop, done;

            // flush before the fast/slow split so both paths see the same D2
            flush_cycles(block);
            emit_tst_l_disp_an(block, JIT_CTX_STACK_IN_RAM, REG_68K_A_CTX);
            slow_pop = block->length;
            emit_beq_b(block, 0);

            // Fast path: sets A and F directly
            emit_move_b_disp_an_dn(block, 1, REG_68K_A_SP, REG_68K_D_A);  // A = [SP+1]
            emit_move_b_ind_an_dn(block, REG_68K_A_SP, REG_68K_D_FLAGS);  // F = [SP]
            emit_addq_w_an(block, REG_68K_A_SP, 2);
            emit_addq_w_disp_an(block, 2, JIT_CTX_GB_SP, REG_68K_A_CTX);
            done = block->length;
            emit_bra_b(block, 0);

            // Slow path
            block->code[slow_pop + 1] = block->length - slow_pop - 2;
            compile_slow_pop_to_d1(block);
            // D1.w = 0xAAFF, A = high byte, F = low byte
            emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_FLAGS);  // F = low
            emit_rol_w_8(block, REG_68K_D_SCRATCH_1);  // D1.b = A
            emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_A);  // A = high

            // Patch done branch
            block->code[done + 1] = block->length - done - 2;
        }
        return 1;

    case 0xe8: // add sp, i8
        {
            int8_t offset = (int8_t) READ_BYTE(*src_ptr);
            (*src_ptr)++;

            if (offset != 0) {
                // update both A3 and gb_sp
                emit_lea_disp_an_an(block, offset, REG_68K_A_SP, REG_68K_A_SP);
                if (offset > 0 && offset <= 8) {
                    emit_addq_w_disp_an(block, offset, JIT_CTX_GB_SP, REG_68K_A_CTX);
                } else if (offset < 0 && offset >= -8) {
                    emit_subq_w_disp_an(block, -offset, JIT_CTX_GB_SP, REG_68K_A_CTX);
                } else {
                    emit_addi_w_disp_an(block, offset, JIT_CTX_GB_SP, REG_68K_A_CTX);
                }
            }
        }
        return 1;

    case 0xf8: // ld hl, sp+i8
        {
            int8_t offset = (int8_t) READ_BYTE(*src_ptr);
            (*src_ptr)++;

            // Load gb_sp from context, not A3, might be native pointer
            emit_move_w_disp_an_dn(block, JIT_CTX_GB_SP, REG_68K_A_CTX, REG_68K_D_SCRATCH_0);

            // Compute HL = GB_SP + sign_extended(offset)
            if (offset > 0 && offset <= 8) {
                emit_addq_w_dn(block, REG_68K_D_SCRATCH_0, offset);
            } else if (offset < 0 && -offset <= 8) {
                emit_subq_w_dn(block, REG_68K_D_SCRATCH_0, -offset);
            } else if (offset != 0) {
                emit_move_w_dn(block, REG_68K_D_SCRATCH_1, offset);
                emit_add_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
            }

            // Store result in HL
            emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_0, REG_68K_A_HL);
        }
        return 1;

    case 0xf9: // ld sp, hl
        {
            // Store HL to gb_sp
            emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
            emit_move_w_dn_disp_an(block, REG_68K_D_SCRATCH_1, JIT_CTX_GB_SP, REG_68K_A_CTX);

            if (ctx && ctx->wram_base) {
                // Runtime range check for WRAM and HRAM
                size_t not_wram, not_hram, done, done2;

                // Check WRAM: $c002 <= HL <= $e000, same bounds as the
                // compile-time path above
                emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
                emit_subi_w_dn(block, 0xc002, REG_68K_D_SCRATCH_1);
                emit_cmpi_w_imm_dn(block, 0xe000 - 0xc002, REG_68K_D_SCRATCH_1);
                not_wram = block->length;
                emit_bcc_opcode_w(block, COND_HI, 0);  // branch if out of range

                // WRAM path: use page table for correct bank
                // A3 = read_page[(HL-1) >> 8] + (s16)HL (entries are
                // biased). page of HL-1, same rule as the compile-time
                // path: HL = $e000 anchors in page $df
                emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
                // D0 = (HL - 1) >> 8 (page number)
                emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
                emit_subq_w_dn(block, REG_68K_D_SCRATCH_0, 1);
                emit_lsr_w_imm_dn(block, 8, REG_68K_D_SCRATCH_0);
                // A3 = read_page[page] (biased entry, see PAGE_BIAS in dmg.h)
                compile_page_lookup(block, REG_68K_A_READ_PAGE, REG_68K_D_SCRATCH_0, REG_68K_A_SP);
                // A3 += (s16)HL to complete the biased address
                emit_adda_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_SP);
                emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 1);
                emit_move_l_dn_disp_an(block, REG_68K_D_SCRATCH_1, JIT_CTX_STACK_IN_RAM, REG_68K_A_CTX);
                done = block->length;
                emit_bra_w(block, 0);

                // Not WRAM - check HRAM
                block->code[not_wram + 2] = (block->length - not_wram - 2) >> 8;
                block->code[not_wram + 3] = (block->length - not_wram - 2) & 0xff;

                if (ctx->hram_base) {
                    // Check HRAM: $ff82 <= HL <= $fffe
                    emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
                    emit_subi_w_dn(block, 0xff82, REG_68K_D_SCRATCH_1);
                    emit_cmpi_w_imm_dn(block, 0xfffe - 0xff82, REG_68K_D_SCRATCH_1);
                    not_hram = block->length;
                    emit_bcc_opcode_w(block, COND_HI, 0);  // branch if out of range

                    // HRAM path: A3 = hram_base + (HL - $FF80)
                    emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
                    emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
                    emit_movea_l_imm32(block, REG_68K_A_SP, (uint32_t) ctx->hram_base - 0xff80);
                    emit_adda_l_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_SP);
                    emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 1);
                    emit_move_l_dn_disp_an(block, REG_68K_D_SCRATCH_1, JIT_CTX_STACK_IN_RAM, REG_68K_A_CTX);
                    done2 = block->length;
                    emit_bra_w(block, 0);

                    // Patch not_hram branch to slow mode
                    block->code[not_hram + 2] = (block->length - not_hram - 2) >> 8;
                    block->code[not_hram + 3] = (block->length - not_hram - 2) & 0xff;
                }

                // Slow mode: A3 = HL (GB SP value)
                emit_movea_w_an_an(block, REG_68K_A_HL, REG_68K_A_SP);
                emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
                emit_move_l_dn_disp_an(block, REG_68K_D_SCRATCH_1, JIT_CTX_STACK_IN_RAM, REG_68K_A_CTX);

                // Patch done branches
                block->code[done + 2] = (block->length - done - 2) >> 8;
                block->code[done + 3] = (block->length - done - 2) & 0xff;
                if (ctx->hram_base) {
                    block->code[done2 + 2] = (block->length - done2 - 2) >> 8;
                    block->code[done2 + 3] = (block->length - done2 - 2) & 0xff;
                }
            } else {
                // No context - simple path for testing
                emit_movea_w_an_an(block, REG_68K_A_HL, REG_68K_A_SP);
                emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
                emit_move_l_dn_disp_an(block, REG_68K_D_SCRATCH_1, JIT_CTX_STACK_IN_RAM, REG_68K_A_CTX);
            }
        }
        return 1;

    default:
        return 0;
    }
}
