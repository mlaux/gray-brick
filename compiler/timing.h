#ifndef _TIMING_H
#define _TIMING_H

// GB register indices for cp r instruction
#define GB_REG_B    0
#define GB_REG_C    1
#define GB_REG_D    2
#define GB_REG_E    3
#define GB_REG_H    4
#define GB_REG_L    5
#define GB_REG_HL   6

// synthesize wait for LY to reach target value
// detects ldh a, [$44]; cp N; jr cc, back to the ldh (loop_pc).
// tail_cycles = compare + untaken jr cost; the ldh is already counted
void compile_ly_wait(
    struct code_block *block,
    uint8_t target_ly,
    uint8_t jr_opcode,
    uint16_t next_pc,
    uint16_t loop_pc,
    int tail_cycles
);

void compile_get_gb_reg_d0(struct code_block *block, int gb_reg);

void compile_ly_wait_reg(
    struct code_block *block,
    int gb_reg,
    uint8_t jr_opcode,
    uint16_t next_pc,
    uint16_t loop_pc,
    int tail_cycles
);

void compile_halt(struct code_block *block, int next_pc);

// synthesize wait for an interrupt handler to change a flag byte in HRAM
// detects ldh a, [nn]; and a / or a; jr z/nz back to the ldh
void compile_hram_idle_wait(
    struct code_block *block,
    uint8_t addr_lo,
    uint8_t jr_opcode,
    uint16_t loop_pc
);

#endif