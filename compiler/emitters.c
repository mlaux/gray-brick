#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "compiler.h"

void emit_byte(struct code_block *block, uint8_t byte)
{
    if (block->length < sizeof(block->code)) {
        block->code[block->length++] = byte;
    }
}

void emit_word(struct code_block *block, uint16_t word)
{
    emit_byte(block, word >> 8);
    emit_byte(block, word & 0xff);
}

void emit_long(struct code_block *block, uint32_t val)
{
    emit_byte(block, val >> 24);
    emit_byte(block, val >> 16);
    emit_byte(block, val >> 8);
    emit_byte(block, val & 0xff);
}

void emit_moveq_dn(struct code_block *block, uint8_t reg, int8_t imm)
{
    emit_byte(block, 0x70 | reg << 1);
    emit_byte(block, (uint8_t) imm);
}

// move.b #imm, Dn
void emit_move_b_dn(struct code_block *block, uint8_t reg, int8_t imm)
{
    // 01 = byte mode
    // dest effective address = rrr 000
    // src effective address = 111 100 means immediate
    uint16_t ins = (1 << 12) | (reg << 9) | (7 << 3) | 4;
    emit_word(block, ins);
    emit_word(block, (uint8_t) imm);
}

// move.w #imm, Dn
void emit_move_w_dn(struct code_block *block, uint8_t reg, int16_t imm)
{
    uint16_t ins = (3 << 12) | (reg << 9) | (7 << 3) | 4;
    emit_word(block, ins);
    emit_word(block, (uint16_t) imm);
}

// move.l #imm, Dn
void emit_move_l_dn(struct code_block *block, uint8_t reg, int32_t imm)
{
    uint16_t ins = (2 << 12) | (reg << 9) | (7 << 3) | 4;
    emit_word(block, ins);
    emit_long(block, (uint32_t) imm);
}

// rol.w #8, Dn
void emit_rol_w_8(struct code_block *block, uint8_t reg)
{
    // 1110 ccc d ss i 11 rrr
    // ccc=000 (8), d=1 (left), ss=01 (word), i=0 (immediate), rrr=reg
    emit_word(block, 0xe158 | reg);
}

// ror.w #8, Dn
void emit_ror_w_8(struct code_block *block, uint8_t reg)
{
    // ccc=000 (8), d=0 (right), ss=01 (word), i=0 (immediate), rrr=reg
    emit_word(block, 0xe058 | reg);
}

// swap Dn - exchange high and low 16-bit words
void emit_swap(struct code_block *block, uint8_t reg)
{
    // 0100 1000 0100 0 rrr
    emit_word(block, 0x4840 | reg);
}

// move.w An, Dn - copy address register to data register
void emit_move_w_an_dn(struct code_block *block, uint8_t areg, uint8_t dreg)
{
    // 00 11 ddd 000 001 aaa
    emit_word(block, 0x3008 | (dreg << 9) | areg);
}

// movea.w Dn, An - copy data register to address register
void emit_movea_w_dn_an(struct code_block *block, uint8_t dreg, uint8_t areg)
{
    // 00 11 aaa 001 000 ddd
    emit_word(block, 0x3040 | (areg << 9) | dreg);
}

// movea.w An, An - copy address register to address register
void emit_movea_w_an_an(struct code_block *block, uint8_t src_areg, uint8_t dest_areg)
{
    // 00 11 ddd 001 001 sss
    emit_word(block, 0x3048 | (dest_areg << 9) | src_areg);
}

void emit_movea_w_imm16(struct code_block *block, uint8_t areg, uint16_t val)
{
    // 00 11 aaa 001 111 100
    emit_word(block, 0x307c | areg << 9);
    emit_word(block, val);
}

// subq.b #val, Dn
void emit_subq_b_dn(struct code_block *block, uint8_t dreg, uint8_t val)
{
    uint16_t ddd;

    // 0101 ddd 1 00 000 rrr
    if (val == 0 || val > 8) {
        printf("can only subq values between 1 and 8\n");
        exit(1);
    }
    ddd = val == 8 ? 0 : val;
    emit_word(block, 0x5100 | ddd << 9 | dreg);
}

// subq.l #val, Dn
void emit_subq_l_dn(struct code_block *block, uint8_t dreg, uint8_t val)
{
    uint16_t ddd;

    // 0101 ddd 1 10 000 rrr
    if (val == 0 || val > 8) {
        printf("can only subq values between 1 and 8\n");
        exit(1);
    }
    ddd = val == 8 ? 0 : val;
    emit_word(block, 0x5180 | ddd << 9 | dreg);
}

// subq.w #val, An
void emit_subq_w_an(struct code_block *block, uint8_t areg, uint8_t val)
{
    uint16_t ddd;

    // 0101 ddd 1 01 001 aaa
    if (val == 0 || val > 8) {
        printf("can only subq values between 1 and 8\n");
        exit(1);
    }
    ddd = val == 8 ? 0 : val;
    emit_word(block, 0x5148 | ddd << 9 | areg);
}

// subq.l #val, An
void emit_subq_l_an(struct code_block *block, uint8_t areg, uint8_t val)
{
    uint16_t ddd;

    // 0101 ddd 1 10 001 rrr
    if (val == 0 || val > 8) {
        printf("can only subq values between 1 and 8\n");
        exit(1);
    }
    ddd = val == 8 ? 0 : val;
    emit_word(block, 0x5188 | ddd << 9 | areg);
}

// subq.w #val, Dn
void emit_subq_w_dn(struct code_block *block, uint8_t dreg, uint8_t val)
{
    uint16_t ddd;

    // 0101 ddd 1 01 000 rrr
    if (val == 0 || val > 8) {
        printf("can only subq values between 1 and 8\n");
        exit(1);
    }
    ddd = val == 8 ? 0 : val;
    emit_word(block, 0x5140 | ddd << 9 | dreg);
}

