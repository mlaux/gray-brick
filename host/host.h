#ifndef _HOST_H
#define _HOST_H

#include <stddef.h>
#include "types.h"

struct lcd;
struct dmg;

// emulated 68k address space (24-bit, 16 MB - the 68000 masks addresses
// to 24 bits, so everything must fit here in both CPU modes)
#define M68K_MEM_SIZE 0x1000000

// layout. GB-visible buffers (rom, wram, vram, hram, cart ram) live inside
// this space so compiled fast paths can reach them; the biased page table
// entries translate linearly (68k entry = host entry - m68k_mem)
#define VECTOR_TABLE_END   0x000400
#define RETURN_STUB_ADDR   0x000400  // blocks return here; write-traps to host
#define EXC_STUB_ADDR      0x000410  // all exception vectors point here
#define CHAIN_STUB_ADDR    0x000420  // dispatcher_return AND patch_helper:
                                     // flags a chainable exit, then rts.
                                     // fast-forward exits (plain rts) skip
                                     // it, so they always sync, as on the Mac
#define GATE_STUB_BASE     0x000440  // call-gate stubs, 16 bytes apart
#define JIT_CTX_ADDR       0x000500  // 68k-side jit_context (A4)
                                     // (0x60 bytes - full at JIT_CTX_MBC_WRITE)
#define FRAME_SHADOW_ADDR  0x000560  // big-endian copy of dmg->frame_cycles
#define READ_TABLE_ADDR    0x000600  // 68k-side page tables (A5/A6),
#define WRITE_TABLE_ADDR   0x000a00  // 16 4KB pages = 64 bytes each
#define STACK_TOP          0x003000  // 68k stack, grows down
#define DMG_ADDR           0x008000  // struct dmg
#define WRAM_ADDR          0x00c000  // dmg->wram, WRAM_SIZE
#define VRAM_ADDR          0x014000  // dmg->vram, VRAM_SIZE
#define HRAM_ADDR          0x018000  // dmg->hram, HRAM_SIZE
#define MBC_ADDR           0x020000  // struct mbc (holds cart ram)
#define ROM_ADDR           0x040000
#define ROM_MAX            0x800000
#define ARENA_ADDR         0x840000  // compiled code, executed in place
#define ARENA_END          0xff0000
#define GATE_BASE          0xff0000  // magic MMIO: writes trap to host_gate

// gate indices; a stub triggers index k by writing SP to GATE_BASE + k*4
enum {
    GATE_READ,
    GATE_WRITE,
    GATE_READ16,
    GATE_WRITE16,
    GATE_EI_DI,
    GATE_STOP,
    GATE_RETURN,
    GATE_EXC,
    GATE_CHAIN,
    GATE_MBC_WRITE,
};

extern u8 m68k_mem[M68K_MEM_SIZE];

// m68k_mem.c calls this when a write hits the gate range
void host_gate(unsigned int index, unsigned int sp);

// host_jit.c - dispatcher loop ported from system6/jit.c
#include <stdio.h>
void host_jit_init(struct dmg *dmg);
int host_jit_run(void);
u32 host_frames(void);
void host_dump_state(FILE *fp);
extern int host_chain;
extern int host_trace;
extern FILE *host_insn_log;
extern u32 host_dispatches;
extern u32 host_int_delivered[5];
extern u32 host_exit_cause[];

// gb6run.c - sink for captured serial bytes ($ff01/$ff02 writes)
void host_serial_byte(u8 byte);

// shims.c
void arena_set_region(u8 *base, size_t size);
extern int host_show_status;
extern u32 host_frames_drawn;
extern void (*host_lcd_draw_hook)(struct lcd *lcd);

#endif
