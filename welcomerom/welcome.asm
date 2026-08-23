INCLUDE "common.inc"

DEF TEXT_ROW EQU 6
DEF TEXT_COL EQU 1
DEF TEXT_TOP EQU TEXT_ROW * 8
DEF TEXT_HEIGHT EQU 8

DEF RIPPLE_LEN EQU 64
DEF RIPPLE_AMPL EQU 6
DEF RIPPLE_LINE_STEP EQU 1
DEF RIPPLE_SPEED EQU 1

; "File > Open" line
DEF PULSE_ROW EQU 8
DEF PULSE_TOP EQU PULSE_ROW * 8
DEF PULSE_HEIGHT EQU 8

DEF FADE_STEPS EQU 4
DEF FADE_DELAY EQU 10

SECTION "main", ROM0[$150]
start:
    di
    ld sp, $fffe

    call init_common
    call draw_text_row

    ; start fully black
    ld a, $ff
    ld [wBgp], a
    ld [wPulseBgp], a

    xor a
    ld [wPhase], a
    ld [wFadeIndex], a
    ld [wFadeTimer], a
    ld [wPulseTimer], a
    ld [wKeys], a
    call build_scx

    ld a, LCDC_ON
    ldh [rLCDC], a

    ld a, $1
    ldh [rIE], a
    ei

.loop:
    halt

    ld a, [wBgp]
    ldh [rBGP], a
    ldh [rOBP0], a

    call ripple_raster
    call pulse_raster

    call read_joypad
    call update_markers
    call update_fade
    call update_pulse

    ld a, [wPhase]
    add RIPPLE_SPEED
    ld [wPhase], a
    call build_scx
    jr .loop

draw_text_row:
    ld hl, $9800 + TEXT_ROW * 32 + TEXT_COL
    ld a, TEXT_TILE_FIRST
.tile:
    ld [hl+], a
    inc a
    cp TEXT_TILE_LAST + 1
    jr nz, .tile
    ret

build_scx:
    ld hl, wScxLine
    ld d, HIGH(sine_table)
    ld a, [wPhase]
    ld b, a
    ld c, TEXT_HEIGHT
.line:
    ld a, b
    and RIPPLE_LEN - 1
    add LOW(sine_table)
    ld e, a
    ld a, [de]
    ld [hl+], a

    ld a, b
    add RIPPLE_LINE_STEP
    ld b, a

    dec c
    jr nz, .line

    ; reset after the text
    xor a
    ld [hl], a
    ret

ripple_raster:
    ld hl, wScxLine
    ld b, TEXT_TOP
    ld c, TEXT_HEIGHT + 1
.wait:
    ldh a, [rLY]
    cp b
    jr nz, .wait

    ld a, [hl+]
    ldh [rSCX], a
    inc b
    dec c
    jr nz, .wait
    ret

pulse_raster:
    ld b, PULSE_TOP
.wait:
    ldh a, [rLY]
    cp b
    jr nz, .wait
    ld a, [wPulseBgp]
    ldh [rBGP], a

    ld b, PULSE_TOP + PULSE_HEIGHT
.wait_end:
    ldh a, [rLY]
    cp b
    jr nz, .wait_end
    ld a, [wBgp]
    ldh [rBGP], a
    ret

read_joypad:
    ld a, P1_DPAD
    ldh [rP1], a
    ldh a, [rP1]
    ldh a, [rP1]
    cpl
    and $0f
    ld b, a

    ld a, P1_BUTTONS
    ldh [rP1], a
    ldh a, [rP1]
    ldh a, [rP1]
    ldh a, [rP1]
    ldh a, [rP1]
    cpl
    and $0f
    swap a
    or b
    ld [wKeys], a

    ld a, P1_NONE
    ldh [rP1], a
    ret

update_markers:
    ld hl, wShadowOam
    ld de, marker_pos
    ld a, [wKeys]
    ld b, a
    ld c, 8
.marker:
    ld a, [de]
    inc de
    srl b
    jr c, .show
    xor a ; y = 0 hides it
.show:
    ld [hl+], a
    ld a, [de]
    inc de
    ld [hl+], a
    ld a, CIRCLE_TILE
    ld [hl+], a
    ld a, OAM_NORMAL
    ld [hl+], a
    dec c
    jr nz, .marker
    ret

update_fade:
    ld a, [wFadeIndex]
    cp FADE_STEPS - 1
    ret z

    ld hl, wFadeTimer
    inc [hl]
    ld a, [hl]
    cp FADE_DELAY
    ret nz
    ld [hl], 0

    ld hl, wFadeIndex
    inc [hl]
    ld a, [hl]
    ld e, a
    ld d, 0
    ld hl, fade_table
    add hl, de
    ld a, [hl]
    ld [wBgp], a
    ret

update_pulse:
    ld a, [wFadeIndex]
    cp FADE_STEPS - 1
    jr z, .pulsing
    ld a, [wBgp]
    ld [wPulseBgp], a
    ret
.pulsing:
    ld hl, wPulseTimer
    inc [hl]
    ld a, [hl]
    rrca
    rrca
    rrca
    and 7
    ld e, a
    ld d, 0
    ld hl, pulse_table
    add hl, de
    ld a, [hl]
    ld [wPulseBgp], a
    ret

tilemap:
    db $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00,  $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
    db $00, $01, $02, $03, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00,  $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
    db $00, $04, $05, $06, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00,  $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
    db $00, $07, $08, $09, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00,  $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
    db $00, $0a, $0b, $0c, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00,  $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
    db $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00,  $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
    db $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00,  $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
    db $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00,  $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
    db $00, $29, $2a, $2b, $2c, $2d, $2e, $2f, $30, $31, $32, $00, $00, $00, $00, $00, $00, $00, $00, $00,  $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
    db $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00,  $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
    db $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00,  $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
    db $00, $00, $1b, $00, $00, $00, $00, $00, $00, $20, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00,  $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
    db $00, $1d, $1f, $1e, $00, $00, $00, $00, $20, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00,  $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
    db $00, $00, $1c, $00, $00, $21, $21, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00,  $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
    db $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00,  $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00
    db $00, $22, $23, $24, $00, $25, $26, $00, $27, $28, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00,  $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00, $00

tiles: INCBIN "tiles.2bpp"
tiles_end:

circle_tile:
    db %00111100, %00111100
    db %01111110, %01000010
    db %11111111, %10000001
    db %11111111, %10000001
    db %11111111, %10000001
    db %11111111, %10000001
    db %01111110, %01000010
    db %00111100, %00111100
circle_tile_end:

DEF CIRCLE_TILE EQU (circle_tile - tiles) / 16

marker_pos:
    db 112, 32 ; right
    db 112, 16 ; left
    db 104, 24 ; up
    db 120, 24 ; down
    db 104, 80 ; a
    db 112, 72 ; b
    db 120, 48 ; select
    db 120, 56 ; start

fade_table:
    db $ff, $fe, $f9, $e4

pulse_table:
    db $e4, $e4, $a4, $a4, $64, $64, $a4, $a4

SECTION "sine", ROM0, ALIGN[6]
sine_table:
    def_sine_table RIPPLE_LEN, RIPPLE_AMPL

SECTION "ripple wram", WRAM0
wScxLine: ds TEXT_HEIGHT + 1
wPhase: ds 1
wKeys: ds 1
wBgp: ds 1
wPulseBgp: ds 1
wFadeIndex: ds 1
wFadeTimer: ds 1
wPulseTimer: ds 1
