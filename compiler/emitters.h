#ifndef EMITTERS_H
#define EMITTERS_H

#include <stdint.h>
#include "compiler.h"

void emit_byte(struct code_block *block, uint8_t byte);
void emit_word(struct code_block *block, uint16_t word);
void emit_long(struct code_block *block, uint32_t val);

void emit_moveq_dn(struct code_block *block, uint8_t reg, int8_t imm);
void emit_move_b_dn(struct code_block *block, uint8_t reg, int8_t imm);
void emit_move_w_dn(struct code_block *block, uint8_t reg, int16_t imm);
void emit_move_l_dn(struct code_block *block, uint8_t reg, int32_t imm);

void emit_rol_w_8(struct code_block *block, uint8_t reg);
void emit_ror_w_8(struct code_block *block, uint8_t reg);
void emit_swap(struct code_block *block, uint8_t reg);

void emit_move_w_an_dn(struct code_block *block, uint8_t areg, uint8_t dreg);
void emit_movea_w_dn_an(struct code_block *block, uint8_t dreg, uint8_t areg);
void emit_movea_w_an_an(struct code_block *block, uint8_t src_areg, uint8_t dest_areg);
void emit_movea_w_imm16(struct code_block *block, uint8_t areg, uint16_t val);

void emit_subq_b_dn(struct code_block *block, uint8_t dreg, uint8_t val);
void emit_subq_l_dn(struct code_block *block, uint8_t dreg, uint8_t val);
void emit_subq_w_an(struct code_block *block, uint8_t areg, uint8_t val);
void emit_subq_l_an(struct code_block *block, uint8_t areg, uint8_t val);
void emit_addq_b_dn(struct code_block *block, uint8_t dreg, uint8_t val);
void emit_addq_w_dn(struct code_block *block, uint8_t dreg, uint8_t val);
void emit_addq_l_dn(struct code_block *block, uint8_t dreg, uint8_t val);
void emit_subq_w_dn(struct code_block *block, uint8_t dreg, uint8_t val);
void emit_addq_w_an(struct code_block *block, uint8_t areg, uint8_t val);
void emit_move_w_dn_ind_an(struct code_block *block, uint8_t dreg, uint8_t areg);
void emit_move_w_ind_an_dn(struct code_block *block, uint8_t areg, uint8_t dreg);
void emit_move_b_dn_ind_an(struct code_block *block, uint8_t dreg, uint8_t areg);
void emit_move_b_dn_disp_an(struct code_block *block, uint8_t dreg, int16_t disp, uint8_t areg);
void emit_move_b_imm_ind_an(struct code_block *block, uint8_t imm, uint8_t areg);
void emit_move_b_imm_disp_an(struct code_block *block, uint8_t imm, int16_t disp, uint8_t areg);
void emit_move_b_ind_an_dn(struct code_block *block, uint8_t areg, uint8_t dreg);
void emit_move_b_disp_an_dn(struct code_block *block, int16_t disp, uint8_t areg, uint8_t dreg);
void emit_andi_w_dn(struct code_block *block, uint8_t dreg, uint16_t imm);
void emit_andi_l_dn(struct code_block *block, uint8_t dreg, uint32_t imm);
void emit_cmp_b_imm_dn(struct code_block *block, uint8_t dreg, uint8_t imm);
void emit_cmp_b_dn_dn(struct code_block *block, uint8_t src, uint8_t dest);
void emit_scc(struct code_block *block, uint8_t cond, uint8_t dreg);
void emit_andi_b_dn(struct code_block *block, uint8_t dreg, uint8_t imm);
void emit_ori_b_dn(struct code_block *block, uint8_t dreg, uint8_t imm);
void emit_subi_b_dn(struct code_block *block, uint8_t dreg, uint8_t imm);
void emit_addi_b_dn(struct code_block *block, uint8_t dreg, uint8_t imm);
void emit_addi_l_dn(struct code_block *block, uint8_t dreg, uint32_t imm);
void emit_or_b_dn_dn(struct code_block *block, uint8_t src, uint8_t dest);
void emit_or_l_dn_dn(struct code_block *block, uint8_t src, uint8_t dest);

