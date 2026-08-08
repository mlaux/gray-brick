#ifndef _INTEROP_H
#define _INTEROP_H

#include <stdint.h>

// shared memory-access helpers written into the arena on start
struct jit_helpers {
    uint32_t read8;         // addr D1.w -> D0.b; clobbers D0/A0, slow path D1/A1 too
    uint32_t read8_hl;      // loads D1 from A2 itself, falls into read8
    uint32_t read8_slow;    // straight to the C call, addr D1.w
    uint32_t write8;        // addr D1.w, value D0.b; clobbers D3/A0, slow D0/D1/A1 too
    uint32_t write8_a;      // value taken from A (D4)
    uint32_t write8_hl;     // loads D1 from A2, value D0.b
    uint32_t write8_hl_a;   // loads D1 from A2, value from A
    uint32_t write8_slow;   // straight to the C call, value D0.b
    uint32_t write8_slow_a; // straight to the C call, value from A
    uint32_t write8_mbc_a;  // ROM-range write: straight to mbc_write_func,
                            // addr D1.w, value from A
};
extern struct jit_helpers jit_helpers;

// upper bound on emitted helper bytes, for the harness's region alloc
#define JIT_HELPERS_SIZE 320

// emit the helpers (position-independent) into an internal block and
// record entry addresses as base + offset
const struct code_block *compile_emit_helpers(uint32_t base, void *hram_base);

void compile_call_dmg_write_a(struct code_block *block);
void compile_call_dmg_write_mbc_a(struct code_block *block);
void compile_call_dmg_write_imm(struct code_block *block, uint8_t val);
void compile_call_dmg_write_d0(struct code_block *block);  // value in D0, address in D1
void compile_call_dmg_read_a(struct code_block *block);
void compile_call_dmg_read(struct code_block *block);
// (hl)-addressed variants, D1 loaded from A2 inside the helper
void compile_call_dmg_read_hl(struct code_block *block);
void compile_call_dmg_read_hl_a(struct code_block *block);
void compile_call_dmg_write_hl_d0(struct code_block *block);
void compile_call_dmg_write_hl_a(struct code_block *block);
void compile_call_dmg_write_hl_imm(struct code_block *block, uint8_t val);
void compile_call_ei_di(struct code_block *block, int enabled);

void compile_slow_dmg_read(struct code_block *block);
void compile_slow_dmg_write(struct code_block *block, uint8_t val_reg);

// page table entry lookup, GB address in addr_dreg (clobbered),
// biased entry left in dest_areg
void compile_page_lookup(
    struct code_block *block,
    uint8_t table_areg,
    uint8_t addr_dreg,
    uint8_t dest_areg
);

void compile_call_dmg_read16(struct code_block *block);    // addr in D1, result in D0
void compile_call_dmg_write16_d0(struct code_block *block); // addr in D1, data in D0

void compile_slow_dmg_read16(struct code_block *block);
void compile_slow_dmg_write16(struct code_block *block);

// STOP instruction - calls stop_func, returns 0=continue, 1=halt
void compile_call_stop(struct code_block *block, int next_pc);

#endif