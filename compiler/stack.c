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
    uint16_t sp_val = READ_BYTE(*src_ptr) | (READ_BYTE(*src_ptr + 1) << 8);
    *src_ptr += 2;

    // A3 = virtual address (0x08000000 | SP)
    emit_movea_l_imm32(block, REG_68K_A_SP, GB_VIRTUAL_BASE | sp_val);
}

// Slow path for pop: read 16-bit value via dmg_read16, result in D1.w
// Increments A3 by 2. Clobbers D0, D1.
static void compile_slow_pop_to_d1(struct code_block *block)
{
    // D1 = SP (low word of A3)
    emit_move_w_an_dn(block, REG_68K_A_SP, REG_68K_D_SCRATCH_1);
    // call dmg_read16 - result in D0.w
    compile_slow_dmg_read16(block);
    // increment A3 by 2
    emit_addq_w_an(block, REG_68K_A_SP, 2);
    // move result to D1
    emit_move_w_dn_dn(block, REG_68K_D_SCRATCH_0, REG_68K_D_SCRATCH_1);
}

// Slow path for push: write D0.w to stack via dmg_write16
// Decrements A3 by 2 first. Clobbers D0, D1.
static void compile_slow_push_d0(struct code_block *block)
{
    // decrement A3 by 2 first
    emit_subq_w_an(block, REG_68K_A_SP, 2);
    // D1 = SP (new value, low word of A3)
    emit_move_w_an_dn(block, REG_68K_A_SP, REG_68K_D_SCRATCH_1);
    // call dmg_write16 - value in D0.w, addr in D1.w
    compile_slow_dmg_write16(block);
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

            // read SP from A3's low word and write to memory
            emit_move_w_an_dn(block, REG_68K_A_SP, REG_68K_D_SCRATCH_0);
            emit_move_l_dn(block, REG_68K_D_SCRATCH_1, addr);
            compile_call_dmg_write16_d0(block);
        }
        return 1;

    case 0xc5: // push bc
        {
            size_t slow_push, done;

            // Check if SP is in HRAM (low word >= $ff80)
            emit_move_w_an_dn(block, REG_68K_A_SP, REG_68K_D_SCRATCH_1);
            emit_cmpi_w_imm_dn(block, 0xff80, REG_68K_D_SCRATCH_1);
            slow_push = block->length;
            emit_bcc_w(block, 0);  // branch to slow path if >= $ff80

            // Fast path: use A3 directly (MMU translates virtual address)
            emit_subq_w_an(block, REG_68K_A_SP, 2);
            // Reconstruct BC into D1.w
            compile_join_bc(block, REG_68K_D_SCRATCH_1);
            // [SP] = low byte (C)
            emit_move_b_dn_ind_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_SP);
            // swap to get high byte
            emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
            // [SP+1] = high byte (B)
            emit_move_b_dn_disp_an(block, REG_68K_D_SCRATCH_1, 1, REG_68K_A_SP);
            done = block->length;
            emit_bra_w(block, 0);

            // Slow path
            block->code[slow_push + 2] = (block->length - slow_push - 2) >> 8;
            block->code[slow_push + 3] = (block->length - slow_push - 2) & 0xff;
            compile_join_bc(block, REG_68K_D_SCRATCH_0);
            compile_slow_push_d0(block);

            // Patch done branch
            block->code[done + 2] = (block->length - done - 2) >> 8;
            block->code[done + 3] = (block->length - done - 2) & 0xff;
        }
        return 1;

    case 0xd5: // push de
        {
            size_t slow_push, done;

            emit_move_w_an_dn(block, REG_68K_A_SP, REG_68K_D_SCRATCH_1);
            emit_cmpi_w_imm_dn(block, 0xff80, REG_68K_D_SCRATCH_1);
            slow_push = block->length;
            emit_bcc_w(block, 0);

            // Fast path
            emit_subq_w_an(block, REG_68K_A_SP, 2);
            compile_join_de(block, REG_68K_D_SCRATCH_1);
            emit_move_b_dn_ind_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_SP);
            emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
            emit_move_b_dn_disp_an(block, REG_68K_D_SCRATCH_1, 1, REG_68K_A_SP);
            done = block->length;
            emit_bra_w(block, 0);

            // Slow path
            block->code[slow_push + 2] = (block->length - slow_push - 2) >> 8;
            block->code[slow_push + 3] = (block->length - slow_push - 2) & 0xff;
            compile_join_de(block, REG_68K_D_SCRATCH_0);
            compile_slow_push_d0(block);

            block->code[done + 2] = (block->length - done - 2) >> 8;
            block->code[done + 3] = (block->length - done - 2) & 0xff;
        }
        return 1;

    case 0xe5: // push hl
        {
            size_t slow_push, done;

            emit_move_w_an_dn(block, REG_68K_A_SP, REG_68K_D_SCRATCH_1);
            emit_cmpi_w_imm_dn(block, 0xff80, REG_68K_D_SCRATCH_1);
            slow_push = block->length;
            emit_bcc_w(block, 0);

            // Fast path
            emit_subq_w_an(block, REG_68K_A_SP, 2);
            emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
            emit_move_b_dn_ind_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_SP);
            emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
            emit_move_b_dn_disp_an(block, REG_68K_D_SCRATCH_1, 1, REG_68K_A_SP);
            done = block->length;
            emit_bra_w(block, 0);

            // Slow path
            block->code[slow_push + 2] = (block->length - slow_push - 2) >> 8;
            block->code[slow_push + 3] = (block->length - slow_push - 2) & 0xff;
            emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_0);
            compile_slow_push_d0(block);

            block->code[done + 2] = (block->length - done - 2) >> 8;
            block->code[done + 3] = (block->length - done - 2) & 0xff;
        }
        return 1;

    case 0xf5: // push af
        {
            size_t slow_push, done;

            emit_move_w_an_dn(block, REG_68K_A_SP, REG_68K_D_SCRATCH_1);
            emit_cmpi_w_imm_dn(block, 0xff80, REG_68K_D_SCRATCH_1);
            slow_push = block->length;
            emit_bcc_w(block, 0);

            // Fast path
            emit_subq_w_an(block, REG_68K_A_SP, 2);
            // [SP] = F (low byte - flags)
            emit_move_b_dn_ind_an(block, REG_68K_D_FLAGS, REG_68K_A_SP);
            // [SP+1] = A (high byte)
            emit_move_b_dn_disp_an(block, REG_68K_D_A, 1, REG_68K_A_SP);
            done = block->length;
            emit_bra_w(block, 0);

            // Slow path: build AF in D0.w
            block->code[slow_push + 2] = (block->length - slow_push - 2) >> 8;
            block->code[slow_push + 3] = (block->length - slow_push - 2) & 0xff;
            emit_move_b_dn_dn(block, REG_68K_D_A, REG_68K_D_SCRATCH_0);
            emit_rol_w_8(block, REG_68K_D_SCRATCH_0);
            emit_move_b_dn_dn(block, REG_68K_D_FLAGS, REG_68K_D_SCRATCH_0);
            compile_slow_push_d0(block);

            block->code[done + 2] = (block->length - done - 2) >> 8;
            block->code[done + 3] = (block->length - done - 2) & 0xff;
        }
        return 1;

    case 0xc1: // pop bc
        {
            size_t slow_pop, done;

            emit_move_w_an_dn(block, REG_68K_A_SP, REG_68K_D_SCRATCH_1);
            emit_cmpi_w_imm_dn(block, 0xff80, REG_68K_D_SCRATCH_1);
            slow_pop = block->length;
            emit_bcc_w(block, 0);

            // Fast path: use A3
            emit_move_b_disp_an_dn(block, 1, REG_68K_A_SP, REG_68K_D_SCRATCH_1);
            emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
            emit_move_b_ind_an_dn(block, REG_68K_A_SP, REG_68K_D_SCRATCH_1);
            emit_addq_w_an(block, REG_68K_A_SP, 2);
            done = block->length;
            emit_bra_w(block, 0);

            // Slow path
            block->code[slow_pop + 2] = (block->length - slow_pop - 2) >> 8;
            block->code[slow_pop + 3] = (block->length - slow_pop - 2) & 0xff;
            compile_slow_pop_to_d1(block);

            // Patch done branch
            block->code[done + 2] = (block->length - done - 2) >> 8;
            block->code[done + 3] = (block->length - done - 2) & 0xff;

            // Convert D1.w = 0xBBCC to 0x00BB00CC in BC
            emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_BC);  // C = low byte
            emit_rol_w_8(block, REG_68K_D_SCRATCH_1);  // D1.b = B
            emit_swap(block, REG_68K_D_BC);
            emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_BC);  // B = high byte
            emit_swap(block, REG_68K_D_BC);
        }
        return 1;

    case 0xd1: // pop de
        {
            size_t slow_pop, done;

            emit_move_w_an_dn(block, REG_68K_A_SP, REG_68K_D_SCRATCH_1);
            emit_cmpi_w_imm_dn(block, 0xff80, REG_68K_D_SCRATCH_1);
            slow_pop = block->length;
            emit_bcc_w(block, 0);

            // Fast path
            emit_move_b_disp_an_dn(block, 1, REG_68K_A_SP, REG_68K_D_SCRATCH_1);
            emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
            emit_move_b_ind_an_dn(block, REG_68K_A_SP, REG_68K_D_SCRATCH_1);
            emit_addq_w_an(block, REG_68K_A_SP, 2);
            done = block->length;
            emit_bra_w(block, 0);

            // Slow path
            block->code[slow_pop + 2] = (block->length - slow_pop - 2) >> 8;
            block->code[slow_pop + 3] = (block->length - slow_pop - 2) & 0xff;
            compile_slow_pop_to_d1(block);

            block->code[done + 2] = (block->length - done - 2) >> 8;
            block->code[done + 3] = (block->length - done - 2) & 0xff;

            // Convert D1.w = 0xDDEE to 0x00DD00EE in DE
            emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_DE);
            emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
            emit_swap(block, REG_68K_D_DE);
            emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_DE);
            emit_swap(block, REG_68K_D_DE);
        }
        return 1;

    case 0xe1: // pop hl
        {
            size_t slow_pop, done;

            emit_move_w_an_dn(block, REG_68K_A_SP, REG_68K_D_SCRATCH_1);
            emit_cmpi_w_imm_dn(block, 0xff80, REG_68K_D_SCRATCH_1);
            slow_pop = block->length;
            emit_bcc_w(block, 0);

            // Fast path
            emit_move_b_disp_an_dn(block, 1, REG_68K_A_SP, REG_68K_D_SCRATCH_1);
            emit_rol_w_8(block, REG_68K_D_SCRATCH_1);
            emit_move_b_ind_an_dn(block, REG_68K_A_SP, REG_68K_D_SCRATCH_1);
            emit_addq_w_an(block, REG_68K_A_SP, 2);
            done = block->length;
            emit_bra_w(block, 0);

            // Slow path
            block->code[slow_pop + 2] = (block->length - slow_pop - 2) >> 8;
            block->code[slow_pop + 3] = (block->length - slow_pop - 2) & 0xff;
            compile_slow_pop_to_d1(block);

            block->code[done + 2] = (block->length - done - 2) >> 8;
            block->code[done + 3] = (block->length - done - 2) & 0xff;

            // HL = D1.w
            emit_movea_w_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_HL);
        }
        return 1;

    case 0xf1: // pop af
        {
            size_t slow_pop, done;

            emit_move_w_an_dn(block, REG_68K_A_SP, REG_68K_D_SCRATCH_1);
            emit_cmpi_w_imm_dn(block, 0xff80, REG_68K_D_SCRATCH_1);
            slow_pop = block->length;
            emit_bcc_w(block, 0);

            // Fast path: sets A and F directly
            emit_move_b_disp_an_dn(block, 1, REG_68K_A_SP, REG_68K_D_A);  // A = [SP+1]
            emit_move_b_ind_an_dn(block, REG_68K_A_SP, REG_68K_D_FLAGS);  // F = [SP]
            emit_addq_w_an(block, REG_68K_A_SP, 2);
            done = block->length;
            emit_bra_w(block, 0);

            // Slow path
            block->code[slow_pop + 2] = (block->length - slow_pop - 2) >> 8;
            block->code[slow_pop + 3] = (block->length - slow_pop - 2) & 0xff;
            compile_slow_pop_to_d1(block);
            // D1.w = 0xAAFF, A = high byte, F = low byte
            emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_FLAGS);  // F = low
            emit_rol_w_8(block, REG_68K_D_SCRATCH_1);  // D1.b = A
            emit_move_b_dn_dn(block, REG_68K_D_SCRATCH_1, REG_68K_D_A);  // A = high

            // Patch done branch
            block->code[done + 2] = (block->length - done - 2) >> 8;
            block->code[done + 3] = (block->length - done - 2) & 0xff;
        }
        return 1;

    case 0xe8: // add sp, i8
        {
            int8_t offset = (int8_t) READ_BYTE(*src_ptr);
            (*src_ptr)++;

            if (offset != 0) {
                emit_lea_disp_an_an(block, offset, REG_68K_A_SP, REG_68K_A_SP);
            }
        }
        return 1;

    case 0xf8: // ld hl, sp+i8
        {
            int8_t offset = (int8_t) READ_BYTE(*src_ptr);
            (*src_ptr)++;

            // Load SP from A3's low word
            emit_move_w_an_dn(block, REG_68K_A_SP, REG_68K_D_SCRATCH_0);

            // Compute HL = SP + sign_extended(offset)
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
            // A3 = 0x08000000 | HL (virtual address)
            emit_moveq_dn(block, REG_68K_D_SCRATCH_1, 0);
            emit_move_w_an_dn(block, REG_68K_A_HL, REG_68K_D_SCRATCH_1);
            emit_movea_l_imm32(block, REG_68K_A_SP, GB_VIRTUAL_BASE);
            emit_adda_l_dn_an(block, REG_68K_D_SCRATCH_1, REG_68K_A_SP);
        }
        return 1;

    default:
        return 0;
    }
}
