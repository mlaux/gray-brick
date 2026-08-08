// host port of system6/jit.c: the C dispatcher loop driving compiled
// blocks under Musashi, with a write-trap call gate routing slow-path
// memory access to the real hardware code in src/
//
// state ownership at block boundaries:
// - gb_sp / stack_in_ram / read_cycles: 68k-side ctx is authoritative
//   (emitted code maintains them); synced 68k -> host after execution,
//   written back only when check_interrupts modifies gb_sp
// - wake_limit / eff_double_speed / current_rom_bank /
//   frame_cycles shadow: host is authoritative; synced host -> 68k before
//   each entry
// - page tables: host dmg->read_page/write_page are authoritative; the
//   68k copies re-sync after every gated write (bank switches and SMC
//   unmapping must be visible to the rest of the running block)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#include "types.h"
#include "dmg.h"
#include "cgb.h"
#include "lcd.h"
#include "rom.h"
#include "compiler.h"
#include "interop.h"
#include "m68k.h"

#include "../system6/jit.h"
#include "../system6/cache.h"
#include "../system6/arena.h"
#include "../system6/settings.h"
#include "host.h"

int host_chain;
int host_trace;
FILE *host_insn_log;
u32 host_dispatches;

// per-vector interrupt delivery counts and which deadline bounded each
// exit budget (--exit-stats)
u32 host_int_delivered[5];
u32 host_exit_cause[EV_COUNT];

static struct dmg *dmg;
static struct compile_ctx compile_ctx;

static u32 host_total_ppu;
static int block_returned;
// set when the exit went through dispatcher_return or patch_helper - the
// only exits the Mac dispatcher may chain. fast-forward exits (HALT, LY
// waits) leave via plain rts and must sync so frame_cycles advances
static int exit_chainable;
static u8 serial_sb;

#define PC_HISTORY_SIZE 16
static u32 pc_history[PC_HISTORY_SIZE];
static u8 op_history[PC_HISTORY_SIZE];
static int pc_history_idx;

// big-endian accessors for emulated memory

static void m68_w16(u32 addr, u16 v)
{
    m68k_mem[addr] = v >> 8;
    m68k_mem[addr + 1] = v & 0xff;
}

static void m68_w32(u32 addr, u32 v)
{
    m68k_mem[addr] = v >> 24;
    m68k_mem[addr + 1] = (v >> 16) & 0xff;
    m68k_mem[addr + 2] = (v >> 8) & 0xff;
    m68k_mem[addr + 3] = v & 0xff;
}

static u16 m68_r16(u32 addr)
{
    return (m68k_mem[addr] << 8) | m68k_mem[addr + 1];
}

static u32 m68_r32(u32 addr)
{
    return ((u32) m68k_mem[addr] << 24) | (m68k_mem[addr + 1] << 16)
         | (m68k_mem[addr + 2] << 8) | m68k_mem[addr + 3];
}

static void ctx_w8(int off, u8 v)
{
    m68k_mem[JIT_CTX_ADDR + off] = v;
}

static void ctx_w16(int off, u16 v)
{
    m68_w16(JIT_CTX_ADDR + off, v);
}

static void ctx_w32(int off, u32 v)
{
    m68_w32(JIT_CTX_ADDR + off, v);
}

static void host_fatal(const char *msg) __attribute__((noreturn));
static void host_fatal(const char *msg)
{
    int k;

    fprintf(stderr, "gb6run: FATAL: %s\n", msg);
    fprintf(stderr, "  68k pc=%08x ppc=%08x sp=%08x\n",
            m68k_get_reg(NULL, M68K_REG_PC),
            m68k_get_reg(NULL, M68K_REG_PPC),
            m68k_get_reg(NULL, M68K_REG_SP));
    for (k = 0; k < 8; k++) {
        fprintf(stderr, "  d%d=%08x a%d=%08x\n", k,
                m68k_get_reg(NULL, M68K_REG_D0 + k), k,
                m68k_get_reg(NULL, M68K_REG_A0 + k));
    }
    fprintf(stderr, "  gb block history (oldest first):\n");
    for (k = 0; k < PC_HISTORY_SIZE; k++) {
        int idx = (pc_history_idx + k) % PC_HISTORY_SIZE;
        fprintf(stderr, "    pc=%04x op=%02x\n", pc_history[idx], op_history[idx]);
    }
    fprintf(stderr, "  bank=%d frame_cycles=%u IF=%02x IE=%02x IME=%d "
            "LCDC=%02x STAT=%02x gb_sp=%04x\n",
            jit_ctx.current_rom_bank, dmg->frame_cycles,
            dmg->interrupt_request_mask, dmg->zero_page[0x7f],
            dmg->interrupt_enable, lcd_read(dmg->lcd, REG_LCDC),
            lcd_read(dmg->lcd, REG_STAT), jit_ctx.gb_sp);
    exit(2);
}

