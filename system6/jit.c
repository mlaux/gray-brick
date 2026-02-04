#include <Memory.h>
#include <Timer.h>

#include <stdio.h>
#include <string.h>

#include "types.h"
#include "jit.h"
#include "compiler.h"
#include "dmg.h"
#include "cgb.h"
#include "cache.h"
#include "lcd.h"
#include "rom.h"
#include "dispatcher_asm.h"
#include "emulator.h"
#include "debug.h"
#include "arena.h"
#include "cpu_cache.h"

// Debug: ring buffer of last 16 PCs executed
#define PC_HISTORY_SIZE 16
static u32 pc_history[PC_HISTORY_SIZE];
static u8 op_history[PC_HISTORY_SIZE];  // first opcode of each block
static int pc_history_idx = 0;

static u32 time_in_jit = 0;
static u32 time_in_sync = 0;
static u32 call_count = 0;
static u32 last_report_tick = 0;

// register state that persists between block executions
struct {
  u32 d2; // accumulated cycles, output
  u32 d3; // next pc, output only
  u32 d4, d5, d6, d7; // a, bc, de, f
  u32 a2, a3, a4; // hl, sp, ctx
  u32 a5, a6; // read_page, write_page
} jit_regs;

// exposed to main emulator.c
jit_context jit_ctx;
int jit_halted = 0;

// compile-time context for address calculation
static struct compile_ctx compile_ctx;

// this is a huge context switch and my main goal is to do this as little as
// possible. currently it will not return to C when jumping to another compiled 
// block. it still does to check and handle interrupts, though. 
static void enter_asm_world(void *code)
{
  asm volatile(
    // save callee-saved registers
    "movem.l %%d2-%%d7/%%a2-%%a6, -(%%sp)\n\t"

    // copy code pointer to A0
    "movea.l %[code], %%a0\n\t"

    // load GB state into 68k registers
    "lea %[jit_regs], %%a1\n\t"
    "movem.l (%%a1), %%d2-%%d7/%%a2-%%a6\n\t"

    // call the generated code, this can then chain to other blocks
    "jsr (%%a0)\n\t"

    // save results back to memory
    "lea %[jit_regs], %%a0\n\t"
    "movem.l %%d2-%%d7/%%a2-%%a3, (%%a0)\n\t"

    // restore callee-saved registers
    "movem.l (%%sp)+, %%d2-%%d7/%%a2-%%a6\n\t"

    : // no outputs
    : [jit_regs] "m" (jit_regs),
      [code] "a" (code)
    : "d0", "d1", "a0", "a1", "cc", "memory"
  );
}

// Sync jit_ctx cache pointers from lru.c, need to do this when the arena
// is cleared and the cache is reinitialized with new arrays
static void sync_cache_pointers(void)
{
  cache_get_arrays(&jit_ctx.bank0_cache, &jit_ctx.banked_cache, &jit_ctx.upper_cache);
}

// Handle STOP instruction - checks for CGB speed switch
// Returns 0 to continue execution, non-zero to halt
static int jit_handle_stop(struct dmg *dmg)
{
  if (dmg->cgb && cgb_speed_switch(dmg->cgb)) {
    // Speed switched successfully, continue execution
    return 0;
  }
  // DMG mode or speed switch not armed - halt
  return 1;
}

