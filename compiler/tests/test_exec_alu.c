#include "tests.h"

// 8-bit inc/dec
TEST_EXEC(test_dec_a,               REG_A,  0x1,  0x3e, 0x02, 0x3d, 0x10)
TEST_EXEC(test_dec_b,               REG_BC, 0x00040000,  0x06, 0x05, 0x05, 0x10)
TEST_EXEC(test_dec_c,               REG_BC, 0x00000004,  0x0e, 0x05, 0x0d, 0x10)
TEST_EXEC(test_inc_c,               REG_BC, 0x00000007,  0x0e, 0x06, 0x0c, 0x10)
TEST_EXEC(test_inc_e,               REG_DE, 0x00000006,  0x1e, 0x05, 0x1c, 0x10)
TEST_EXEC(test_xor_a,               REG_A,  0x0,  0x3e, 0x20, 0xaf, 0x10)

// 16-bit inc/dec
TEST_EXEC(test_dec_bc,              REG_BC, 0x00110021, 0x01, 0x22, 0x11, 0x0b, 0x10)

// ld hl, $1234; inc l -> HL = 0x1235
TEST_EXEC(test_inc_l,               REG_HL, 0x1235, 0x21, 0x34, 0x12, 0x2c, 0x10)

// ld hl, $1234; dec l -> HL = 0x1233
TEST_EXEC(test_dec_l,               REG_HL, 0x1233, 0x21, 0x34, 0x12, 0x2d, 0x10)

// ld hl, $1234; inc hl -> HL = 0x1235
TEST_EXEC(test_inc_hl,              REG_HL, 0x1235, 0x21, 0x34, 0x12, 0x23, 0x10)

// Logical operations
// ld a, $10; ld c, $01; or a, c
TEST_EXEC(test_or_a_c,              REG_A,  0x11, 0x3e, 0x10, 0x0e, 0x01, 0xb1, 0x10)

// ld a, $f0; cpl -> A = 0x0f
TEST_EXEC(test_cpl,                 REG_A,  0x0f, 0x3e, 0xf0, 0x2f, 0x10)

// ld a, $ff; and a, $0f -> A = 0x0f
TEST_EXEC(test_and_a_imm,           REG_A,  0x0f, 0x3e, 0xff, 0xe6, 0x0f, 0x10)

// ld a, $10; ld b, $01; or a, b -> A = 0x11
TEST_EXEC(test_or_a_b,              REG_A,  0x11, 0x3e, 0x10, 0x06, 0x01, 0xb0, 0x10)

// ld a, $ff; ld c, $0f; xor a, c -> A = 0xf0
TEST_EXEC(test_xor_a_c,             REG_A,  0xf0, 0x3e, 0xff, 0x0e, 0x0f, 0xa9, 0x10)

// ld a, $ff; ld c, $0f; and a, c -> A = 0x0f
TEST_EXEC(test_and_a_c,             REG_A,  0x0f, 0x3e, 0xff, 0x0e, 0x0f, 0xa1, 0x10)

// ld a, $42; and a, a -> A = 0x42, Z = 0
TEST_EXEC(test_and_a_a_nonzero,     REG_A,  0x42, 0x3e, 0x42, 0xa7, 0x10)

// ld a, $00; and a, a -> A = 0x00, Z = 1
TEST_EXEC(test_and_a_a_zero,        REG_A,  0x00, 0x3e, 0x00, 0xa7, 0x10)

// Add operations
// ld a, $20; add a, a -> A = 0x40
TEST_EXEC(test_add_a_a,             REG_A,  0x40, 0x3e, 0x20, 0x87, 0x10)

// ld a, $10; ld d, $05; add a, d -> A = 0x15
TEST_EXEC(test_add_a_d,             REG_A,  0x15, 0x3e, 0x10, 0x16, 0x05, 0x82, 0x10)