void emit_addq_b_dn(struct code_block *block, uint8_t dreg, uint8_t val)
{
    uint16_t ddd;

    // 0101 ddd 0 00 000 rrr
    if (val == 0 || val > 8) {
        printf("can only addq values between 1 and 8\n");
        exit(1);
    }
    ddd = val == 8 ? 0 : val;
    emit_word(block, 0x5000 | ddd << 9 | dreg);
}

void emit_addq_w_dn(struct code_block *block, uint8_t dreg, uint8_t val)
{
    uint16_t ddd;

    // 0101 ddd 0 01 000 rrr
    if (val == 0 || val > 8) {
        printf("can only addq values between 1 and 8\n");
        exit(1);
    }
    ddd = val == 8 ? 0 : val;
    emit_word(block, 0x5040 | ddd << 9 | dreg);
}

void emit_addq_l_dn(struct code_block *block, uint8_t dreg, uint8_t val)
{
    uint16_t ddd;

    // 0101 ddd 0 10 000 rrr
    if (val == 0 || val > 8) {
        printf("can only addq values between 1 and 8\n");
        exit(1);
    }
    ddd = val == 8 ? 0 : val;
    emit_word(block, 0x5080 | ddd << 9 | dreg);
}

// addq.w #val, An
void emit_addq_w_an(struct code_block *block, uint8_t areg, uint8_t val)
{
    uint16_t ddd;

    // 0101 ddd 0 01 001 aaa
    if (val == 0 || val > 8) {
        printf("can only addq values between 1 and 8\n");
        exit(1);
    }
    ddd = val == 8 ? 0 : val;
    emit_word(block, 0x5048 | ddd << 9 | areg);
}

// move.w Dn, (An) - store data register to memory via address register
void emit_move_w_dn_ind_an(struct code_block *block, uint8_t dreg, uint8_t areg)
{
    // 00 11 aaa 010 000 ddd
    emit_word(block, 0x3080 | (areg << 9) | dreg);
}

// move.w (An), Dn - load from memory via address register to data register
void emit_move_w_ind_an_dn(struct code_block *block, uint8_t areg, uint8_t dreg)
{
    // 00 11 ddd 000 010 aaa
    emit_word(block, 0x3010 | (dreg << 9) | areg);
}

// move.b Dn, (An) - store byte to memory via address register
void emit_move_b_dn_ind_an(struct code_block *block, uint8_t dreg, uint8_t areg)
{
    // 00 01 aaa 010 000 ddd
    emit_word(block, 0x1080 | (areg << 9) | dreg);
}

// move.b Dn, d(An) - store byte to memory with displacement
void emit_move_b_dn_disp_an(struct code_block *block, uint8_t dreg, int16_t disp, uint8_t areg)
{
    // 00 01 aaa 101 000 ddd
    emit_word(block, 0x1140 | (areg << 9) | dreg);
    emit_word(block, disp);
}

// move.b (An), Dn - load byte from memory via address register
void emit_move_b_ind_an_dn(struct code_block *block, uint8_t areg, uint8_t dreg)
{
    // 00 01 ddd 000 010 aaa
    emit_word(block, 0x1010 | (dreg << 9) | areg);
}

// move.b d(An), Dn - load byte from memory with displacement
void emit_move_b_disp_an_dn(struct code_block *block, int16_t disp, uint8_t areg, uint8_t dreg)
{
    // 00 01 ddd 000 101 aaa
    emit_word(block, 0x1028 | (dreg << 9) | areg);
    emit_word(block, disp);
}

// andi.w #imm, Dn
void emit_andi_w_dn(struct code_block *block, uint8_t dreg, uint16_t imm)
{
    // 0000 0010 01 000 rrr
    emit_word(block, 0x0240 | dreg);
    emit_word(block, imm);
}

// andi.l #imm, Dn
void emit_andi_l_dn(struct code_block *block, uint8_t dreg, uint32_t imm)
{
    // 0000 0010 10 000 rrr
    emit_word(block, 0x0280 | dreg);
    emit_long(block, imm);
}

// cmp.b #imm, Dn
void emit_cmp_b_imm_dn(struct code_block *block, uint8_t dreg, uint8_t imm)
{
    // 0000 1100 00 000 rrr
    emit_word(block, 0x0c00 | dreg);
    emit_word(block, imm);
}

// cmp.b Ds, Dd - compare data registers (Dd - Ds, set flags)
void emit_cmp_b_dn_dn(struct code_block *block, uint8_t src, uint8_t dest)
{
    // 1011 ddd 000 000 sss
    emit_word(block, 0xb000 | (dest << 9) | src);
}

// scc Dn - set byte based on condition
// cond: 0x7 = seq (Z=1), 0x6 = sne (Z=0), 0x5 = scs (C=1), 0x4 = scc (C=0)
void emit_scc(struct code_block *block, uint8_t cond, uint8_t dreg)
{
    // 0101 cccc 11 000 rrr
    emit_word(block, 0x50c0 | (cond << 8) | dreg);
}

// andi.b #imm, Dn
void emit_andi_b_dn(struct code_block *block, uint8_t dreg, uint8_t imm)
{
    // 0000 0010 00 000 rrr
    emit_word(block, 0x0200 | dreg);
    emit_word(block, imm);
}

// ori.b #imm, Dn
void emit_ori_b_dn(struct code_block *block, uint8_t dreg, uint8_t imm)
{
    // 0000 0000 00 000 rrr
    emit_word(block, 0x0000 | dreg);
    emit_word(block, imm);
}