// Initialize JIT state for a new emulation session
void jit_init(struct dmg *dmg)
{
  set_status_bar("Loading...");
  compiler_init();

  if (!arena_init()) {
    set_status_bar("Arena alloc fail");
    jit_halted = 1;
    return;
  }

  // pre-allocate cache arrays so dispatcher never sees NULL
  if (!cache_init()) {
    set_status_bar("Cache alloc fail");
    jit_halted = 1;
    return;
  }

  memset(&jit_regs, 0, sizeof jit_regs);

  // Set initial A register for CGB mode ($11) vs DMG mode ($01)
  jit_regs.d4 = (dmg->cgb && dmg->cgb->mode) ? 0x11 : 0x01;

  compile_ctx.dmg = dmg;
  compile_ctx.read = dmg_read;
  compile_ctx.cache_store = cache_store;
  compile_ctx.alloc = arena_alloc;
  compile_ctx.wram_base = dmg->main_ram;
  compile_ctx.hram_base = dmg->zero_page;

  jit_ctx.dmg = dmg;
  jit_ctx.read_func = dmg_read;
  jit_ctx.write_func = dmg_write;
  jit_ctx.read16_func = dmg_read16;
  jit_ctx.write16_func = dmg_write16;
  jit_ctx.ei_di_func = dmg_ei_di;
  jit_ctx.stop_func = jit_handle_stop;
  jit_ctx.current_rom_bank = 1; // bank 1 is default after boot
  jit_ctx.dispatcher_return = get_dispatcher_code();
  jit_ctx.patch_helper = get_patch_helper_code();
  jit_ctx.frame_cycles_ptr = &dmg->frame_cycles;
  jit_ctx.gb_sp = 0xfffe;  // initial SP (HRAM)
  jit_ctx.stack_in_ram = 1;   // fast mode - A3 points to native HRAM
  sync_cache_pointers();

  jit_regs.d3 = 0x100; // initial PC
  // A register: 0x11 for CGB, 0x01 for DMG
  jit_regs.d4 = (dmg->cgb && dmg->cgb->mode) ? 0x11 : 0x01;
  jit_regs.d5 = 0x00000013; // BC
  jit_regs.d6 = 0x000000d8; // DE
  jit_regs.d7 = 0x05; // flags
  jit_regs.a2 = 0x014d; // HL
  // A3 = native pointer to HRAM at GB SP 0xFFFE
  // HRAM is at dmg->zero_page (0xFF80-0xFFFF), 0xFFFE - 0xFF80 = 0x7E
  jit_regs.a3 = (unsigned long) (dmg->zero_page + 0x7e);
  jit_regs.a4 = (unsigned long) &jit_ctx;
  jit_regs.a5 = (unsigned long) dmg->read_page;
  jit_regs.a6 = (unsigned long) dmg->write_page;

  jit_halted = 0;
}

int jit_clear_all_blocks(void)
{
  arena_reset();
  if (!cache_init()) {
    set_status_bar("Cache alloc fail");
    jit_halted = 1;
    return 0;
  }
  sync_cache_pointers();
  return 1;
}

// i moved this out of dmg.c because it needs to mess with the JIT state
static void check_interrupts(struct dmg *dmg)
{
  static const u16 handlers[] = { 0x40, 0x48, 0x50, 0x58, 0x60 };
  u8 pending = dmg->zero_page[0x7f] & dmg->interrupt_request_mask & 0x1f;

  if (!pending) {
    return;
  }

  int k;
  for (k = 0; k < 5; k++) {
    if (pending & (1 << k)) {
      // clear IF bit and disable IME
      dmg->interrupt_request_mask &= ~(1 << k);
      dmg->interrupt_enable = 0;

      jit_ctx.gb_sp -= 2;
      jit_regs.a3 -= 2;
      dmg_write16(dmg, jit_ctx.gb_sp, jit_regs.d3);

      // Jump to handler
      jit_regs.d3 = handlers[k];
      break;
    }
  }
}

static void update_profiling_status_bar(u32 frames_now)
{
  char buf[64];
  static u32 last_jit = 0, last_sync = 0, last_frames_rendered = 0;

  u32 now = TickCount();
  u32 elapsed = now - last_report_tick;
  u32 exits_per_sec = elapsed > 0 ? (100 * 60) / elapsed : 0;

  u32 d_jit = time_in_jit - last_jit;
  u32 d_sync = time_in_sync - last_sync;

  u32 pct_jit = elapsed > 0 ? (d_jit * 100) / elapsed : 0;
  u32 pct_sync = elapsed > 0 ? (d_sync * 100) / elapsed : 0;

  u32 frames_delta = frames_now - last_frames_rendered;
  u32 fps = elapsed > 0 ? (frames_delta * 60) / elapsed : 0;
  last_frames_rendered = frames_now;

  last_jit = time_in_jit;
  last_sync = time_in_sync;
  last_report_tick = now;

  sprintf(buf, "%lu FPS (J: %lu, S: %lu)", fps, pct_jit, pct_sync);
  set_status_bar(buf);
}

