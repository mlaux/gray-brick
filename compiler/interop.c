#include "compiler.h"
#include "emitters.h"
#include "interop.h"

// Retro68 uses D0-D2 as scratch so I have to push cycle count before calling
// back into C. i'm not sure if this is a mac calling convention or specific
// to this gcc port.
// also interestingly, it doesn't appear to use the "A5 world" or A6, so i can
// use those registers while in the JIT world. calling back into C won't mess
// them up

// addr in D1, val_reg specifies value register
void compile_slow_dmg_write(struct code_block *block, uint8_t val_reg)
{
    // store current cycle count for lazy register evaluation
    emit_move_l_dn_disp_an(block, REG_68K_D_CYCLE_COUNT, JIT_CTX_READ_CYCLES, REG_68K_A_CTX);
    // and push so retro68 doesn't erase
    emit_push_l_dn(block, REG_68K_D_CYCLE_COUNT); // 2
    emit_push_b_dn(block, val_reg); // 2
    emit_push_w_dn(block, REG_68K_D_SCRATCH_1); // 2
    emit_push_l_disp_an(block, JIT_CTX_DMG, REG_68K_A_CTX); // 4
    emit_movea_l_disp_an_an(block, JIT_CTX_WRITE, REG_68K_A_CTX, REG_68K_A_SCRATCH_1); // 4
    emit_jsr_ind_an(block, REG_68K_A_SCRATCH_1); // 2
    emit_addq_l_an(block, 7, 8); // 2
    emit_pop_l_dn(block, REG_68K_D_CYCLE_COUNT); // 2
}

// MMU virtual address base for GB memory (0x0000-0xFDFF)
#define GB_VIRT_BASE 0x08000000

// move.w (An,Dm.l), Dd - load word from indexed address
static void emit_move_w_idx_an_dn(
    struct code_block *block,
    uint8_t base_areg,
    uint8_t idx_dreg,
    uint8_t dest_dreg
) {
    // move.w ea, Dn: 0011 ddd 000 110 aaa (mode 110 = An with index)
    // extension word: bit 11 = 1 for .L index (no sign extension)
    emit_word(block, 0x3030 | (dest_dreg << 9) | base_areg);
    emit_word(block, (idx_dreg << 12) | 0x0800);
}

// move.w Ds, (An,Dm.l) - store word to indexed address
static void emit_move_w_dn_idx_an(
    struct code_block *block,
    uint8_t src_dreg,
    uint8_t base_areg,
    uint8_t idx_dreg
) {
    // move.w Dn, ea: 0011 aaa 110 000 sss (mode 110 = An with index)
    // extension word: bit 11 = 1 for .L index (no sign extension)
    emit_word(block, 0x3180 | (base_areg << 9) | src_dreg);
    emit_word(block, (idx_dreg << 12) | 0x0800);
}

// inline dmg_write with MMU fast path - addr in D1, value in val_reg
static void compile_inline_dmg_write(struct code_block *block, uint8_t val_reg)
{
    // Fast path: use MMU for 0x8000 <= addr < 0xFE00
    // cmpi.w #$fe00, d1                 ; 4 bytes [0-3]
    emit_cmpi_w_imm_dn(block, 0xfe00, REG_68K_D_SCRATCH_1);
    // bcc.s slow_path (+24)             ; 2 bytes [4-5] -> offset 30
    emit_bcc_s(block, 24);
    // btst #15, d1                      ; 4 bytes [6-9] - check if addr >= 0x8000
    emit_btst_imm_dn(block, 15, REG_68K_D_SCRATCH_1);
    // beq.s slow_path (+18)             ; 2 bytes [10-11] -> offset 30 (MBC writes)
    emit_beq_b(block, 18);
    // andi.l #$ffff, d1                 ; 6 bytes [12-17] - clear high word
    emit_andi_l_dn(block, REG_68K_D_SCRATCH_1, 0xffff);
    // movea.l #$08000000, a0            ; 6 bytes [18-23]
    emit_movea_l_imm32(block, REG_68K_A_SCRATCH_1, GB_VIRT_BASE);
    // move.b val_reg, (a0,d1.l)         ; 4 bytes [24-27]
    emit_move_b_dn_idx_an(block, val_reg, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_1);
    // bra.s done (+24)                  ; 2 bytes [28-29] -> offset 54
    emit_bra_b(block, 24);

    // slow_path: (offset 30)
    compile_slow_dmg_write(block, val_reg);
    // falls through to done (offset 54)
}

// Call dmg_write(dmg, addr, val) - addr in D1, val in D4 (A register)
void compile_call_dmg_write_a(struct code_block *block)
{
    compile_inline_dmg_write(block, REG_68K_D_A);
    // compile_slow_dmg_write(block, REG_68K_D_A);
}