// subi.b #imm, Dn
void emit_subi_b_dn(struct code_block *block, uint8_t dreg, uint8_t imm)
{
    // 0000 0100 00 000 rrr
    emit_word(block, 0x0400 | dreg);
    emit_word(block, imm);
}

// addi.b #imm, Dn
void emit_addi_b_dn(struct code_block *block, uint8_t dreg, uint8_t imm)
{
    // 0000 0110 00 000 rrr
    emit_word(block, 0x0600 | dreg);
    emit_word(block, imm);
}

// addi.l #imm, Dn
void emit_addi_l_dn(struct code_block *block, uint8_t dreg, uint32_t imm)
{
    // 0000 0110 10 000 rrr
    emit_word(block, 0x0680 | dreg);
    emit_long(block, imm);
}

// or.b Ds, Dd (result to Dd)
void emit_or_b_dn_dn(struct code_block *block, uint8_t src, uint8_t dest)
{
    // 1000 ddd 0 00 000 sss (direction bit 0 = result to Dn)
    emit_word(block, 0x8000 | (dest << 9) | src);
}

// or.l Ds, Dd (result to Dd)
void emit_or_l_dn_dn(struct code_block *block, uint8_t src, uint8_t dest)
{
    // 1000 ddd 0 10 000 sss (direction bit 0 = result to Dn, size=10 for long)
    emit_word(block, 0x8080 | (dest << 9) | src);
}

// Emit: rts
void emit_rts(struct code_block *block)
{
    emit_word(block, 0x4e75);
}

// Emit: movea.l JIT_CTX_DISPATCH(a4), a0; jmp (a0)
// Jump to dispatcher_return routine for block chaining
// 6 bytes total
void emit_dispatch_jump(struct code_block *block)
{
    // movea.l 24(a4), a0 = 206c 0018
    emit_word(block, 0x206c);
    emit_word(block, JIT_CTX_DISPATCH);
    // jmp (a0) = 4ed0
    emit_word(block, 0x4ed0);
}

// bra.b - branch always with 8-bit displacement
void emit_bra_b(struct code_block *block, int8_t disp)
{
    emit_word(block, 0x6000 | ((uint8_t) disp));
}

// bra.w - branch always with 16-bit displacement
void emit_bra_w(struct code_block *block, int16_t disp)
{
    emit_word(block, 0x6000);
    emit_word(block, disp);
}

// beq.b - branch if Z=1 with 8-bit displacement
void emit_beq_b(struct code_block *block, int8_t disp)
{
    // 0110 0111 dddd dddd
    emit_word(block, 0x6700 | (disp & 0xff));
}

// beq.w - branch if Z=1 with 16-bit displacement
void emit_beq_w(struct code_block *block, int16_t disp)
{
    emit_word(block, 0x6700);
    emit_word(block, disp);
}

// bne.w - branch if Z=0 with 16-bit displacement
void emit_bne_w(struct code_block *block, int16_t disp)
{
    emit_word(block, 0x6600);
    emit_word(block, disp);
}

// bcs.w - branch if C=1 with 16-bit displacement
void emit_bcs_w(struct code_block *block, int16_t disp)
{
    emit_word(block, 0x6500);
    emit_word(block, disp);
}

// bcc.w - branch if C=0 with 16-bit displacement
void emit_bcc_w(struct code_block *block, int16_t disp)
{
    emit_word(block, 0x6400);
    emit_word(block, disp);
}

// bcc.s - branch if C=0 with 8-bit displacement
void emit_bcc_s(struct code_block *block, int8_t disp)
{
    emit_word(block, 0x6400 | ((uint8_t) disp));
}

// emit Bcc.w with condition code (4=cc, 5=cs, 6=ne, 7=eq)
void emit_bcc_opcode_w(struct code_block *block, int cond, int16_t disp)
{
    emit_word(block, 0x6000 | (cond << 8));
    emit_word(block, disp);
}

// btst #bit, Dn - test bit, sets Z=1 if bit is 0
void emit_btst_imm_dn(struct code_block *block, uint8_t bit, uint8_t dreg)
{
    // 0000 1000 00 000 rrr, followed by bit number
    emit_word(block, 0x0800 | dreg);
    emit_word(block, bit);
}

// bclr #bit, Dn - clear bit, sets Z based on previous bit value
void emit_bclr_imm_dn(struct code_block *block, uint8_t bit, uint8_t dreg)
{
    // 0000 1000 10 000 rrr, followed by bit number
    emit_word(block, 0x0880 | dreg);
    emit_word(block, bit);
}

// bset #bit, Dn - set bit, sets Z based on previous bit value
void emit_bset_imm_dn(struct code_block *block, uint8_t bit, uint8_t dreg)
{
    // 0000 1000 11 000 rrr, followed by bit number
    emit_word(block, 0x08c0 | dreg);
    emit_word(block, bit);
}

// move.l Ds, Dd - copy data register to data register (long)
void emit_move_l_dn_dn(struct code_block *block, uint8_t src, uint8_t dest)
{
    // 00 10 ddd 000 000 sss
    emit_word(block, 0x2000 | (dest << 9) | src);
}

// move.w Ds, Dd - copy data register to data register (word)
void emit_move_w_dn_dn(struct code_block *block, uint8_t src, uint8_t dest)
{
    // 00 11 ddd 000 000 sss
    emit_word(block, 0x3000 | (dest << 9) | src);
}

