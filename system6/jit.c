#include <Memory.h>
#include <Timer.h>
#include <Gestalt.h>

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
#include "settings.h"
#include "debug.h"
#include "arena.h"
#include "cpu_cache.h"
#include "prof.h"

// Debug: ring buffer of last 16 PCs executed
#define PC_HISTORY_SIZE 16
static u32 pc_history[PC_HISTORY_SIZE];
static u8 op_history[PC_HISTORY_SIZE];  // first opcode of each block
static int pc_history_idx = 0;

static u32 call_count = 0;
static u32 last_report_tick = 0;

#ifdef GB6_PROFILING
// counted, not sampled: the slow-path memory accesses compiled code made,
// reported per emulated frame. the inline fast paths absorb everything else
static u32 prof_interop;

static u8 prof_read(void *dmg, u16 address)
{
  prof_interop++;
  return dmg_read(dmg, address);
}

static void prof_write(void *dmg, u16 address, u8 data)
{
  prof_interop++;
  dmg_write(dmg, address, data);
}

static u16 prof_read16(void *dmg, u16 address)
{
  prof_interop++;
  return dmg_read16(dmg, address);
}

static void prof_write16(void *dmg, u16 address, u16 data)
{
  prof_interop++;
  dmg_write16(dmg, address, data);
}
#endif

// register state that persists between block executions
struct {
  u32 d2; // cycle countdown from wake_limit, executed = wake_limit - d2
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
    // Speed switched successfully - update effective_double_speed
    jit_ctx.effective_double_speed = (dmg->cgb->double_speed && !ignore_double_speed) ? 1 : 0;
    dmg_speed_changed(dmg);
    return 0;
  }
  // DMG mode or speed switch not armed - halt
  return 1;
}

// Initialize JIT state for a new emulation session
void jit_init(struct dmg *dmg)
{
  long cpu_type;

  set_status_bar("Loading...");
  compiler_init();

  // scaled index addressing needs a 68020 or better
  compiler_68020 = Gestalt(gestaltProcessorType, &cpu_type) == noErr
      && cpu_type >= gestalt68020;

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
#ifdef GB6_PROFILING
  jit_ctx.read_func = prof_read;
  jit_ctx.write_func = prof_write;
  jit_ctx.read16_func = prof_read16;
  jit_ctx.write16_func = prof_write16;
#else
  jit_ctx.read_func = dmg_read;
  jit_ctx.write_func = dmg_write;
  jit_ctx.read16_func = dmg_read16;
  jit_ctx.write16_func = dmg_write16;
#endif
  jit_ctx.ei_di_func = dmg_ei_di;
  jit_ctx.stop_func = jit_handle_stop;
  jit_ctx.current_rom_bank = 1; // bank 1 is default after boot
  jit_ctx.dispatcher_return = get_dispatcher_code();
  jit_ctx.patch_helper = get_patch_helper_code();
  jit_ctx.frame_cycles_ptr = &dmg->frame_cycles;
  jit_ctx.gb_sp = 0xfffe;  // initial SP (HRAM)
  jit_ctx.stack_in_ram = 1;   // fast mode - A3 points to native HRAM
  jit_ctx.effective_double_speed = 0;
  // refined by update_wake_limit after the first dispatch
  jit_ctx.wake_limit = CYCLES_PER_FRAME;
  jit_ctx.skipped_cycles = 0;
  jit_ctx.ly_clamp_skips = 0;
  sync_cache_pointers();

  jit_regs.d2 = jit_ctx.wake_limit; // countdown starts at the full budget
  jit_ctx.read_cycles = jit_ctx.wake_limit; // in-flight count 0
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
  struct dmg *dmg = compile_ctx.dmg;
  int k;

  arena_reset();
  if (!cache_init()) {
    set_status_bar("Cache alloc fail");
    jit_halted = 1;
    return 0;
  }
  sync_cache_pointers();

  // every block is gone, so restore fast writes for pages that were
  // unmapped because they held compiled code
  for (k = 0; k < 0x80; k++) {
    if (dmg->saved_write_page[k]) {
      dmg->write_page[k + 0x80] = dmg->saved_write_page[k];
      dmg->saved_write_page[k] = NULL;
    }
  }

  return 1;
}

