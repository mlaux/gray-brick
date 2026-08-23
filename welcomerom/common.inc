IF !DEF(COMMON_INC)
DEF COMMON_INC EQU 1

DEF rP1    EQU $ff00
DEF rDIV   EQU $ff04
DEF rIF    EQU $ff0f
DEF rLCDC  EQU $ff40
DEF rSTAT  EQU $ff41
DEF rSCY   EQU $ff42
DEF rSCX   EQU $ff43
DEF rLY    EQU $ff44
DEF rDMA   EQU $ff46
DEF rBGP   EQU $ff47
DEF rOBP0  EQU $ff48
DEF rOBP1  EQU $ff49
DEF rWY    EQU $ff4a
DEF rWX    EQU $ff4b
DEF rIE    EQU $ffff

DEF MAX_SPRITES EQU 40

DEF P1_BUTTONS EQU %00010000
DEF P1_DPAD    EQU %00100000
DEF P1_NONE    EQU %00110000

DEF OAM_NORMAL EQU %00000000

DEF hDma EQU $ff90

DEF TEXT_TILE_FIRST EQU $0d
DEF TEXT_TILE_LAST EQU $1a
DEF TEXT_TILE_COUNT EQU TEXT_TILE_LAST - TEXT_TILE_FIRST + 1

; LCD on, window tilemap $9c00, window off, BG/win tiles $8000,
; BG tilemap $9800, 8x8 obj, obj on, BG on
DEF LCDC_ON EQU %11010011

; \1 = entry count
; \2 = amplitude
MACRO def_sine_table
    FOR k, \1
        db (SIN(DIV(k * 1.0, (\1) * 1.0)) * (\2)) >> 16
    ENDR
ENDM

SECTION "vblank", ROM0[$40]
    push af
    call hDma
    pop af
    reti

SECTION "entry", ROM0[$100]
    nop
    jp start

SECTION "common", ROM0

; everything except turning the LCD on
init_common:
    ldh a, [rLY]
    cp 144
    jr nz, init_common

    xor a
    ldh [rLCDC], a
    ldh [rSCY], a
    ldh [rSCX], a

    ld hl, $8000
    ld bc, $2000
.clear_vram:
    xor a
    ld [hl+], a
    dec bc
    ld a, b
    or c
    jr nz, .clear_vram

    ld hl, wShadowOam
    ld b, 0
.clear_oam:
    xor a
    ld [hl+], a
    ld [hl+], a
    ld [hl+], a
    ld [hl+], a
    inc b
    ld a, b
    cp MAX_SPRITES
    jr nz, .clear_oam

    ld hl, $9800
    ld de, tilemap
    ld bc, 512
    call copy_de_hl

    ld hl, $8000
    ld de, tiles
    ld bc, circle_tile_end - tiles
    call copy_de_hl

    ld hl, hDma
    ld de, dma_routine
    ld bc, dma_routine_end - dma_routine
    call copy_de_hl

    ld a, %11100100
    ldh [rBGP], a
    ldh [rOBP0], a
    ldh [rOBP1], a
    ret

copy_de_hl:
    ld a, [de]
    inc de
    ld [hl+], a
    dec bc
    ld a, b
    or c
    jr nz, copy_de_hl
    ret

dma_routine:
    ld a, HIGH(wShadowOam)
    ldh [rDMA], a
    ld a, 40
.wait:
    dec a
    jr nz, .wait
    ret
dma_routine_end:

SECTION "common wram", WRAM0
wShadowOam: ds MAX_SPRITES * 4

ENDC
