#ifndef _BRANCHES_H
#define _BRANCHES_H

void compile_jr(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t *src_ptr,
    uint16_t src_address
);

void compile_jr_cond(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t *src_ptr,
    uint16_t src_address,
    uint8_t flag_bit,
    int branch_if_set
);

void compile_jp_cond(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t *src_ptr,
    uint16_t src_address,
    uint8_t flag_bit,
    int branch_if_set
);

void compile_call_imm16(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t *src_ptr,
    uint16_t src_address
);

void compile_call_cond(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t *src_ptr,
    uint16_t src_address,
    uint8_t flag_bit,
    int branch_if_set
);

void compile_ret(struct code_block *block);
void compile_ret_cond(struct code_block *block, uint8_t flag_bit, int branch_if_set);
void compile_rst_n(struct code_block *block, uint8_t target, uint16_t ret_addr);

#endif