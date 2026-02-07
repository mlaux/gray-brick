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
#define JIT_CTX_READ_CYCLES   52  // u32: in-flight cycles at dmg_read call
#define JIT_CTX_DAA_STATE     56  // 2 bytes: [0]=old_A, [1]=N flag (for DAA)
#define JIT_CTX_FRAME_CYCLES_PTR 60  // u32 *frame_cycles_ptr (dmg->frame_cycles)
#define JIT_CTX_STOP_FUNC   64  // void *stop_func (for STOP/speed switch)
#define JIT_CTX_EFF_DOUBLE_SPEED 68  // u8: 1 if double speed active AND not ignored
#define JIT_CTX_GB_SP       72  // u16: GB stack pointer value
#define JIT_CTX_STACK_IN_RAM 76  // non-zero if A3 points to native WRAM/HRAM

struct code_block {
    uint8_t code[1024];
    uint16_t m68k_offsets[256];
    // number of bytes populated in code[]
    size_t length;
    // number of GB instructions
    size_t count;
    uint16_t src_address;
    uint16_t end_address; // address after last instruction

    // set when compilation hits unknown opcode
    uint8_t error;
    uint16_t failed_opcode;
    uint16_t failed_address;
};

typedef uint8_t (*dmg_read_fn)(void *dmg, uint16_t address);

// cache store function signature for registering mid-block entry points
typedef int (*cache_store_fn)(uint16_t pc, uint8_t bank, void *code_ptr);

// allocator function signature for arena allocation
typedef void *(*alloc_fn)(size_t size);

// compile-time context
struct compile_ctx {
    void *dmg;              // dmg pointer for memory reads
    dmg_read_fn read;       // read function
    int single_instruction; // if set, compile only one instruction then dispatch
    cache_store_fn cache_store;  // NULL in tests, registers mid-block entries
    alloc_fn alloc;              // NULL uses malloc, otherwise arena_alloc
    uint8_t current_bank;        // current ROM bank for cache_store calls
    void *wram_base;        // dmg->main_ram for compile-time WRAM SP detection
    void *hram_base;
};

void compiler_init(void);

struct code_block *compile_block(uint16_t src_address, struct compile_ctx *ctx);

// Free a compiled block
void block_free(struct code_block *block);

// Emit helpers (exposed for testing)
void emit_byte(struct code_block *block, uint8_t byte);
void emit_word(struct code_block *block, uint16_t word);

void compile_join_bc(struct code_block *block, int dreg);
void compile_join_de(struct code_block *block, int dreg);

extern int cycles_per_exit;

#endif