// translate the host page tables into the 68k-side copies. entries are
// biased host pointers into m68k_mem, so subtracting the base gives the
// correctly-biased 68k entry
static void sync_page_tables(void)
{
    int k;

    for (k = 0; k < 16; k++) {
        u8 *rp = dmg->read_page[k];
        u8 *wp = dmg->write_page[k];
        m68_w32(READ_TABLE_ADDR + k * 4, rp ? (u32) (rp - m68k_mem) : 0);
        m68_w32(WRITE_TABLE_ADDR + k * 4, wp ? (u32) (wp - m68k_mem) : 0);
    }
}

// host-authoritative fields, written before every block entry
static void sync_ctx_to_68k(void)
{
    ctx_w8(JIT_CTX_ROM_BANK, jit_ctx.current_rom_bank);
    ctx_w8(JIT_CTX_EFF_DOUBLE_SPEED, jit_ctx.effective_double_speed);
    ctx_w32(JIT_CTX_WAKE_LIMIT, jit_ctx.wake_limit);
    m68_w32(FRAME_SHADOW_ADDR, dmg->frame_cycles);
}

// 68k-authoritative fields, read after every block execution
static void sync_ctx_from_68k(void)
{
    jit_ctx.gb_sp = m68_r16(JIT_CTX_ADDR + JIT_CTX_GB_SP);
    jit_ctx.stack_in_ram = m68_r32(JIT_CTX_ADDR + JIT_CTX_STACK_IN_RAM);
    jit_ctx.read_cycles = m68_r32(JIT_CTX_ADDR + JIT_CTX_READ_CYCLES);
}

// dmg_budget_update tightens wake_limit mid-call (on the Mac jit_ctx is
// the memory compiled code reads; here the 68k side needs the copy
// refreshed so in-block loop-back checks and wake skips see it)
static void sync_budget_to_68k(void)
{
    ctx_w32(JIT_CTX_WAKE_LIMIT, jit_ctx.wake_limit);
}

// --insn-log: every instruction Musashi executes, one line each, with
// "=" event lines interleaved at block entries, gate calls into the C
// side, interrupt delivery, and hardware sync. cycles column is
// m68k_cycles_run(), which restarts at every "enter" line
static void insn_hook(unsigned int pc)
{
    char dis[128];

    m68k_disassemble(dis, pc, compiler_68020 ? M68K_CPU_TYPE_68020
                                             : M68K_CPU_TYPE_68000);
    fprintf(host_insn_log, "%06x %6d  %s\n",
            pc & 0xffffff, m68k_cycles_run(), dis);
}

// serial capture: SB write latches the byte, SC write with bit 7 set
// sends it. dmg.c has no serial support, so this is the only observer
static void gated_write(u16 addr, u8 data)
{
    if (addr == 0xff01) {
        serial_sb = data;
    } else if (addr == 0xff02 && (data & 0x80)) {
        host_serial_byte(serial_sb);
    }
    dmg_write(dmg, addr, data);
}

// port of jit_handle_stop
static int handle_stop(void)
{
    if (dmg->cgb && cgb_speed_switch(dmg->cgb)) {
        jit_ctx.effective_double_speed =
            (dmg->cgb->double_speed && !ignore_double_speed) ? 1 : 0;
        // the block continues executing after STOP and reads this
        ctx_w8(JIT_CTX_EFF_DOUBLE_SPEED, jit_ctx.effective_double_speed);
        dmg_speed_changed(dmg);
        return 0;
    }
    return 1;
}

