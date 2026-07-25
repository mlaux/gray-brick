| lcd_render_bg.s - bg tile row renderer (68000)
|
| void lcd_render_bg_tiles(
|     const u8 *map_row,      36(sp)  32-byte tile map row
|     const u8 *tile_base,    40(sp)  vram + tile data base + row_in_tile*2
|     const u8 *lut,          44(sp)  tile_decode_packed
|     u8 *row,                48(sp)  packed pixel output, 2 bytes/tile
|     u8 *opac,               52(sp)  opacity output, 1 byte/tile
|     int tile_col,           56(sp)  0-31 start column
|     int count,              60(sp)  tiles to render, <= 21
|     int unsigned_mode)      64(sp)  nonzero = unsigned tile indices

	.section .text.lcd_render_bg_tiles,"ax",@progbits
	.align 2
	.globl lcd_render_bg_tiles
	.type lcd_render_bg_tiles, @function
lcd_render_bg_tiles:
	movem.l %d2-%d6/%a2-%a4,-(%sp)
	move.l 40(%sp),%a3	| tile_base
	move.l 44(%sp),%a2	| lut
	move.l 48(%sp),%a0	| row out
	move.l 52(%sp),%a4	| opac out
	move.l 56(%sp),%d1	| tile_col
	move.l 60(%sp),%d5	| count
	move.l 36(%sp),%a1	| map_row
	add.l %d1,%a1

	moveq #32,%d6
	sub.l %d1,%d6		| tiles until the map row wraps
	cmp.l %d5,%d6
	jle .Lrun1_ok
	move.l %d5,%d6		| run1 = min(count, 32 - tile_col)
.Lrun1_ok:
	sub.l %d6,%d5		| run2 = tiles after the wrap

	moveq #0,%d3		| index regs are byte-only in the loops,
	moveq #0,%d4		| upper bits must start clear for (a2,dn.w)
	tst.l 64(%sp)
	jne .Lu_entry

| ---- signed tile indices (8800 mode) ----
.Ls_entry:
	subq.w #1,%d6
	jmi .Ls_wrap
.Ls_loop:
	move.b (%a1)+,%d0
	ext.w %d0
	asl.w #4,%d0		| 16 * (s8)tile_idx
	move.b (%a3,%d0.w),%d1	| data1
	move.b 1(%a3,%d0.w),%d2	| data2

	move.b %d2,%d3
	lsr.b #4,%d3
	move.b %d1,%d4
	and.b #0xf0,%d4
	or.b %d4,%d3		| (data1 & f0) | (data2 >> 4)
	move.b (%a2,%d3.w),(%a0)+

	move.b %d1,%d3
	lsl.b #4,%d3
	move.b %d2,%d4
	and.b #0x0f,%d4
	or.b %d4,%d3		| ((data1 & 0f) << 4) | (data2 & 0f)
	move.b (%a2,%d3.w),(%a0)+

	or.b %d1,%d2
	move.b %d2,(%a4)+	| opacity
	dbra %d6,.Ls_loop
.Ls_wrap:
	move.w %d5,%d6		| second run, from the row start
	moveq #0,%d5
	move.l 36(%sp),%a1
	subq.w #1,%d6
	jpl .Ls_loop
	jra .Ldone

| ---- unsigned tile indices (8000 mode) ----
.Lu_entry:
	subq.w #1,%d6
	jmi .Lu_wrap
.Lu_loop:
	moveq #0,%d0		| asl.w would drag last tile's bits up
	move.b (%a1)+,%d0
	asl.w #4,%d0		| 16 * tile_idx
	move.b (%a3,%d0.w),%d1
	move.b 1(%a3,%d0.w),%d2

	move.b %d2,%d3
	lsr.b #4,%d3
	move.b %d1,%d4
	and.b #0xf0,%d4
	or.b %d4,%d3
	move.b (%a2,%d3.w),(%a0)+

	move.b %d1,%d3
	lsl.b #4,%d3
	move.b %d2,%d4
	and.b #0x0f,%d4
	or.b %d4,%d3
	move.b (%a2,%d3.w),(%a0)+

	or.b %d1,%d2
	move.b %d2,(%a4)+
	dbra %d6,.Lu_loop
.Lu_wrap:
	move.w %d5,%d6
	moveq #0,%d5
	move.l 36(%sp),%a1
	subq.w #1,%d6
	jpl .Lu_loop

.Ldone:
	movem.l (%sp)+,%d2-%d6/%a2-%a4
	rts
	.size lcd_render_bg_tiles, .-lcd_render_bg_tiles

	.pushsection .text.lcd_render_bg_tiles.macsbug,"ax",@progbits
	.byte 147
	.ascii "lcd_render_bg_tiles"
	.align 2,0
	.short 0
	.popsection