// Call dmg_write(dmg, addr, val) - addr in D1, val is immediate
void compile_call_dmg_write_imm(struct code_block *block, uint8_t val)
{
    emit_move_b_dn(block, 3, val);
    compile_inline_dmg_write(block, 3);
    // emit_move_b_dn(block, 0, val);
    // compile_slow_dmg_write(block, 0);
}

// Call dmg_write(dmg, addr, val) - addr in D1, val in D0
void compile_call_dmg_write_d0(struct code_block *block)
{
    // uses d0 as scratch so need to move to d3, but it's so long that
    // what's one more instruction...
    emit_move_b_dn_dn(block, 0, 3);
    compile_inline_dmg_write(block, 3);
    // compile_slow_dmg_write(block, 0);
}

// Emit slow path call to dmg_read - expects address in D1, returns in D0
void compile_slow_dmg_read(struct code_block *block)
{
    // store current cycle count for DIV/LY evaluation
    emit_move_l_dn_disp_an(block, REG_68K_D_CYCLE_COUNT, JIT_CTX_READ_CYCLES, REG_68K_A_CTX); // 4
    emit_push_l_dn(block, REG_68K_D_CYCLE_COUNT); // 2
    emit_push_w_dn(block, REG_68K_D_SCRATCH_1); // 2
    emit_push_l_disp_an(block, JIT_CTX_DMG, REG_68K_A_CTX); // 4
    emit_movea_l_disp_an_an(block, JIT_CTX_READ, REG_68K_A_CTX, REG_68K_A_SCRATCH_1); // 4
    emit_jsr_ind_an(block, REG_68K_A_SCRATCH_1); // 2
    emit_addq_l_an(block, 7, 6); // 2
    emit_pop_l_dn(block, REG_68K_D_CYCLE_COUNT); // 2
}

// Call dmg_read(dmg, addr) - addr in D1, result stays in D0
// MMU fast path for addresses < 0xFE00, slow path for I/O
void compile_call_dmg_read(struct code_block *block)
{
    // Fast path: use MMU for addresses < 0xFE00
    // cmpi.w #$fe00, d1                 ; 4 bytes [0-3]
    emit_cmpi_w_imm_dn(block, 0xfe00, REG_68K_D_SCRATCH_1);
    // bcc.s slow_path (+18)             ; 2 bytes [4-5] -> offset 24
    emit_bcc_s(block, 18);
    // andi.l #$ffff, d1                 ; 6 bytes [6-11] - clear high word
    emit_andi_l_dn(block, REG_68K_D_SCRATCH_1, 0xffff);
    // movea.l #$08000000, a0            ; 6 bytes [12-17]
    emit_movea_l_imm32(block, REG_68K_A_SCRATCH_1, GB_VIRT_BASE);
    // move.b (a0,d1.l), d0              ; 4 bytes [18-21]
    emit_move_b_idx_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // bra.s done (+22)                  ; 2 bytes [22-23] -> offset 46
    emit_bra_b(block, 22);

    // slow_path: (offset 24)
    compile_slow_dmg_read(block);
    // falls through to done (offset 46)
}

// Call dmg_read(dmg, addr) - addr in D1, result goes to D4 (A register)
void compile_call_dmg_read_a(struct code_block *block)
{
    compile_call_dmg_read(block);
    emit_move_b_dn_dn(block, 0, REG_68K_D_A);
}

void compile_call_ei_di(struct code_block *block, int enabled)
{
    // push enabled
    emit_moveq_dn(block, REG_68K_D_SCRATCH_1, (int8_t) enabled);
    // i actually have this as a 16-bit int for some reason
    emit_push_w_dn(block, REG_68K_D_SCRATCH_1);
    // push dmg pointer
    emit_push_l_disp_an(block, JIT_CTX_DMG, REG_68K_A_CTX);
    // load address of function
    emit_movea_l_disp_an_an(block, JIT_CTX_EI_DI, REG_68K_A_CTX, REG_68K_A_SCRATCH_1);
    // call dmg_ei_di
    emit_jsr_ind_an(block, REG_68K_A_SCRATCH_1);
    // clean up stack
    emit_addq_l_an(block, 7, 6);
}

// Slow path for dmg_read16 - addr in D1.w, result in D0.w
void compile_slow_dmg_read16(struct code_block *block)
{
    emit_move_l_dn_disp_an(block, REG_68K_D_CYCLE_COUNT, JIT_CTX_READ_CYCLES, REG_68K_A_CTX);
    emit_push_l_dn(block, REG_68K_D_CYCLE_COUNT);
    emit_push_w_dn(block, REG_68K_D_SCRATCH_1);
    emit_push_l_disp_an(block, JIT_CTX_DMG, REG_68K_A_CTX);
    emit_movea_l_disp_an_an(block, JIT_CTX_READ16, REG_68K_A_CTX, REG_68K_A_SCRATCH_1);
    emit_jsr_ind_an(block, REG_68K_A_SCRATCH_1);
    emit_addq_l_an(block, 7, 6);
    emit_pop_l_dn(block, REG_68K_D_CYCLE_COUNT);
}