// lsl.w #imm, Dn - logical shift left word by immediate
void emit_lsl_w_imm_dn(struct code_block *block, uint8_t count, uint8_t dreg)
{
    uint16_t ccc;

    // 1110 ccc 1 01 i 01 rrr  (i=0 for immediate, size=01 for word)
    if (count == 0 || count > 8) {
        printf("can only lsl by 1-8\n");
        exit(1);
    }
    ccc = count == 8 ? 0 : count;
    emit_word(block, 0xe148 | (ccc << 9) | dreg);
}

// lsl.l #imm, Dn - logical shift left long by immediate
void emit_lsl_l_imm_dn(struct code_block *block, uint8_t count, uint8_t dreg)
{
    uint16_t ccc;

    // 1110 ccc 1 10 i 01 rrr  (i=0 for immediate, size=10 for long)
    if (count == 0 || count > 8) {
        printf("can only lsl by 1-8\n");
        exit(1);
    }
    ccc = count == 8 ? 0 : count;
    emit_word(block, 0xe188 | (ccc << 9) | dreg);
}

// move.b #imm, -(A7) - push immediate word
void emit_push_b_imm(struct code_block *block, uint16_t val)
{
    // 00 01 111 100 111 100  (dest = -(A7), src = immediate word)
    emit_word(block, 0x1f3c);
    emit_word(block, val);
}

// move.w #imm, -(A7) - push immediate word
void emit_push_w_imm(struct code_block *block, uint16_t val)
{
    // 00 11 111 100 111 100  (dest = -(A7), src = immediate word)
    emit_word(block, 0x3f3c);
    emit_word(block, val);
}

// move.b Dn, -(A7) - push byte from data register
// this actually decreases SP by 2
void emit_push_b_dn(struct code_block *block, uint8_t dreg)
{
    // 00 01 111 100 000 ddd  (dest = -(A7), src = Dn)
    emit_word(block, 0x1f00 | dreg);
}

// move.w Dn, -(A7) - push word from data register
void emit_push_w_dn(struct code_block *block, uint8_t dreg)
{
    // 00 11 111 100 000 ddd  (dest = -(A7), src = Dn)
    emit_word(block, 0x3f00 | dreg);
}

// move.w (A7)+, Dn - pop word to data register
void emit_pop_w_dn(struct code_block *block, uint8_t dreg)
{
    // 00 11 ddd 000 011 111  (dest = Dn, src = (A7)+)
    emit_word(block, 0x301f | (dreg << 9));
}

// move.l Dn, -(A7) - push word from data register
void emit_push_l_dn(struct code_block *block, uint8_t dreg)
{
    // 00 10 111 100 000 ddd  (dest = -(A7), src = Dn)
    emit_word(block, 0x2f00 | dreg);
}

// move.l (A7)+, Dn - pop word to data register
void emit_pop_l_dn(struct code_block *block, uint8_t dreg)
{
    // 00 10 ddd 000 011 111  (dest = Dn, src = (A7)+)
    emit_word(block, 0x201f | (dreg << 9));
}

// move.l d(An), -(A7) - push long from memory with displacement
void emit_push_l_disp_an(struct code_block *block, int16_t disp, uint8_t areg)
{
    // 00 10 111 100 101 aaa  (dest = -(A7), src = d(An))
    emit_word(block, 0x2f28 | areg);
    emit_word(block, disp);
}

// movea.l (An), Ad - load address from memory
void emit_movea_l_ind_an_an(struct code_block *block, uint8_t src_areg, uint8_t dest_areg)
{
    // 00 10 ddd 001 010 sss
    emit_word(block, 0x2050 | (dest_areg << 9) | src_areg);
}

// movea.l d(An), Ad - load address from memory with displacement
void emit_movea_l_disp_an_an(struct code_block *block, int16_t disp, uint8_t src_areg, uint8_t dest_areg)
{
    // 00 10 ddd 001 101 sss
    emit_word(block, 0x2068 | (dest_areg << 9) | src_areg);
    emit_word(block, disp);
}

// jsr (An) - jump to subroutine via address register
void emit_jsr_ind_an(struct code_block *block, uint8_t areg)
{
    // 0100 1110 10 010 aaa
    emit_word(block, 0x4e90 | areg);
}

// addq.l #val, An - add quick to address register (long)
void emit_addq_l_an(struct code_block *block, uint8_t areg, uint8_t val)
{
    uint16_t ddd;

    // 0101 ddd 0 10 001 aaa
    if (val == 0 || val > 8) {
        printf("can only addq values between 1 and 8\n");
        exit(1);
    }
    ddd = val == 8 ? 0 : val;
    emit_word(block, 0x5088 | (ddd << 9) | areg);
}

// movem.l <list>, -(A7) - push multiple registers
// mask uses reversed bit order for predecrement:
//   bit 15=D0, 14=D1, ..., 8=D7, 7=A0, 6=A1, ..., 0=A7
void emit_movem_l_to_predec(struct code_block *block, uint16_t mask)
{
    // 0100 1000 11 100 111 = 0x48e7
    emit_word(block, 0x48e7);
    emit_word(block, mask);
}

// movem.l (A7)+, <list> - pop multiple registers
// mask uses normal bit order for postincrement:
//   bit 0=D0, 1=D1, ..., 7=D7, 8=A0, 9=A1, ..., 15=A7
void emit_movem_l_from_postinc(struct code_block *block, uint16_t mask)
{
    // 0100 1100 11 011 111 = 0x4cdf
    emit_word(block, 0x4cdf);
    emit_word(block, mask);
}

// move.b Dn, Dm - copy data register to data register (byte)
void emit_move_b_dn_dn(struct code_block *block, uint8_t src, uint8_t dest)
{
    // 00 01 ddd 000 000 sss
    emit_word(block, 0x1000 | (dest << 9) | src);
}

