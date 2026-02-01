#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../compiler.h"
#include "tests.h"
#include "../musashi/m68k.h"

#define MEM_SIZE 0x10000
static uint8_t mem[MEM_SIZE];

#define CODE_BASE 0x1000
#define STUB_BASE 0x2000   // Where stub functions live
#define JIT_CTX_ADDR 0x3000 // jit_runtime context structure
#define STACK_BASE 0x8000

// GB memory is mapped at base of 68k address space
#define GB_MEM_BASE 0x0000
#define DEFAULT_GB_SP 0x0fff

// Test compile context
uint8_t *test_gb_rom;
static struct compile_ctx test_ctx;
struct compile_ctx *test_compile_ctx = &test_ctx;

// Read function for test compiler context
static uint8_t test_read(void *dmg, uint16_t address)
{
    (void)dmg;
    return test_gb_rom[address];
}

// Memory access callbacks for Musashi
unsigned int m68k_read_memory_8(unsigned int address)
{
    if (address < MEM_SIZE) {
        return mem[address];
    }
    printf("attempted to read byte from %08x\n", address);
    exit(1);
}

unsigned int m68k_read_memory_16(unsigned int address)
{
    if (address + 1 < MEM_SIZE) {
        return (mem[address] << 8) | mem[address + 1];
    }
    printf("attempted to read word from %08x\n", address);
    exit(1);
}

unsigned int m68k_read_memory_32(unsigned int address)
{
    if (address + 3 < MEM_SIZE) {
        return (mem[address] << 24) | (mem[address + 1] << 16) |
               (mem[address + 2] << 8) | mem[address + 3];
    }
    printf("attempted to read long from %08x\n", address);
    exit(1);
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    if (address < MEM_SIZE) {
        mem[address] = value & 0xff;
    } else {
        printf("attempted to write byte %08x = %d\n", address, value);
    }
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    if (address + 1 < MEM_SIZE) {
        mem[address] = (value >> 8) & 0xff;
        mem[address + 1] = value & 0xff;
    } else {
        printf("attempted to write word %08x = %d\n", address, value);
    }
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    if (address + 3 < MEM_SIZE) {
        mem[address] = (value >> 24) & 0xff;
        mem[address + 1] = (value >> 16) & 0xff;
        mem[address + 2] = (value >> 8) & 0xff;
        mem[address + 3] = value & 0xff;
    } else {
        printf("attempted to write long %08x = %d\n", address, value);
    }
}