int jit_run(struct dmg *dmg)
{
  void *code;
  struct code_block *block;
  char buf[64];
  u32 t0, t1, t2, t3;

  if (jit_halted) {
      return 0;
  }

  // look up or compile block
  t0 = TickCount();
  code = cache_lookup(jit_regs.d3, jit_ctx.current_rom_bank);

  if (!code) {
    sprintf(buf, "$%02x:%04x %luk/%luk",
      jit_ctx.current_rom_bank,
      jit_regs.d3,
      arena_remaining() / 1024,
      arena_size() / 1024
    );
    set_status_bar(buf);

    compile_ctx.current_bank = jit_ctx.current_rom_bank;
    block = compile_block(jit_regs.d3, &compile_ctx);

    if (!block) {
      // arena full, reset and retry once
      if (!jit_clear_all_blocks()) {
        return 0;
      }

      block = compile_block(jit_regs.d3, &compile_ctx);
      if (!block) {
        sprintf(buf, "JIT: block fail pc=%04x", jit_regs.d3);
        set_status_bar(buf);
        jit_halted = 1;
        return 0;
      }
    }

    if (block->error) {
      sprintf(buf, "Error pc=%02x:%04x op=%02x", jit_ctx.current_rom_bank, 
                block->failed_address, block->failed_opcode);
      set_status_bar(buf);
      jit_halted = 1;
      return 0;
    }

    if (!cache_store(jit_regs.d3, jit_ctx.current_rom_bank, block->code)) {
      // this means this was the first block to be stored for a given bank, 
      // and the bank cache array couldn't be allocated. unrecoverable OOM?
      // i'm not actually sure...
      if (!jit_clear_all_blocks()) {
        return 0;
      }

      // try again
      if (!cache_store(jit_regs.d3, jit_ctx.current_rom_bank, block->code)) {
        // something is really wrong
        sprintf(buf, "JIT: bank array fail pc=%04x", jit_regs.d3);
        set_status_bar(buf);
        jit_halted = 1;
        return 0;
      }

      // recovered
    }

    if (TrapAvailable(_CacheFlush)) {
      // for 68040. 68030 needed a cache flush when blocks were patched, but
      // 040 needs it here too because the caches are copy-back, so the code that
      // was just compiled isn't necessarily in main memory yet, and it won't
      // look in the data cache for instructions, just the instruction cache.
      // see Apple Technical Note HW06: Cache As Cache Can
      FlushCodeCache();
    }

    code = block->code;
  }

  // trace mode: show PC before every block execution
  if (jit_ctx.trace_enabled) {
    sprintf(buf, "$%02x:%04lx", jit_ctx.current_rom_bank, jit_regs.d3);
    set_status_bar(buf);
  }

  // Log PC and first opcode to ring buffer for crash debugging
  pc_history[pc_history_idx] = jit_regs.d3;
  op_history[pc_history_idx] = dmg_read(dmg, jit_regs.d3);
  pc_history_idx = (pc_history_idx + 1) % PC_HISTORY_SIZE;

  t1 = TickCount();
  enter_asm_world(code);
  t2 = TickCount();

  // Get next PC from D3
  if (jit_regs.d3 == HALT_SENTINEL) {
      set_status_bar("HALT");
      jit_halted = 1;
      return 0;
  }

  // sync hardware with cycles accumulated by compiled code
  // sprintf(buf, "%lu %lu", dmg->frame_cycles, jit_regs.d2);
  // set_status_bar(buf);
  dmg_sync_hw(dmg, jit_regs.d2);
  if (dmg->interrupt_enable) {
    check_interrupts(dmg);
  }
  jit_regs.d2 = 0;

  t3 = TickCount();
  time_in_jit += t2 - t1;
  time_in_sync += t3 - t2;

  call_count++;
  if (call_count % 100 == 0) {
    update_profiling_status_bar(dmg->frames_rendered);
  }

  return 1;
}

void jit_cleanup(void)
{
  // we need this memory back to load the next ROM
  arena_destroy();
}