// movea.l #imm32, An - load 32-bit immediate into address register
void emit_movea_l_imm32(struct code_block *block, uint8_t areg, uint32_t val)
{
    // 00 10 aaa 001 111 100 = 0x207C | (areg << 9)
    emit_word(block, 0x207c | (areg << 9));
    emit_long(block, val);
}

// eor.b Ds, Dd - XOR data registers (result to Dd)
void emit_eor_b_dn_dn(struct code_block *block, uint8_t src, uint8_t dest)
{
    // 1011 sss 1 00 000 ddd
    emit_word(block, 0xb100 | (src << 9) | dest);
}

// eori.b #imm, Dn - XOR immediate with data register
void emit_eor_b_imm_dn(struct code_block *block, uint8_t imm, uint8_t dreg)
{
    // 0000 1010 00 000 rrr
    emit_word(block, 0x0a00 | dreg);
    emit_word(block, imm);
}

void emit_ext_w_dn(struct code_block *block, uint8_t dreg)
{
    // 0100 100 010 000 ddd
    emit_word(block, 0x4880 | dreg);
}

// not.b Dn - one's complement (flip all bits)
void emit_not_b_dn(struct code_block *block, uint8_t dreg)
{
    // 0100 0110 00 000 rrr
    emit_word(block, 0x4600 | dreg);
}

// and.b Ds, Dd - AND data registers (result to Dd)
void emit_and_b_dn_dn(struct code_block *block, uint8_t src, uint8_t dest)
{
    // 1100 ddd 0 00 000 sss
    emit_word(block, 0xc000 | (dest << 9) | src);
}

// ror.b #count, Dn - rotate right byte by immediate (1-8)
void emit_ror_b_imm(struct code_block *block, uint8_t count, uint8_t dreg)
{
    uint16_t ccc;

    // 1110 ccc 0 00 0 11 rrr (d=0 for right, size=00 for byte, i=0 for imm)
    if (count == 0 || count > 8) {
        printf("can only ror by 1-8\n");
        exit(1);
    }
    ccc = count == 8 ? 0 : count;
    emit_word(block, 0xe018 | (ccc << 9) | dreg);
}

// rol.b #count, Dn - rotate left byte by immediate (1-8)
void emit_rol_b_imm(struct code_block *block, uint8_t count, uint8_t dreg)
{
    uint16_t ccc;

    // 1110 ccc 1 00 0 11 rrr (d=1 for left, size=00 for byte, i=0 for imm)
    if (count == 0 || count > 8) {
        printf("can only rol by 1-8\n");
        exit(1);
    }
    ccc = count == 8 ? 0 : count;
    emit_word(block, 0xe118 | (ccc << 9) | dreg);
}

// add.b Ds, Dd - ADD data registers (result to Dd)
void emit_add_b_dn_dn(struct code_block *block, uint8_t src, uint8_t dest)
{
    // 1101 ddd 0 00 000 sss
    emit_word(block, 0xd000 | (dest << 9) | src);
}

// add.w Ds, Dd - ADD data registers (result to Dd)
void emit_add_w_dn_dn(struct code_block *block, uint8_t src, uint8_t dest)
{
    // 1101 ddd 0 01 000 sss
    emit_word(block, 0xd040 | (dest << 9) | src);
}

// add.l Ds, Dd - ADD data registers (result to Dd)
void emit_add_l_dn_dn(struct code_block *block, uint8_t src, uint8_t dest)
{
    // 1101 ddd 0 10 000 sss
    emit_word(block, 0xd080 | (dest << 9) | src);
}

// sub.b Ds, Dd - SUB data registers (result to Dd)
void emit_sub_b_dn_dn(struct code_block *block, uint8_t src, uint8_t dest)
{
    // 1001 ddd 0 00 000 sss
    emit_word(block, 0x9000 | (dest << 9) | src);
}

// sub.w Ds, Dd - SUB data registers (result to Dd)
void emit_sub_w_dn_dn(struct code_block *block, uint8_t src, uint8_t dest)
{
    // 1001 ddd 0 01 000 sss
    emit_word(block, 0x9040 | (dest << 9) | src);
}

// adda.w Dn, An - ADD data register to address register (sign-extends source)
void emit_adda_w_dn_an(struct code_block *block, uint8_t dreg, uint8_t areg)
{
    // 1101 aaa 011 000 ddd
    emit_word(block, 0xd0c0 | (areg << 9) | dreg);
}

// adda.l Dn, An - ADD data register to address register (full 32-bit)
void emit_adda_l_dn_an(struct code_block *block, uint8_t dreg, uint8_t areg)
{
    // 1101 aaa 111 000 ddd
    emit_word(block, 0xd1c0 | (areg << 9) | dreg);
}

// tst.b d(An) - test byte at displacement from address register
void emit_tst_b_disp_an(struct code_block *block, int16_t disp, uint8_t areg)
{
    // 0100 1010 00 101 aaa
    emit_word(block, 0x4a28 | areg);
    emit_word(block, disp);
}

// lsl.b #count, Dn - logical shift left byte by immediate (1-8)
void emit_lsl_b_imm_dn(struct code_block *block, uint8_t count, uint8_t dreg)
{
    uint16_t ccc;

    // 1110 ccc 1 00 i 01 rrr (d=1 for left, size=00 for byte, i=0 for imm, type=01 for lsl)
    if (count == 0 || count > 8) {
        printf("can only lsl by 1-8\n");
        exit(1);
    }
    ccc = count == 8 ? 0 : count;
    emit_word(block, 0xe108 | (ccc << 9) | dreg);
}

