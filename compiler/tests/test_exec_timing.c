#include "tests.h"

// ============================================================================
// HALT instruction tests
// HALT fast-forwards D2 to jit_ctx.wake_limit, the dispatcher-computed
// distance to the next armed deadline. the harness defaults it to what the
// dispatcher sets with only vblank and the frame wrap armed: vblank start
// (cycle 65664), or the wrap (70224) once vblank is past
// ============================================================================

TEST(test_halt_before_vblank)
{
    // HALT when frame_cycles=0 should wait 65664 cycles to reach vblank
    uint8_t rom[] = {
        0x76              // halt
    };
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_cycle_count(), 65664);
}

TEST(test_halt_mid_frame)
{
    // HALT at frame_cycles=10000 should wait 55664 cycles (65664-10000)
    uint8_t rom[] = {
        0x76              // halt
    };
    run_block_with_frame_cycles(rom, 10000);
    ASSERT_EQ(get_cycle_count(), 65664 - 10000);
}

TEST(test_halt_just_before_vblank)
{
    // HALT at frame_cycles=65663 should wait 1 cycle
    uint8_t rom[] = {
        0x76              // halt
    };
    run_block_with_frame_cycles(rom, 65663);
    ASSERT_EQ(get_cycle_count(), 1);
}

TEST(test_halt_at_vblank_start)
{
    // HALT at exactly cycle 65664: vblank already fired, so the wake lands
    // on the frame wrap and the dispatcher re-enters the HALT from there
    uint8_t rom[] = {
        0x76              // halt
    };
    run_block_with_frame_cycles(rom, 65664);
    ASSERT_EQ(get_cycle_count(), 70224 - 65664);
}

TEST(test_halt_during_vblank)
{
    // HALT at frame_cycles=68000 (in vblank) wakes at the frame wrap
    uint8_t rom[] = {
        0x76              // halt
    };
    run_block_with_frame_cycles(rom, 68000);
    ASSERT_EQ(get_cycle_count(), 70224 - 68000);
}

TEST(test_halt_near_frame_end)
{
    // HALT at frame_cycles=70000 wakes at the frame wrap
    uint8_t rom[] = {
        0x76              // halt
    };
    run_block_with_frame_cycles(rom, 70000);
    ASSERT_EQ(get_cycle_count(), 70224 - 70000);
}

// ============================================================================
// LY wait pattern tests
// Pattern: ldh a, [$44]; cp N; jr cc, back
// Compiler synthesizes a wait instead of spinning in a loop. The wait
// matches the real loop: if the exit condition already holds it falls
// through after one pass (ldh+cp+jr = 28 cycles), and a wait that would
// cross an armed deadline clamps to wake_limit and exits at the loop head
// ============================================================================

TEST(test_ly_wait_jr_nz_ly0)
{
    // ldh a, [$44]; cp 0; jr nz, back
    // at frame_cycles=0 LY is already 0: the loop exits on the first pass
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44) - read LY
        0xfe, 0x00,       // cp 0
        0x20, 0xfa,       // jr nz, -6 (back to ldh)
        0x10              // stop (not reached, pattern is fused)
    };
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0);
    ASSERT_EQ(get_cycle_count(), 28);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x05, 0x04);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 6);
}

TEST(test_ly_wait_jr_nz_ly90)
{
    // ldh a, [$44]; cp 90; jr nz, back
    // Wait for LY=90, from frame_cycles=0
    // target_cycles = 90 * 456 = 41040
    // D2 = 41040 - 0 = 41040, A = 90
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44) - read LY
        0xfe, 0x5a,       // cp 90
        0x20, 0xfa,       // jr nz, -6
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 90);
    ASSERT_EQ(get_cycle_count(), 90 * 456);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x05, 0x04);
}

TEST(test_ly_wait_jr_nz_ly144)
{
    // Wait for LY=144 (vblank start), from frame_cycles=0
    // target_cycles = 144 * 456 = 65664
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44)
        0xfe, 0x90,       // cp 144
        0x20, 0xfa,       // jr nz, -6
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 144);
    ASSERT_EQ(get_cycle_count(), 144 * 456);
}