void host_gate(unsigned int index, unsigned int sp)
{
    switch (index) {
    case GATE_READ: {
        u16 addr = m68_r16(sp + 8);
        u8 v;
        jit_ctx.read_cycles = m68_r32(JIT_CTX_ADDR + JIT_CTX_READ_CYCLES);
        v = dmg_read(dmg, addr);
        if (host_insn_log) {
            fprintf(host_insn_log, "= read %04x -> %02x\n", addr, v);
        }
        m68k_set_reg(M68K_REG_D0, v);
        break;
    }
    case GATE_WRITE: {
        u16 addr = m68_r16(sp + 8);
        u8 data = m68k_mem[sp + 10];
        jit_ctx.read_cycles = m68_r32(JIT_CTX_ADDR + JIT_CTX_READ_CYCLES);
        if (host_insn_log) {
            fprintf(host_insn_log, "= write %04x = %02x\n", addr, data);
        }
        gated_write(addr, data);
        sync_page_tables();
        sync_budget_to_68k();
        break;
    }
    case GATE_MBC_WRITE: {
        u16 addr = m68_r16(sp + 8);
        u8 data = m68k_mem[sp + 10];
        jit_ctx.read_cycles = m68_r32(JIT_CTX_ADDR + JIT_CTX_READ_CYCLES);
        if (host_insn_log) {
            fprintf(host_insn_log, "= mbcw %04x = %02x\n", addr, data);
        }
        dmg->rom->mbc->write(dmg, addr, data);
        sync_page_tables();
        sync_budget_to_68k();
        break;
    }
    case GATE_READ16: {
        u16 addr = m68_r16(sp + 8);
        u16 v;
        jit_ctx.read_cycles = m68_r32(JIT_CTX_ADDR + JIT_CTX_READ_CYCLES);
        v = dmg_read16(dmg, addr);
        if (host_insn_log) {
            fprintf(host_insn_log, "= read16 %04x -> %04x\n", addr, v);
        }
        m68k_set_reg(M68K_REG_D0, v);
        break;
    }
    case GATE_WRITE16: {
        u16 addr = m68_r16(sp + 8);
        u16 data = m68_r16(sp + 10);
        jit_ctx.read_cycles = m68_r32(JIT_CTX_ADDR + JIT_CTX_READ_CYCLES);
        if (host_insn_log) {
            fprintf(host_insn_log, "= write16 %04x = %04x\n", addr, data);
        }
        gated_write(addr, data & 0xff);
        gated_write(addr + 1, data >> 8);
        sync_page_tables();
        sync_budget_to_68k();
        break;
    }
    case GATE_EI_DI: {
        u16 v = m68_r16(sp + 8);
        jit_ctx.read_cycles = m68_r32(JIT_CTX_ADDR + JIT_CTX_READ_CYCLES);
        if (host_insn_log) {
            fprintf(host_insn_log, "= %s\n", v ? "ei" : "di");
        }
        dmg_ei_di(dmg, v);
        sync_budget_to_68k();
        break;
    }
    case GATE_STOP:
        jit_ctx.read_cycles = m68_r32(JIT_CTX_ADDR + JIT_CTX_READ_CYCLES);
        if (host_insn_log) {
            fprintf(host_insn_log, "= stop\n");
        }
        m68k_set_reg(M68K_REG_D0, handle_stop());
        sync_budget_to_68k();
        break;
    case GATE_RETURN:
        if (host_insn_log) {
            fprintf(host_insn_log, "= exit d3=%x d2=%u\n",
                    m68k_get_reg(NULL, M68K_REG_D3),
                    m68k_get_reg(NULL, M68K_REG_D2));
        }
        block_returned = 1;
        m68k_end_timeslice();
        break;
    case GATE_CHAIN:
        exit_chainable = 1;
        break;
    case GATE_EXC:
        host_fatal("68k exception (vector fetch)");
        break;
    default:
        host_fatal("write to unassigned gate address");
    }
}

static void rom_bank_hook(int bank)
{
    // page tables re-sync in the write gate that triggered this. the
    // 68k-side bank byte must update immediately, like on the Mac where
    // A4 points at the real jit_ctx: emitted same-bank skips read it
    // between block entries
    jit_ctx.current_rom_bank = (u8) bank;
    ctx_w8(JIT_CTX_ROM_BANK, (u8) bank);
}

