cmake_minimum_required(VERSION 3.13)
project("Gray Brick")
enable_language(ASM)

include(add_application.cmake)

include_directories(../src ../compiler)

add_application("Gray Brick"
    TYPE APPL
    CREATOR MGBE
    ../src/dmg.c
    ../src/lcd.c
    ../src/lcd_cgb.c
    ../src/cgb.c
    ../src/audio.c
    ../src/rom.c
    ../src/rom_patches.c
    ../src/mbc.c
    ../compiler/compiler.c
    ../compiler/emitters.c
    ../compiler/branches.c
    ../compiler/interop.c
    ../compiler/flags.c
    ../compiler/cb_prefix.c
    ../compiler/reg_loads.c
    ../compiler/alu.c
    ../compiler/instructions.c
    ../compiler/stack.c
    ../compiler/timing.c
    arena.c
    cpu_cache.c
    dialogs.c
    debug.c
    dispatcher_asm.c
    jit.c
    input.c
    lcd_render_bg.s
    lcd_mac.c
    lcd_mac_cgb.c
    cache.c
    blocklist.c
    audio_mac.c
    emulator.c
    palette_menu.c
    gb_palettes.c
    prof.c
    resources.r
)

target_compile_options(Gray_Brick PRIVATE
    -O3
    -ffunction-sections
    -fdata-sections
)

target_compile_definitions(Gray_Brick PRIVATE BG_TILES_IN_ASM)

# remove for release builds
#target_compile_definitions(Gray_Brick PRIVATE GB6_PROFILING)

target_link_options(Gray_Brick PRIVATE
    "-Wl,--gc-sections"
    #"-Wl,--print-gc-sections"
)