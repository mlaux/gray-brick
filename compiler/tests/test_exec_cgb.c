// CGB-specific tests
// Note: These tests verify GB bytecode compilation. Full CGB register emulation
// requires the actual dmg_read_slow/dmg_write_slow handlers which aren't used
// by the test stubs. These tests verify the bytecode patterns compile and execute.

#include "tests.h"

// Test VBK register read pattern (ldh a, ($4f))
TEST(test_cgb_vbk_read_pattern)
{
    uint8_t rom[] = {
        0xf0, 0x4f,       // ldh a, ($ff4f)  ; read VBK
        0x10              // stop
    };
    run_program(rom, 0);
    // In test environment, this reads from flat memory at $ff4f
    // Result depends on what's in memory at that address
}

// Test VBK register write pattern (ldh ($4f), a)
TEST(test_cgb_vbk_write_pattern)
{
    uint8_t rom[] = {
        0x3e, 0x01,       // ld a, $01
        0xe0, 0x4f,       // ldh ($ff4f), a  ; write VBK
        0x10              // stop
    };
    run_program(rom, 0);
    // Verify A register contains expected value
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x01);
}

// Test SVBK register access pattern
TEST(test_cgb_svbk_pattern)
{
    uint8_t rom[] = {
        0x3e, 0x03,       // ld a, $03
        0xe0, 0x70,       // ldh ($ff70), a  ; write SVBK
        0xf0, 0x70,       // ldh a, ($ff70)  ; read SVBK
        0x10              // stop
    };
    run_program(rom, 0);
    // Verify A holds the value we wrote (in test env, reads back directly)
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x03);
}

// Test KEY1 register access pattern
TEST(test_cgb_key1_pattern)
{
    uint8_t rom[] = {
        0x3e, 0x01,       // ld a, $01       ; arm speed switch
        0xe0, 0x4d,       // ldh ($ff4d), a  ; write KEY1
        0xf0, 0x4d,       // ldh a, ($ff4d)  ; read KEY1
        0x10              // stop
    };
    run_program(rom, 0);
    // Verify A holds the value we wrote
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x01);
}

// Test BCPS/BCPD palette access pattern
TEST(test_cgb_palette_pattern)
{
    uint8_t rom[] = {
        0x3e, 0x80,       // ld a, $80       ; index 0, auto-increment ON
        0xe0, 0x68,       // ldh ($ff68), a  ; write BCPS
        0x3e, 0x1f,       // ld a, $1f       ; white (red component)
        0xe0, 0x69,       // ldh ($ff69), a  ; write BCPD (index now 1)
        0x3e, 0x00,       // ld a, $00
        0xe0, 0x69,       // ldh ($ff69), a  ; write BCPD (index now 2)
        0xf0, 0x68,       // ldh a, ($ff68)  ; read BCPS
        0x10              // stop
    };
    run_program(rom, 0);
    // After 2 palette writes with auto-increment, index should be 2
    // But test stubs don't implement auto-increment, so this verifies
    // the bytecode pattern compiles and executes
}

// Test HDMA register setup pattern
TEST(test_cgb_hdma_setup_pattern)
{
    uint8_t rom[] = {
        // Set up HDMA source ($C000)
        0x3e, 0xc0,       // ld a, $c0
        0xe0, 0x51,       // ldh ($ff51), a  ; HDMA1 = $c0
        0x3e, 0x00,       // ld a, $00
        0xe0, 0x52,       // ldh ($ff52), a  ; HDMA2 = $00
        // Set up HDMA dest ($8000)
        0x3e, 0x80,       // ld a, $80
        0xe0, 0x53,       // ldh ($ff53), a  ; HDMA3 = $80
        0x3e, 0x00,       // ld a, $00
        0xe0, 0x54,       // ldh ($ff54), a  ; HDMA4 = $00
        // Read back HDMA1
        0xf0, 0x51,       // ldh a, ($ff51)
        0x10              // stop
    };
    run_program(rom, 0);
    // Verify HDMA1 value is what we wrote
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0xc0);
}

// Test VRAM bank switching pattern (typical tile copy)
TEST(test_cgb_vram_bank_switch_pattern)
{
    uint8_t rom[] = {
        // Select VRAM bank 0
        0x3e, 0x00,       // ld a, $00
        0xe0, 0x4f,       // ldh ($ff4f), a  ; VBK = 0
        // Write to VRAM
        0x21, 0x00, 0x80, // ld hl, $8000
        0x36, 0xaa,       // ld (hl), $aa
        // Select VRAM bank 1
        0x3e, 0x01,       // ld a, $01
        0xe0, 0x4f,       // ldh ($ff4f), a  ; VBK = 1
        // Write to VRAM (same address, different bank)
        0x36, 0x55,       // ld (hl), $55
        // Switch back to bank 0 and read
        0x3e, 0x00,       // ld a, $00
        0xe0, 0x4f,       // ldh ($ff4f), a  ; VBK = 0
        0x7e,             // ld a, (hl)
        0x10              // stop
    };
    run_program(rom, 0);
    // In test env with flat memory, this reads back $55 (last write)
    // With real CGB emulation, would read $aa (bank 0 value)
}

// Test WRAM bank switching pattern
TEST(test_cgb_wram_bank_switch_pattern)
{
    uint8_t rom[] = {
        // Select WRAM bank 2
        0x3e, 0x02,       // ld a, $02
        0xe0, 0x70,       // ldh ($ff70), a  ; SVBK = 2
        // Write to switchable bank region
        0x21, 0x00, 0xd0, // ld hl, $d000
        0x36, 0xaa,       // ld (hl), $aa
        // Select WRAM bank 3
        0x3e, 0x03,       // ld a, $03
        0xe0, 0x70,       // ldh ($ff70), a  ; SVBK = 3
        // Write different value
        0x36, 0x55,       // ld (hl), $55
        // Switch back to bank 2 and read
        0x3e, 0x02,       // ld a, $02
        0xe0, 0x70,       // ldh ($ff70), a  ; SVBK = 2
        0x7e,             // ld a, (hl)
        0x10              // stop
    };
    run_program(rom, 0);
    // Similar to VRAM test - verifies bytecode pattern
}

void register_cgb_tests(void)
{
    printf("\nCGB register access patterns:\n");
    RUN_TEST(test_cgb_vbk_read_pattern);
    RUN_TEST(test_cgb_vbk_write_pattern);
    RUN_TEST(test_cgb_svbk_pattern);
    RUN_TEST(test_cgb_key1_pattern);
    RUN_TEST(test_cgb_palette_pattern);
    RUN_TEST(test_cgb_hdma_setup_pattern);

    printf("\nCGB bank switching patterns:\n");
    RUN_TEST(test_cgb_vram_bank_switch_pattern);
    RUN_TEST(test_cgb_wram_bank_switch_pattern);
}