// lsr.b #count, Dn - logical shift right byte by immediate (1-8)
void emit_lsr_b_imm_dn(struct code_block *block, uint8_t count, uint8_t dreg)
{
    uint16_t ccc;

    // 1110 ccc 0 00 i 01 rrr (d=0 for right, size=00 for byte, i=0 for imm, type=01 for lsr)
    if (count == 0 || count > 8) {
        printf("can only lsr by 1-8\n");
        exit(1);
    }
    ccc = count == 8 ? 0 : count;
    emit_word(block, 0xe008 | (ccc << 9) | dreg);
}

// asr.b #count, Dn - arithmetic shift right byte by immediate (1-8)
void emit_asr_b_imm_dn(struct code_block *block, uint8_t count, uint8_t dreg)
{
    uint16_t ccc;

    // 1110 ccc 0 00 i 00 rrr (d=0 for right, size=00 for byte, i=0 for imm, type=00 for asr)
    if (count == 0 || count > 8) {
        printf("can only asr by 1-8\n");
        exit(1);
    }
    ccc = count == 8 ? 0 : count;
    emit_word(block, 0xe000 | (ccc << 9) | dreg);
}

// tst.b Dn - test byte in data register (sets Z and N flags)
void emit_tst_b_dn(struct code_block *block, uint8_t dreg)
{
    // 0100 1010 00 000 rrr
    emit_word(block, 0x4a00 | dreg);
}

// lsr.w #count, Dn - logical shift right word by immediate (1-8)
void emit_lsr_w_imm_dn(struct code_block *block, uint8_t count, uint8_t dreg)
{
    uint16_t ccc;

    // 1110 ccc 0 01 i 01 rrr (d=0 for right, size=01 for word, i=0 for imm, type=01 for lsr)
    if (count == 0 || count > 8) {
        printf("can only lsr by 1-8\n");
        exit(1);
    }
    ccc = count == 8 ? 0 : count;
    emit_word(block, 0xe048 | (ccc << 9) | dreg);
}

// lsr.l #count, Dn - logical shift right long by immediate (1-8)
void emit_lsr_l_imm_dn(struct code_block *block, uint8_t count, uint8_t dreg)
{
    uint16_t ccc;

    // 1110 ccc 0 10 i 01 rrr (d=0 for right, size=10 for long, i=0 for imm, type=01 for lsr)
    if (count == 0 || count > 8) {
        printf("can only lsr by 1-8\n");
        exit(1);
    }
    ccc = count == 8 ? 0 : count;
    emit_word(block, 0xe088 | (ccc << 9) | dreg);
}

// move.l An, Dn - copy address register to data register (long)
void emit_move_l_an_dn(struct code_block *block, uint8_t areg, uint8_t dreg)
{
    // 00 10 ddd 000 001 aaa
    emit_word(block, 0x2008 | (dreg << 9) | areg);
}

// movea.l d(An,Dm.w), Ad - load long from indexed address
void emit_movea_l_idx_an_an(
    struct code_block *block,
    int8_t disp,
    uint8_t base_areg,
    uint8_t idx_dreg,
    uint8_t dest_areg
) {
    // movea.l ea, An: 00 10 aaa 001 110 bbb (mode 110 = An with index)
    // extension word: D/A=0 | idx_reg | W/L=0 | 000 | disp8
    emit_word(block, 0x2070 | (dest_areg << 9) | base_areg);
    emit_word(block, (idx_dreg << 12) | ((uint8_t) disp));
}

// move.b (An,Dm.w), Dd - load byte from indexed address
void emit_move_b_idx_an_dn(
    struct code_block *block,
    uint8_t base_areg,
    uint8_t idx_dreg,
    uint8_t dest_dreg
) {
    // move.b ea, Dn: 00 01 ddd 000 110 aaa (mode 110 = An with index)
    // extension word: D/A=0 | idx_reg | W/L=1 | 000 | 0 (disp=0)
    // W/L=1 (bit 11) uses .L index - no sign extension for values >= 0x8000
    emit_word(block, 0x1030 | (dest_dreg << 9) | base_areg);
    emit_word(block, (idx_dreg << 12) | 0x0800);
}

// move.b Ds, (An,Dm.l) - store byte to indexed address
void emit_move_b_dn_idx_an(
    struct code_block *block,
    uint8_t src_dreg,
    uint8_t base_areg,
    uint8_t idx_dreg
) {
    // move.b Dn, ea: 00 01 aaa 110 000 sss (mode 110 = An with index)
    // extension word: D/A=0 | idx_reg | W/L=1 | 000 | 0 (disp=0)
    // W/L=1 (bit 11) uses .L index - no sign extension for values >= 0x8000
    emit_word(block, 0x1180 | (base_areg << 9) | src_dreg);
    emit_word(block, (idx_dreg << 12) | 0x0800);
}

// move.b d(An,Dm.l), Dd - load byte from indexed address with 8-bit displacement
void emit_move_b_disp_idx_an_dn(
    struct code_block *block,
    int8_t disp,
    uint8_t base_areg,
    uint8_t idx_dreg,
    uint8_t dest_dreg
) {
    // move.b ea, Dn: 00 01 ddd 000 110 aaa (mode 110 = An with index)
    // extension word: D/A=0 | idx_reg | W/L=1 | 000 | disp8
    // W/L=1 (bit 11) uses .L index - no sign extension for values >= 0x8000
    emit_word(block, 0x1030 | (dest_dreg << 9) | base_areg);
    emit_word(block, (idx_dreg << 12) | 0x0800 | ((uint8_t) disp));
}