// "move.l a7, (gate).l; rts" - triggers gate `index`, then returns.
// with 0x60fe (bra.s *) as the tail instead, it parks the CPU: used for
// the block-return and exception traps, which stop the timeslice
static void write_trap_stub(u32 addr, int index, int spin)
{
    m68_w16(addr, 0x23cf);
    m68_w32(addr + 2, GATE_BASE + index * 4);
    m68_w16(addr + 6, spin ? 0x60fe : 0x4e75);
}

static u32 gate_stub(int index)
{
    return GATE_STUB_BASE + index * 16;
}

// port of jit_emit_helpers: first arena allocation, stable across resets
static int emit_helpers(void)
{
    const struct code_block *blk;
    u8 *region = arena_alloc(JIT_HELPERS_SIZE + 15);
    u32 base;

    if (!region) {
        return 0;
    }
    base = ((u32) (region - m68k_mem) + 15) & ~15u;
    blk = compile_emit_helpers(base, compile_ctx.hram_base);
    if (!blk) {
        return 0;
    }
    memcpy(m68k_mem + base, blk->code, blk->length);
    if (host_insn_log) {
        fprintf(host_insn_log, "= helpers %06x len=%zu\n",
                base, blk->length);
    }
    return 1;
}

// port of jit_clear_all_blocks
static int clear_all_blocks(void)
{
    int k;

    arena_reset();
    if (!emit_helpers()) {
        return 0;
    }
    if (!cache_init()) {
        return 0;
    }

    for (k = 0; k < 8; k++) {
        if (dmg->saved_write_page[k]) {
            dmg->write_page[k + 8] = dmg->saved_write_page[k];
            dmg->saved_write_page[k] = NULL;
        }
    }

    sync_page_tables();
    if (host_insn_log) {
        fprintf(host_insn_log, "= arena-reset\n");
    }
    return 1;
}

// port of jit_run's compile path; fatal on unrecoverable failure
static void *compile_checked(u32 pc)
{
    struct code_block *block;

    compile_ctx.current_bank = jit_ctx.current_rom_bank;
    block = compile_block(pc, &compile_ctx);

    if (!block) {
        if (!clear_all_blocks()) {
            host_fatal("cache alloc fail after arena reset");
        }
        block = compile_block(pc, &compile_ctx);
        if (!block) {
            host_fatal("block alloc fail after arena reset");
        }
    }

    if (block->error) {
        fprintf(stderr, "gb6run: compile error pc=%02x:%04x op=%02x\n",
                jit_ctx.current_rom_bank, block->failed_address,
                block->failed_opcode);
        host_fatal("unsupported opcode");
    }

    if (!cache_store(pc, jit_ctx.current_rom_bank, block->code)) {
        if (!clear_all_blocks()
                || !cache_store(pc, jit_ctx.current_rom_bank, block->code)) {
            host_fatal("bank cache array alloc fail");
        }
    }

    // upper region code can be rewritten by the game: remember the compiled
    // byte range and unmap fast writes so dmg_write_slow can invalidate
    if (pc >= 0x8000) {
        int p;
        u32 last = block->end_address;
        if (last <= pc) {
            last = 0x10000;
        }
        last--;

        cache_mark_upper_range(pc, last);
        for (p = (pc >> 12); p <= (int) (last >> 12); p++) {
            if (dmg->write_page[p] && !dmg->saved_write_page[p - 8]) {
                dmg->saved_write_page[p - 8] = dmg->write_page[p];
                dmg->write_page[p] = 0;
            }
        }
        sync_page_tables();
    }

    // block map: lets a post-processor attribute arena pcs to GB blocks
    if (host_insn_log) {
        fprintf(host_insn_log, "= compile %02x:%04x..%04x arena=%06x len=%zu\n",
                jit_ctx.current_rom_bank, pc, block->end_address,
                (u32) ((u8 *) block->code - m68k_mem), block->length);
    }

    return block->code;
}