TEST(test_ly_wait_jr_nz_past_target)
{
    // Wait for LY=50, but frame_cycles=30000 is already past line 50.
    // The next occurrence is behind the vblank deadline, so the wait
    // advances at deadline pace: D2 = wake_limit, exit at the loop head
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44)
        0xfe, 0x32,       // cp 50
        0x20, 0xfa,       // jr nz, -6
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 30000);
    ASSERT_EQ(get_cycle_count(), 65664 - 30000);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 0);
}

TEST(test_ly_wait_jr_z_ly90)
{
    // ldh a, [$44]; cp 90; jr z, back
    // jr z: loop while LY == 90. At frame_cycles=0 LY is 0, so the loop
    // exits on the first pass with A = actual LY and C set (0 < 90)
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44)
        0xfe, 0x5a,       // cp 90
        0x28, 0xfa,       // jr z, -6
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0);
    ASSERT_EQ(get_cycle_count(), 28);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x05, 0x01);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 6);
}

TEST(test_ly_wait_jr_z_ly90_on_line)
{
    // on line 90 the loop spins until line 91 starts
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44)
        0xfe, 0x5a,       // cp 90
        0x28, 0xfa,       // jr z, -6
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 90 * 456 + 100);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 91);
    ASSERT_EQ(get_cycle_count(), 91 * 456 - (90 * 456 + 100));
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x05, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 6);
}

TEST(test_ly_wait_jr_z_ly153)
{
    // ldh a, [$44]; cp 153; jr z, back
    // at frame_cycles=0 LY is 0, not 153: first-pass exit
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44)
        0xfe, 0x99,       // cp 153
        0x28, 0xfa,       // jr z, -6
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0);
    ASSERT_EQ(get_cycle_count(), 28);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x05, 0x01);
}

TEST(test_ly_wait_jr_z_ly153_on_line)
{
    // on line 153 the wait crosses the frame wrap to line 0
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44)
        0xfe, 0x99,       // cp 153
        0x28, 0xfa,       // jr z, -6
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 153 * 456 + 8);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0);
    ASSERT_EQ(get_cycle_count(), 70224 - (153 * 456 + 8));
    // A wrapped to 0, so C is set (0 < 153)
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x05, 0x01);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 6);
}

TEST(test_ly_wait_jr_c_ly100)
{
    // ldh a, [$44]; cp 100; jr c, back
    // jr c: loop while LY < 100, exit when LY >= 100
    // wait_ly = 100, target_cycles = 100 * 456 = 45600
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44)
        0xfe, 0x64,       // cp 100
        0x38, 0xfa,       // jr c, -6
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 100);
    ASSERT_EQ(get_cycle_count(), 100 * 456);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x05, 0x04);
}

TEST(test_ly_wait_jr_c_already_past)
{
    // frame_cycles=50000 is on line 109, past 100: first-pass exit with
    // A = the actual LY
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44)
        0xfe, 0x64,       // cp 100
        0x38, 0xfa,       // jr c, -6
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 50000);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 109);
    ASSERT_EQ(get_cycle_count(), 28);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x05, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 6);
}

TEST(test_ly_wait_jr_c_exactly_on)
{
    // on line 100 exactly: exit with Z set like the real cp would
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44)
        0xfe, 0x64,       // cp 100
        0x38, 0xfa,       // jr c, -6
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 100 * 456 + 12);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 100);
    ASSERT_EQ(get_cycle_count(), 28);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x05, 0x04);
}

TEST(test_ly_wait_mid_frame)
{
    // Wait for LY=100, starting at frame_cycles=20000
    // LY 100 is at cycle 45600
    // D2 = 45600 - 20000 = 25600
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44)
        0xfe, 0x64,       // cp 100
        0x20, 0xfa,       // jr nz, -6
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 20000);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 100);
    ASSERT_EQ(get_cycle_count(), 45600 - 20000);
}

TEST(test_ly_wait_exact_target)
{
    // Start exactly at the target LY cycle: LY == 50 already, so the
    // loop exits on the first pass instead of waiting a whole frame
    // (Pokemon's raster scroll polls hit this every frame)
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44)
        0xfe, 0x32,       // cp 50
        0x20, 0xfa,       // jr nz, -6
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 22800);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 50);
    ASSERT_EQ(get_cycle_count(), 28);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x05, 0x04);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 6);
}

