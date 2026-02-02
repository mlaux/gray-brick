#ifndef _INTEROP_H
#define _INTEROP_H

#include <stdint.h>

void compile_call_dmg_write_a(struct code_block *block);
void compile_call_dmg_write_imm(struct code_block *block, uint8_t val);
void compile_call_dmg_write_d0(struct code_block *block);  // value in D0, address in D1
void compile_call_dmg_read_a(struct code_block *block);
void compile_call_dmg_read(struct code_block *block);
void compile_call_ei_di(struct code_block *block, int enabled);

void compile_slow_dmg_read(struct code_block *block);
void compile_slow_dmg_write(struct code_block *block, uint8_t val_reg);

void compile_call_dmg_read16(struct code_block *block);    // addr in D1, result in D0
void compile_call_dmg_write16_d0(struct code_block *block); // addr in D1, data in D0

void compile_slow_dmg_read16(struct code_block *block);
void compile_slow_dmg_write16(struct code_block *block);

// STOP instruction - calls stop_func, returns 0=continue, 1=halt
void compile_call_stop(struct code_block *block, int next_pc);

#endif