static void execute_block(void *code)
{
    int guard = 0;

    m68_w32(STACK_TOP - 4, RETURN_STUB_ADDR);
    m68k_set_reg(M68K_REG_SP, STACK_TOP - 4);
    m68k_set_reg(M68K_REG_PC, (u32) ((u8 *) code - m68k_mem));

    block_returned = 0;
    exit_chainable = 0;
    while (!block_returned) {
        m68k_execute(8000000);
        if (++guard > 32) {
            host_fatal("watchdog: block did not return to dispatcher");
        }
    }
}

// which IE-gated deadline is the current minimum; mirrors
// dmg_cycles_to_next_event for the --exit-stats histogram
static int exit_cause(void)
{
    u8 ie = dmg->zero_page[0x7f];
    u32 best = dmg->event_deadline[EV_WRAP];
    int cause = EV_WRAP;

    if ((ie & 0x02) && dmg->event_deadline[EV_STAT] < best) {
        best = dmg->event_deadline[EV_STAT];
        cause = EV_STAT;
    }
    if ((ie & 0x04) && dmg->event_deadline[EV_TIMA] < best) {
        best = dmg->event_deadline[EV_TIMA];
        cause = EV_TIMA;
    }
    if ((ie & 0x08) && dmg->event_deadline[EV_SERIAL] < best) {
        best = dmg->event_deadline[EV_SERIAL];
        cause = EV_SERIAL;
    }
    if (dmg->event_deadline[EV_VBLANK] < best) {
        best = dmg->event_deadline[EV_VBLANK];
        cause = EV_VBLANK;
    }
    return cause;
}

// port of update_wake_limit
static void update_wake_limit(void)
{
    u32 dist = dmg_cycles_to_next_event(dmg);

    host_exit_cause[exit_cause()]++;

    if (jit_ctx.effective_double_speed) {
        dist <<= 1;
    }

    jit_ctx.wake_limit = dist;
}

// port of check_interrupts; D3/A3 live in Musashi
static void check_interrupts(void)
{
    static const u16 handlers[] = { 0x40, 0x48, 0x50, 0x58, 0x60 };
    u8 pending = dmg->zero_page[0x7f] & dmg->interrupt_request_mask & 0x1f;
    int k;

    if (!pending) {
        return;
    }

    for (k = 0; k < 5; k++) {
        if (pending & (1 << k)) {
            host_int_delivered[k]++;
            if (host_insn_log) {
                fprintf(host_insn_log, "= int %d vector=%04x\n",
                        k, handlers[k]);
            }
            dmg->interrupt_request_mask &= ~(1 << k);
            dmg->interrupt_enable = 0;

            jit_ctx.gb_sp -= 2;
            m68k_set_reg(M68K_REG_A3, m68k_get_reg(NULL, M68K_REG_A3) - 2);
            dmg_write16(dmg, jit_ctx.gb_sp,
                        (u16) m68k_get_reg(NULL, M68K_REG_D3));
            m68k_set_reg(M68K_REG_D3, handlers[k]);

            ctx_w16(JIT_CTX_GB_SP, jit_ctx.gb_sp);
            break;
        }
    }
}

static void trace_block(u32 pc, u32 d2)
{
    fprintf(stderr,
            "[%02x:%04x] d2=%u frame=%u IF=%02x IE=%02x IME=%d LY=%u "
            "LCDC=%02x STAT=%02x SP=%04x\n",
            jit_ctx.current_rom_bank, pc, d2, dmg->frame_cycles,
            dmg->interrupt_request_mask, dmg->zero_page[0x7f],
            dmg->interrupt_enable, dmg->frame_cycles / 456,
            lcd_read(dmg->lcd, REG_LCDC), lcd_read(dmg->lcd, REG_STAT),
            m68_r16(JIT_CTX_ADDR + JIT_CTX_GB_SP));
}

