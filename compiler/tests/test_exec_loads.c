#include "tests.h"

// 8-bit immediate loads
TEST_EXEC(test_exec_ld_a_imm8,      REG_A, 0x55,  0x3e, 0x55, 0x10)
TEST_EXEC(test_exec_ld_a_zero,      REG_A, 0x00,  0x3e, 0x00, 0x10)
TEST_EXEC(test_exec_ld_a_ff,        REG_A, 0xff,  0x3e, 0xff, 0x10)
TEST_EXEC(test_exec_multiple_ld_a,  REG_A, 0x33,  0x3e, 0x11, 0x3e, 0x22, 0x3e, 0x33, 0x10)

TEST_EXEC(test_exec_ld_b_imm8,      REG_BC, 0x00110000,  0x06, 0x11, 0x10)
TEST_EXEC(test_exec_ld_c_imm8,      REG_BC, 0x00000022,  0x0e, 0x22, 0x10)
TEST_EXEC(test_exec_ld_d_imm8,      REG_DE, 0x00330000,  0x16, 0x33, 0x10)
TEST_EXEC(test_exec_ld_e_imm8,      REG_DE, 0x00000044,  0x1e, 0x44, 0x10)
TEST_EXEC(test_exec_ld_h_imm8,      REG_HL, 0x5500,  0x26, 0x55, 0x10)
TEST_EXEC(test_exec_ld_l_imm8,      REG_HL, 0x0066,  0x2e, 0x66, 0x10)

// 16-bit immediate loads
TEST_EXEC(test_exec_ld_bc_imm16,    REG_BC, 0x00110022,  0x01, 0x22, 0x11, 0x10)
TEST_EXEC(test_exec_ld_de_imm16,    REG_DE, 0x00330044,  0x11, 0x44, 0x33, 0x10)
TEST_EXEC(test_exec_ld_hl_imm16,    REG_HL, 0x5566,  0x21, 0x66, 0x55, 0x10)
TEST_EXEC(test_exec_ld_sp_imm16,    REG_SP, 0x7788,  0x31, 0x88, 0x77, 0x10)

// Register-to-register loads
TEST_EXEC(test_ld_a_b,              REG_A,  0x05, 0x06, 0x05, 0x78, 0x10)
TEST_EXEC(test_ld_b_a,              REG_BC, 0x00110000, 0x3e, 0x11, 0x47, 0x10)
TEST_EXEC(test_ld_a_c,              REG_A,  0x05, 0x0e, 0x05, 0x79, 0x10)
TEST_EXEC(test_ld_c_a,              REG_BC, 0x00000011, 0x3e, 0x11, 0x4f, 0x10)
TEST_EXEC(test_ld_d_a,              REG_DE, 0x00ab0000, 0x3e, 0xab, 0x57, 0x10)
TEST_EXEC(test_ld_e_a,              REG_DE, 0x000000cd, 0x1e, 0xab, 0x3e, 0xcd, 0x5f, 0x10)

// ld b, $12; ld hl, $0000; ld h, b -> HL = 0x1200
TEST_EXEC(test_ld_h_b,              REG_HL, 0x1200, 0x06, 0x12, 0x21, 0x00, 0x00, 0x60, 0x10)

// ld c, $34; ld hl, $0000; ld l, c -> HL = 0x0034
TEST_EXEC(test_ld_l_c,              REG_HL, 0x0034, 0x0e, 0x34, 0x21, 0x00, 0x00, 0x69, 0x10)