void emit_rts(struct code_block *block);
void emit_dispatch_jump(struct code_block *block);
void emit_patchable_exit(struct code_block *block);
void emit_block_exit(struct code_block *block, uint16_t target_gb_pc);
void emit_bra_b(struct code_block *block, int8_t disp);
void emit_bra_w(struct code_block *block, int16_t disp);
void emit_beq_b(struct code_block *block, int8_t disp);
void emit_beq_w(struct code_block *block, int16_t disp);
void emit_bne_w(struct code_block *block, int16_t disp);
void emit_bcs_w(struct code_block *block, int16_t disp);
void emit_bcc_w(struct code_block *block, int16_t disp);
void emit_bcc_s(struct code_block *block, int8_t disp);
void emit_bcc_opcode_w(struct code_block *block, int cond, int16_t disp);
void patch_branch_b(struct code_block *block, size_t branch_at);
void patch_branch_w(struct code_block *block, size_t branch_at);
void emit_btst_imm_dn(struct code_block *block, uint8_t bit, uint8_t dreg);
void emit_bclr_imm_dn(struct code_block *block, uint8_t bit, uint8_t dreg);
void emit_bset_imm_dn(struct code_block *block, uint8_t bit, uint8_t dreg);

void emit_move_l_dn_dn(struct code_block *block, uint8_t src, uint8_t dest);
void emit_move_w_dn_dn(struct code_block *block, uint8_t src, uint8_t dest);
void emit_lsl_w_imm_dn(struct code_block *block, uint8_t count, uint8_t dreg);
void emit_lsl_l_imm_dn(struct code_block *block, uint8_t count, uint8_t dreg);
void emit_push_b_imm(struct code_block *block, uint16_t val);
void emit_push_w_imm(struct code_block *block, uint16_t val);
void emit_push_b_dn(struct code_block *block, uint8_t dreg);
void emit_push_w_dn(struct code_block *block, uint8_t dreg);
void emit_push_l_dn(struct code_block *block, uint8_t dreg);
void emit_pop_w_dn(struct code_block *block, uint8_t dreg);
void emit_pop_l_dn(struct code_block *block, uint8_t dreg);
void emit_push_l_disp_an(struct code_block *block, int16_t disp, uint8_t areg);
void emit_movea_l_ind_an_an(struct code_block *block, uint8_t src_areg, uint8_t dest_areg);
void emit_movea_l_disp_an_an(struct code_block *block, int16_t disp, uint8_t src_areg, uint8_t dest_areg);
void emit_jsr_ind_an(struct code_block *block, uint8_t areg);
void emit_addq_l_an(struct code_block *block, uint8_t areg, uint8_t val);
void emit_movem_l_to_predec(struct code_block *block, uint16_t mask);
void emit_movem_l_from_postinc(struct code_block *block, uint16_t mask);
void emit_move_b_dn_dn(struct code_block *block, uint8_t src, uint8_t dest);
void emit_movea_l_imm32(struct code_block *block, uint8_t areg, uint32_t val);
void emit_eor_b_dn_dn(struct code_block *block, uint8_t src, uint8_t dest);
void emit_eor_b_imm_dn(struct code_block *block, uint8_t imm, uint8_t dreg);