// Set up stub functions for dmg_read/dmg_write
// These are 68k code that the compiled JIT code can call
// Use A0 as scratch
static void setup_runtime_stubs(void)
{
    // stub_write: writes value byte to address
    // Stack layout after jsr: ret(4), dmg(4), addr(2), val(1), empty(1)
    // moveq #0, d0        ; clear d0
    // move.w 8(sp), d0    ; d0 = address (zero-extended)
    // movea.l d0, a0      ; a0 = address
    // move.b 10(sp), (a0) ; write value to memory
    // rts
    static const uint8_t stub_write[] = {
        0x70, 0x00,              // moveq #0, d0
        0x30, 0x2f, 0x00, 0x08,  // move.w 8(sp), d0
        0x20, 0x40,              // movea.l d0, a0
        0x10, 0xaf, 0x00, 0x0a,  // move.b 10(sp), (a0)
        0x4e, 0x75               // rts
    };

    // stub_read: reads byte from address, returns in d0
    // Stack layout after jsr: ret(4), dmg(4), addr(2)
    // Note: must zero-extend address since movea.w sign-extends
    // moveq #0, d0        ; clear d0
    // move.w 8(sp), d0    ; d0 = address (zero-extended)
    // movea.l d0, a0      ; a0 = address
    // move.b (a0), d0     ; read value
    // rts
    static const uint8_t stub_read[] = {
        0x70, 0x00,              // moveq #0, d0
        0x30, 0x2f, 0x00, 0x08,  // move.w 8(sp), d0
        0x20, 0x40,              // movea.l d0, a0
        0x10, 0x10,              // move.b (a0), d0
        0x4e, 0x75               // rts
    };

    static const uint8_t stub_ei_di[] = {
        0x31, 0xef, 0x00, 0x08, 0x40, 0x00, // move.w 8(sp), (U16_INTERRUPTS_ENABLED)
        0x4e, 0x75                          // rts
    };

    // stub_read16: reads 16-bit word from address (little-endian), returns in d0.w
    // Stack layout after jsr: ret(4), dmg(4), addr(2)
    // Read high byte first into d0, shift, then OR with low byte
    static const uint8_t stub_read16[] = {
        0x70, 0x00,              // moveq #0, d0
        0x30, 0x2f, 0x00, 0x08,  // move.w 8(sp), d0 (address)
        0x20, 0x40,              // movea.l d0, a0
        0x70, 0x00,              // moveq #0, d0
        0x10, 0x28, 0x00, 0x01,  // move.b 1(a0), d0 (high byte)
        0xe1, 0x48,              // lsl.w #8, d0 (shift high byte to bits 8-15)
        0x10, 0x10,              // move.b (a0), d0 (OR in low byte - overwrites bits 0-7)
        0x4e, 0x75               // rts
    };

    // stub_write16: writes 16-bit word to address (little-endian)
    // Stack layout after jsr: ret(4), dmg(4), addr(2), data(2)
    static const uint8_t stub_write16[] = {
        0x70, 0x00,              // moveq #0, d0
        0x30, 0x2f, 0x00, 0x08,  // move.w 8(sp), d0 (address)
        0x20, 0x40,              // movea.l d0, a0
        0x32, 0x2f, 0x00, 0x0a,  // move.w 10(sp), d1 (data)
        0x10, 0x81,              // move.b d1, (a0) (low byte)
        0xe0, 0x49,              // lsr.w #8, d1
        0x11, 0x41, 0x00, 0x01,  // move.b d1, 1(a0) (high byte)
        0x4e, 0x75               // rts
    };

    // stub_stop: always returns 1 (halt) for tests
    // In tests, STOP always means end of execution
    static const uint8_t stub_stop[] = {
        0x70, 0x01,              // moveq #1, d0
        0x4e, 0x75               // rts
    };

    // Copy stubs to memory
    memcpy(mem + STUB_BASE, stub_read, sizeof(stub_read));
    memcpy(mem + STUB_BASE + 0x20, stub_write, sizeof(stub_write));
    memcpy(mem + STUB_BASE + 0x40, stub_ei_di, sizeof(stub_ei_di));
    memcpy(mem + STUB_BASE + 0x60, stub_read16, sizeof(stub_read16));
    memcpy(mem + STUB_BASE + 0x80, stub_write16, sizeof(stub_write16));
    memcpy(mem + STUB_BASE + 0xa0, stub_stop, sizeof(stub_stop));

    // Set up jit_runtime context structure at JIT_CTX_ADDR
    // See compiler.h for JIT_CTX_* offset definitions
    m68k_write_memory_32(JIT_CTX_ADDR + JIT_CTX_DMG, 0x00004000);
    m68k_write_memory_32(JIT_CTX_ADDR + JIT_CTX_READ, STUB_BASE);
    m68k_write_memory_32(JIT_CTX_ADDR + JIT_CTX_WRITE, STUB_BASE + 0x20);
    m68k_write_memory_32(JIT_CTX_ADDR + JIT_CTX_EI_DI, STUB_BASE + 0x40);
    m68k_write_memory_8(JIT_CTX_ADDR + JIT_CTX_INTCHECK, 0);
    m68k_write_memory_8(JIT_CTX_ADDR + JIT_CTX_ROM_BANK, 1);
    m68k_write_memory_32(JIT_CTX_ADDR + JIT_CTX_DISPATCH, 0); // infinite loop at 0
    m68k_write_memory_32(JIT_CTX_ADDR + JIT_CTX_READ16, STUB_BASE + 0x60);
    m68k_write_memory_32(JIT_CTX_ADDR + JIT_CTX_WRITE16, STUB_BASE + 0x80);
    m68k_write_memory_32(JIT_CTX_ADDR + JIT_CTX_PATCH_HELPER, 0);
    m68k_write_memory_32(JIT_CTX_ADDR + JIT_CTX_READ_CYCLES, 0);
    // frame_cycles pointer for HALT/LY wait tests
    m68k_write_memory_32(JIT_CTX_ADDR + JIT_CTX_FRAME_CYCLES_PTR, FRAME_CYCLES_ADDR);
    m68k_write_memory_32(FRAME_CYCLES_ADDR, 0);
    // stop_func for STOP instruction (always halts in tests)
    m68k_write_memory_32(JIT_CTX_ADDR + JIT_CTX_STOP_FUNC, STUB_BASE + 0xa0);
}

// Initialize Musashi, copy code to memory, set up stack, run
void run_code(struct code_block *block)
{
    int k;

    memset(mem, 0, MEM_SIZE);

    // Set up runtime stubs and context
    setup_runtime_stubs();

    // Copy code to CODE_BASE
    memcpy(mem + CODE_BASE, block->code, block->length);

    // each test case will return to address 0, which contains an infinite loop
    m68k_write_memory_32(STACK_BASE - 4, 0);
    m68k_write_memory_16(0, 0x60fe);  // bra.s *

    m68k_pulse_reset();

    m68k_set_reg(M68K_REG_SP, STACK_BASE - 4);
    m68k_set_reg(M68K_REG_ISP, STACK_BASE);
    m68k_set_reg(M68K_REG_PC, CODE_BASE);

    for (k = 0; k < 8; k++) {
        m68k_set_reg(M68K_REG_D0 + k, 0);
    }
    // Clear A0-A6, but not A7 (stack pointer)
    for (k = 0; k < 7; k++) {
        m68k_set_reg(M68K_REG_A0 + k, 0);
    }

    // Set A4 to runtime context
    m68k_set_reg(M68K_REG_A4, JIT_CTX_ADDR);

    m68k_execute(1000);
}

#define HALT_SENTINEL 0xffffffff
#define MAX_CACHED_BLOCKS 256