TEST(test_ly_wait_unreachable_target)
{
    // cp 200: LY can never reach it, so the wait idles at deadline pace
    // (D2 = wake_limit, exit at the loop head) instead of miscomputing
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44)
        0xfe, 0xc8,       // cp 200
        0x20, 0xfa,       // jr nz, -6
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 10000);
    ASSERT_EQ(get_cycle_count(), 65664 - 10000);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 0);
}

TEST(test_ly_wait_clamped_by_wake_limit)
{
    // an armed deadline (LYC match) sits before line 90: the wait stops
    // there and exits at the loop head so the interrupt runs on its line
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44)
        0xfe, 0x5a,       // cp 90
        0x20, 0xfa,       // jr nz, -6
        0x10              // stop
    };
    set_wake_limit(5000);
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_cycle_count(), 5000);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 0);
}

TEST(test_ly_wait_deadline_on_target_line)
{
    // deadline exactly at the target line start: the wait completes and
    // falls through, so the poll can't miss its line to a long handler
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44)
        0xfe, 0x5a,       // cp 90
        0x20, 0xfa,       // jr nz, -6
        0x10              // stop
    };
    set_wake_limit(90 * 456);
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 90);
    ASSERT_EQ(get_cycle_count(), 90 * 456);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 6);
}

// ============================================================================
// LY wait pattern tests with register compare
// Pattern: ld r, N; ldh a, [$44]; cp r; jr cc, back
// Same as immediate but target comes from a register
// ============================================================================

TEST(test_ly_wait_reg_cp_b)
{
    // ld b, 90; ldh a, [$44]; cp b; jr nz, back
    // Wait for LY=90
    uint8_t rom[] = {
        0x06, 0x5a,       // ld b, 90
        0xf0, 0x44,       // ldh a, ($ff44)
        0xb8,             // cp b
        0x20, 0xfb,       // jr nz, -5 (back to ldh)
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 90);
    ASSERT_EQ(get_cycle_count(), 90 * 456);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x05, 0x04);
}

TEST(test_ly_wait_reg_cp_c)
{
    // ld c, 100; ldh a, [$44]; cp c; jr nz, back
    // Wait for LY=100
    uint8_t rom[] = {
        0x0e, 0x64,       // ld c, 100
        0xf0, 0x44,       // ldh a, ($ff44)
        0xb9,             // cp c
        0x20, 0xfb,       // jr nz, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 100);
    ASSERT_EQ(get_cycle_count(), 100 * 456);
}

TEST(test_ly_wait_reg_cp_h)
{
    // ld h, 144; ldh a, [$44]; cp h; jr nz, back
    // Wait for LY=144 (vblank)
    uint8_t rom[] = {
        0x26, 0x90,       // ld h, 144
        0xf0, 0x44,       // ldh a, ($ff44)
        0xbc,             // cp h
        0x20, 0xfb,       // jr nz, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 144);
    ASSERT_EQ(get_cycle_count(), 144 * 456);
}

TEST(test_ly_wait_reg_cp_l)
{
    // ld l, 50; ldh a, [$44]; cp l; jr nz, back
    // Wait for LY=50
    uint8_t rom[] = {
        0x2e, 0x32,       // ld l, 50
        0xf0, 0x44,       // ldh a, ($ff44)
        0xbd,             // cp l
        0x20, 0xfb,       // jr nz, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 50);
    ASSERT_EQ(get_cycle_count(), 50 * 456);
}

TEST(test_ly_wait_reg_jr_z)
{
    // ld b, 90; ldh a, [$44]; cp b; jr z, back
    // at frame_cycles=0 LY is 0, not 90: the loop exits on the first
    // pass (8 for the ld + 24 for one poll pass)
    uint8_t rom[] = {
        0x06, 0x5a,       // ld b, 90
        0xf0, 0x44,       // ldh a, ($ff44)
        0xb8,             // cp b
        0x28, 0xfb,       // jr z, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0);
    ASSERT_EQ(get_cycle_count(), 32);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x05, 0x01);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 7);
}

TEST(test_ly_wait_reg_jr_z_on_line)
{
    // on line 90 the loop spins until line 91 starts
    uint8_t rom[] = {
        0x06, 0x5a,       // ld b, 90
        0xf0, 0x44,       // ldh a, ($ff44)
        0xb8,             // cp b
        0x28, 0xfb,       // jr z, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 90 * 456 + 100);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 91);
    ASSERT_EQ(get_cycle_count(), 91 * 456 - (90 * 456 + 100));
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x05, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 7);
}

TEST(test_ly_wait_reg_jr_z_wrap)
{
    // ld c, 153; ldh a, [$44]; cp c; jr z, back
    // at frame_cycles=0 LY is 0, not 153: first-pass exit
    uint8_t rom[] = {
        0x0e, 0x99,       // ld c, 153
        0xf0, 0x44,       // ldh a, ($ff44)
        0xb9,             // cp c
        0x28, 0xfb,       // jr z, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0);
    ASSERT_EQ(get_cycle_count(), 32);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x05, 0x01);
}

TEST(test_ly_wait_reg_jr_z_wrap_on_line)
{
    // on line 153 the wait crosses the frame wrap: A wraps to 0
    uint8_t rom[] = {
        0x0e, 0x99,       // ld c, 153
        0xf0, 0x44,       // ldh a, ($ff44)
        0xb9,             // cp c
        0x28, 0xfb,       // jr z, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 153 * 456 + 8);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0);
    ASSERT_EQ(get_cycle_count(), 70224 - (153 * 456 + 8));
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x05, 0x01);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 7);
}

TEST(test_ly_wait_reg_jr_z_over_153)
{
    // Pokemon's raster scroll guard: cp h with h=160 while LY is 64.
    // LY can never equal 160, so this must fall through immediately -
    // fusing it into a wait used to eat a frame and flicker the scroll
    uint8_t rom[] = {
        0x26, 0xa0,       // ld h, 160
        0xf0, 0x44,       // ldh a, ($ff44)
        0xbc,             // cp h
        0x28, 0xfb,       // jr z, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 64 * 456 + 100);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 64);
    ASSERT_EQ(get_cycle_count(), 32);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x05, 0x01);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 7);
}

