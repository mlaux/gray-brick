#ifndef MEM_LOADS_H
#define MEM_LOADS_H

#include <stdint.h>
#include "compiler.h"

// 0xe0: ld ($ff00 + u8), a
void compile_ldh_u8_a(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint8_t addr
);

// 0xf0: ld a, ($ff00 + u8). also recognizes the LY/HRAM polling loops that
// start with this opcode, so it returns 1 when the block is finished
int compile_ldh_a_u8(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t src_address,
    uint16_t *src_ptr
);

// 0xe2: ld ($ff00 + c), a
void compile_ldh_c_a(struct code_block *block);

// 0xf2: ld a, ($ff00 + c)
void compile_ldh_a_c(struct code_block *block);

// 0xea: ld (u16), a
void compile_ld_u16_a(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t src_address,
    uint16_t *src_ptr
);

// 0xfa: ld a, (u16)
void compile_ld_a_u16(
    struct code_block *block,
    struct compile_ctx *ctx,
    uint16_t src_address,
    uint16_t *src_ptr
);

#endif