// Test ADD sets carry and ADC uses it (bug fix verification)
TEST(test_add_adc_carry_propagation)
{
    // add a, b overflows: $f0 + $20 = $110, A = $10, C = 1
    // adc a, c uses carry: $10 + $00 + 1 = $11
    uint8_t rom[] = {
        0x3e, 0xf0,       // ld a, $f0
        0x06, 0x20,       // ld b, $20
        0x80,             // add a, b -> A = $10, C = 1
        0x0e, 0x00,       // ld c, $00
        0x89,             // adc a, c -> A = $10 + $00 + 1 = $11
        0x10              // stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x11);
}

// ADC tests
TEST(test_adc_a_c_no_carry)
{
    // xor a (clear carry), ld a, $10, ld c, $05, adc a, c -> A = 0x15
    uint8_t rom[] = {
        0xaf,             // xor a (A=0, C=0)
        0x3e, 0x10,       // ld a, $10
        0x0e, 0x05,       // ld c, $05
        0x89,             // adc a, c
        0x10              // stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x15);
}

TEST(test_adc_a_c_with_carry)
{
    // Set carry via cp, then adc
    uint8_t rom[] = {
        0x3e, 0x00,       // ld a, $00
        0xfe, 0x01,       // cp a, $01 -> C=1 (0 < 1)
        0x3e, 0x10,       // ld a, $10
        0x0e, 0x05,       // ld c, $05
        0x89,             // adc a, c -> A = $10 + $05 + 1 = $16
        0x10              // stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x16);
}

TEST(test_adc_a_imm_with_carry)
{
    // Set carry, then adc a, #imm
    uint8_t rom[] = {
        0x3e, 0x00,       // ld a, $00
        0xfe, 0x01,       // cp a, $01 -> C=1
        0x3e, 0x20,       // ld a, $20
        0xce, 0x10,       // adc a, $10 -> A = $20 + $10 + 1 = $31
        0x10              // stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x31);
}

TEST(test_adc_overflow_sets_carry)
{
    // adc that overflows should set carry
    uint8_t rom[] = {
        0xaf,             // xor a (C=0)
        0x3e, 0xff,       // ld a, $ff
        0x0e, 0x01,       // ld c, $01
        0x89,             // adc a, c -> $ff + $01 = $100, A = $00, C = 1
        0x10              // stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x00);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 1, 1);  // C flag set
}

TEST(test_adc_zero_flag)
{
    // adc resulting in zero should set Z
    uint8_t rom[] = {
        0xaf,             // xor a (C=0)
        0x3e, 0xff,       // ld a, $ff
        0x0e, 0x01,       // ld c, $01
        0x89,             // adc a, c -> A = $00, Z = 1
        0x10              // stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x04, 0x04);  // Z flag set
}

// SBC tests
TEST(test_sbc_a_c_no_carry)
{
    // xor a (clear carry), ld a, $10, ld c, $05, sbc a, c -> A = 0x0b
    uint8_t rom[] = {
        0xaf,             // xor a (A=0, C=0)
        0x3e, 0x10,       // ld a, $10
        0x0e, 0x05,       // ld c, $05
        0x99,             // sbc a, c
        0x10              // stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x0b);
}

TEST(test_sbc_a_c_with_carry)
{
    // Set carry via cp, then sbc
    uint8_t rom[] = {
        0x3e, 0x00,       // ld a, $00
        0xfe, 0x01,       // cp a, $01 -> C=1 (borrow)
        0x3e, 0x10,       // ld a, $10
        0x0e, 0x05,       // ld c, $05
        0x99,             // sbc a, c -> A = $10 - $05 - 1 = $0a
        0x10              // stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x0a);
}

TEST(test_sbc_a_imm_with_carry)
{
    // Set carry, then sbc a, #imm
    uint8_t rom[] = {
        0x3e, 0x00,       // ld a, $00
        0xfe, 0x01,       // cp a, $01 -> C=1
        0x3e, 0x20,       // ld a, $20
        0xde, 0x10,       // sbc a, $10 -> A = $20 - $10 - 1 = $0f
        0x10              // stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x0f);
}

TEST(test_sbc_underflow_sets_carry)
{
    // sbc that underflows should set carry
    uint8_t rom[] = {
        0xaf,             // xor a (C=0)
        0x3e, 0x00,       // ld a, $00
        0x0e, 0x01,       // ld c, $01
        0x99,             // sbc a, c -> $00 - $01 = $ff, C = 1
        0x10              // stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0xff);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 1, 1);  // C flag set
}

// 16-bit add
// ld hl, $1000; ld de, $0234; add hl, de -> HL = 0x1234
TEST_EXEC(test_add_hl_de,           REG_HL, 0x1234, 0x21, 0x00, 0x10, 0x11, 0x34, 0x02, 0x19, 0x10)

// ld hl, $1000; ld bc, $0234; add hl, bc -> HL = 0x1234
TEST_EXEC(test_add_hl_bc,           REG_HL, 0x1234, 0x21, 0x00, 0x10, 0x01, 0x34, 0x02, 0x09, 0x10)

// Memory inc/dec
TEST(test_inc_hl_ind)
{
    // inc (hl) - increment byte at memory address HL
    uint8_t rom[] = {
        0x21, 0x00, 0x50, // 0x0000: ld hl, 0x5000
        0x36, 0x41,       // 0x0003: ld (hl), 0x41
        0x34,             // 0x0005: inc (hl)
        0x10              // 0x0006: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_mem_byte(0x5000), 0x42);
}

TEST(test_dec_hl_ind)
{
    // dec (hl) - decrement byte at memory address HL
    uint8_t rom[] = {
        0x21, 0x00, 0x50, // 0x0000: ld hl, 0x5000
        0x36, 0x43,       // 0x0003: ld (hl), 0x43
        0x35,             // 0x0005: dec (hl)
        0x10              // 0x0006: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_mem_byte(0x5000), 0x42);
}

// and a, a with conditional jump (tests flag setting)
TEST(test_exec_and_a_a_with_jr)
{
    // and a, a sets Z flag correctly for conditional jump
    uint8_t rom[] = {
        0x3e, 0x00,       // 0x0000: ld a, 0x00
        0xa7,             // 0x0002: and a, a (sets Z=1)
        0x28, 0x02,       // 0x0003: jr z, +2 (taken)
        0x0e, 0x11,       // 0x0005: ld c, 0x11 (skipped)
        0x0e, 0x22,       // 0x0007: ld c, 0x22
        0x10              // 0x0009: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_BC) & 0xff, 0x22);  // jumped over 0x11
}

// DAA tests
TEST(test_daa_add_lower_nibble)
{
    // 0x08 + 0x05 = 0x0D, DAA adjusts to 0x13
    uint8_t rom[] = {
        0x3e, 0x08,       // ld a, $08
        0xc6, 0x05,       // add a, $05 -> A = $0D
        0x27,             // daa -> A = $13
        0x10              // stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x13);
}

TEST(test_daa_add_upper_nibble)
{
    // 0x90 + 0x20 = 0xB0, DAA adjusts to 0x10 with C=1
    uint8_t rom[] = {
        0x3e, 0x90,       // ld a, $90
        0xc6, 0x20,       // add a, $20 -> A = $B0
        0x27,             // daa -> A = $10, C = 1
        0x10              // stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x10);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x01, 0x01);  // C set
}

TEST(test_daa_add_both_nibbles)
{
    // 0x99 + 0x01 = 0x9A, DAA adjusts to 0x00 with C=1 (BCD: 99+01=100)
    uint8_t rom[] = {
        0x3e, 0x99,       // ld a, $99
        0xc6, 0x01,       // add a, $01 -> A = $9A
        0x27,             // daa -> A = $00, C = 1, Z = 1
        0x10              // stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x00);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x01, 0x01);  // C set
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x04, 0x04);  // Z set
}

TEST(test_daa_sub_lower_nibble)
{
    // 0x10 - 0x05 = 0x0B, DAA adjusts to 0x05 (BCD: 10-05=05)
    uint8_t rom[] = {
        0x3e, 0x10,       // ld a, $10
        0xd6, 0x05,       // sub a, $05 -> A = $0B
        0x27,             // daa -> A = $05
        0x10              // stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x05);
}

TEST(test_daa_no_adjustment)
{
    // 0x12 + 0x34 = 0x46, no adjustment needed
    uint8_t rom[] = {
        0x3e, 0x12,       // ld a, $12
        0xc6, 0x34,       // add a, $34 -> A = $46
        0x27,             // daa -> A = $46 (no change)
        0x10              // stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x46);
}

// Test that dec a preserves carry flag (Pokemon Crystal xor_a_dec_a pattern)
TEST(test_dec_a_preserves_carry)
{
    // xor a sets C=0, dec a should preserve C=0
    // Then adc a, $00 should NOT add carry (result = $FF + 0 + 0 = $FF)
    uint8_t rom[] = {
        0xaf,             // xor a (A=0, C=0)
        0x3d,             // dec a (A=$FF, C should stay 0)
        0xce, 0x00,       // adc a, $00 -> if C preserved: $FF+0+0=$FF, if C clobbered: $FF+0+1=$00
        0x10              // stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0xff);
}

// Test that inc a preserves carry flag
TEST(test_inc_a_preserves_carry)
{
    // scf sets C=1, inc a should preserve C=1
    // Then adc a, $00 should add carry (result = $02 + 0 + 1 = $03)
    uint8_t rom[] = {
        0x3e, 0x01,       // ld a, $01
        0x37,             // scf (C=1)
        0x3c,             // inc a (A=$02, C should stay 1)
        0xce, 0x00,       // adc a, $00 -> $02 + 0 + 1 = $03
        0x10              // stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x03);
}

void register_alu_tests(void)
{
    printf("\n8-bit inc/dec:\n");
    RUN_TEST(test_dec_a);
    RUN_TEST(test_dec_b);
    RUN_TEST(test_dec_c);
    RUN_TEST(test_inc_c);
    RUN_TEST(test_inc_e);
    RUN_TEST(test_xor_a);

    printf("\n16-bit inc/dec:\n");
    RUN_TEST(test_dec_bc);
    RUN_TEST(test_inc_l);
    RUN_TEST(test_dec_l);
    RUN_TEST(test_inc_hl);

    printf("\nLogical operations:\n");
    RUN_TEST(test_or_a_c);
    RUN_TEST(test_cpl);
    RUN_TEST(test_and_a_imm);
    RUN_TEST(test_or_a_b);
    RUN_TEST(test_xor_a_c);
    RUN_TEST(test_and_a_c);
    RUN_TEST(test_and_a_a_nonzero);
    RUN_TEST(test_and_a_a_zero);

    printf("\nAdd operations:\n");
    RUN_TEST(test_add_a_a);
    RUN_TEST(test_add_a_d);
    RUN_TEST(test_add_adc_carry_propagation);

    printf("\nADC tests:\n");
    RUN_TEST(test_adc_a_c_no_carry);
    RUN_TEST(test_adc_a_c_with_carry);
    RUN_TEST(test_adc_a_imm_with_carry);
    RUN_TEST(test_adc_overflow_sets_carry);
    RUN_TEST(test_adc_zero_flag);

    printf("\nSBC tests:\n");
    RUN_TEST(test_sbc_a_c_no_carry);
    RUN_TEST(test_sbc_a_c_with_carry);
    RUN_TEST(test_sbc_a_imm_with_carry);
    RUN_TEST(test_sbc_underflow_sets_carry);

    printf("\n16-bit add:\n");
    RUN_TEST(test_add_hl_de);
    RUN_TEST(test_add_hl_bc);

    printf("\nMemory inc/dec:\n");
    RUN_TEST(test_inc_hl_ind);
    RUN_TEST(test_dec_hl_ind);

    printf("\nALU with flags:\n");
    RUN_TEST(test_exec_and_a_a_with_jr);

    printf("\nDAA tests:\n");
    RUN_TEST(test_daa_add_lower_nibble);
    RUN_TEST(test_daa_add_upper_nibble);
    RUN_TEST(test_daa_add_both_nibbles);
    RUN_TEST(test_daa_sub_lower_nibble);
    RUN_TEST(test_daa_no_adjustment);

    printf("\nInc/dec carry preservation:\n");
    RUN_TEST(test_dec_a_preserves_carry);
    RUN_TEST(test_inc_a_preserves_carry);
}