TEST(test_ly_wait_reg_jr_c)
{
    // ld d, 100; ldh a, [$44]; cp d; jr c, back
    // jr c: loop while LY < 100, wait for LY >= 100
    uint8_t rom[] = {
        0x16, 0x64,       // ld d, 100
        0xf0, 0x44,       // ldh a, ($ff44)
        0xba,             // cp d
        0x38, 0xfb,       // jr c, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 100);
    ASSERT_EQ(get_cycle_count(), 100 * 456);
}

TEST(test_ly_wait_reg_mid_frame)
{
    // Wait for LY=100, starting at frame_cycles=20000
    uint8_t rom[] = {
        0x06, 0x64,       // ld b, 100
        0xf0, 0x44,       // ldh a, ($ff44)
        0xb8,             // cp b
        0x20, 0xfb,       // jr nz, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 20000);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 100);
    ASSERT_EQ(get_cycle_count(), 100 * 456 - 20000);
}

TEST(test_ly_wait_reg_past_target)
{
    // Wait for LY=50, but frame_cycles=30000 is already past line 50:
    // advance at deadline pace and exit at the loop head
    uint8_t rom[] = {
        0x1e, 0x32,       // ld e, 50
        0xf0, 0x44,       // ldh a, ($ff44)
        0xbb,             // cp e
        0x20, 0xfb,       // jr nz, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 30000);
    ASSERT_EQ(get_cycle_count(), 65664 - 30000);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 2);
}

TEST(test_ly_wait_reg_on_target_line)
{
    // already on line 50: the loop exits on the first pass with A = N
    uint8_t rom[] = {
        0x1e, 0x32,       // ld e, 50
        0xf0, 0x44,       // ldh a, ($ff44)
        0xbb,             // cp e
        0x20, 0xfb,       // jr nz, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 50 * 456 + 60);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 50);
    ASSERT_EQ(get_cycle_count(), 32);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x05, 0x04);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 7);
}

TEST(test_ly_wait_reg_unreachable_target)
{
    // cp b with b=200: LY can never reach it, idle at deadline pace
    uint8_t rom[] = {
        0x06, 0xc8,       // ld b, 200
        0xf0, 0x44,       // ldh a, ($ff44)
        0xb8,             // cp b
        0x20, 0xfb,       // jr nz, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 10000);
    ASSERT_EQ(get_cycle_count(), 65664 - 10000);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 2);
}

TEST(test_ly_wait_reg_clamped_by_wake_limit)
{
    // a deadline before the target line stops the wait at the loop head
    uint8_t rom[] = {
        0x06, 0x5a,       // ld b, 90
        0xf0, 0x44,       // ldh a, ($ff44)
        0xb8,             // cp b
        0x20, 0xfb,       // jr nz, -5
        0x10              // stop
    };
    set_wake_limit(5000);
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_cycle_count(), 5000);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 2);
}