// Run a complete GB program with block dispatcher
void run_program(uint8_t *gb_rom, uint16_t start_pc)
{
    struct code_block *cache[MAX_CACHED_BLOCKS] = {0};
    uint32_t pc = start_pc;
    int k;

    memset(mem, 0, MEM_SIZE);

    // Set up runtime stubs and context
    setup_runtime_stubs();

    // Set up halt trap at address 0
    m68k_write_memory_16(0, 0x60fe);  // bra.s *

    m68k_pulse_reset();

    // Clear all registers once at start
    for (k = 0; k < 8; k++) {
        m68k_set_reg(M68K_REG_D0 + k, 0);
    }
    for (k = 0; k < 7; k++) {
        m68k_set_reg(M68K_REG_A0 + k, 0);
    }

    // Initialize GB stack pointer (A3 = base + SP, and JIT_CTX_GB_SP for slow path)
    m68k_set_reg(M68K_REG_A3, GB_MEM_BASE + DEFAULT_GB_SP);
    m68k_write_memory_16(JIT_CTX_ADDR + JIT_CTX_GB_SP, DEFAULT_GB_SP);

    // Set A4 to runtime context
    m68k_set_reg(M68K_REG_A4, JIT_CTX_ADDR);

    while (1) {
        // Look up or compile block
        struct code_block *block = NULL;
        if (pc < MAX_CACHED_BLOCKS) {
            block = cache[pc];
        }
        if (!block) {
            test_gb_rom = gb_rom;
            block = compile_block(pc, test_compile_ctx);
            if (pc < MAX_CACHED_BLOCKS) {
                cache[pc] = block;
            }
        }

        // Copy block code to execution area
        memcpy(mem + CODE_BASE, block->code, block->length);

        // Set up return address to trap
        m68k_write_memory_32(STACK_BASE - 4, 0);
        m68k_set_reg(M68K_REG_SP, STACK_BASE - 4);
        m68k_set_reg(M68K_REG_PC, CODE_BASE);

        m68k_execute(5000);

        // Check D0 for next PC or halt
        pc = get_dreg(REG_68K_D_NEXT_PC);
        if (pc == HALT_SENTINEL) {
            break;
        }
    }

    // Clean up cached blocks
    for (k = 0; k < MAX_CACHED_BLOCKS; k++) {
        if (cache[k]) {
            block_free(cache[k]);
        }
    }
}

uint32_t get_dreg(int reg)
{
    return m68k_get_reg(NULL, M68K_REG_D0 + reg);
}

uint32_t get_areg(int reg)
{
    return m68k_get_reg(NULL, M68K_REG_A0 + reg);
}

uint8_t get_mem_byte(uint16_t addr)
{
    return mem[addr];
}

void set_mem_byte(uint16_t addr, uint8_t value)
{
    mem[addr] = value;
}

void set_frame_cycles(uint32_t cycles)
{
    m68k_write_memory_32(FRAME_CYCLES_ADDR, cycles);
}

uint32_t get_cycle_count(void)
{
    return m68k_get_reg(NULL, M68K_REG_D0 + REG_68K_D_CYCLE_COUNT);
}

// Run a single block with specified frame_cycles value
// Used for testing HALT and LY wait patterns
void run_block_with_frame_cycles(uint8_t *gb_rom, uint32_t frame_cycles)
{
    int k;

    memset(mem, 0, MEM_SIZE);
    setup_runtime_stubs();

    // Set frame_cycles before execution
    m68k_write_memory_32(FRAME_CYCLES_ADDR, frame_cycles);

    // Compile block at address 0
    test_gb_rom = gb_rom;
    struct code_block *block = compile_block(0, test_compile_ctx);

    // Copy code to CODE_BASE
    memcpy(mem + CODE_BASE, block->code, block->length);

    // Set up trap at address 0 for when block returns
    m68k_write_memory_32(STACK_BASE - 4, 0);
    m68k_write_memory_16(0, 0x60fe);  // bra.s *

    m68k_pulse_reset();

    m68k_set_reg(M68K_REG_SP, STACK_BASE - 4);
    m68k_set_reg(M68K_REG_ISP, STACK_BASE);
    m68k_set_reg(M68K_REG_PC, CODE_BASE);

    for (k = 0; k < 8; k++) {
        m68k_set_reg(M68K_REG_D0 + k, 0);
    }
    for (k = 0; k < 7; k++) {
        m68k_set_reg(M68K_REG_A0 + k, 0);
    }

    // Set A4 to runtime context
    m68k_set_reg(M68K_REG_A4, JIT_CTX_ADDR);

    // Initialize GB stack pointer (A3 = base + SP)
    m68k_set_reg(M68K_REG_A3, GB_MEM_BASE + DEFAULT_GB_SP);

    m68k_execute(1000);

    block_free(block);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("Initializing...\n");
    compiler_init();
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);

    // Initialize test compile context
    test_ctx.dmg = NULL;
    test_ctx.read = test_read;

    register_load_tests();
    register_alu_tests();
    register_branch_tests();
    register_cb_tests();
    register_stack_tests();
    register_timing_tests();
    register_cgb_tests();

    printf("\nall tests passed\n");

    return 0;
}