void host_jit_init(struct dmg *d)
{
    int k;

    dmg = d;

    compiler_init();

    compile_ctx.dmg = dmg;
    compile_ctx.read = dmg_read;
    compile_ctx.cache_store = cache_store;
    compile_ctx.alloc = arena_alloc;
    compile_ctx.current_bank = 1;
    // 68k-space addresses: emitted stack fast paths embed these as
    // absolute A3 values, so they must be Musashi addresses, not host
    // pointers
    compile_ctx.wram_base =
        (void *) (uintptr_t) (DMG_ADDR + offsetof(struct dmg, main_ram));
    compile_ctx.hram_base =
        (void *) (uintptr_t) (DMG_ADDR + offsetof(struct dmg, zero_page));
    compile_ctx.joyp_base =
        (void *) (uintptr_t) (DMG_ADDR + offsetof(struct dmg, joyp));

    // ROM-bank select range for the compiler's same-bank write skip,
    // mirrors system6/jit.c: MBC1/MBC3 full reg, MBC5 low-byte reg only
    compile_ctx.bank_reg_lo = 0;
    compile_ctx.bank_reg_hi = 0;
    if (dmg->rom->mbc) {
        int mbc_type = dmg->rom->mbc->type;
        if ((mbc_type >= 0x01 && mbc_type <= 0x03)
                || (mbc_type >= 0x0f && mbc_type <= 0x13)) {
            compile_ctx.bank_reg_lo = 0x2000;
            compile_ctx.bank_reg_hi = 0x3fff;
        } else if (mbc_type >= 0x19 && mbc_type <= 0x1e) {
            compile_ctx.bank_reg_lo = 0x2000;
            compile_ctx.bank_reg_hi = 0x2fff;
        }
    }

    arena_set_region(&m68k_mem[ARENA_ADDR], ARENA_END - ARENA_ADDR);
    arena_init();
    if (!emit_helpers()) {
        fprintf(stderr, "gb6run: helper emit failed\n");
        exit(2);
    }
    if (!cache_init()) {
        fprintf(stderr, "gb6run: cache init failed\n");
        exit(2);
    }

    // 68k-side scaffolding: vectors, traps, call stubs
    for (k = 0; k < 256; k++) {
        m68_w32(k * 4, EXC_STUB_ADDR);
    }
    write_trap_stub(RETURN_STUB_ADDR, GATE_RETURN, 1);
    write_trap_stub(EXC_STUB_ADDR, GATE_EXC, 1);
    write_trap_stub(CHAIN_STUB_ADDR, GATE_CHAIN, 0);
    write_trap_stub(gate_stub(GATE_READ), GATE_READ, 0);
    write_trap_stub(gate_stub(GATE_WRITE), GATE_WRITE, 0);
    write_trap_stub(gate_stub(GATE_READ16), GATE_READ16, 0);
    write_trap_stub(gate_stub(GATE_WRITE16), GATE_WRITE16, 0);
    write_trap_stub(gate_stub(GATE_EI_DI), GATE_EI_DI, 0);
    write_trap_stub(gate_stub(GATE_STOP), GATE_STOP, 0);
    write_trap_stub(gate_stub(GATE_MBC_WRITE), GATE_MBC_WRITE, 0);

    // host-side jit_ctx, mirroring jit_init
    memset(&jit_ctx, 0, sizeof jit_ctx);
    jit_ctx.dmg = dmg;
    jit_ctx.read_func = dmg_read;
    jit_ctx.write_func = dmg_write;
    jit_ctx.read16_func = dmg_read16;
    jit_ctx.write16_func = dmg_write16;
    jit_ctx.ei_di_func = dmg_ei_di;
    jit_ctx.current_rom_bank = 1;
    jit_ctx.frame_cycles_ptr = &dmg->frame_cycles;
    jit_ctx.gb_sp = 0xfffe;
    jit_ctx.stack_in_ram = 1;
    jit_ctx.effective_double_speed = 0;
    // refined by update_wake_limit after the first dispatch
    jit_ctx.wake_limit = CYCLES_PER_FRAME;

    // 68k-side jit_ctx
    // NOT opaque: emitted ldh fast paths dereference this and index
    // zero_page as the first struct member, so it must be the 68k-space
    // address of the dmg struct (the gate itself uses the host pointer)
    ctx_w32(JIT_CTX_DMG, DMG_ADDR);
    ctx_w32(JIT_CTX_READ, gate_stub(GATE_READ));
    ctx_w32(JIT_CTX_WRITE, gate_stub(GATE_WRITE));
    ctx_w32(JIT_CTX_READ16, gate_stub(GATE_READ16));
    ctx_w32(JIT_CTX_WRITE16, gate_stub(GATE_WRITE16));
    ctx_w32(JIT_CTX_EI_DI, gate_stub(GATE_EI_DI));
    ctx_w32(JIT_CTX_STOP_FUNC, gate_stub(GATE_STOP));
    ctx_w32(JIT_CTX_MBC_WRITE, gate_stub(GATE_MBC_WRITE));
    ctx_w32(JIT_CTX_DISPATCH, CHAIN_STUB_ADDR);
    ctx_w32(JIT_CTX_PATCH_HELPER, CHAIN_STUB_ADDR);
    ctx_w32(JIT_CTX_FRAME_CYCLES_PTR, FRAME_SHADOW_ADDR);
    ctx_w32(JIT_CTX_READ_CYCLES, 0);
    jit_ctx.read_cycles = 0;
    ctx_w16(JIT_CTX_DAA_STATE, 0);
    ctx_w16(JIT_CTX_GB_SP, jit_ctx.gb_sp);
    ctx_w32(JIT_CTX_STACK_IN_RAM, jit_ctx.stack_in_ram);
    m68k_mem[JIT_CTX_ADDR + 16] = 0; // trace_enabled (dispatcher asm only)

    dmg->rom_bank_switch_hook = rom_bank_hook;

    m68k_init();
    m68k_set_cpu_type(compiler_68020 ? M68K_CPU_TYPE_68020
                                     : M68K_CPU_TYPE_68000);
    m68k_pulse_reset();

    if (host_insn_log) {
        fprintf(host_insn_log,
                "# gb6run insn log, cpu=%s\n"
                "# return-stub=%06x exc-stub=%06x chain-stub=%06x "
                "gate-stubs=%06x+16*k\n"
                "# jit-ctx=%06x arena=%06x..%06x\n"
                "# columns: pc, musashi cycles this timeslice, disasm\n",
                compiler_68020 ? "68020" : "68000",
                RETURN_STUB_ADDR, EXC_STUB_ADDR, CHAIN_STUB_ADDR,
                GATE_STUB_BASE, JIT_CTX_ADDR, ARENA_ADDR, ARENA_END);
        m68k_set_instr_hook_callback(insn_hook);
    }

    for (k = 0; k < 8; k++) {
        m68k_set_reg(M68K_REG_D0 + k, 0);
    }
    for (k = 0; k < 7; k++) {
        m68k_set_reg(M68K_REG_A0 + k, 0);
    }

    // boot-ROM handoff state, mirroring jit_init
    m68k_set_reg(M68K_REG_D2, 0);
    m68k_set_reg(M68K_REG_D3, 0x100);
    m68k_set_reg(M68K_REG_D4, (dmg->cgb && dmg->cgb->mode) ? 0x11 : 0x01);
    m68k_set_reg(M68K_REG_D5, 0x00000013);
    m68k_set_reg(M68K_REG_D6, 0x000000d8);
    m68k_set_reg(M68K_REG_D7, 0x05);
    m68k_set_reg(M68K_REG_A2, 0x014d);
    m68k_set_reg(M68K_REG_A3,
                 DMG_ADDR + offsetof(struct dmg, zero_page) + 0x7e);
    m68k_set_reg(M68K_REG_A4, JIT_CTX_ADDR);
    m68k_set_reg(M68K_REG_A5, READ_TABLE_ADDR);
    m68k_set_reg(M68K_REG_A6, WRITE_TABLE_ADDR);
    m68k_set_reg(M68K_REG_SP, STACK_TOP);
    m68k_set_reg(M68K_REG_ISP, STACK_TOP);

    sync_page_tables();
    sync_ctx_to_68k();

    jit_halted = 0;
}