// ============================================================================
// LY wait pattern tests with and a / or a
// Pattern: ldh a, [$44]; and a / or a; jr nz/z, back
// Equivalent to cp 0. Final Fantasy Legend waits for LY wrap this way.
// ============================================================================

TEST(test_ly_wait_and_a_jr_nz)
{
    // jr nz: loop while LY != 0. Line 0 is behind the vblank deadline,
    // so the wait advances at deadline pace and re-enters at the loop
    // head (the vblank handler gets to run at its real time)
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44)
        0xa7,             // and a
        0x20, 0xfb,       // jr nz, -5 (back to ldh)
        0x10              // stop (not reached, pattern is fused)
    };
    run_block_with_frame_cycles(rom, 20000);
    ASSERT_EQ(get_cycle_count(), 65664 - 20000);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 0);
}

TEST(test_ly_wait_or_a_jr_nz)
{
    // same loop with or a, already on line 0: first-pass exit
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44)
        0xb7,             // or a
        0x20, 0xfb,       // jr nz, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0);
    ASSERT_EQ(get_cycle_count(), 24);
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x05, 0x04);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 5);
}

TEST(test_ly_wait_and_a_jr_z)
{
    // jr z: loop while LY == 0, exit when LY != 0 -> wait for LY 1
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44)
        0xa7,             // and a
        0x28, 0xfb,       // jr z, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 1);
    ASSERT_EQ(get_cycle_count(), 456);
    // and a with A == 1: Z clear, C clear
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x05, 0);
}

TEST(test_ly_wait_and_a_jr_z_off_line_0)
{
    // LY is 43, not 0: first-pass exit with A = actual LY
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44)
        0xa7,             // and a
        0x28, 0xfb,       // jr z, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 20000);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 43);
    ASSERT_EQ(get_cycle_count(), 24);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 5);
}

TEST(test_ly_wait_and_a_during_vblank)
{
    // already in vblank: the wake lands on the frame wrap, then the
    // re-entered loop sees line 0
    uint8_t rom[] = {
        0xf0, 0x44,       // ldh a, ($ff44)
        0xa7,             // and a
        0x20, 0xfb,       // jr nz, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 68000);
    ASSERT_EQ(get_cycle_count(), 70224 - 68000);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 0);
}

// ============================================================================
// HRAM idle wait pattern tests
// Pattern: ldh a, (nn); and a / or a; jr z/nz back to the ldh
// The compiler checks the flag once and fast-forwards to vblank instead of
// spinning. the test hram_base is GLOBALS_BASE, so HRAM byte $ff90 lives
// at host address 0x4010.
// ============================================================================

#define TEST_HRAM_FLAG_HOST_ADDR 0x4010

TEST(test_idle_wait_flag_clear_waits)
{
    // flag is 0, jr z: loop would repeat, so skip to vblank and exit to
    // the dispatcher at the loop head
    uint8_t rom[] = {
        0xf0, 0x90,       // ldh a, ($ff90)
        0xa7,             // and a
        0x28, 0xfb,       // jr z, -5 (back to ldh)
        0x10              // stop (not reached this pass)
    };
    run_block_with_frame_cycles(rom, 10000);
    ASSERT_EQ(get_cycle_count(), 65664 - 10000);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0);
}

TEST(test_idle_wait_flag_set_falls_through)
{
    // flag is nonzero: loop exits immediately, block continues to the stop
    uint8_t rom[] = {
        0xf0, 0x90,       // ldh a, ($ff90)
        0xa7,             // and a
        0x28, 0xfb,       // jr z, -5
        0x10              // stop
    };
    run_block_with_frame_cycles_mem(rom, 10000, TEST_HRAM_FLAG_HOST_ADDR, 0x42);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0x42);
    // Z clear like and a would leave it
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x04, 0);
    // reached the stop
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 0xffffffff);
}