// move.b Ds, d(An,Dm.l) - store byte to indexed address with 8-bit displacement
void emit_move_b_dn_disp_idx_an(
    struct code_block *block,
    uint8_t src_dreg,
    int8_t disp,
    uint8_t base_areg,
    uint8_t idx_dreg
) {
    // move.b Dn, ea: 00 01 aaa 110 000 sss (mode 110 = An with index)
    // extension word: D/A=0 | idx_reg | W/L=1 | 000 | disp8
    // W/L=1 (bit 11) uses .L index - no sign extension for values >= 0x8000
    emit_word(block, 0x1180 | (base_areg << 9) | src_dreg);
    emit_word(block, (idx_dreg << 12) | 0x0800 | ((uint8_t) disp));
}

// lea d(An), An - load effective address with 16-bit displacement
void emit_lea_disp_an_an(
    struct code_block *block,
    int16_t disp,
    uint8_t src_areg,
    uint8_t dest_areg
) {
    // 0100 ddd 111 101 sss
    emit_word(block, 0x41e8 | (dest_areg << 9) | src_areg);
    emit_word(block, disp);
}

// move.l Dn, d(An) - store long from data register to memory with displacement
void emit_move_l_dn_disp_an(
    struct code_block *block,
    uint8_t dreg,
    int16_t disp,
    uint8_t areg
) {
    // 00 10 aaa 101 000 ddd
    emit_word(block, 0x2140 | (areg << 9) | dreg);
    emit_word(block, disp);
}

// add.l d(An), Dn - add long from memory to data register
void emit_add_l_disp_an_dn(
    struct code_block *block,
    int16_t disp,
    uint8_t areg,
    uint8_t dreg
) {
    // 1101 ddd 0 10 101 aaa
    emit_word(block, 0xd0a8 | (dreg << 9) | areg);
    emit_word(block, disp);
}

// sub.l d(An), Dn - subtract long from memory from data register
void emit_sub_l_disp_an_dn(
    struct code_block *block,
    int16_t disp,
    uint8_t areg,
    uint8_t dreg
) {
    // 1001 ddd 0 10 101 aaa
    emit_word(block, 0x90a8 | (dreg << 9) | areg);
    emit_word(block, disp);
}

// movea.l Dn, An - copy data register to address register (long)
void emit_movea_l_dn_an(struct code_block *block, uint8_t dreg, uint8_t areg)
{
    // 0010 aaa 001 000 ddd
    emit_word(block, 0x2040 | (areg << 9) | dreg);
}

// move.w Dn, d(An) - store word to memory with displacement
void emit_move_w_dn_disp_an(
    struct code_block *block,
    uint8_t dreg,
    int16_t disp,
    uint8_t areg
) {
    // 00 11 aaa 101 000 ddd
    emit_word(block, 0x3140 | (areg << 9) | dreg);
    emit_word(block, disp);
}

// move.w d(An), Dn - load word from memory with displacement
void emit_move_w_disp_an_dn(
    struct code_block *block,
    int16_t disp,
    uint8_t areg,
    uint8_t dreg
) {
    // 00 11 ddd 000 101 aaa
    emit_word(block, 0x3028 | (dreg << 9) | areg);
    emit_word(block, disp);
}

// tst.l d(An) - test long at displacement from address register
void emit_tst_l_disp_an(struct code_block *block, int16_t disp, uint8_t areg)
{
    // 0100 1010 10 101 aaa
    emit_word(block, 0x4aa8 | areg);
    emit_word(block, disp);
}

// addi.w #imm, d(An) - add immediate word to memory
void emit_addi_w_disp_an(
    struct code_block *block,
    int16_t imm,
    int16_t disp,
    uint8_t areg
) {
    // 0000 0110 01 101 aaa
    emit_word(block, 0x0668 | areg);
    emit_word(block, imm);
    emit_word(block, disp);
}

// subi.w #imm, d(An) - subtract immediate word from memory
void emit_subi_w_disp_an(
    struct code_block *block,
    int16_t imm,
    int16_t disp,
    uint8_t areg
) {
    // 0000 0100 01 101 aaa
    emit_word(block, 0x0468 | areg);
    emit_word(block, imm);
    emit_word(block, disp);
}

// addq.l #data, d(An) - add quick (1-8) to memory long
void emit_addq_l_disp_an(
    struct code_block *block,
    uint8_t data,
    int16_t disp,
    uint8_t areg
) {
    // 0101 ddd 0 10 101 aaa (ddd: 1-7 = 1-7, 0 = 8)
    uint8_t ddd = (data == 8) ? 0 : data;
    emit_word(block, 0x5080 | (ddd << 9) | 0x28 | areg);
    emit_word(block, disp);
}

// addq.b #data, d(An) - add quick (1-8) to memory byte
void emit_addq_b_disp_an(
    struct code_block *block,
    uint8_t data,
    int16_t disp,
    uint8_t areg
) {
    // 0101 ddd 0 00 101 aaa (ddd: 1-7 = 1-7, 0 = 8)
    uint8_t ddd = (data == 8) ? 0 : data;
    emit_word(block, 0x5000 | (ddd << 9) | 0x28 | areg);
    emit_word(block, disp);
}


// addi.l #imm32, d(An) - add immediate long to memory
void emit_addi_l_disp_an(
    struct code_block *block,
    uint32_t imm,
    int16_t disp,
    uint8_t areg
) {
    // 0000 0110 10 101 aaa
    emit_word(block, 0x06a8 | areg);
    emit_long(block, imm);
    emit_word(block, disp);
}

// cmpi.l #imm32, d(An) - compare immediate long with memory
void emit_cmpi_l_imm32_disp_an(
    struct code_block *block,
    uint32_t imm,
    int16_t disp,
    uint8_t areg
) {
    // 0000 1100 10 101 aaa
    emit_word(block, 0x0ca8 | areg);
    emit_long(block, imm);
    emit_word(block, disp);
}