u32 host_frames(void)
{
    return host_total_ppu / 70224;
}

// machine-readable final state for scripts (mooneye tests report pass via
// fibonacci values in B..L). D5/D6 hold BC/DE split as 0x00XX00YY
void host_dump_state(FILE *fp)
{
    u32 d5 = m68k_get_reg(NULL, M68K_REG_D5);
    u32 d6 = m68k_get_reg(NULL, M68K_REG_D6);

    fprintf(fp,
            "state A=%02x BC=%02x%02x DE=%02x%02x HL=%04x SP=%04x PC=%04x "
            "F68=%02x IE=%02x IF=%02x IME=%d LCDC=%02x STAT=%02x LY=%u "
            "bank=%02x frames=%u\n",
            m68k_get_reg(NULL, M68K_REG_D4) & 0xff,
            (d5 >> 16) & 0xff, d5 & 0xff,
            (d6 >> 16) & 0xff, d6 & 0xff,
            m68k_get_reg(NULL, M68K_REG_A2) & 0xffff,
            jit_ctx.gb_sp,
            m68k_get_reg(NULL, M68K_REG_D3) & 0xffff,
            m68k_get_reg(NULL, M68K_REG_D7) & 0xff,
            dmg->zero_page[0x7f], dmg->interrupt_request_mask,
            dmg->interrupt_enable, lcd_read(dmg->lcd, REG_LCDC),
            lcd_read(dmg->lcd, REG_STAT), dmg->frame_cycles / 456,
            jit_ctx.current_rom_bank, host_frames());
}