TEST(test_idle_wait_jr_nz_waits_while_set)
{
    // jr nz: loop repeats while the flag is nonzero
    uint8_t rom[] = {
        0xf0, 0x90,       // ldh a, ($ff90)
        0xa7,             // and a
        0x20, 0xfb,       // jr nz, -5
        0x10              // stop
    };
    run_block_with_frame_cycles_mem(rom, 20000, TEST_HRAM_FLAG_HOST_ADDR, 5);
    ASSERT_EQ(get_cycle_count(), 65664 - 20000);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 0);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 5);
}

TEST(test_idle_wait_jr_nz_flag_zero_falls_through)
{
    uint8_t rom[] = {
        0xf0, 0x90,       // ldh a, ($ff90)
        0xa7,             // and a
        0x20, 0xfb,       // jr nz, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 20000);
    ASSERT_EQ(get_dreg(REG_68K_D_A) & 0xff, 0);
    // Z set like and a would leave it
    ASSERT_EQ(get_dreg(REG_68K_D_FLAGS) & 0x04, 0x04);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 0xffffffff);
}

TEST(test_idle_wait_or_a_variant)
{
    // same pattern with or a instead of and a
    uint8_t rom[] = {
        0xf0, 0x90,       // ldh a, ($ff90)
        0xb7,             // or a
        0x28, 0xfb,       // jr z, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 30000);
    ASSERT_EQ(get_cycle_count(), 65664 - 30000);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 0);
}

TEST(test_idle_wait_during_vblank)
{
    // already past vblank start: wake at the frame wrap and re-enter
    uint8_t rom[] = {
        0xf0, 0x90,       // ldh a, ($ff90)
        0xa7,             // and a
        0x28, 0xfb,       // jr z, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 68000);
    ASSERT_EQ(get_cycle_count(), 70224 - 68000);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 0);
}

TEST(test_idle_wait_mid_block)
{
    // pattern preceded by other instructions: exit PC must be the ldh,
    // not the block start
    uint8_t rom[] = {
        0x06, 0x07,       // ld b, 7
        0xf0, 0x90,       // ldh a, ($ff90)
        0xa7,             // and a
        0x28, 0xfb,       // jr z, -5
        0x10              // stop
    };
    run_block_with_frame_cycles(rom, 0);
    ASSERT_EQ(get_cycle_count(), 65664);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 2);
    ASSERT_EQ((get_dreg(REG_68K_D_BC) >> 16) & 0xff, 7);
}

// ============================================================================
// Fast-forward wake limit tests
// HALT and the HRAM idle wait load jit_ctx.wake_limit verbatim; the C
// dispatcher guarantees it is the distance to the earliest armed deadline
// (LYC match lines, timer overflow, vblank, frame wrap), so the interrupt
// gets delivered on the line it belongs to instead of being jumped over.
// ============================================================================

TEST(test_halt_clamped_by_wake_limit)
{
    // HALT at frame_cycles=10000 would skip 55664 to vblank, but the
    // wake limit stops it 5000 cycles in (an LYC match line)
    uint8_t rom[] = {
        0x76              // halt
    };
    set_wake_limit(5000);
    run_block_with_frame_cycles(rom, 10000);
    ASSERT_EQ(get_cycle_count(), 5000);
}

TEST(test_halt_wake_limit_beyond_vblank)
{
    // the emitted code trusts wake_limit verbatim, even past vblank (the
    // dispatcher never sets it beyond the earliest armed deadline)
    uint8_t rom[] = {
        0x76              // halt
    };
    set_wake_limit(60000);
    run_block_with_frame_cycles(rom, 10000);
    ASSERT_EQ(get_cycle_count(), 60000);
}

TEST(test_halt_wake_limit_zero)
{
    // overdue match: skip nothing so the next sync fires and delivers it
    uint8_t rom[] = {
        0x76              // halt
    };
    set_wake_limit(0);
    run_block_with_frame_cycles(rom, 10000);
    ASSERT_EQ(get_cycle_count(), 0);
}