// Call dmg_read16(dmg, addr) - addr in D1.w, result in D0.w
// MMU fast path for addresses < 0xFDFF (so addr+1 < 0xFE00)
void compile_call_dmg_read16(struct code_block *block)
{
    // Fast path: use MMU for addresses where both bytes < 0xFE00
    // cmpi.w #$fdff, d1                 ; 4 bytes [0-3]
    emit_cmpi_w_imm_dn(block, 0xfdff, REG_68K_D_SCRATCH_1);
    // bcc.s slow_path (+20)             ; 2 bytes [4-5] -> offset 26
    emit_bcc_s(block, 20);
    // andi.l #$ffff, d1                 ; 6 bytes [6-11] - clear high word
    emit_andi_l_dn(block, REG_68K_D_SCRATCH_1, 0xffff);
    // movea.l #$08000000, a0            ; 6 bytes [12-17]
    emit_movea_l_imm32(block, REG_68K_A_SCRATCH_1, GB_VIRT_BASE);
    // move.w (a0,d1.l), d0              ; 4 bytes [18-21] - word read (bytes swapped)
    emit_move_w_idx_an_dn(block, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_1, REG_68K_D_SCRATCH_0);
    // rol.w #8, d0                      ; 2 bytes [22-23] - fix byte order
    emit_rol_w_8(block, REG_68K_D_SCRATCH_0);
    // bra.b done (+22)                  ; 2 bytes [24-25] -> offset 48
    emit_bra_b(block, 22);

    // slow_path: (offset 26)
    compile_slow_dmg_read16(block);
    // falls through to done (offset 48)
}

// Slow path for dmg_write16 - addr in D1.w, data in D0.w
void compile_slow_dmg_write16(struct code_block *block)
{
    emit_move_l_dn_disp_an(block, REG_68K_D_CYCLE_COUNT, JIT_CTX_READ_CYCLES, REG_68K_A_CTX); // 4
    emit_push_l_dn(block, REG_68K_D_CYCLE_COUNT); // 2
    emit_push_w_dn(block, REG_68K_D_SCRATCH_0); // 2
    emit_push_w_dn(block, REG_68K_D_SCRATCH_1); // 2
    emit_push_l_disp_an(block, JIT_CTX_DMG, REG_68K_A_CTX); // 4
    emit_movea_l_disp_an_an(block, JIT_CTX_WRITE16, REG_68K_A_CTX, REG_68K_A_SCRATCH_1); // 4
    emit_jsr_ind_an(block, REG_68K_A_SCRATCH_1); // 2
    emit_addq_l_an(block, 7, 8); // 2
    emit_pop_l_dn(block, REG_68K_D_CYCLE_COUNT); // 2
}

// Call dmg_write16(dmg, addr, data) - addr in D1.w, data in D0.w
// MMU fast path for 0x8000 <= addr < 0xFDFF (so addr+1 < 0xFE00)
void compile_call_dmg_write16_d0(struct code_block *block)
{
    // Fast path: use MMU for addresses where both bytes < 0xFE00 and >= 0x8000
    // cmpi.w #$fdff, d1                 ; 4 bytes [0-3]
    emit_cmpi_w_imm_dn(block, 0xfdff, REG_68K_D_SCRATCH_1);
    // bcc.s slow_path (+26)             ; 2 bytes [4-5] -> offset 32
    emit_bcc_s(block, 26);
    // btst #15, d1                      ; 4 bytes [6-9] - check if addr >= 0x8000
    emit_btst_imm_dn(block, 15, REG_68K_D_SCRATCH_1);
    // beq.s slow_path (+20)             ; 2 bytes [10-11] -> offset 32 (MBC writes)
    emit_beq_b(block, 20);
    // andi.l #$ffff, d1                 ; 6 bytes [12-17] - clear high word
    emit_andi_l_dn(block, REG_68K_D_SCRATCH_1, 0xffff);
    // movea.l #$08000000, a0            ; 6 bytes [18-23]
    emit_movea_l_imm32(block, REG_68K_A_SCRATCH_1, GB_VIRT_BASE);
    // rol.w #8, d0                      ; 2 bytes [24-25] - swap bytes for big-endian
    emit_rol_w_8(block, REG_68K_D_SCRATCH_0);
    // move.w d0, (a0,d1.l)              ; 4 bytes [26-29] - word write
    emit_move_w_dn_idx_an(block, REG_68K_D_SCRATCH_0, REG_68K_A_SCRATCH_1, REG_68K_D_SCRATCH_1);
    // bra.b done (+24)                  ; 2 bytes [30-31] -> offset 56
    emit_bra_b(block, 24);

    // slow_path: (offset 32)
    compile_slow_dmg_write16(block);
    // falls through to done (offset 56)
}