// one dispatch: look up or compile the block at D3, execute it (chaining
// through cached successors in --chain mode), then sync hardware and
// deliver interrupts. returns 0 when emulation has stopped
int host_jit_run(void)
{
    void *code;
    u32 d2, d3, executed;

    if (jit_halted) {
        return 0;
    }

    d3 = m68k_get_reg(NULL, M68K_REG_D3);
    code = cache_lookup(d3, jit_ctx.current_rom_bank);
    if (!code) {
        code = compile_checked(d3);
    }

enter:
    pc_history[pc_history_idx] = d3;
    op_history[pc_history_idx] = dmg_read(dmg, d3);
    pc_history_idx = (pc_history_idx + 1) % PC_HISTORY_SIZE;
    if (host_trace) {
        trace_block(d3, m68k_get_reg(NULL, M68K_REG_D2));
    }
    if (host_insn_log) {
        fprintf(host_insn_log, "= enter %02x:%04x arena=%06x d2=%u frame=%u\n",
                jit_ctx.current_rom_bank, d3,
                (u32) ((u8 *) code - m68k_mem),
                m68k_get_reg(NULL, M68K_REG_D2),
                dmg->frame_cycles);
    }

    sync_ctx_to_68k();
    execute_block(code);

    d3 = m68k_get_reg(NULL, M68K_REG_D3);
    d2 = m68k_get_reg(NULL, M68K_REG_D2);

    if (d3 == HALT_SENTINEL) {
        fprintf(stderr, "gb6run: cpu halted (no wake source)\n");
        jit_halted = 1;
        return 0;
    }

    if (d3 >> 16) {
        int h = (pc_history_idx + PC_HISTORY_SIZE - 1) % PC_HISTORY_SIZE;
        fprintf(stderr, "gb6run: DIRTY D3 %08x after block %02x:%04x "
                "frame %u\n", d3, jit_ctx.current_rom_bank,
                pc_history[h], host_frames());
    }

    // mimic the Mac's native chaining (dispatcher asm + patched exits):
    // follow cached successors without hardware sync until the budget
    // expires, a block misses, or the exit was a plain-rts fast-forward
    if (host_chain && exit_chainable && d2 < jit_ctx.wake_limit) {
        code = cache_lookup(d3, jit_ctx.current_rom_bank);
        if (code) {
            goto enter;
        }
    }

    sync_ctx_from_68k();
    executed = d2;
    host_total_ppu += jit_ctx.effective_double_speed ? (executed >> 1)
                                                     : executed;
    dmg_sync_hw(dmg, executed);
    if (dmg->interrupt_enable) {
        check_interrupts();
    }
    update_wake_limit();
    m68k_set_reg(M68K_REG_D2, 0);
    jit_ctx.read_cycles = 0;
    ctx_w32(JIT_CTX_READ_CYCLES, 0);
    sync_page_tables();

    if (host_insn_log) {
        fprintf(host_insn_log, "= sync d2=%u frame=%u wake=%u\n",
                executed, dmg->frame_cycles, jit_ctx.wake_limit);
    }

    host_dispatches++;
    return 1;
}
