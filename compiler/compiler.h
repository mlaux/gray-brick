#ifndef COMPILER_H
#define COMPILER_H

#include <stdint.h>
#include <stddef.h>

// D0 = scratch/C interop return value
// D1 = scratch
// D2 = accumulated cycle count
// D3 = scratch/dispatcher return value (next GB PC)
// D4 = A (GB accumulator)
// D5 = BC (split: 0x00BB00CC)
// D6 = DE (split: 0x00DD00EE)
// D7 = flags (00000Z0C)

// A0 = scratch
// A1 = scratch
// A2 = HL (contiguous: 0xHHLL)
// A3 = SP
// A4 = runtime context pointer
// A5 = read page table base (dmg + 0x80)
// A6 = write page table base (dmg + 0x480)
// A7 = 68k stack pointer

#define REG_68K_D_SCRATCH_0 0
#define REG_68K_D_SCRATCH_1 1
#define REG_68K_D_CYCLE_COUNT 2
#define REG_68K_D_NEXT_PC 3
#define REG_68K_D_A 4
#define REG_68K_D_BC 5
#define REG_68K_D_DE 6
#define REG_68K_D_FLAGS 7

#define REG_68K_A_SCRATCH_1 0
#define REG_68K_A_SCRATCH_2 1
#define REG_68K_A_HL 2
#define REG_68K_A_SP 3
#define REG_68K_A_CTX 4
#define REG_68K_A_READ_PAGE 5
#define REG_68K_A_WRITE_PAGE 6

#define COND_HI  2   // unsigned higher
#define COND_CC  4   // carry clear (nc)
#define COND_CS  5   // carry set (c)
#define COND_NE  6   // not equal/not zero (nz)
#define COND_EQ  7   // equal/zero (z)
#define COND_NONE -1 // not a conditional branch

// Runtime context offsets
#define JIT_CTX_DMG         0
#define JIT_CTX_READ        4
#define JIT_CTX_WRITE       8
#define JIT_CTX_EI_DI       12
#define JIT_CTX_INTCHECK    16  // unused
#define JIT_CTX_ROM_BANK    17  // 1 byte (current ROM bank for MBC)
// 2 bytes padding to align to 4 bytes
#define JIT_CTX_BANK0_CACHE   20  // struct code_block **bank0_cache
#define JIT_CTX_BANKED_CACHE  24  // struct code_block ***banked_cache
#define JIT_CTX_UPPER_CACHE   28  // struct code_block **upper_cache
#define JIT_CTX_DISPATCH      32  // void *dispatcher_return
#define JIT_CTX_READ16        36  // u16 (*dmg_read16)(void *_dmg, u16 address);
#define JIT_CTX_WRITE16       40  // void (*dmg_write16)(void *_dmg, u16 address, u16 data);
#define JIT_CTX_CYCLES        44  // u32: accumulated GB cycles
#define JIT_CTX_PATCH_HELPER  48  // void *patch_helper routine
#define JIT_CTX_READ_CYCLES   52  // u32: GB cycles at dmg_read call
#define JIT_CTX_DAA_STATE     56  // 2 bytes: [0]=old_A, [1]=N flag (for DAA)
#define JIT_CTX_FRAME_CYCLES_PTR 60  // u32 *frame_cycles_ptr (dmg->frame_cycles)
#define JIT_CTX_STOP_FUNC   64  // void *stop_func (for STOP/speed switch)
#define JIT_CTX_EFF_DOUBLE_SPEED 68  // u8: 1 if double speed active AND not ignored
#define JIT_CTX_GB_SP       72  // u16: GB stack pointer value
#define JIT_CTX_STACK_IN_RAM 76  // non-zero if A3 points to native WRAM/HRAM
#define JIT_CTX_WAKE_LIMIT  80  // u32: CPU-cycle distance to the earliest
                                // deadline; dispatcher exit budget and the
                                // HALT/idle fast-forward target
// GB6_PROFILING only, written by the fast-forward paths
#define JIT_CTX_SKIPPED     84  // u32: GB cycles skipped by wake_skip
#define JIT_CTX_LY_SKIPS    88  // u32: LY-wait clamp skips (cycles unknown)
#define JIT_CTX_MBC_WRITE   92  // void *mbc_write_func (dmg_mbc_write)

struct code_block {
    // number of bytes populated in code[]
    size_t length;
    // number of GB instructions
    size_t count;
    uint16_t src_address;
    uint16_t end_address; // address after last instruction

    // set when compilation hits unknown opcode
    uint16_t error;
    uint16_t failed_opcode;
    uint16_t failed_address;
    // at the end so arena can only be bumped by actual code size
    uint8_t code[2048];
};

extern uint16_t m68k_offsets[256];

// deferred cycle counting: instruction cycles accumulate at compile time in
// pending_cycles and are materialized into D2 on exit
extern int pending_cycles;
void defer_cycles(int n);
void flush_cycles(struct code_block *block);

// GB offsets targeted by backward jumps in the current block (pre-scan)
extern uint8_t flush_at[256];

// SM83 instruction lengths (CB counted as 2)
extern const uint8_t insn_length[256];

typedef uint8_t (*dmg_read_fn)(void *dmg, uint16_t address);

// cache store function signature for registering mid-block entry points
typedef int (*cache_store_fn)(uint16_t pc, uint8_t bank, void *code_ptr);

// allocator function signature for arena allocation
typedef void *(*alloc_fn)(size_t size);

// compile-time context
struct compile_ctx {
    void *dmg;                  // for memory reads
    dmg_read_fn read;
    cache_store_fn cache_store; // NULL in tests, registers mid-block entries
    alloc_fn alloc;             // NULL uses malloc, otherwise arena_alloc
    uint8_t current_bank;       // current ROM bank for cache_store calls
    // GB memory the emitted fast paths address absolutely
    void *wram_base;            // dmg->main_ram
    void *hram_base;            // dmg->hram
    void *joyp_ptr;             // &dmg->joyp, the maintained FF00 shadow
    uint16_t bank_reg_lo;       // MBC ROM-bank select range for the
    uint16_t bank_reg_hi;       // same-bank write skip, both 0 = off
};

struct code_block *compile_block(uint16_t src_address, struct compile_ctx *ctx);

// Free a compiled block
void block_free(struct code_block *block);

void compile_join_bc(struct code_block *block, int dreg);
void compile_join_de(struct code_block *block, int dreg);

#endif