TEST(test_idle_wait_clamped_by_wake_limit)
{
    // flag clear, loop would skip to vblank, but the wake limit caps the
    // skip; exit PC is still the loop head so the wait re-fuses after
    // the interrupt is serviced
    uint8_t rom[] = {
        0xf0, 0x90,       // ldh a, ($ff90)
        0xa7,             // and a
        0x28, 0xfb,       // jr z, -5
        0x10              // stop
    };
    set_wake_limit(3000);
    run_block_with_frame_cycles(rom, 10000);
    ASSERT_EQ(get_cycle_count(), 3000);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 0);
}

// ============================================================================
// Dispatcher exit budget tests
// Backward branches compare D2 against jit_ctx.exit_budget at run time.
// Large budget: loops run natively to completion. Small budget: the loop
// exits to the dispatcher with D3 = loop target.
// ============================================================================

TEST(test_budget_cond_loop_runs_natively)
{
    // dec b / inc c makes the loop body > 3 bytes so the compiler emits
    // the cycle-checked path instead of the tiny-loop path
    uint8_t rom[] = {
        0x06, 0x05,       // ld b, 5
        0x0c,             // inc c
        0x05,             // dec b
        0x20, 0xfc,       // jr nz, -4 (back to dec b)
        0x10              // stop
    };
    run_block_with_budget(rom, 100000);
    // looped 5 times and fell through to the stop
    ASSERT_EQ((get_dreg(REG_68K_D_BC) >> 16) & 0xff, 0);
    ASSERT_EQ(get_dreg(REG_68K_D_BC) & 0xff, 5);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 0xffffffff);
}

TEST(test_budget_cond_loop_exits_early)
{
    uint8_t rom[] = {
        0x06, 0x05,       // ld b, 5
        0x0c,             // inc c
        0x05,             // dec b
        0x20, 0xfc,       // jr nz, -4
        0x10              // stop
    };
    run_block_with_budget(rom, 1);
    // one iteration, then the taken branch sees D2 >= budget and exits
    // to the dispatcher at the loop head
    ASSERT_EQ((get_dreg(REG_68K_D_BC) >> 16) & 0xff, 4);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 2);
}

TEST(test_budget_uncond_loop_exits)
{
    // unconditional backward jr can never fall through, so any budget
    // makes it exit to the dispatcher once cycles accumulate
    uint8_t rom[] = {
        0x0c,             // inc c
        0x0c,             // inc c
        0x0c,             // inc c
        0x18, 0xfb,       // jr -5 (back to start)
        0x10              // stop (never reached)
    };
    run_block_with_budget(rom, 50);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 0);
    // 24 cycles per pass (3 inc + taken jr) means the exit lands on the
    // third pass regardless of where in the pass the check happens
    ASSERT_EQ(get_dreg(REG_68K_D_BC) & 0xff, 9);
    ASSERT_EQ(get_cycle_count() >= 50 && get_cycle_count() <= 74, 1);
}

// "wait for the interrupt handler to change [hl]"
TEST(test_budget_cp_hl_cond_loop_exits)
{
    uint8_t rom[] = {
        0x21, 0x00, 0xc0, // ld hl, $c000 ([hl] reads 0)
        0xaf,             // xor a
        0xbe,             // cp [hl]
        0x28, 0xfd,       // jr z, -3 (back to cp [hl])
        0x10              // stop (only via interrupt changing $c000)
    };
    run_block_with_budget(rom, 100);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 4);
    ASSERT_EQ(get_cycle_count() >= 100, 1);
}

TEST(test_budget_cp_hl_uncond_loop_exits)
{
    uint8_t rom[] = {
        0x21, 0x00, 0xc0, // ld hl, $c000
        0xbe,             // cp [hl]
        0x18, 0xfd,       // jr -3 (back to cp [hl])
        0x10              // stop (never reached)
    };
    run_block_with_budget(rom, 100);
    ASSERT_EQ(get_dreg(REG_68K_D_NEXT_PC), 3);
    ASSERT_EQ(get_cycle_count() >= 100, 1);
}

