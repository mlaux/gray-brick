// Musashi memory callbacks over a flat 16 MB address space, plus the
// call-gate MMIO range: 32-bit writes to GATE_BASE trap to host_gate(),
// which routes slow-path memory access to the real hardware code

#include <stdio.h>
#include <stdlib.h>

#include "types.h"
#include "m68k.h"
#include "host.h"

u8 m68k_mem[M68K_MEM_SIZE];

static void bad_access(const char *what, unsigned int address)
{
    fprintf(stderr, "gb6run: %s at %08x out of range (pc=%08x)\n",
            what, address, m68k_get_reg(NULL, M68K_REG_PPC));
    exit(2);
}

unsigned int m68k_read_memory_8(unsigned int address)
{
    if (address >= M68K_MEM_SIZE) {
        bad_access("byte read", address);
    }
    return m68k_mem[address];
}

unsigned int m68k_read_memory_16(unsigned int address)
{
    if (address + 1 >= M68K_MEM_SIZE) {
        bad_access("word read", address);
    }
    return (m68k_mem[address] << 8) | m68k_mem[address + 1];
}

unsigned int m68k_read_memory_32(unsigned int address)
{
    if (address + 3 >= M68K_MEM_SIZE) {
        bad_access("long read", address);
    }
    return ((unsigned int) m68k_mem[address] << 24)
         | (m68k_mem[address + 1] << 16)
         | (m68k_mem[address + 2] << 8)
         | m68k_mem[address + 3];
}

// side-effect-free reads for m68kdasm (--insn-log)

unsigned int m68k_read_disassembler_8(unsigned int address)
{
    return address < M68K_MEM_SIZE ? m68k_mem[address] : 0;
}

unsigned int m68k_read_disassembler_16(unsigned int address)
{
    if (address + 1 >= M68K_MEM_SIZE) {
        return 0;
    }
    return (m68k_mem[address] << 8) | m68k_mem[address + 1];
}

unsigned int m68k_read_disassembler_32(unsigned int address)
{
    if (address + 3 >= M68K_MEM_SIZE) {
        return 0;
    }
    return ((unsigned int) m68k_mem[address] << 24)
         | (m68k_mem[address + 1] << 16)
         | (m68k_mem[address + 2] << 8)
         | m68k_mem[address + 3];
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    if (address >= M68K_MEM_SIZE) {
        bad_access("byte write", address);
    }
    m68k_mem[address] = value & 0xff;
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    if (address + 1 >= M68K_MEM_SIZE) {
        bad_access("word write", address);
    }
    m68k_mem[address] = (value >> 8) & 0xff;
    m68k_mem[address + 1] = value & 0xff;
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    // call gate: stubs do "move.l a7, (GATE_BASE + k*4).l"; the written
    // value is the emulated SP, used to read args off the emulated stack
    if ((address & 0xffffc0) == GATE_BASE) {
        host_gate((address >> 2) & 0xf, value);
        return;
    }
    if (address + 3 >= M68K_MEM_SIZE) {
        bad_access("long write", address);
    }
    m68k_mem[address] = (value >> 24) & 0xff;
    m68k_mem[address + 1] = (value >> 16) & 0xff;
    m68k_mem[address + 2] = (value >> 8) & 0xff;
    m68k_mem[address + 3] = value & 0xff;
}
