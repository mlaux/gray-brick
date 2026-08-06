#include "tests.h"

// Push/pop BC
TEST(test_push_bc)
{
    // push bc, pop hl - transfer BC to HL via stack
    uint8_t rom[] = {
        0x01, 0x34, 0x12, // 0x0000: ld bc, 0x1234
        0xc5,             // 0x0003: push bc
        0xe1,             // 0x0004: pop hl
        0x10              // 0x0005: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_areg(REG_68K_A_HL), 0x1234);
}

// Push/pop DE
TEST(test_push_de)
{
    // push de, pop hl - transfer DE to HL via stack
    uint8_t rom[] = {
        0x11, 0x34, 0x12, // 0x0000: ld de, 0x1234
        0xd5,             // 0x0003: push de
        0xe1,             // 0x0004: pop hl
        0x10              // 0x0005: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_areg(REG_68K_A_HL), 0x1234);
}

// Push/pop HL
TEST(test_push_hl)
{
    // push hl, pop hl - verify stack round-trip
    uint8_t rom[] = {
        0x21, 0x78, 0x56, // 0x0000: ld hl, 0x5678
        0xe5,             // 0x0003: push hl
        0x21, 0x00, 0x00, // 0x0004: ld hl, 0x0000 (clear HL)
        0xe1,             // 0x0007: pop hl
        0x10              // 0x0008: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_areg(REG_68K_A_HL), 0x5678);
}

// Pop DE
TEST(test_pop_de)
{
    // push hl, pop de - transfer HL to DE via stack
    uint8_t rom[] = {
        0x21, 0x34, 0x12, // 0x0000: ld hl, 0x1234
        0xe5,             // 0x0003: push hl
        0xd1,             // 0x0004: pop de
        0x10              // 0x0005: stop
    };
    run_program(rom, 0);
    // DE in split format: 0x00120034
    ASSERT_EQ(get_dreg(REG_68K_D_DE) & 0xff0000, 0x120000);
    ASSERT_EQ(get_dreg(REG_68K_D_DE) & 0xff, 0x34);

}

// Push/pop AF
TEST(test_push_af)
{
    // push af, pop hl - transfer AF to HL via stack
    uint8_t rom[] = {
        0x3e, 0x42,       // 0x0000: ld a, 0x42
        0xaf,             // 0x0002: xor a (A=0, F=0x04 Z flag set)
        0x3e, 0xab,       // 0x0003: ld a, 0xab (F unchanged)
        0xf5,             // 0x0005: push af
        0xe1,             // 0x0006: pop hl
        0x10              // 0x0007: stop
    };
    run_program(rom, 0);
    // H = A = 0xab, L = F = 0x04
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0xab);
    ASSERT_EQ(get_areg(REG_68K_A_HL) & 0xffff, 0xab04);
}

TEST(test_pop_af)
{
    // push hl, pop af - transfer HL to AF via stack
    uint8_t rom[] = {
        0x21, 0x90, 0xcd, // 0x0000: ld hl, 0xcd90 (A=0xcd, F=0x90)
        0xe5,             // 0x0003: push hl
        0xf1,             // 0x0004: pop af
        0x10              // 0x0005: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0xcd);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0xff, 0x90);
}

// LD HL, SP+n tests
TEST(test_ld_hl_sp_plus_0)
{
    // ld hl, sp+0 - copy SP to HL
    uint8_t rom[] = {
        0x31, 0xf0, 0xdf, // 0x0000: ld sp, 0xdff0
        0xf8, 0x00,       // 0x0003: ld hl, sp+0
        0x10              // 0x0005: stop
    };
    run_program(rom, 0);
    // Mask to 16 bits (movea.w sign-extends for values >= 0x8000)
    ASSERT_EQ(get_areg(REG_68K_A_HL) & 0xffff, 0xdff0);
}

TEST(test_ld_hl_sp_plus_positive)
{
    // ld hl, sp+5 - SP + positive offset
    uint8_t rom[] = {
        0x31, 0xf0, 0xdf, // 0x0000: ld sp, 0xdff0
        0xf8, 0x05,       // 0x0003: ld hl, sp+5
        0x10              // 0x0005: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_areg(REG_68K_A_HL) & 0xffff, 0xdff5);
}

TEST(test_ld_hl_sp_plus_negative)
{
    // ld hl, sp-5 - SP + negative offset (0xfb = -5)
    uint8_t rom[] = {
        0x31, 0xf0, 0xdf, // 0x0000: ld sp, 0xdff0
        0xf8, 0xfb,       // 0x0003: ld hl, sp-5
        0x10              // 0x0005: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_areg(REG_68K_A_HL) & 0xffff, 0xdfeb);
}

// add sp,e8 and ld hl,sp+e8 both clear Z and set C from the low-byte
// addition, with e8 treated as unsigned
TEST(test_add_sp_flags_carry)
{
    uint8_t rom[] = {
        0x31, 0xf0, 0xdf, // 0x0000: ld sp, 0xdff0
        0xaf,             // 0x0003: xor a (Z set)
        0xe8, 0x20,       // 0x0004: add sp, 0x20 (0xf0+0x20 carries)
        0x10              // 0x0006: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0xff, 0x01);
}

TEST(test_add_sp_flags_no_carry)
{
    uint8_t rom[] = {
        0x31, 0xf0, 0xdf, // 0x0000: ld sp, 0xdff0
        0xaf,             // 0x0003: xor a (Z set)
        0xe8, 0x05,       // 0x0004: add sp, 5 (0xf0+0x05 does not carry)
        0x10              // 0x0006: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0xff, 0x00);
}

TEST(test_add_sp_flags_negative_offset)
{
    // e8 = -8, but C comes from the unsigned byte: 0xf0 + 0xf8 carries
    uint8_t rom[] = {
        0x31, 0xf0, 0xdf, // 0x0000: ld sp, 0xdff0
        0xe8, 0xf8,       // 0x0003: add sp, -8
        0x10              // 0x0005: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0xff, 0x01);
}

TEST(test_ld_hl_sp_flags)
{
    uint8_t rom[] = {
        0x31, 0xf0, 0xdf, // 0x0000: ld sp, 0xdff0
        0xaf,             // 0x0003: xor a (Z set)
        0xf8, 0x20,       // 0x0004: ld hl, sp+0x20
        0x10              // 0x0006: stop
    };
    run_program(rom, 0);
    ASSERT_EQ(get_areg(REG_68K_A_HL) & 0xffff, 0xe010);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0xff, 0x01);
}

// LD SP, HL roundtrip test
TEST(test_ld_sp_hl_roundtrip)
{
    // Save SP to HL, push something, then restore SP
    uint8_t rom[] = {
        0x31, 0xf0, 0xdf, // 0x0000: ld sp, 0xdff0
        0xf8, 0x00,       // 0x0003: ld hl, sp+0  (save SP to HL)
        0x01, 0x34, 0x12, // 0x0005: ld bc, 0x1234
        0xc5,             // 0x0008: push bc (SP now 0xdfee)
        0xf9,             // 0x0009: ld sp, hl (restore SP to 0xdff0)
        0xc5,             // 0x000a: push bc again
        0xe1,             // 0x000b: pop hl (should get 0x1234)
        0x10              // 0x000c: stop
    };
    run_program(rom, 0);
    // If SP was correctly restored, the second push overwrote the first
    // and pop should give us 0x1234
    ASSERT_EQ(get_areg(REG_68K_A_HL) & 0xffff, 0x1234);
}

void register_stack_tests(void)
{
    printf("\nPush/pop BC:\n");
    RUN_TEST(test_push_bc);

    printf("\nPush/pop DE:\n");
    RUN_TEST(test_push_de);

    printf("\nPush/pop HL:\n");
    RUN_TEST(test_push_hl);

    printf("\nPop DE:\n");
    RUN_TEST(test_pop_de);

    printf("\nPush/pop AF:\n");
    RUN_TEST(test_push_af);
    RUN_TEST(test_pop_af);

    printf("\nLD HL, SP+n:\n");
    RUN_TEST(test_ld_hl_sp_plus_0);
    RUN_TEST(test_ld_hl_sp_plus_positive);
    RUN_TEST(test_ld_hl_sp_plus_negative);

    printf("\nSP offset flags:\n");
    RUN_TEST(test_add_sp_flags_carry);
    RUN_TEST(test_add_sp_flags_no_carry);
    RUN_TEST(test_add_sp_flags_negative_offset);
    RUN_TEST(test_ld_hl_sp_flags);

    printf("\nLD SP, HL roundtrip:\n");
    RUN_TEST(test_ld_sp_hl_roundtrip);
}