void emit_ext_w_dn(struct code_block *block, uint8_t dreg);
void emit_not_b_dn(struct code_block *block, uint8_t dreg);
void emit_and_b_dn_dn(struct code_block *block, uint8_t src, uint8_t dest);
void emit_ror_b_imm(struct code_block *block, uint8_t count, uint8_t dreg);
void emit_rol_b_imm(struct code_block *block, uint8_t count, uint8_t dreg);
void emit_add_b_dn_dn(struct code_block *block, uint8_t src, uint8_t dest);
void emit_add_w_dn_dn(struct code_block *block, uint8_t src, uint8_t dest);
void emit_add_l_dn_dn(struct code_block *block, uint8_t src, uint8_t dest);
void emit_sub_b_dn_dn(struct code_block *block, uint8_t src, uint8_t dest);
void emit_sub_w_dn_dn(struct code_block *block, uint8_t src, uint8_t dest);
void emit_adda_w_dn_an(struct code_block *block, uint8_t dreg, uint8_t areg);
void emit_adda_l_dn_an(struct code_block *block, uint8_t dreg, uint8_t areg);
void emit_tst_b_disp_an(struct code_block *block, int16_t disp, uint8_t areg);
void emit_lsl_b_imm_dn(struct code_block *block, uint8_t count, uint8_t dreg);
void emit_lsr_b_imm_dn(struct code_block *block, uint8_t count, uint8_t dreg);
void emit_asr_b_imm_dn(struct code_block *block, uint8_t count, uint8_t dreg);
void emit_tst_b_dn(struct code_block *block, uint8_t dreg);
void emit_lsr_w_imm_dn(struct code_block *block, uint8_t count, uint8_t dreg);
void emit_lsr_l_imm_dn(struct code_block *block, uint8_t count, uint8_t dreg);
void emit_move_l_an_dn(struct code_block *block, uint8_t areg, uint8_t dreg);
void emit_movea_l_idx_an_an(struct code_block *block, int8_t disp, uint8_t base_areg, uint8_t idx_dreg, uint8_t dest_areg);
void emit_movea_l_idx_scale4_an_an(struct code_block *block, uint8_t base_areg, uint8_t idx_dreg, uint8_t dest_areg);
void emit_move_b_idx_an_dn(struct code_block *block, uint8_t base_areg, uint8_t idx_dreg, uint8_t dest_dreg);
void emit_move_b_dn_idx_an(struct code_block *block, uint8_t src_dreg, uint8_t base_areg, uint8_t idx_dreg);
void emit_move_b_disp_idx_an_dn(struct code_block *block, int8_t disp, uint8_t base_areg, uint8_t idx_dreg, uint8_t dest_dreg);
void emit_move_b_dn_disp_idx_an(struct code_block *block, uint8_t src_dreg, int8_t disp, uint8_t base_areg, uint8_t idx_dreg);
void emit_lea_disp_an_an(struct code_block *block, int16_t disp, uint8_t src_areg, uint8_t dest_areg);
void emit_move_l_dn_disp_an(struct code_block *block, uint8_t dreg, int16_t disp, uint8_t areg);
void emit_add_l_disp_an_dn(struct code_block *block, int16_t disp, uint8_t areg, uint8_t dreg);
void emit_sub_l_disp_an_dn(struct code_block *block, int16_t disp, uint8_t areg, uint8_t dreg);
void emit_movea_l_dn_an(struct code_block *block, uint8_t dreg, uint8_t areg);
void emit_move_w_dn_disp_an(struct code_block *block, uint8_t dreg, int16_t disp, uint8_t areg);
void emit_move_w_disp_an_dn(struct code_block *block, int16_t disp, uint8_t areg, uint8_t dreg);
void emit_move_l_disp_an_dn(struct code_block *block, int16_t disp, uint8_t areg, uint8_t dreg);
void emit_tst_l_disp_an(struct code_block *block, int16_t disp, uint8_t areg);
void emit_addi_w_disp_an(struct code_block *block, int16_t imm, int16_t disp, uint8_t areg);
void emit_subi_w_disp_an(struct code_block *block, int16_t imm, int16_t disp, uint8_t areg);
void emit_addq_b_disp_an(struct code_block *block, uint8_t data, int16_t disp, uint8_t areg);
void emit_addq_w_disp_an(struct code_block *block, uint8_t data, int16_t disp, uint8_t areg);
void emit_subq_w_disp_an(struct code_block *block, uint8_t data, int16_t disp, uint8_t areg);
void emit_addq_l_disp_an(struct code_block *block, uint8_t data, int16_t disp, uint8_t areg);
void emit_addi_l_disp_an(struct code_block *block, uint32_t imm, int16_t disp, uint8_t areg);
void emit_cmpi_l_imm32_disp_an(struct code_block *block, uint32_t imm, int16_t disp, uint8_t areg);
void emit_cmpi_l_imm_dn(struct code_block *block, uint32_t imm, uint8_t dreg);
void emit_cmpi_w_imm_dn(struct code_block *block, uint16_t imm, uint8_t dreg);
void emit_cmpa_w_imm_an(struct code_block *block, uint16_t imm, uint8_t areg);
void emit_bcs_b(struct code_block *block, int8_t disp);
void emit_bne_b(struct code_block *block, int8_t disp);
void emit_bls_b(struct code_block *block, int8_t disp);
void emit_bhi_b(struct code_block *block, int8_t disp);
void emit_subi_w_dn(struct code_block *block, uint16_t imm, uint8_t dreg);
void emit_move_l_ind_an_dn(struct code_block *block, uint8_t areg, uint8_t dreg);
void emit_sub_l_dn_dn(struct code_block *block, uint8_t src, uint8_t dest);
void emit_add_cycles(struct code_block *block, int cycles);
void emit_move_sr_dn(struct code_block *block, uint8_t dreg);
void emit_move_dn_ccr(struct code_block *block, uint8_t dreg);
void emit_mulu_w_imm_dn(struct code_block *block, uint16_t imm, uint8_t dreg);
void emit_divu_w_imm_dn(struct code_block *block, uint16_t imm, uint8_t dreg);
void emit_cmp_l_dn_dn(struct code_block *block, uint8_t src, uint8_t dest);
void emit_cmp_l_disp_an_dn(
    struct code_block *block,
    int16_t disp,
    uint8_t areg,
    uint8_t dreg
);

#endif