// cmpi.l #imm32, Dn - compare immediate long with data register
void emit_cmpi_l_imm_dn(struct code_block *block, uint32_t imm, uint8_t dreg)
{
    // 0000 1100 10 000 rrr
    emit_word(block, 0x0c80 | dreg);
    emit_long(block, imm);
}

// cmpi.w #imm16, Dn - compare immediate word with data register
void emit_cmpi_w_imm_dn(struct code_block *block, uint16_t imm, uint8_t dreg)
{
    // 0000 1100 01 000 rrr
    emit_word(block, 0x0c40 | dreg);
    emit_word(block, imm);
}

// cmpa.w #imm16, An - compare immediate word with address register
void emit_cmpa_w_imm_an(struct code_block *block, uint16_t imm, uint8_t areg)
{
    // cmpa.w #imm, An: 1011 aaa 011 111 100
    emit_word(block, 0xb0fc | (areg << 9));
    emit_word(block, imm);
}

// bcs.b - branch if carry set with 8-bit displacement
void emit_bcs_b(struct code_block *block, int8_t disp)
{
    // 0110 0101 dddd dddd
    emit_word(block, 0x6500 | ((uint8_t) disp));
}

// bne.b - branch if not equal with 8-bit displacement
void emit_bne_b(struct code_block *block, int8_t disp)
{
    // 0110 0110 dddd dddd
    emit_word(block, 0x6600 | ((uint8_t) disp));
}

// bls.b - branch if lower or same (unsigned <=)
void emit_bls_b(struct code_block *block, int8_t disp)
{
    // 0110 0011 dddd dddd
    emit_word(block, 0x6300 | ((uint8_t) disp));
}

// bhi.b - branch if higher (unsigned >)
void emit_bhi_b(struct code_block *block, int8_t disp)
{
    // 0110 0010 dddd dddd
    emit_word(block, 0x6200 | ((uint8_t) disp));
}

// subi.w #imm16, Dn - subtract immediate word from data register
void emit_subi_w_dn(struct code_block *block, uint16_t imm, uint8_t dreg)
{
    // 0000 0100 01 000 rrr
    emit_word(block, 0x0440 | dreg);
    emit_word(block, imm);
}

void emit_move_sr_dn(struct code_block *block, uint8_t dreg)
{
    // 0100 0000 11 000 rrr
    emit_word(block, 0x40c0 | dreg);
}

void emit_move_dn_ccr(struct code_block *block, uint8_t dreg)
{
    // 0100 0100 11 000 rrr
    emit_word(block, 0x44c0 | dreg);
}

// move.l (An), Dn - load long from memory via address register
void emit_move_l_ind_an_dn(struct code_block *block, uint8_t areg, uint8_t dreg)
{
    // 00 10 ddd 000 010 aaa
    emit_word(block, 0x2010 | (dreg << 9) | areg);
}

// sub.l Ds, Dd - subtract data registers (long, result to Dd)
void emit_sub_l_dn_dn(struct code_block *block, uint8_t src, uint8_t dest)
{
    // 1001 ddd 0 10 000 sss
    emit_word(block, 0x9080 | (dest << 9) | src);
}

// mulu.w #imm, Dn
void emit_mulu_w_imm_dn(struct code_block *block, uint16_t imm, uint8_t dreg)
{
    // MULU <ea>,Dn: 1100 ddd 011 <ea>
    // immediate mode: <ea> = 111 100
    emit_word(block, 0xc0fc | (dreg << 9));
    emit_word(block, imm);
}

// cmp.l Dn,Dn (src - dest comparison, sets flags)
void emit_cmp_l_dn_dn(struct code_block *block, uint8_t src, uint8_t dest)
{
    // CMP.L Dn,Dn: 1011 dest 010 000 src
    emit_word(block, 0xb080 | (dest << 9) | src);
}

// emit_add_cycles - add GB cycles to context, picks optimal instruction
void emit_add_cycles(struct code_block *block, int cycles)
{
    if (cycles <= 0) {
        return;
    }
    if (cycles <= 8) {
        emit_addq_l_dn(block, REG_68K_D_CYCLE_COUNT, cycles);
        // emit_addq_l_disp_an(block, cycles, JIT_CTX_CYCLES, REG_68K_A_CTX);
    } else {
        emit_addi_l_dn(block, REG_68K_D_CYCLE_COUNT, cycles);
        // emit_addi_l_disp_an(block, cycles, JIT_CTX_CYCLES, REG_68K_A_CTX);
    }
}

// Emit inline mini-dispatcher with patchable exit
// This sequence:
// 1. Checks cycle count (exit if >= 456)
// 2. Calls patch_helper via JSR (first execution)
// 3. patch_helper will patch the movea.l+jsr into jmp.l <target> for future runs
// 16 bytes total
void emit_patchable_exit(struct code_block *block)
{
    // cmpi.l #cycles_per_exit, d2 (6 bytes)
    emit_cmpi_l_imm_dn(block, cycles_per_exit, REG_68K_D_CYCLE_COUNT);

    // bcc.s +6 = skip over movea.l + jsr to rts (2 bytes)
    emit_bcc_s(block, 6);

    // movea.l JIT_CTX_PATCH_HELPER(a4), a0 (4 bytes)
    emit_movea_l_disp_an_an(block, JIT_CTX_PATCH_HELPER, REG_68K_A_CTX, REG_68K_A_SCRATCH_1);

    // jsr (a0) (2 bytes)
    emit_jsr_ind_an(block, REG_68K_A_SCRATCH_1);

    // rts (2 bytes)
    emit_rts(block);
}