void register_timing_tests(void)
{
    printf("\nHALT instruction tests:\n");
    RUN_TEST(test_halt_before_vblank);
    RUN_TEST(test_halt_mid_frame);
    RUN_TEST(test_halt_just_before_vblank);
    RUN_TEST(test_halt_at_vblank_start);
    RUN_TEST(test_halt_during_vblank);
    RUN_TEST(test_halt_near_frame_end);

    printf("\nLY wait pattern tests:\n");
    RUN_TEST(test_ly_wait_jr_nz_ly0);
    RUN_TEST(test_ly_wait_jr_nz_ly90);
    RUN_TEST(test_ly_wait_jr_nz_ly144);
    RUN_TEST(test_ly_wait_jr_nz_past_target);
    RUN_TEST(test_ly_wait_jr_z_ly90);
    RUN_TEST(test_ly_wait_jr_z_ly90_on_line);
    RUN_TEST(test_ly_wait_jr_z_ly153);
    RUN_TEST(test_ly_wait_jr_z_ly153_on_line);
    RUN_TEST(test_ly_wait_jr_c_ly100);
    RUN_TEST(test_ly_wait_jr_c_already_past);
    RUN_TEST(test_ly_wait_jr_c_exactly_on);
    RUN_TEST(test_ly_wait_mid_frame);
    RUN_TEST(test_ly_wait_exact_target);
    RUN_TEST(test_ly_wait_unreachable_target);
    RUN_TEST(test_ly_wait_clamped_by_wake_limit);
    RUN_TEST(test_ly_wait_deadline_on_target_line);

    printf("\nLY wait register pattern tests:\n");
    RUN_TEST(test_ly_wait_reg_cp_b);
    RUN_TEST(test_ly_wait_reg_cp_c);
    RUN_TEST(test_ly_wait_reg_cp_h);
    RUN_TEST(test_ly_wait_reg_cp_l);
    RUN_TEST(test_ly_wait_reg_jr_z);
    RUN_TEST(test_ly_wait_reg_jr_z_on_line);
    RUN_TEST(test_ly_wait_reg_jr_z_wrap);
    RUN_TEST(test_ly_wait_reg_jr_z_wrap_on_line);
    RUN_TEST(test_ly_wait_reg_jr_z_over_153);
    RUN_TEST(test_ly_wait_reg_jr_c);
    RUN_TEST(test_ly_wait_reg_mid_frame);
    RUN_TEST(test_ly_wait_reg_past_target);
    RUN_TEST(test_ly_wait_reg_on_target_line);
    RUN_TEST(test_ly_wait_reg_unreachable_target);
    RUN_TEST(test_ly_wait_reg_clamped_by_wake_limit);

    printf("\nLY wait and/or pattern tests:\n");
    RUN_TEST(test_ly_wait_and_a_jr_nz);
    RUN_TEST(test_ly_wait_or_a_jr_nz);
    RUN_TEST(test_ly_wait_and_a_jr_z);
    RUN_TEST(test_ly_wait_and_a_jr_z_off_line_0);
    RUN_TEST(test_ly_wait_and_a_during_vblank);

    printf("\nFast-forward wake limit tests:\n");
    RUN_TEST(test_halt_clamped_by_wake_limit);
    RUN_TEST(test_halt_wake_limit_beyond_vblank);
    RUN_TEST(test_halt_wake_limit_zero);
    RUN_TEST(test_idle_wait_clamped_by_wake_limit);

    printf("\nDispatcher exit budget tests:\n");
    RUN_TEST(test_budget_cond_loop_runs_natively);
    RUN_TEST(test_budget_cond_loop_exits_early);
    RUN_TEST(test_budget_uncond_loop_exits);
    RUN_TEST(test_budget_cp_hl_cond_loop_exits);
    RUN_TEST(test_budget_cp_hl_uncond_loop_exits);

    printf("\nHRAM idle wait pattern tests:\n");
    RUN_TEST(test_idle_wait_flag_clear_waits);
    RUN_TEST(test_idle_wait_flag_set_falls_through);
    RUN_TEST(test_idle_wait_jr_nz_waits_while_set);
    RUN_TEST(test_idle_wait_jr_nz_flag_zero_falls_through);
    RUN_TEST(test_idle_wait_or_a_variant);
    RUN_TEST(test_idle_wait_during_vblank);
    RUN_TEST(test_idle_wait_mid_block);
}