// CPU-cycle budget for the next dispatch: exits land right at the next
// armed hardware deadline, and HALT/idle fast-forwards skip exactly there
static void update_wake_limit(struct dmg *dmg)
{
  u32 dist = dmg_cycles_to_next_event(dmg);

  if (jit_ctx.effective_double_speed) {
    // compared against D2, which counts CPU cycles; dist is PPU cycles
    dist <<= 1;
  }

  jit_ctx.wake_limit = dist;
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
  static u32 last_frames_rendered = 0;

  u32 now = TickCount();
  u32 elapsed = now - last_report_tick;
  u32 frames_delta;
  u32 fps;

  // dispatches outnumber frames by thousands to one, so a report every
  // 100 of them lands inside the same tick and averages nothing
  if (elapsed < 30 || last_report_tick == 0) {
    if (last_report_tick == 0) {
      last_report_tick = now;
      last_frames_rendered = frames_now;
    }
    return;
  }

  frames_delta = frames_now - last_frames_rendered;
  fps = (frames_delta * 60) / elapsed;
  last_frames_rendered = frames_now;
  last_report_tick = now;

#ifdef GB6_PROFILING
  {
    // the 1 khz sampler's counts are already milliseconds. everything is
    // reported per emulated frame: percentages move whenever another phase
    // moves, which makes them useless for comparing two scenes
    static u32 last_counts[PROF_NUM_PHASES];
    u32 d[PROF_NUM_PHASES];
    u32 total = 0;
    u32 jit_ms, render_ms, draw_ms, mem, skip, exec, norm, ly;
    int k;

    for (k = 0; k < PROF_NUM_PHASES; k++) {
      d[k] = prof_counts[k] - last_counts[k];
      last_counts[k] = prof_counts[k];
      total += d[k];
    }

    if (!frames_delta) {
      frames_delta = 1;
    }

    jit_ms = d[PROF_JIT] / frames_delta;
    render_ms = d[PROF_RENDER] / frames_delta;
    draw_ms = d[PROF_DRAW] / frames_delta;
    mem = prof_interop / frames_delta;
    prof_interop = 0;

    // GB cycles the fast-forwards skipped outright. reads high in CGB
    // double speed, where the countdown is in CPU cycles
    skip = jit_ctx.skipped_cycles / frames_delta;
    ly = jit_ctx.ly_clamp_skips / frames_delta;
    jit_ctx.skipped_cycles = 0;
    jit_ctx.ly_clamp_skips = 0;

    exec = skip < CYCLES_PER_FRAME ? CYCLES_PER_FRAME - skip : 0;

    // N: jit ms per frame's worth of GB code that actually ran. J and K
    // both move with the scene, this does not - A/B codegen against it
    norm = exec ? jit_ms * CYCLES_PER_FRAME / exec : 0;
    skip = skip * 100 / CYCLES_PER_FRAME;

    if (total > 0) {
      // sync/other/compile are dropped: the remainder is 1000/fps - J - R - B,
      // and the compile phase overwrites the status bar with its own text
      sprintf(buf, "%lu FPS N%lu J%lu R%lu B%lu M%lu K%lu",
          fps, norm, jit_ms, render_ms, draw_ms, mem, skip);

      // L only appears when it has something to say: a nonzero count means
      // the LY clamp skipped cycles that K could not account for
      if (ly) {
        sprintf(buf + strlen(buf), " L%lu", ly);
      }
      set_status_bar(buf);
      return;
    }
  }
#endif

  sprintf(buf, "%lu FPS", fps);
  set_status_bar(buf);
}

int jit_run(struct dmg *dmg)
{
  void *code;
  struct code_block *block;
  char buf[64];

  if (jit_halted) {
      return 0;
  }

  // look up or compile block
  code = cache_lookup(jit_regs.d3, jit_ctx.current_rom_bank);

  if (!code) {
    PROF_SET(PROF_COMPILE);
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

    arena_shrink(block, sizeof *block,
        offsetof(struct code_block, code) + block->length);

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

    // upper region code can be rewritten by the game (RAM interrupt
    // trampolines, HRAM routines). remember which bytes hold compiled
    // code and unmap fast writes for those pages so dmg_write_slow can
    // catch the modification and invalidate
    if ((u16) jit_regs.d3 >= 0x8000) {
      int p;
      u16 pc = (u16) jit_regs.d3;
      u32 last = block->end_address;
      if (last <= pc) {
        last = 0x10000; // block ran to the top of the address space
      }
      last--;

      cache_mark_upper_range(pc, last);
      for (p = (pc >> 8); p <= (int) (last >> 8); p++) {
        if (dmg->write_page[p] && !dmg->saved_write_page[p - 0x80]) {
          dmg->saved_write_page[p - 0x80] = dmg->write_page[p];
          dmg->write_page[p] = 0;
        }
      }
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
    debug_log_block(code);
  }

  // Log PC and first opcode to ring buffer for crash debugging
  pc_history[pc_history_idx] = jit_regs.d3;
  op_history[pc_history_idx] = dmg_read(dmg, jit_regs.d3);
  pc_history_idx = (pc_history_idx + 1) % PC_HISTORY_SIZE;

  PROF_SET(PROF_JIT);
  enter_asm_world(code);

  // Get next PC from D3
  if (jit_regs.d3 == HALT_SENTINEL) {
      PROF_SET(PROF_OTHER);
      set_status_bar("HALT");
      jit_halted = 1;
      return 0;
  }

  // sync hardware with cycles accumulated by compiled code. D2 counts
  // down from wake_limit; read wake_limit AFTER the block since slow
  // writes tighten it (and D2) in tandem mid-block
  PROF_SET(PROF_SYNC);
  dmg_sync_hw(dmg, jit_ctx.wake_limit - jit_regs.d2);
  if (dmg->interrupt_enable) {
    check_interrupts(dmg);
  }
  update_wake_limit(dmg);
  jit_regs.d2 = jit_ctx.wake_limit;
  jit_ctx.read_cycles = jit_ctx.wake_limit;
  PROF_SET(PROF_OTHER);

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