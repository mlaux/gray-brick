; for text boxes only, do them more efficiently by only using
; a few STAT interrupts instead of one per line
; intro crawl and other STAT effects still slow
SECTION "patch", ROM0[$3c13]

NEW_ENTRY:
    push af
    push hl
    ldh a, [$ffbc]        ; mode byte $ffbc
    and a
    jr nz, .notmode0
; mode 0: ensure STAT source = LYC only ($40)
    ldh a, [$ff41]        ; STAT
    and $08               ; hblank source enable?
    jr z, NEW_MODE0       ; already LYC-only
    ld a, $40
    ldh [$ff41], a
    jr NEW_MODE0
.notmode0:
; ensure STAT source = hblank + LYC ($48)
    ldh a, [$ff41]
    and $08
    jr nz, .orig
    ld a, $48
    ldh [$ff41], a
.orig:
    ldh a, [$ffbc]
    jp $0369              ; original dispatch chain

NEW_MODE0:
    ldh a, [$ff44]        ; LY
    ld hl, $ffbe          ; top boundary
    cp [hl]
    jr c, .regionA        ; LY < top
    inc hl                ; -> $ffbf bottom
    cp [hl]
    jr nc, .regionC       ; LY >= bottom
.regionB:                 ; band [top, bottom)
    ld a, $e4
    ldh [$ff47], a        ; BGP
    ldh a, [$ffc0]
    ldh [$ff42], a        ; SCY
    xor a
    ldh [$ff43], a        ; SCX
    ld a, $e1
    ldh [$ff40], a        ; LCDC
    ldh a, [$ffbf]        ; bottom
    ldh [$ff45], a        ; arm LYC = bottom
    pop hl
    pop af
    reti
.regionC:                 ; below [bottom, 143]
    ldh a, [$ffa6]
    ldh [$ff47], a        ; BGP
    ldh a, [$ffaa]
    ldh [$ff42], a        ; SCY
    ldh a, [$ffa9]
    ldh [$ff43], a        ; SCX
    ld a, $e3
    ldh [$ff40], a        ; LCDC
    xor a
    ldh [$ff45], a        ; arm LYC = 0 (start of next frame)
    pop hl
    pop af
    reti
.regionA:                 ; above [0, top)
    ld hl, $df71
    ldh a, [$ff44]        ; LY
    add l
    ld l, a
    ldh a, [$ffa9]
    add [hl]
    ldh [$ff43], a        ; SCX = [$ffa9] + table[LY]
    ldh a, [$ffbe]        ; top
    ldh [$ff45], a        ; arm LYC = top
    pop hl
    pop af
    reti