// Memory indirect via BC
TEST(test_exec_ld_bc_ind_a)
{
    // ld (bc), a - write A to memory address BC
    uint8_t rom[] = {
        0x01, 0x00, 0x50, // 0x0000: ld bc, 0x5000
        0x3e, 0x42,       // 0x0003: ld a, 0x42
        0x02,             // 0x0005: ld (bc), a
        0x10              // 0x0006: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_mem_byte(0x5000), 0x42);
}

TEST(test_exec_ld_a_bc_ind)
{
    // ld a, (bc) - read byte from memory address BC into A
    uint8_t rom[] = {
        0x01, 0x00, 0x50, // 0x0000: ld bc, 0x5000
        0x3e, 0x42,       // 0x0003: ld a, 0x42
        0x02,             // 0x0005: ld (bc), a  ; write 0x42 to memory
        0x3e, 0x00,       // 0x0006: ld a, 0x00  ; clear A
        0x0a,             // 0x0008: ld a, (bc)  ; read back from memory
        0x10              // 0x0009: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x42);
}

// Memory indirect via DE
TEST(test_exec_ld_de_ind_a)
{
    // ld (de), a - write A to memory address DE
    uint8_t rom[] = {
        0x11, 0x00, 0x50, // 0x0000: ld de, 0x5000
        0x3e, 0x42,       // 0x0003: ld a, 0x42
        0x12,             // 0x0005: ld (de), a
        0x10              // 0x0006: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_mem_byte(0x5000), 0x42);
}

TEST(test_exec_ld_a_de_ind)
{
    // ld a, (de) - read byte from memory address DE into A
    uint8_t rom[] = {
        0x11, 0x00, 0x50, // 0x0000: ld de, 0x5000
        0x3e, 0x42,       // 0x0003: ld a, 0x42
        0x12,             // 0x0005: ld (de), a  ; write 0x42 to memory
        0x3e, 0x00,       // 0x0006: ld a, 0x00  ; clear A
        0x1a,             // 0x0008: ld a, (de)  ; read back from memory
        0x10              // 0x0009: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x42);
}

// Memory indirect via HL with post-increment/decrement
TEST(test_exec_ld_hld_a)
{
    // ld (hl-), a - write A to memory address HL, then decrement HL
    uint8_t rom[] = {
        0x21, 0x02, 0x50, // 0x0000: ld hl, 0x5002
        0x3e, 0xaa,       // 0x0003: ld a, 0xaa
        0x32,             // 0x0005: ld (hl-), a  ; write 0xaa to 0x5002, HL becomes 0x5001
        0x3e, 0xbb,       // 0x0006: ld a, 0xbb
        0x32,             // 0x0008: ld (hl-), a  ; write 0xbb to 0x5001, HL becomes 0x5000
        0x10              // 0x0009: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_mem_byte(0x5002), 0xaa);
    ASSERT_EQ(get_mem_byte(0x5001), 0xbb);
    ASSERT_EQ(get_areg(REG_68K_A_HL), 0x5000);
}

TEST(test_exec_ld_a_hli)
{
    // ld a, (hl+) - write memory address HL to A, then increment HL
    uint8_t rom[] = {
        0x21, 0x00, 0x50, // 0x0000: ld hl, 0x5000
        0x36, 0x11,       // 0x0003: ld (hl), 0x11
        0x2a,             // 0x0005: ld a, (hl+)
        0x10              // 0x0006: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A), 0x11);
    ASSERT_EQ(get_areg(REG_68K_A_HL), 0x5001);
}

// Load from (HL) to register
TEST(test_ld_d_hl_ind)
{
    // ld d, (hl) - load byte at (HL) into D
    uint8_t rom[] = {
        0x21, 0x00, 0x50, // 0x0000: ld hl, 0x5000
        0x36, 0xab,       // 0x0003: ld (hl), 0xab
        0x56,             // 0x0005: ld d, (hl)
        0x10              // 0x0006: stop
    };
    run_program(rom, 0);
    ASSERT_EQ((get_dreg(REG_68K_D_DE) >> 16) & 0xff, 0xab);
}

TEST(test_ld_e_hl_ind)
{
    // ld e, (hl) - load byte at (HL) into E
    uint8_t rom[] = {
        0x21, 0x00, 0x50, // 0x0000: ld hl, 0x5000
        0x36, 0xcd,       // 0x0003: ld (hl), 0xcd
        0x5e,             // 0x0005: ld e, (hl)
        0x10              // 0x0006: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_DE) & 0xff, 0xcd);
}

TEST(test_ld_b_hl_ind)
{
    // ld b, (hl) - load byte at (HL) into B
    uint8_t rom[] = {
        0x21, 0x00, 0x50, // 0x0000: ld hl, 0x5000
        0x36, 0x12,       // 0x0003: ld (hl), 0x12
        0x46,             // 0x0005: ld b, (hl)
        0x10              // 0x0006: stop
    };
    run_program(rom, 0);
    ASSERT_EQ((get_dreg(REG_68K_D_BC) >> 16) & 0xff, 0x12);
}

TEST(test_ld_c_hl_ind)
{
    // ld c, (hl) - load byte at (HL) into C
    uint8_t rom[] = {
        0x21, 0x00, 0x50, // 0x0000: ld hl, 0x5000
        0x36, 0x34,       // 0x0003: ld (hl), 0x34
        0x4e,             // 0x0005: ld c, (hl)
        0x10              // 0x0006: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_BC) & 0xff, 0x34);
}

// ld (hl), imm8
TEST(test_exec_ld_hl_ind_imm8)
{
    // ld (hl), u8 - write immediate byte to memory at HL
    uint8_t rom[] = {
        0x21, 0x00, 0x50, // 0x0000: ld hl, 0x5000
        0x36, 0x42,       // 0x0003: ld (hl), 0x42
        0x21, 0x01, 0x50, // 0x0005: ld hl, 0x5001
        0x36, 0x99,       // 0x0008: ld (hl), 0x99
        0x10              // 0x000a: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_mem_byte(0x5000), 0x42);
    ASSERT_EQ(get_mem_byte(0x5001), 0x99);
}

// MBC bank-select same-bank skip (ctx starts with current_rom_bank = 1)
TEST(test_exec_bank_write_same_skipped)
{
    // writing the current bank number to the bank reg skips the call
    uint8_t rom[] = {
        0x3e, 0x01,       // 0x0000: ld a, 0x01
        0xea, 0x00, 0x3f, // 0x0002: ld (0x3f00), a
        0x10              // 0x0005: stop
    };
    test_compile_ctx->bank_reg_lo = 0x2000;
    test_compile_ctx->bank_reg_hi = 0x3fff;
    run_program(rom, 0);
    test_compile_ctx->bank_reg_lo = 0;
    test_compile_ctx->bank_reg_hi = 0;
    ASSERT_EQ(get_mem_byte(0x3f00), 0x00);
}

TEST(test_exec_bank_write_different_passes)
{
    // a different bank number still reaches the write handler
    uint8_t rom[] = {
        0x3e, 0x02,       // 0x0000: ld a, 0x02
        0xea, 0x00, 0x3f, // 0x0002: ld (0x3f00), a
        0x10              // 0x0005: stop
    };
    test_compile_ctx->bank_reg_lo = 0x2000;
    test_compile_ctx->bank_reg_hi = 0x3fff;
    run_program(rom, 0);
    test_compile_ctx->bank_reg_lo = 0;
    test_compile_ctx->bank_reg_hi = 0;
    ASSERT_EQ(get_mem_byte(0x3f00), 0x02);
}

TEST(test_exec_bank_write_outside_range)
{
    // same value outside the bank reg range is a normal write
    uint8_t rom[] = {
        0x3e, 0x01,       // 0x0000: ld a, 0x01
        0xea, 0x00, 0x50, // 0x0002: ld (0x5000), a
        0x10              // 0x0005: stop
    };
    test_compile_ctx->bank_reg_lo = 0x2000;
    test_compile_ctx->bank_reg_hi = 0x3fff;
    run_program(rom, 0);
    test_compile_ctx->bank_reg_lo = 0;
    test_compile_ctx->bank_reg_hi = 0;
    ASSERT_EQ(get_mem_byte(0x5000), 0x01);
}

// LDH instructions
TEST(test_exec_ldh_imm8_a)
{
    // ld ($ff00 + u8), a - write A to high memory
    uint8_t rom[] = {
        0x3e, 0x42,       // 0x0000: ld a, 0x42
        0xe0, 0x80,       // 0x0002: ld ($ff80), a
        0x3e, 0x99,       // 0x0004: ld a, 0x99
        0xe0, 0x81,       // 0x0006: ld ($ff81), a
        0x10              // 0x0008: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_mem_byte(0x4000), 0x42);
    ASSERT_EQ(get_mem_byte(0x4001), 0x99);
}

TEST(test_exec_ldh_c_a)
{
    // ld ($ff00 + c), a - write A to $ff00 + C
    uint8_t rom[] = {
        0x0e, 0x42,       // 0x0000: ld c, 0x42
        0x3e, 0xaa,       // 0x0002: ld a, 0xaa
        0xe2,             // 0x0004: ld ($ff00 + c), a
        0x0e, 0x43,       // 0x0005: ld c, 0x43
        0x3e, 0xbb,       // 0x0007: ld a, 0xbb
        0xe2,             // 0x0009: ld ($ff00 + c), a
        0x10              // 0x000a: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_mem_byte(0xff42), 0xaa);
    ASSERT_EQ(get_mem_byte(0xff43), 0xbb);
}

TEST(test_exec_ldh_a_imm8)
{
    // ld a, ($ff00 + u8) - read from high memory into A
    uint8_t rom[] = {
        0x3e, 0xab,       // 0x0000: ld a, 0xab
        0xe0, 0x90,       // 0x0002: ld ($ff90), a  ; write 0xab to $ff90
        0x3e, 0x00,       // 0x0004: ld a, 0x00     ; clear A
        0xf0, 0x90,       // 0x0006: ld a, ($ff90)  ; read back from $ff90
        0x10              // 0x0008: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0xab);
}

// LDH counter patterns
TEST(test_ldh_dec_ldh_loop)
{
    // Test the pattern: ldh a, (n); dec a; ldh (n), a; jr nz, loop
    // This is similar to the sprite counter decrement loop
    uint8_t rom[] = {
        0x3e, 0x03,       // 0x0000: ld a, 3
        0xe0, 0x90,       // 0x0002: ldh ($ff90), a  ; counter = 3
        // loop:
        0xf0, 0x90,       // 0x0004: ldh a, ($ff90)  ; read counter
        0x3d,             // 0x0006: dec a
        0xe0, 0x90,       // 0x0007: ldh ($ff90), a  ; write counter
        0x20, 0xf9,       // 0x0009: jr nz, -7 (back to 0x0004)
        0x10              // 0x000b: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x00);
    ASSERT_EQ(get_mem_byte(0xff90), 0x00);
}

TEST(test_ldh_dec_preserves_value)
{
    // Verify that ldh read -> dec -> ldh write preserves the correct byte value
    // even if other registers have garbage in high bytes
    uint8_t rom[] = {
        0x3e, 0x05,       // 0x0000: ld a, 5
        0xe0, 0x91,       // 0x0002: ldh ($ff91), a  ; mem = 5
        0x3e, 0xff,       // 0x0004: ld a, 0xff      ; pollute A with 0xff
        0xf0, 0x91,       // 0x0006: ldh a, ($ff91)  ; read back (should be 5)
        0x3d,             // 0x0008: dec a           ; a = 4
        0xe0, 0x91,       // 0x0009: ldh ($ff91), a  ; write 4
        0x10              // 0x000b: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x04);
    ASSERT_EQ(get_mem_byte(0x4011), 0x04);
}

TEST(test_page_fast_read)
{
    uint8_t rom[] = {
        0x21, 0x05, 0xc0, // ld hl, 0xc005
        0x7e,             // ld a, (hl)
        0x10              // stop
    };
    prepare_block(rom);
    set_mem_byte(PAGE_BUF_C + 5, 0x5a);
    run_prepared_block();
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x5a);
}

TEST(test_page_fast_write)
{
    uint8_t rom[] = {
        0x21, 0x05, 0xc0, // ld hl, 0xc005
        0x3e, 0x77,       // ld a, 0x77
        0x77,             // ld (hl), a
        0x10              // stop
    };
    prepare_block(rom);
    run_prepared_block();
    ASSERT_EQ(get_mem_byte(PAGE_BUF_C + 5), 0x77);
}

TEST(test_page_fast_read_sign_boundary)
{
    // 0x7fff is positive as s16, 0x8000 is negative - the biased entries
    // must land both in the right host page
    uint8_t rom[] = {
        0x21, 0xff, 0x7f, // ld hl, 0x7fff
        0x46,             // ld b, (hl)
        0x21, 0x00, 0x80, // ld hl, 0x8000
        0x4e,             // ld c, (hl)
        0x10              // stop
    };
    prepare_block(rom);
    set_mem_byte(PAGE_BUF_7 + 0xfff, 0x11);
    set_mem_byte(PAGE_BUF_8, 0x22);
    run_prepared_block();
    ASSERT_EQ((get_dreg(REG_68K_D_BC) >> 16) & 0xff, 0x11);
    ASSERT_EQ(get_dreg(REG_68K_D_BC) & 0xff, 0x22);
}

TEST(test_wram_stack_fast_pop_push)
{
    // sp in WRAM: ld sp,imm16 resolves A3 to wram_base at compile time and
    // marks the stack in-ram, so pop and push use A3 directly
    uint8_t rom[] = {
        0x31, 0x80, 0xc1, // ld sp, 0xc180
        0xc1,             // pop bc
        0x31, 0x40, 0xc1, // ld sp, 0xc140
        0xc5,             // push bc
        0x10              // stop
    };
    prepare_block(rom);
    set_mem_byte(PAGE_BUF_C + 0x180, 0x34);
    set_mem_byte(PAGE_BUF_C + 0x181, 0x12);
    run_prepared_block();
    // BC split format 0x00BB00CC
    ASSERT_EQ(get_dreg(REG_68K_D_BC) & 0x00ff00ff, 0x00120034);
    ASSERT_EQ(get_mem_byte(PAGE_BUF_C + 0x13e), 0x34);
    ASSERT_EQ(get_mem_byte(PAGE_BUF_C + 0x13f), 0x12);
}

TEST(test_page_fast_pop_push)
{
    // a cart ram stack stays in slow mode, so pop goes through the read16
    // fast path and push through the write16 fast path
    uint8_t rom[] = {
        0x31, 0x80, 0xa1, // ld sp, 0xa180
        0xc1,             // pop bc
        0x31, 0x40, 0xa1, // ld sp, 0xa140
        0xc5,             // push bc
        0x10              // stop
    };
    prepare_block(rom);
    set_mem_byte(PAGE_BUF_A + 0x180, 0x34);
    set_mem_byte(PAGE_BUF_A + 0x181, 0x12);
    run_prepared_block();
    ASSERT_EQ(get_dreg(REG_68K_D_BC) & 0x00ff00ff, 0x00120034);
    ASSERT_EQ(get_mem_byte(PAGE_BUF_A + 0x13e), 0x34);
    ASSERT_EQ(get_mem_byte(PAGE_BUF_A + 0x13f), 0x12);
}

TEST(test_page_read16_cross_falls_back)
{
    // low byte at 0xafff, high byte at 0xb000 crosses the 4K page
    // boundary, so the fast path must fall back to the stub (which reads
    // literal addresses in test memory)
    uint8_t rom[] = {
        0x31, 0xff, 0xaf, // ld sp, 0xafff
        0xc1,             // pop bc
        0x10              // stop
    };
    prepare_block(rom);
    set_mem_byte(0xafff, 0x78);
    set_mem_byte(0xb000, 0x56);
    run_prepared_block();
    ASSERT_EQ(get_dreg(REG_68K_D_BC) & 0x00ff00ff, 0x00560078);
}

TEST(test_page_write16_cross_falls_back)
{
    // push with sp = 0xb001 writes 0xafff/0xb000, crossing the 4K page
    // boundary - must fall back to the stub
    uint8_t rom[] = {
        0x01, 0x34, 0x12, // ld bc, 0x1234
        0x31, 0x01, 0xb0, // ld sp, 0xb001
        0xc5,             // push bc
        0x10              // stop
    };
    prepare_block(rom);
    run_prepared_block();
    ASSERT_EQ(get_mem_byte(0xafff), 0x34);
    ASSERT_EQ(get_mem_byte(0xb000), 0x12);
}

void register_load_tests(void)
{
    printf("\n8-bit immediate loads:\n");
    RUN_TEST(test_exec_ld_a_imm8);
    RUN_TEST(test_exec_ld_a_zero);
    RUN_TEST(test_exec_ld_a_ff);
    RUN_TEST(test_exec_multiple_ld_a);

    RUN_TEST(test_exec_ld_b_imm8);
    RUN_TEST(test_exec_ld_c_imm8);
    RUN_TEST(test_exec_ld_d_imm8);
    RUN_TEST(test_exec_ld_e_imm8);
    RUN_TEST(test_exec_ld_h_imm8);
    RUN_TEST(test_exec_ld_l_imm8);

    printf("\n16-bit immediate loads:\n");
    RUN_TEST(test_exec_ld_bc_imm16);
    RUN_TEST(test_exec_ld_de_imm16);
    RUN_TEST(test_exec_ld_hl_imm16);
    RUN_TEST(test_exec_ld_sp_imm16);

    printf("\nRegister-to-register loads:\n");
    RUN_TEST(test_ld_a_b);
    RUN_TEST(test_ld_b_a);
    RUN_TEST(test_ld_a_c);
    RUN_TEST(test_ld_c_a);
    RUN_TEST(test_ld_d_a);
    RUN_TEST(test_ld_e_a);
    RUN_TEST(test_ld_h_b);
    RUN_TEST(test_ld_l_c);

    printf("\nMemory indirect (BC/DE):\n");
    RUN_TEST(test_exec_ld_bc_ind_a);
    RUN_TEST(test_exec_ld_a_bc_ind);
    RUN_TEST(test_exec_ld_de_ind_a);
    RUN_TEST(test_exec_ld_a_de_ind);

    printf("\nMemory indirect (HL):\n");
    RUN_TEST(test_exec_ld_hld_a);
    RUN_TEST(test_exec_ld_a_hli);
    RUN_TEST(test_exec_ld_hl_ind_imm8);

    printf("\nLoad from (HL) to register:\n");
    RUN_TEST(test_ld_d_hl_ind);
    RUN_TEST(test_ld_e_hl_ind);
    RUN_TEST(test_ld_b_hl_ind);
    RUN_TEST(test_ld_c_hl_ind);

    printf("\nMBC bank-select skip:\n");
    RUN_TEST(test_exec_bank_write_same_skipped);
    RUN_TEST(test_exec_bank_write_different_passes);
    RUN_TEST(test_exec_bank_write_outside_range);

    printf("\nLDH instructions:\n");
    RUN_TEST(test_exec_ldh_imm8_a);
    RUN_TEST(test_exec_ldh_c_a);
    RUN_TEST(test_exec_ldh_a_imm8);

    printf("\nLDH counter patterns:\n");
    RUN_TEST(test_ldh_dec_ldh_loop);
    RUN_TEST(test_ldh_dec_preserves_value);

    printf("\nPage table fast paths:\n");
    RUN_TEST(test_page_fast_read);
    RUN_TEST(test_page_fast_write);
    RUN_TEST(test_page_fast_read_sign_boundary);
    RUN_TEST(test_wram_stack_fast_pop_push);
    RUN_TEST(test_page_fast_pop_push);
    RUN_TEST(test_page_read16_cross_falls_back);
    RUN_TEST(test_page_write16_cross_falls_back);
}
