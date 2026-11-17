#define ALRT_RezTemplateVersion 0
#define DLOG_RezTemplateVersion 0

#include "Finder.r"
#include "Processes.r"
#include "Types.r"

type 'mstr' as 'STR ';
type 'MGBE' as 'STR ';

resource 'ALRT' (128) {
	{78, 72, 138, 362},
	129,
	{
		OK, visible, silent,
		OK, visible, silent,
		OK, visible, silent,
		OK, visible, silent
	}
};

resource 'ALRT' (129) {
	{40, 40, 150, 370},
	130,
	{
		OK, visible, sound1,
		OK, visible, sound1,
		OK, visible, sound1,
		OK, visible, sound1
	}
};

resource 'ALRT' (130) {
	{40, 40, 150, 370},
	133,
	{
		OK, visible, sound1,
		OK, visible, sound1,
		OK, visible, sound1,
		OK, visible, sound1
	}
};

resource 'BNDL' (128) {
	'MGBE',
	0,
	{
		'FREF',
		{
			0, 128,
			1, 129,
			2, 130,
			3, 131
		};
		'ICN#',
		{
			0, 128,
			1, 129,
			2, 130,
			3, 131
		}
	}
};

resource 'DITL' (128) {
	{
		{110, 240, 130, 300}, Button {
			enabled,
			"Close"
		},
		{19, 10, 51, 42}, Icon {
			enabled,
			128
		},
		{10, 50, 110, 300}, StaticText {
			disabled,
			"Gray Brick: GB emulator for 68k Mac\rby Matthew Laux http://constcast.org/\r\rGBC support by Tanner Fokkens\rPalettes from lospec.com\r"
		}
	}
};

resource 'DITL' (129) {
	{
		{30, 220, 50, 278}, Button {
			enabled,
			"Cry"
		},
		{10, 10, 42, 42}, Icon {
			disabled,
			0
		},
		{10, 50, 27, 279}, StaticText {
			disabled,
			"Not enough memory to open ROM."
		}
	}
};

resource 'DITL' (130) {
	{
		{80, 260, 100, 318}, Button {
			enabled,
			"OK"
		},
		{10, 20, 42, 52}, Icon {
			enabled,
			1
		},
		{10, 70, 25, 320}, StaticText {
			disabled,
			"^0"
		},
		{25, 70, 40, 320}, StaticText {
			disabled,
			"^1"
		},
		{40, 70, 55, 320}, StaticText {
			disabled,
			"^2"
		},
		{55, 70, 70, 320}, StaticText {
			disabled,
			"^3"
		}
	}
};

resource 'DITL' (131) {
	{
		{140, 185, 160, 243}, Button {
			enabled,
			"Save"
		},
		{140, 115, 160, 173}, Button {
			enabled,
			"Cancel"
		},
		{15, 60, 31, 105}, UserItem { enabled },
		{45, 60, 61, 105}, UserItem { enabled },
		{75, 60, 91, 105}, UserItem { enabled },
		{105, 60, 121, 105}, UserItem { enabled },
		{15, 195, 31, 240}, UserItem { enabled },
		{45, 195, 61, 240}, UserItem { enabled },
		{75, 195, 91, 240}, UserItem { enabled },
		{105, 195, 121, 240}, UserItem { enabled },
		{15, 10, 31, 55}, StaticText {
			disabled,
			"Up"
		},
		{45, 10, 61, 55}, StaticText {
			disabled,
			"Down"
		},
		{75, 10, 91, 55}, StaticText {
			disabled,
			"Left"
		},
		{105, 10, 121, 55}, StaticText {
			disabled,
			"Right"
		},
		{15, 140, 31, 190}, StaticText {
			disabled,
			"A"
		},
		{45, 140, 61, 190}, StaticText {
			disabled,
			"B"
		},
		{75, 140, 91, 190}, StaticText {
			disabled,
			"Select"
		},
		{105, 140, 121, 190}, StaticText {
			disabled,
			"Start"
		},
		{130, 10, 162, 42}, UserItem { disabled }
	}
};

resource 'DITL' (132) {
	{
		{230, 195, 250, 253}, Button {
			enabled,
			"Save"
		},
		{230, 125, 250, 183}, Button {
			enabled,
			"Cancel"
		},
		{30, 20, 48, 130}, RadioButton {
			enabled,
			"every frame"
		},
		{50, 20, 68, 130}, RadioButton {
			enabled,
			"every 16 lines"
		},
		{100, 20, 118, 200}, RadioButton {
			enabled,
			"Monochrome, CopyBits"
		},
		{120, 20, 138, 200}, RadioButton {
			enabled,
			"Monochrome, direct"
		},
		{140, 20, 158, 200}, RadioButton {
			enabled,
			"Color"
		},
		{190, 20, 208, 50}, RadioButton {
			enabled,
			"0"
		},
		{190, 70, 208, 100}, RadioButton {
			enabled,
			"1"
		},
		{190, 120, 208, 150}, RadioButton {
			enabled,
			"2"
		},
		{190, 170, 208, 200}, RadioButton {
			enabled,
			"3"
		},
		{190, 220, 208, 250}, RadioButton {
			enabled,
			"4"
		},
		{10, 10, 26, 200}, StaticText {
			disabled,
			"Check for interrupts:"
		},
		{31, 140, 47, 255}, StaticText {
			disabled,
			"(faster)"
		},
		{51, 140, 67, 255}, StaticText {
			disabled,
			"(more accurate)"
		},
		{80, 10, 96, 200}, StaticText {
			disabled,
			"Display mode:"
		},
		{170, 10, 186, 200}, StaticText {
			disabled,
			"Frame skip:"
		},
		{220, 10, 252, 42}, UserItem { disabled }
	}
};

resource 'DITL' (133) {
	{
		{80, 210, 100, 318}, Button {
			enabled,
			"Switch to 256"
		},
		{80, 90, 100, 198}, Button {
			enabled,
			"Keep current"
		},
		{10, 20, 42, 52}, Icon {
			enabled,
			2
		},
		{10, 70, 25, 320}, StaticText {
			disabled,
			"Your monitor is set to thousands or"
		},
		{25, 70, 40, 325}, StaticText {
			disabled,
			"millions of colors. I highly recommend"
		},
		{40, 70, 55, 320}, StaticText {
			disabled,
			"switching to 256 colors for better"
		},
		{55, 70, 70, 320}, StaticText {
			disabled,
			"performance. Should I switch?"
		}
	}
};

resource 'DLOG' (128) {
	{52, 126, 192, 436},
	altDBoxProc,
	visible,
	goAway,
	0x0,
	128,
	""
};

resource 'DLOG' (129) {
	{40, 40, 210, 295},
	dBoxProc,
	visible,
	goAway,
	0x0,
	131,
	"Key Mappings"
};

resource 'DLOG' (130) {
	{40, 40, 300, 305},
	dBoxProc,
	visible,
	goAway,
	0x0,
	132,
	""
};

resource 'FREF' (128) {
	'APPL', 0, ""
};

resource 'FREF' (129) {
	'GBRM', 1, ""
};

resource 'FREF' (130) {
	'SRAM', 2, ""
};

resource 'FREF' (131) {
	'BLST', 3, ""
};

data 'ICN#' (128) {
	$"07FF FFE0 0A00 0050 0800 0010 09FF FF90"            /* .......P....∆..ê */
	$"0A00 0050 0A0F FC50 0A10 0250 0A10 0250"            /* ...P...P...P...P */
	$"0AD2 1250 0A12 1250 0A10 0250 0A10 0250"            /* .“.P...P...P...P */
	$"0A12 1250 0A11 E250 0A10 0250 0A0F FC50"            /* ...P...P...P...P */
	$"0A00 0090 09FF FF10 0800 0010 0860 0010"            /* ...ê∆........`.. */
	$"0860 0190 09F8 0190 09F8 1810 0860 1810"            /* .`.ê∆..ê∆....`.. */
	$"0860 0010 0800 0010 0800 0010 0802 2010"            /* .`............ . */
	$"0804 4020 0800 0020 0800 00C0 07FF FF00"            /* ..@ ... ...¿.... */
	$"07FF FFE0 0FFF FFF0 0FFF FFF0 0FFF FFF0"            /* ................ */
	$"0FFF FFF0 0FFF FFF0 0FFF FFF0 0FFF FFF0"            /* ................ */
	$"0FFF FFF0 0FFF FFF0 0FFF FFF0 0FFF FFF0"            /* ................ */
	$"0FFF FFF0 0FFF FFF0 0FFF FFF0 0FFF FFF0"            /* ................ */
	$"0FFF FFF0 0FFF FFF0 0FFF FFF0 0FFF FFF0"            /* ................ */
	$"0FFF FFF0 0FFF FFF0 0FFF FFF0 0FFF FFF0"            /* ................ */
	$"0FFF FFF0 0FFF FFF0 0FFF FFF0 0FFF FFF0"            /* ................ */
	$"0FFF FFE0 0FFF FFE0 0FFF FFC0 07FF FF00"            /* ...........¿.... */
};

data 'ICN#' (129) {
	$"0000 0000 0FFF FFC0 1000 0040 1F3F FCF8"            /* .......¿...@.?.. */
	$"1040 0208 1E40 0278 1040 0208 1F3F FCF8"            /* .@...@.x.@...?.. */
	$"1000 0008 11FF FF88 1340 0348 12C0 02C8"            /* .......à.@.H.¿.» */
	$"1340 0348 12C0 02C8 1340 0348 12C0 02C8"            /* .@.H.¿.».@.H.¿.» */
	$"1340 0348 12C0 02C8 1340 0348 12C0 02C8"            /* .@.H.¿.».@.H.¿.» */
	$"1340 0348 12C0 02C8 1340 0348 12C0 02C8"            /* .@.H.¿.».@.H.¿.» */
	$"1340 0348 12C0 02C8 11FF FF88 1000 0008"            /* .@.H.¿.»...à.... */
	$"1001 C008 1000 8008 1000 0008 0FFF FFF0"            /* ..¿...Ä......... */
	$"0000 0000 0FFF FFC0 1FFF FFC0 1FFF FFF8"            /* .......¿...¿.... */
	$"1FFF FFF8 1FFF FFF8 1FFF FFF8 1FFF FFF8"            /* ................ */
	$"1FFF FFF8 1FFF FFF8 1FFF FFF8 1FFF FFF8"            /* ................ */
	$"1FFF FFF8 1FFF FFF8 1FFF FFF8 1FFF FFF8"            /* ................ */
	$"1FFF FFF8 1FFF FFF8 1FFF FFF8 1FFF FFF8"            /* ................ */
	$"1FFF FFF8 1FFF FFF8 1FFF FFF8 1FFF FFF8"            /* ................ */
	$"1FFF FFF8 1FFF FFF8 1FFF FFF8 1FFF FFF8"            /* ................ */
	$"1FFF FFF8 1FFF FFF8 1FFF FFF8 0FFF FFF0"            /* ................ */
};

data 'ICN#' (130) {
	$"0FFF FE00 0800 0300 0855 4280 0855 4240"            /* .........UBÄ.UB@ */
	$"08FF E220 0BFF FA10 08DF E3F8 0BFF F808"            /* ... ............ */
	$"08FF E008 0BFF F808 08FF 0008 0BFC FE08"            /* ................ */
	$"08FB 3988 0BF4 7C48 08F4 0048 0857 01C8"            /* ..9à..|H...H.W.» */
	$"0854 FE48 0804 0048 0804 1048 0804 3848"            /* .T.H...H...H..8H */
	$"0804 1048 0804 0048 0804 0048 0804 0048"            /* ...H...H...H...H */
	$"0804 0048 0804 0048 0804 3848 0804 0048"            /* ...H...H..8H...H */
	$"0803 0188 0800 FE08 0800 0008 0FFF FFF8"            /* ...à............ */
	$"0FFF FE00 0FFF FF00 0FFF FF80 0FFF FFC0"            /* ...........Ä...¿ */
	$"0FFF FFE0 0FFF FFF0 0FFF FFF8 0FFF FFF8"            /* ................ */
	$"0FFF FFF8 0FFF FFF8 0FFF FFF8 0FFF FFF8"            /* ................ */
	$"0FFF FFF8 0FFF FFF8 0FFF FFF8 0FFF FFF8"            /* ................ */
	$"0FFF FFF8 0FFF FFF8 0FFF FFF8 0FFF FFF8"            /* ................ */
	$"0FFF FFF8 0FFF FFF8 0FFF FFF8 0FFF FFF8"            /* ................ */
	$"0FFF FFF8 0FFF FFF8 0FFF FFF8 0FFF FFF8"            /* ................ */
	$"0FFF FFF8 0FFF FFF8 0FFF FFF8 0FFF FFF8"            /* ................ */
};

data 'ICN#' (131) {
	$"0FFF FE00 0800 0300 0800 0280 0918 4240"            /* ...........Ä∆.B@ */
	$"0914 4220 0914 4210 09D8 73F8 0800 0008"            /* ∆.B ∆.B.∆ÿs..... */
	$"0800 0008 0800 0008 099C C708 0951 0608"            /* ........∆ú«.∆Q.. */
	$"0959 0508 099C C608 0800 0008 0800 0008"            /* ∆Y..∆ú∆......... */
	$"0800 0008 088C 3008 0954 4008 0958 4008"            /* .....å0.∆T@.∆X@. */
	$"0894 3008 0800 0008 0800 0008 0800 0008"            /* .î0............. */
	$"084C 4B88 0854 6888 0858 5908 0994 4BA8"            /* .LKà.Thà.XY.∆îK® */
	$"0800 0048 0800 0008 0800 0008 0FFF FFF8"            /* ...H............ */
	$"0FFF FE00 0FFF FF00 0FFF FF80 0FFF FFC0"            /* ...........Ä...¿ */
	$"0FFF FFE0 0FFF FFF0 0FFF FFF8 0FFF FFF8"            /* ................ */
	$"0FFF FFF8 0FFF FFF8 0FFF FFF8 0FFF FFF8"            /* ................ */
	$"0FFF FFF8 0FFF FFF8 0FFF FFF8 0FFF FFF8"            /* ................ */
	$"0FFF FFF8 0FFF FFF8 0FFF FFF8 0FFF FFF8"            /* ................ */
	$"0FFF FFF8 0FFF FFF8 0FFF FFF8 0FFF FFF8"            /* ................ */
	$"0FFF FFF8 0FFF FFF8 0FFF FFF8 0FFF FFF8"            /* ................ */
	$"0FFF FFF8 0FFF FFF8 0FFF FFF8 0FFF FFF8"            /* ................ */
};

data 'ICON' (128) {
	$"07FF FFE0 0A00 0050 0800 0010 09FF FF90"            /* .......P....∆..ê */
	$"0A00 0050 0A0F FC50 0A10 0250 0A10 0250"            /* ...P...P...P...P */
	$"0AD2 1250 0A12 1250 0A10 0250 0A10 0250"            /* .“.P...P...P...P */
	$"0A12 1250 0A11 E250 0A10 0250 0A0F FC50"            /* ...P...P...P...P */
	$"0A00 0090 09FF FF10 0800 0010 0860 0010"            /* ...ê∆........`.. */
	$"0860 0190 09F8 0190 09F8 1810 0860 1810"            /* .`.ê∆..ê∆....`.. */
	$"0860 0010 0800 0010 0800 0010 0802 2010"            /* .`............ . */
	$"0804 4020 0800 0020 0800 00C0 07FF FF00"            /* ..@ ... ...¿.... */
};

data 'ICON' (129) {
	$"0000 0000 0FFF FFC0 1000 0040 1F3F FCF8"            /* .......¿...@.?.. */
	$"1040 0208 1E40 0278 1040 0208 1F3F FCF8"            /* .@...@.x.@...?.. */
	$"1000 0008 11FF FF88 1340 0348 12C0 02C8"            /* .......à.@.H.¿.» */
	$"1340 0348 12C0 02C8 1340 0348 12C0 02C8"            /* .@.H.¿.».@.H.¿.» */
	$"1340 0348 12C0 02C8 1340 0348 12C0 02C8"            /* .@.H.¿.».@.H.¿.» */
	$"1340 0348 12C0 02C8 1340 0348 12C0 02C8"            /* .@.H.¿.».@.H.¿.» */
	$"1340 0348 12C0 02C8 11FF FF88 1000 0008"            /* .@.H.¿.»...à.... */
	$"1001 C008 1000 8008 1000 0008 0FFF FFF0"            /* ..¿...Ä......... */
};

data 'ICON' (130) {
	$"0000 0000 0000 0000 00AA A800 00AA A800"            /* .........™®..™®. */
	$"01FF FC00 07FF FF00 01BF FC00 07FF FF00"            /* .........ø...... */
	$"01FF FC00 07FF FF00 01FF 8000 07FE 7F00"            /* ..........Ä..... */
	$"01FD 9CC0 07FA 3E20 01FA 0020 07FB 80E0"            /* ..ú¿..> ... ..Ä. */
	$"01FA 7F20 07FA 0020 01FA 0820 00AA 1C20"            /* ... ... ... .™.  */
	$"00AA 0820 0002 0020 0002 0020 0002 0020"            /* .™. ... ... ...  */
	$"0002 0020 0002 0020 0002 1C20 0002 0020"            /* ... ... ... ...  */
	$"0001 80C0 0000 7F00 0000 0000 0000 0000"            /* ..Ä¿............ */
};

data 'ICON' (131) {
	$"0FFF FE00 0800 0300 0855 4280 0855 4240"            /* .........UBÄ.UB@ */
	$"08FF E220 0BFF FA10 08DF E3F8 0BFF F808"            /* ... ............ */
	$"08FF E008 0BFF F808 08FF 0008 0BFC FE08"            /* ................ */
	$"08FB 3988 0BF4 7C48 08F4 0048 0857 01C8"            /* ..9à..|H...H.W.» */
	$"0854 FE48 0804 0048 0804 1048 0804 3848"            /* .T.H...H...H..8H */
	$"0804 1048 0804 0048 0804 0048 0804 0048"            /* ...H...H...H...H */
	$"0804 0048 0804 0048 0804 3848 0804 0048"            /* ...H...H..8H...H */
	$"0803 0188 0800 FE08 0800 0008 0FFF FFF8"            /* ...à............ */
};

data 'ICON' (132) {
	$"0017 6000 003F FC00 00FE FF00 01FE 7F80"            /* ..`..?.........Ä */
	$"03E8 1F80 06F0 0BC0 07C0 07E0 0F80 01A0"            /* ...Ä...¿.¿...Ä.† */
	$"0F00 00E0 0600 0070 0EFE 7F70 0F01 80F0"            /* .......p...p..Ä. */
	$"0F7A 5EF0 0FB6 6DD0 0F7A 5EF0 0F02 40F0"            /* .z^..∂m–.z^...@. */
	$"0AFC 3F70 0E00 0070 0E03 C070 0D00 00F0"            /* ..?p...p..¿p¬... */
	$"0F00 00F0 0F08 10D0 0F87 E170 07C0 03F0"            /* .......–.á.p.¿.. */
	$"02C0 03B0 07E0 07E0 07D8 1FA0 0A57 EB40"            /* .¿.∞.....ÿ.†.W.@ */
	$"0010 0800 0FE0 07E0 1010 0810 200F F008"            /* ............ ... */
};

data 'cicn' (128) {
	$"0000 0000 8010 0000 0000 0020 0020 0000"            /* ....Ä...... . .. */
	$"0000 0000 0000 0048 0000 0048 0000 0000"            /* .......H...H.... */
	$"0004 0001 0004 0000 0000 0000 0000 0000"            /* ................ */
	$"0000 0000 0000 0004 0000 0000 0020 0020"            /* ............. .  */
	$"0000 0000 0004 0000 0000 0020 0020 0000"            /* ........... . .. */
	$"0000 07FF FFE0 0FFF FFF0 0FFF FFF0 0FFF"            /* ................ */
	$"FFF0 0FFF FFF0 0FFF FFF0 0FFF FFF0 0FFF"            /* ................ */
	$"FFF0 0FFF FFF0 0FFF FFF0 0FFF FFF0 0FFF"            /* ................ */
	$"FFF0 0FFF FFF0 0FFF FFF0 0FFF FFF0 0FFF"            /* ................ */
	$"FFF0 0FFF FFF0 0FFF FFF0 0FFF FFF0 0FFF"            /* ................ */
	$"FFF0 0FFF FFF0 0FFF FFF0 0FFF FFF0 0FFF"            /* ................ */
	$"FFF0 0FFF FFF0 0FFF FFF0 0FFF FFF0 0FFF"            /* ................ */
	$"FFF0 0FFF FFE0 0FFF FFE0 0FFF FFC0 07FF"            /* .............¿.. */
	$"FF00 07FF FFE0 0A00 0050 0800 0010 09FF"            /* .........P....∆. */
	$"FF90 0A00 0050 0A0F FC50 0A10 0250 0A10"            /* .ê...P...P...P.. */
	$"0250 0AD2 1250 0A12 1250 0A10 0250 0A10"            /* .P.“.P...P...P.. */
	$"0250 0A12 1250 0A11 E250 0A10 0250 0A0F"            /* .P...P...P...P.. */
	$"FC50 0A00 0090 09FF FF10 0800 0010 0860"            /* .P...ê∆........` */
	$"0010 0860 0190 09F8 0190 09F8 1810 0860"            /* ...`.ê∆..ê∆....` */
	$"1810 0860 0010 0800 0010 0800 0010 0802"            /* ...`............ */
	$"2010 0804 4020 0800 0020 0800 00C0 07FF"            /*  ...@ ... ...¿.. */
	$"FF00 0000 0000 0000 0007 0000 FFFF FFFF"            /* ................ */
	$"FFFF 0001 9999 0000 6666 0002 DDDD DDDD"            /* ....ôô..ff...... */
	$"DDDD 0003 AAAA AAAA AAAA 0004 4444 4444"            /* ....™™™™™™..DDDD */
	$"4444 0005 DDDD 0000 0000 0006 CCCC FFFF"            /* DD..........ÃÃ.. */
	$"0000 000F 0000 0000 0000 0000 0FFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FFF0 0000 0000 F2F2 2222"            /* .............."" */
	$"2222 2222 2222 2F2F 0000 0000 F222 2222"            /* """"""//.....""" */
	$"2222 2222 2222 222F 0000 0000 F22F FFFF"            /* """""""/...../.. */
	$"FFFF FFFF FFFF F22F 0000 0000 F2F3 3333"            /* ......./......33 */
	$"3333 3333 3333 3F2F 0000 0000 F2F3 3333"            /* 333333?/......33 */
	$"FFFF FFFF FF33 3F2F 0000 0000 F2F3 333F"            /* .....3?/......3? */
	$"6666 6666 66F3 3F2F 0000 0000 F2F3 333F"            /* fffff.?/......3? */
	$"6666 6666 66F3 3F2F 0000 0000 F2F3 553F"            /* fffff.?/......U? */
	$"66F6 666F 66F3 3F2F 0000 0000 F2F3 333F"            /* f.fof.?/......3? */
	$"66F6 666F 66F3 3F2F 0000 0000 F2F3 333F"            /* f.fof.?/......3? */
	$"6666 6666 66F3 3F2F 0000 0000 F2F3 333F"            /* fffff.?/......3? */
	$"6666 6666 66F3 3F2F 0000 0000 F2F3 333F"            /* fffff.?/......3? */
	$"66F6 666F 66F3 3F2F 0000 0000 F2F3 333F"            /* f.fof.?/......3? */
	$"666F FFF6 66F3 3F2F 0000 0000 F2F3 333F"            /* fo..f.?/......3? */
	$"6666 6666 66F3 3F2F 0000 0000 F2F3 3333"            /* fffff.?/......33 */
	$"FFFF FFFF FF33 3F2F 0000 0000 F2F3 3333"            /* .....3?/......33 */
	$"3333 3333 3333 F22F 0000 0000 F22F FFFF"            /* 333333./...../.. */
	$"FFFF FFFF FFFF 222F 0000 0000 F222 2222"            /* ......"/.....""" */
	$"2222 2222 2222 222F 0000 0000 F222 2FF2"            /* """""""/....."/. */
	$"2222 2222 2222 222F 0000 0000 F222 2FF2"            /* """""""/....."/. */
	$"2222 2222 2221 122F 0000 0000 F22F FFFF"            /* """""!./...../.. */
	$"F222 2222 2221 122F 0000 0000 F22F FFFF"            /* .""""!./...../.. */
	$"F222 2221 1222 222F 0000 0000 F222 2FF2"            /* .""!.""/....."/. */
	$"2222 2221 1222 222F 0000 0000 F222 2FF2"            /* """!.""/....."/. */
	$"2222 2222 2222 222F 0000 0000 F222 2222"            /* """""""/.....""" */
	$"2222 2222 2222 222F 0000 0000 F222 2222"            /* """""""/.....""" */
	$"2222 2222 2222 222F 0000 0000 F222 2222"            /* """""""/.....""" */
	$"2242 2242 2222 222F 0000 0000 F222 2222"            /* "B"B"""/.....""" */
	$"2422 2422 2222 22F0 0000 0000 F222 2222"            /* $"$""""......""" */
	$"2222 2222 2222 22F0 0000 0000 F222 2222"            /* """""""......""" */
	$"2222 2222 2222 FF00 0000 0000 0FFF FFFF"            /* """""".......... */
	$"FFFF FFFF FFFF 0000 0000"                           /* .......... */
};

data 'cicn' (132) {
	$"0000 0000 8010 0000 0000 0020 0020 0000"            /* ....Ä...... . .. */
	$"0000 0000 0000 0048 0000 0048 0000 0000"            /* .......H...H.... */
	$"0004 0001 0004 0000 0000 0000 0000 0000"            /* ................ */
	$"0000 0000 0000 0004 0000 0000 0020 0020"            /* ............. .  */
	$"0000 0000 0004 0000 0000 0020 0020 0000"            /* ........... . .. */
	$"0000 FFFF FFFF FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF 0000 0000 0000 0000 0000 0000 0000"            /* ................ */
	$"0000 0000 0000 0000 0000 0000 0000 0000"            /* ................ */
	$"0000 0000 0000 0000 0000 0000 0000 0000"            /* ................ */
	$"0000 0000 0000 0000 0000 0000 0000 0000"            /* ................ */
	$"0000 0000 0000 0000 0000 0000 0000 0000"            /* ................ */
	$"0000 0000 0000 0000 0000 0000 0000 0000"            /* ................ */
	$"0000 0000 0000 0000 0000 0000 0000 0000"            /* ................ */
	$"0000 0000 0000 0000 0000 0000 0000 0000"            /* ................ */
	$"0000 0000 0000 0000 0004 0000 FFFF FFFF"            /* ................ */
	$"FFFF 0001 FFFF CCCC CCCC 0002 9999 9999"            /* ......ÃÃÃÃ..ôôôô */
	$"CCCC 0003 CCCC 9999 CCCC 000F 0000 0000"            /* ÃÃ..ÃÃôôÃÃ...... */
	$"0000 0000 0000 0002 0222 0220 0000 0000"            /* .........". .... */
	$"0000 0000 0000 0022 2222 2222 2200 0000"            /* .......""""""... */
	$"0000 0000 0000 2222 2221 2222 2222 0000"            /* ......"""!"""".. */
	$"0000 0000 0002 2222 2221 1222 2222 2000"            /* ......"""!.""" . */
	$"0000 0000 0022 2220 2111 1112 2222 2000"            /* ....."" !..."" . */
	$"0000 0000 0220 2222 1111 1111 2022 2200"            /* ..... "".... "". */
	$"0000 0000 0222 2211 1111 1111 1222 2220"            /* .....""......""  */
	$"0000 0000 2222 2111 1111 1111 1112 2020"            /* ....""!.......   */
	$"0000 0000 2222 1111 1111 1111 1111 2220"            /* ....""........"  */
	$"0000 0000 0221 1111 1111 1111 1111 1222"            /* .....!........." */
	$"0000 0000 2221 FFFF FFF1 1FFF FFFF 1222"            /* ...."!........." */
	$"0000 0000 222F 1111 111F F111 1111 F222"            /* ...."/........." */
	$"0000 0000 222F 1FFF F1F1 1F1F FFF1 F222"            /* ...."/........." */
	$"0000 0000 222F F0FF 0FF1 1FF0 FF0F F202"            /* ...."/.......... */
	$"0000 0000 222F 1FFF F1F1 1F1F FFF1 F222"            /* ...."/........." */
	$"0000 0000 222F 1111 11F1 1F11 1111 F222"            /* ...."/........." */
	$"0000 0000 2021 FFFF FF11 11FF FFFF 1222"            /* .... !........." */
	$"0000 0000 2221 1111 1111 1111 1111 1222"            /* ...."!........." */
	$"0000 0000 2221 1111 11FF FF11 1111 1222"            /* ...."!........." */
	$"0000 0000 2202 1111 1111 1111 1111 2222"            /* ...."........."" */
	$"0000 0000 2222 1111 1111 1111 1111 2222"            /* ....""........"" */
	$"0000 0000 2222 1111 F111 111F 1111 2202"            /* ....""........". */
	$"0000 0000 2222 2111 1FFF FFF1 1112 0222"            /* ....""!........" */
	$"0000 0000 0222 2211 1111 1111 1122 2222"            /* .....""......""" */
	$"0000 0000 0020 2211 1111 1111 1122 2022"            /* ..... "......" " */
	$"0000 0000 0222 2221 1111 1111 1222 2220"            /* .....""!.....""  */
	$"0000 0000 0222 220F F111 1112 2222 2020"            /* ....."".....""   */
	$"0000 0000 2020 020F 1FFF FFF1 2022 0200"            /* ....  ...... ".. */
	$"0000 0000 0000 000F 1111 1111 2000 0000"            /* ............ ... */
	$"0000 0000 FFFF FFF1 1111 1111 1FFF FFF0"            /* ................ */
	$"0000 000F 3333 333F 1111 1111 F333 333F"            /* ....333?.....33? */
	$"0000 00F3 3333 3333 FFFF FFFF 3333 3333"            /* ....3333....3333 */
	$"F000"                                               /* .. */
};

resource 'MBAR' (128) {
	{ 128, 129, 130, 132 }
};

resource 'MENU' (128) {
	128,
	textMenuProc,
	0x7fffffff,
	enabled,
	apple,
	{
		"About Gray Brick\0xc9", noIcon, noKey, noMark, plain;
		"-", noIcon, noKey, noMark, plain
	}
};

resource 'MENU' (129) {
	129,
	textMenuProc,
	0x7fffffdd,
	enabled,
	"File",
	{
		"Open ROM\0xc9", noIcon, "O", noMark, plain;
		"-", noIcon, noKey, noMark, plain;
		"Save Game", noIcon, "S", noMark, plain;
		"Save Screenshot\0xc9", noIcon, "D", noMark, plain;
		"Soft Reset", noIcon, "R", noMark, plain;
		"-", noIcon, noKey, noMark, plain;
		"Close", noIcon, "W", noMark, plain;
		"Quit", noIcon, "Q", noMark, plain
	}
};

resource 'MENU' (130) {
	130,
	textMenuProc,
	0x7ffdb3e7,
	enabled,
	"Options",
	{
		"Enable Audio", noIcon, "A", noMark, plain;
		"Enable STAT Interrupts", noIcon, "I", noMark, plain;
		"Limit to 60 FPS", noIcon, "L", noMark, plain;
		"-", noIcon, noKey, noMark, plain;
		"Frame Skip", noIcon, noKey, noMark, plain;
		"Off", noIcon, "1", noMark, plain;
		"1/2", noIcon, "2", noMark, plain;
		"1/3", noIcon, "3", noMark, plain;
		"1/4", noIcon, "4", noMark, plain;
		"1/5", noIcon, "5", noMark, plain;
		"-", noIcon, noKey, noMark, plain;
		"Scale", noIcon, noKey, noMark, plain;
		"1x", noIcon, noKey, noMark, plain;
		"2x", noIcon, noKey, noMark, plain;
		"-", noIcon, noKey, noMark, plain;
		"Run as GBC", noIcon, noKey, noMark, plain;
		"Ignore Double Speed", noIcon, noKey, noMark, plain;
		"-", noIcon, noKey, noMark, plain;
		"Key Mappings\0xc9", noIcon, "K", noMark, plain
	}
};

resource 'MENU' (132) {
	132,
	textMenuProc,
	0x7fffffff,
	enabled,
	"Debug",
	{
		"Dump VRAM", noIcon, noKey, noMark, plain
	}
};

data 'MDEF' (128) {
	$"2F3A 0004 4E75 0000 0000"
};

resource 'MGBE' (0, "Owner resource") {
	"2013-2026 constcast.org"
};

data 'SICN' (128) {
	$"1FF0 3FF8 27C8 2828 2828 2828 2828 27C8"
	$"2008 2428 2E88 2408 2008 26C8 2010 1FE0"
	$"1FF0 3FF8 3FF8 3FF8 3FF8 3FF8 3FF8 3FF8"
	$"3FF8 3FF8 3FF8 3FF8 3FF8 3FF8 3FF0 1FE0"
};

resource 'SIZE' (-1) {
	reserved,
	ignoreSuspendResumeEvents,
	reserved,
	cannotBackground,
	needsActivateOnFGSwitch,
	backgroundAndForeground,
	dontGetFrontClicks,
	ignoreAppDiedEvents,
	is32BitCompatible,
	notHighLevelEventAware,
	onlyLocalHLEvents,
	notStationeryAware,
	dontUseTextEditServices,
	notDisplayManagerAware,
	reserved,
	reserved,
	8 * 1024 * 1024,
	2 * 1024 * 1024
};

// missing-application name string copied into save and cache files
resource 'STR ' (-16396, purgeable) {
	"Gray Brick"
};

data 'icl4' (128) {
	$"0000 0FFF FFFF FFFF FFFF FFFF FFF0 0000"            /* ................ */
	$"0000 FCFC CCCC CCCC CCCC CCCC CFCF 0000"            /* ....ÃÃÃÃÃÃÃÃœœ.. */
	$"0000 FCCC CCCC CCCC CCCC CCCC CCCF 0000"            /* ...ÃÃÃÃÃÃÃÃÃÃœ.. */
	$"0000 FCCF FFFF FFFF FFFF FFFF FCCF 0000"            /* ...œ.........œ.. */
	$"0000 FCFD DDDD DDDD DDDD DDDD DFCF 0000"            /* .............œ.. */
	$"0000 FCFD DDDD FFFF FFFF FFDD DFCF 0000"            /* .............œ.. */
	$"0000 FCFD DDDF 1111 1111 11FD DFCF 0000"            /* .............œ.. */
	$"0000 FCFD DDDF 1111 1111 11FD DFCF 0000"            /* .............œ.. */
	$"0000 FCFD 33DF 11F1 111F 11FD DFCF 0000"            /* ....3........œ.. */
	$"0000 FCFD DDDF 11F1 111F 11FD DFCF 0000"            /* .............œ.. */
	$"0000 FCFD DDDF 1111 1111 11FD DFCF 0000"            /* .............œ.. */
	$"0000 FCFD DDDF 1111 1111 11FD DFCF 0000"            /* .............œ.. */
	$"0000 FCFD DDDF 11F1 111F 11FD DFCF 0000"            /* .............œ.. */
	$"0000 FCFD DDDF 111F FFF1 11FD DFCF 0000"            /* .............œ.. */
	$"0000 FCFD DDDF 1111 1111 11FD DFCF 0000"            /* .............œ.. */
	$"0000 FCFD DDDD FFFF FFFF FFDD DFCF 0000"            /* .............œ.. */
	$"0000 FCFD DDDD DDDD DDDD DDDD FCCF 0000"            /* .............œ.. */
	$"0000 FCCF FFFF FFFF FFFF FFFF CCCF 0000"            /* ...œ........Ãœ.. */
	$"0000 FCCC CCCC CCCC CCCC CCCC CCCF 0000"            /* ...ÃÃÃÃÃÃÃÃÃÃœ.. */
	$"0000 FCCC CFFC CCCC CCCC CCCC CCCF 0000"            /* ...Ãœ.ÃÃÃÃÃÃÃœ.. */
	$"0000 FCCC CFFC CCCC CCCC CCC4 4CCF 0000"            /* ...Ãœ.ÃÃÃÃÃƒLœ.. */
	$"0000 FCCF FFFF FCCC CCCC CCC4 4CCF 0000"            /* ...œ...ÃÃÃÃƒLœ.. */
	$"0000 FCCF FFFF FCCC CCC4 4CCC CCCF 0000"            /* ...œ...ÃÃƒLÃÃœ.. */
	$"0000 FCCC CFFC CCCC CCC4 4CCC CCCF 0000"            /* ...Ãœ.ÃÃÃƒLÃÃœ.. */
	$"0000 FCCC CFFC CCCC CCCC CCCC CCCF 0000"            /* ...Ãœ.ÃÃÃÃÃÃÃœ.. */
	$"0000 FCCC CCCC CCCC CCCC CCCC CCCF 0000"            /* ...ÃÃÃÃÃÃÃÃÃÃœ.. */
	$"0000 FCCC CCCC CCCC CCCC CCCC CCCF 0000"            /* ...ÃÃÃÃÃÃÃÃÃÃœ.. */
	$"0000 FCCC CCCC CCEC CCEC CCCC CCCF 0000"            /* ...ÃÃÃÃ.Ã.ÃÃÃœ.. */
	$"0000 FCCC CCCC CECC CECC CCCC CCF0 0000"            /* ...ÃÃÃŒÃŒÃÃÃÃ... */
	$"0000 FCCC CCCC CCCC CCCC CCCC CCF0 0000"            /* ...ÃÃÃÃÃÃÃÃÃÃ... */
	$"0000 FCCC CCCC CCCC CCCC CCCC FF00 0000"            /* ...ÃÃÃÃÃÃÃÃÃ.... */
	$"0000 0FFF FFFF FFFF FFFF FFFF 0000 0000"            /* ................ */
};

data 'icl4' (129) {
	$"0000 0000 0000 0000 0000 0000 0000 0000"            /* ................ */
	$"0000 FFFF FFFF FFFF FFFF FFFF FF00 0000"            /* ................ */
	$"000F DDDD DDDD DDDD DDDD DDDD DF00 0000"            /* ................ */
	$"000F FFFF DDFF FFFF FFFF FFDD FFFF F000"            /* ................ */
	$"000F DDDD DFCC CCCC CCCC CCFD DDDD F000"            /* .....ÃÃÃÃÃÃ..... */
	$"000F FFFD DFCC CCCC CCCC CCFD DFFF F000"            /* .....ÃÃÃÃÃÃ..... */
	$"000F DDDD DFCC CCCC CCCC CCFD DDDD F000"            /* .....ÃÃÃÃÃÃ..... */
	$"000F FFFF DDFF FFFF FFFF FFDD FFFF F000"            /* ................ */
	$"000F DDDD DDDD DDDD DDDD DDDD DDDD F000"            /* ................ */
	$"000F DDDF FFFF FFFF FFFF FFFF FDDD F000"            /* ................ */
	$"000F DDFC CF00 5000 5000 50FC CFDD F000"            /* ....œ.P.P.P.œ... */
	$"000F DDFC CF50 0050 0050 00FC CFDD F000"            /* ....œP.P.P..œ... */
	$"000F DDFC CF00 5000 5000 50FC CFDD F000"            /* ....œ.P.P.P.œ... */
	$"000F DDFC CF50 0050 0050 00FC CFDD F000"            /* ....œP.P.P..œ... */
	$"000F DDFC CF00 5000 5000 50FC CFDD F000"            /* ....œ.P.P.P.œ... */
	$"000F DDFC CF50 0050 0050 00FC CFDD F000"            /* ....œP.P.P..œ... */
	$"000F DDFC CF00 5000 5000 50FC CFDD F000"            /* ....œ.P.P.P.œ... */
	$"000F DDFC CF50 0050 0050 00FC CFDD F000"            /* ....œP.P.P..œ... */
	$"000F DDFC CF00 5000 5000 50FC CFDD F000"            /* ....œ.P.P.P.œ... */
	$"000F DDFC CF50 0050 0050 00FC CFDD F000"            /* ....œP.P.P..œ... */
	$"000F DDFC CF00 5000 5000 50FC CFDD F000"            /* ....œ.P.P.P.œ... */
	$"000F DDFC CF50 0050 0050 00FC CFDD F000"            /* ....œP.P.P..œ... */
	$"000F DDFC CF00 5000 5000 50FC CFDD F000"            /* ....œ.P.P.P.œ... */
	$"000F DDFC CF50 0050 0050 00FC CFDD F000"            /* ....œP.P.P..œ... */
	$"000F DDFC CF00 5000 5000 50FC CFDD F000"            /* ....œ.P.P.P.œ... */
	$"000F DDFC CF50 0050 0050 00FC CFDD F000"            /* ....œP.P.P..œ... */
	$"000F DDDF FFFF FFFF FFFF FFFF FDDD F000"            /* ................ */
	$"000F DDDD DDDD DDDD DDDD DDDD DDDD F000"            /* ................ */
	$"000F DDDD DDDD DDDF FFDD DDDD DDDD F000"            /* ................ */
	$"000F DDDD DDDD DDDD FDDD DDDD DDDD F000"            /* ................ */
	$"000F DDDD DDDD DDDD DDDD DDDD DDDD F000"            /* ................ */
	$"0000 FFFF FFFF FFFF FFFF FFFF FFFF 0000"            /* ................ */
};

data 'icl4' (130) {
	$"0000 FFFF FFFF FFFF FFFF FFF0 0000 0000"            /* ................ */
	$"0000 FC0C 0C0C 0C0C 0C0C 0CFF 0000 0000"            /* ................ */
	$"0000 F0C0 CFCF CFCF CFC0 C0FC F000 0000"            /* ...¿œœœœœ¿¿..... */
	$"0000 FC0C 0F0F 0F0F 0F0C 0CFC CF00 0000"            /* ............œ... */
	$"0000 F0C0 FFFF FFFF FFF0 C0FC CCF0 0000"            /* ...¿......¿.Ã... */
	$"0000 FCFF FFFF FFFF FFFF FCFC CCCF 0000"            /* ............Ãœ.. */
	$"0000 F0C0 FF0F FFFF FFF0 C0FF FFFF F000"            /* ...¿......¿..... */
	$"0000 FCFF FFFF FFFF FFFF FC0C 0C0C F000"            /* ................ */
	$"0000 F0C0 FFFF FFFF FFF0 C0C0 C0C0 F000"            /* ...¿......¿¿¿¿.. */
	$"0000 FCFF FFFF FFFF FFFF FC0C 0C0C F000"            /* ................ */
	$"0000 F0C0 FFFF FFFF C0C0 C0C0 C0C0 F000"            /* ...¿....¿¿¿¿¿¿.. */
	$"0000 FCFF FFFF FF00 FFFF FFFC 0C0C F000"            /* ................ */
	$"0000 F0C0 FFFF F0FF 00FF F00F F0C0 F000"            /* ...¿.........¿.. */
	$"0000 FCFF FFFF 0F00 0FFF FF00 0F0C F000"            /* ................ */
	$"0000 F0C0 FFFF CF00 0000 0000 0FC0 F000"            /* ...¿..œ......¿.. */
	$"0000 FC0C 0F0F 0FFF 0000 000F FF0C F000"            /* ................ */
	$"0000 F0C0 CFCF CF00 FFFF FFF0 0FC0 F000"            /* ...¿œœœ......¿.. */
	$"0000 FC0C 0C0C 0F00 0000 0000 0F0C F000"            /* ................ */
	$"0000 F0C0 C0C0 CF00 000F 0000 0FC0 F000"            /* ...¿¿¿œ......¿.. */
	$"0000 FC0C 0C0C 0F00 00FF F000 0F0C F000"            /* ................ */
	$"0000 F0C0 C0C0 CF00 000F 0000 0FC0 F000"            /* ...¿¿¿œ......¿.. */
	$"0000 FC0C 0C0C 0F00 0000 0000 0F0C F000"            /* ................ */
	$"0000 F0C0 C0C0 CF00 0000 0000 0FC0 F000"            /* ...¿¿¿œ......¿.. */
	$"0000 FC0C 0C0C 0F00 0000 0000 0F0C F000"            /* ................ */
	$"0000 F0C0 C0C0 CF00 0000 0000 0FC0 F000"            /* ...¿¿¿œ......¿.. */
	$"0000 FC0C 0C0C 0F00 0000 0000 0F0C F000"            /* ................ */
	$"0000 F0C0 C0C0 CF00 00FF F000 0FC0 F000"            /* ...¿¿¿œ......¿.. */
	$"0000 FC0C 0C0C 0F00 0000 0000 0F0C F000"            /* ................ */
	$"0000 F0C0 C0C0 C0FF 0000 000F F0C0 F000"            /* ...¿¿¿¿......¿.. */
	$"0000 FC0C 0C0C 0C0C FFFF FFFC 0C0C F000"            /* ................ */
	$"0000 F0C0 C0C0 C0C0 C0C0 C0C0 C0C0 F000"            /* ...¿¿¿¿¿¿¿¿¿¿¿.. */
	$"0000 FFFF FFFF FFFF FFFF FFFF FFFF F000"            /* ................ */
};

data 'icl4' (131) {
	$"0000 FFFF FFFF FFFF FFFF FFF0 0000 0000"            /* ................ */
	$"0000 FC0C 0C0C 0C0C 0C0C 0CFF 0000 0000"            /* ................ */
	$"0000 F0C0 C0C0 C0C0 C0C0 C0FC F000 0000"            /* ...¿¿¿¿¿¿¿¿..... */
	$"0000 FC06 0C06 6C0C 0F0C 0CFC CF00 0000"            /* ......l.....œ... */
	$"0000 F0C6 C0C6 C6C0 CFC0 C0FC CCF0 0000"            /* ...∆¿∆∆¿œ¿¿.Ã... */
	$"0000 FC06 0C06 060C 0F0C 0CFC CCCF 0000"            /* ............Ãœ.. */
	$"0000 F0C6 66C6 60C0 CFFF C0FF FFFF F000"            /* ...∆f∆`¿œ.¿..... */
	$"0000 FC0C 0C0C 0C0C 0C0C 0C0C 0C0C F000"            /* ................ */
	$"0000 F0C0 C0C0 C0C0 C0C0 C0C0 C0C0 F000"            /* ...¿¿¿¿¿¿¿¿¿¿¿.. */
	$"0000 FC0C 0C0C 0C0C 0C0C 0C0C 0C0C F000"            /* ................ */
	$"0000 F0C6 60C6 66C0 66C0 CFFF C0C0 F000"            /* ...∆`∆f¿f¿œ.¿¿.. */
	$"0000 FC06 0606 0C06 0C0C 0FFC 0C0C F000"            /* ................ */
	$"0000 F0C6 C6C6 60C6 C0C0 CFCF C0C0 F000"            /* ...∆∆∆`∆¿¿œœ¿¿.. */
	$"0000 FC06 6C06 660C 660C 0FFC 0C0C F000"            /* ....l.f.f....... */
	$"0000 F0C0 C0C0 C0C0 C0C0 C0C0 C0C0 F000"            /* ...¿¿¿¿¿¿¿¿¿¿¿.. */
	$"0000 FC0C 0C0C 0C0C 0C0C 0C0C 0C0C F000"            /* ................ */
	$"0000 F0C0 C0C0 C0C0 C0C0 C0C0 C0C0 F000"            /* ...¿¿¿¿¿¿¿¿¿¿¿.. */
	$"0000 FC0C 6C0C 660C 0C66 0C0C 0C0C F000"            /* ....l.f..f...... */
	$"0000 F0C6 C6C6 C6C0 C6C0 C0C0 C0C0 F000"            /* ...∆∆∆∆¿∆¿¿¿¿¿.. */
	$"0000 FC06 0606 6C0C 060C 0C0C 0C0C F000"            /* ......l......... */
	$"0000 F0C0 60C6 C6C0 C066 C0C0 C0C0 F000"            /* ...¿`∆∆¿¿f¿¿¿¿.. */
	$"0000 FC0C 0C0C 0C0C 0C0C 0C0C 0C0C F000"            /* ................ */
	$"0000 F0C0 C0C0 C0C0 C0C0 C0C0 C0C0 F000"            /* ...¿¿¿¿¿¿¿¿¿¿¿.. */
	$"0000 FC0C 0C0C 0C0C 0C0C 0C0C 0C0C F000"            /* ................ */
	$"0000 F0C0 C6C0 66C0 C3C0 3033 30C0 F000"            /* ...¿∆¿f¿√¿030¿.. */
	$"0000 FC0C 0606 060C 033C 3C0C 3C0C F000"            /* .........<<.<... */
	$"0000 F0C0 C6C6 60C0 C3C3 30C3 C0C0 F000"            /* ...¿∆∆`¿√√0√¿¿.. */
	$"0000 FC06 6C06 060C 030C 3C33 3CFC F000"            /* ....l.....<3<... */
	$"0000 F0C0 C0C0 C0C0 C0C0 C0C0 CF00 F000"            /* ...¿¿¿¿¿¿¿¿¿œ... */
	$"0000 FC0C 0C0C 0C0C 0C0C 0C0C 0C0C F000"            /* ................ */
	$"0000 F0C0 C0C0 C0C0 C0C0 C0C0 C0C0 F000"            /* ...¿¿¿¿¿¿¿¿¿¿¿.. */
	$"0000 FFFF FFFF FFFF FFFF FFFF FFFF F000"            /* ................ */
};

data 'icl8' (128) {
	$"0000 0000 00FF FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FFFF FFFF FF00 0000 0000"            /* ................ */
	$"0000 0000 FFF6 FFF6 F6F6 F6F6 F6F6 F6F6"            /* ................ */
	$"F6F6 F6F6 F6F6 F6F6 F6FF F6FF 0000 0000"            /* ................ */
	$"0000 0000 FFF6 F6F6 F6F6 F6F6 F6F6 F6F6"            /* ................ */
	$"F6F6 F6F6 F6F6 F6F6 F6F6 F6FF 0000 0000"            /* ................ */
	$"0000 0000 FFF6 F6FF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FFFF FFF6 F6FF 0000 0000"            /* ................ */
	$"0000 0000 FFF6 FFF8 F8F8 F8F8 F8F8 F8F8"            /* ................ */
	$"F8F8 F8F8 F8F8 F8F8 F8FF F6FF 0000 0000"            /* ................ */
	$"0000 0000 FFF6 FFF8 F8F8 F8F8 FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF F8F8 F8FF F6FF 0000 0000"            /* ................ */
	$"0000 0000 FFF6 FFF8 F8F8 F8FF 2929 2929"            /* ............)))) */
	$"2929 2929 2929 FFF8 F8FF F6FF 0000 0000"            /* )))))).......... */
	$"0000 0000 FFF6 FFF8 F8F8 F8FF 2929 2929"            /* ............)))) */
	$"2929 2929 2929 FFF8 F8FF F6FF 0000 0000"            /* )))))).......... */
	$"0000 0000 FFF6 FFF8 D8D8 F8FF 2929 FF29"            /* ........ÿÿ..)).) */
	$"2929 29FF 2929 FFF8 F8FF F6FF 0000 0000"            /* ))).)).......... */
	$"0000 0000 FFF6 FFF8 F8F8 F8FF 2929 FF29"            /* ............)).) */
	$"2929 29FF 2929 FFF8 F8FF F6FF 0000 0000"            /* ))).)).......... */
	$"0000 0000 FFF6 FFF8 F8F8 F8FF 2929 2929"            /* ............)))) */
	$"2929 2929 2929 FFF8 F8FF F6FF 0000 0000"            /* )))))).......... */
	$"0000 0000 FFF6 FFF8 F8F8 F8FF 2929 2929"            /* ............)))) */
	$"2929 2929 2929 FFF8 F8FF F6FF 0000 0000"            /* )))))).......... */
	$"0000 0000 FFF6 FFF8 F8F8 F8FF 2929 FF29"            /* ............)).) */
	$"2929 29FF 2929 FFF8 F8FF F6FF 0000 0000"            /* ))).)).......... */
	$"0000 0000 FFF6 FFF8 F8F8 F8FF 2929 29FF"            /* ............))). */
	$"FFFF FF29 2929 FFF8 F8FF F6FF 0000 0000"            /* ...))).......... */
	$"0000 0000 FFF6 FFF8 F8F8 F8FF 2929 2929"            /* ............)))) */
	$"2929 2929 2929 FFF8 F8FF F6FF 0000 0000"            /* )))))).......... */
	$"0000 0000 FFF6 FFF8 F8F8 F8F8 FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF F8F8 F8FF F6FF 0000 0000"            /* ................ */
	$"0000 0000 FFF6 FFF8 F8F8 F8F8 F8F8 F8F8"            /* ................ */
	$"F8F8 F8F8 F8F8 F8F8 FFF6 F6FF 0000 0000"            /* ................ */
	$"0000 0000 FFF6 F6FF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FFFF F6F6 F6FF 0000 0000"            /* ................ */
	$"0000 0000 FFF6 F6F6 F6F6 F6F6 F6F6 F6F6"            /* ................ */
	$"F6F6 F6F6 F6F6 F6F6 F6F6 F6FF 0000 0000"            /* ................ */
	$"0000 0000 FFF6 F6F6 F6FF FFF6 F6F6 F6F6"            /* ................ */
	$"F6F6 F6F6 F6F6 F6F6 F6F6 F6FF 0000 0000"            /* ................ */
	$"0000 0000 FFF6 F6F6 F6FF FFF6 F6F6 F6F6"            /* ................ */
	$"F6F6 F6F6 F6F6 F669 69F6 F6FF 0000 0000"            /* .......ii....... */
	$"0000 0000 FFF6 F6FF FFFF FFFF FFF6 F6F6"            /* ................ */
	$"F6F6 F6F6 F6F6 F669 69F6 F6FF 0000 0000"            /* .......ii....... */
	$"0000 0000 FFF6 F6FF FFFF FFFF FFF6 F6F6"            /* ................ */
	$"F6F6 F669 69F6 F6F6 F6F6 F6FF 0000 0000"            /* ...ii........... */
	$"0000 0000 FFF6 F6F6 F6FF FFF6 F6F6 F6F6"            /* ................ */
	$"F6F6 F669 69F6 F6F6 F6F6 F6FF 0000 0000"            /* ...ii........... */
	$"0000 0000 FFF6 F6F6 F6FF FFF6 F6F6 F6F6"            /* ................ */
	$"F6F6 F6F6 F6F6 F6F6 F6F6 F6FF 0000 0000"            /* ................ */
	$"0000 0000 FFF6 F6F6 F6F6 F6F6 F6F6 F6F6"            /* ................ */
	$"F6F6 F6F6 F6F6 F6F6 F6F6 F6FF 0000 0000"            /* ................ */
	$"0000 0000 FFF6 F6F6 F6F6 F6F6 F6F6 F6F6"            /* ................ */
	$"F6F6 F6F6 F6F6 F6F6 F6F6 F6FF 0000 0000"            /* ................ */
	$"0000 0000 FFF6 F6F6 F6F6 F6F6 F6F6 FCF6"            /* ................ */
	$"F6F6 FCF6 F6F6 F6F6 F6F6 F6FF 0000 0000"            /* ................ */
	$"0000 0000 FFF6 F6F6 F6F6 F6F6 F6FC F6F6"            /* ................ */
	$"F6FC F6F6 F6F6 F6F6 F6F6 FF00 0000 0000"            /* ................ */
	$"0000 0000 FFF6 F6F6 F6F6 F6F6 F6F6 F6F6"            /* ................ */
	$"F6F6 F6F6 F6F6 F6F6 F6F6 FF00 0000 0000"            /* ................ */
	$"0000 0000 FFF6 F6F6 F6F6 F6F6 F6F6 F6F6"            /* ................ */
	$"F6F6 F6F6 F6F6 F6F6 FFFF 0000 0000 0000"            /* ................ */
	$"0000 0000 00FF FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FFFF 0000 0000 0000 0000"            /* ................ */
};

data 'icl8' (129) {
	$"0000 0000 0000 0000 0000 0000 0000 0000"            /* ................ */
	$"0000 0000 0000 0000 0000 0000 0000 0000"            /* ................ */
	$"0000 0000 FFFF FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FFFF FFFF 0000 0000 0000"            /* ................ */
	$"0000 00FF F8F8 F8F8 F8F8 F8F8 F8F8 F8F8"            /* ................ */
	$"F8F8 F8F8 F8F8 F8F8 F8FF 0000 0000 0000"            /* ................ */
	$"0000 00FF FFFF FFFF F8F8 FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF F8F8 FFFF FFFF FF00 0000"            /* ................ */
	$"0000 00FF F8F8 F8F8 F8FF 2B2B 2B2B 2B2B"            /* ..........++++++ */
	$"2B2B 2B2B 2B2B FFF8 F8F8 F8F8 FF00 0000"            /* ++++++.......... */
	$"0000 00FF FFFF FFF8 F8FF 2B2B 2B2B 2B2B"            /* ..........++++++ */
	$"2B2B 2B2B 2B2B FFF8 F8FF FFFF FF00 0000"            /* ++++++.......... */
	$"0000 00FF F8F8 F8F8 F8FF 2B2B 2B2B 2B2B"            /* ..........++++++ */
	$"2B2B 2B2B 2B2B FFF8 F8F8 F8F8 FF00 0000"            /* ++++++.......... */
	$"0000 00FF FFFF FFFF F8F8 FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF F8F8 FFFF FFFF FF00 0000"            /* ................ */
	$"0000 00FF F8F8 F8F8 F8F8 F8F8 F8F8 F8F8"            /* ................ */
	$"F8F8 F8F8 F8F8 F8F8 F8F8 F8F8 FF00 0000"            /* ................ */
	$"0000 00FF F8F8 F8FF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FFFF FFF8 F8F8 FF00 0000"            /* ................ */
	$"0000 00FF F8F8 FFF5 F5FF 2A2A 2A2A 2A2A"            /* ..........****** */
	$"2A2A 2A2A 2A2A FFF5 F5FF F8F8 FF00 0000"            /* ******.......... */
	$"0000 00FF F8F8 FFF5 F5FF 2A2A 2A2A 2A2A"            /* ..........****** */
	$"2A2A 2A2A 2A2A FFF5 F5FF F8F8 FF00 0000"            /* ******.......... */
	$"0000 00FF F8F8 FFF5 F5FF 2A2A 2A2A 2A2A"            /* ..........****** */
	$"2A2A 2A2A 2A2A FFF5 F5FF F8F8 FF00 0000"            /* ******.......... */
	$"0000 00FF F8F8 FFF5 F5FF 2A2A 2A2A 2A2A"            /* ..........****** */
	$"2A2A 2A2A 2A2A FFF5 F5FF F8F8 FF00 0000"            /* ******.......... */
	$"0000 00FF F8F8 FFF5 F5FF 2A2A 2A2A 2A2A"            /* ..........****** */
	$"2A2A 2A2A 2A2A FFF5 F5FF F8F8 FF00 0000"            /* ******.......... */
	$"0000 00FF F8F8 FFF5 F5FF 2A2A 2A2A 2A2A"            /* ..........****** */
	$"2A2A 2A2A 2A2A FFF5 F5FF F8F8 FF00 0000"            /* ******.......... */
	$"0000 00FF F8F8 FFF5 F5FF 2A2A 2A2A 2A2A"            /* ..........****** */
	$"2A2A 2A2A 2A2A FFF5 F5FF F8F8 FF00 0000"            /* ******.......... */
	$"0000 00FF F8F8 FFF5 F5FF 2A2A 2A2A 2A2A"            /* ..........****** */
	$"2A2A 2A2A 2A2A FFF5 F5FF F8F8 FF00 0000"            /* ******.......... */
	$"0000 00FF F8F8 FFF5 F5FF 2A2A 2A2A 2A2A"            /* ..........****** */
	$"2A2A 2A2A 2A2A FFF5 F5FF F8F8 FF00 0000"            /* ******.......... */
	$"0000 00FF F8F8 FFF5 F5FF 2A2A 2A2A 2A2A"            /* ..........****** */
	$"2A2A 2A2A 2A2A FFF5 F5FF F8F8 FF00 0000"            /* ******.......... */
	$"0000 00FF F8F8 FFF5 F5FF 2A2A 2A2A 2A2A"            /* ..........****** */
	$"2A2A 2A2A 2A2A FFF5 F5FF F8F8 FF00 0000"            /* ******.......... */
	$"0000 00FF F8F8 FFF5 F5FF 2A2A 2A2A 2A2A"            /* ..........****** */
	$"2A2A 2A2A 2A2A FFF5 F5FF F8F8 FF00 0000"            /* ******.......... */
	$"0000 00FF F8F8 FFF5 F5FF 2A2A 2A2A 2A2A"            /* ..........****** */
	$"2A2A 2A2A 2A2A FFF5 F5FF F8F8 FF00 0000"            /* ******.......... */
	$"0000 00FF F8F8 FFF5 F5FF 2A2A 2A2A 2A2A"            /* ..........****** */
	$"2A2A 2A2A 2A2A FFF5 F5FF F8F8 FF00 0000"            /* ******.......... */
	$"0000 00FF F8F8 FFF5 F5FF 2A2A 2A2A 2A2A"            /* ..........****** */
	$"2A2A 2A2A 2A2A FFF5 F5FF F8F8 FF00 0000"            /* ******.......... */
	$"0000 00FF F8F8 FFF5 F5FF 2A2A 2A2A 2A2A"            /* ..........****** */
	$"2A2A 2A2A 2A2A FFF5 F5FF F8F8 FF00 0000"            /* ******.......... */
	$"0000 00FF F8F8 F8FF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FFFF FFF8 F8F8 FF00 0000"            /* ................ */
	$"0000 00FF F8F8 F8F8 F8F8 F8F8 F8F8 F8F8"            /* ................ */
	$"F8F8 F8F8 F8F8 F8F8 F8F8 F8F8 FF00 0000"            /* ................ */
	$"0000 00FF F8F8 F8F8 F8F8 F8F8 F8F8 F8FF"            /* ................ */
	$"FFFF F8F8 F8F8 F8F8 F8F8 F8F8 FF00 0000"            /* ................ */
	$"0000 00FF F8F8 F8F8 F8F8 F8F8 F8F8 F8F8"            /* ................ */
	$"FFF8 F8F8 F8F8 F8F8 F8F8 F8F8 FF00 0000"            /* ................ */
	$"0000 00FF F8F8 F8F8 F8F8 F8F8 F8F8 F8F8"            /* ................ */
	$"F8F8 F8F8 F8F8 F8F8 F8F8 F8F8 FF00 0000"            /* ................ */
	$"0000 0000 FFFF FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FFFF FFFF FFFF 0000 0000"            /* ................ */
};

data 'icl8' (130) {
	$"0000 0000 FFFF FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FF00 0000 0000 0000 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5F5 F5F5"            /* ................ */
	$"F5F5 F5F5 F5F5 FFFF 0000 0000 0000 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5FF F5FF F5FF F5FF"            /* ................ */
	$"F5FF F5F5 F5F5 FF2B FF00 0000 0000 0000"            /* .......+........ */
	$"0000 0000 FFF5 F5F5 F5FF F5FF F5FF F5FF"            /* ................ */
	$"F5FF F5F5 F5F5 FF2B 2BFF 0000 0000 0000"            /* .......++....... */
	$"0000 0000 FFF5 F5F5 FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFF5 F5F5 FF2B 2B2B FF00 0000 0000"            /* .......+++...... */
	$"0000 0000 FFF5 FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFF5 FF2B 2B2B 2BFF 0000 0000"            /* .......++++..... */
	$"0000 0000 FFF5 F5F5 FFFF 00FF FFFF FFFF"            /* ................ */
	$"FFFF FFF5 F5F5 FFFF FFFF FFFF FF00 0000"            /* ................ */
	$"0000 0000 FFF5 FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFF5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFF5 F5F5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFF5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 FFFF FFFF FFFF FFFF"            /* ................ */
	$"F5F5 F5F5 F5F5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 FFFF FFFF FFFF FFFF 0000"            /* ................ */
	$"FFFF FFFF FFFF FFF5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 FFFF FFFF FF00 FFFF"            /* ................ */
	$"0000 FFFF FF00 00FF FFF5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 FFFF FFFF FFFF F5FF 0000"            /* ................ */
	$"00FF FFFF FFFF 0000 00FF F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 FFFF FFFF F5FF 0000"            /* ................ */
	$"0000 0000 0000 0000 00FF F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5FF F5FF F5FF FFFF"            /* ................ */
	$"0000 0000 0000 00FF FFFF F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5FF F5FF F5FF 0000"            /* ................ */
	$"FFFF FFFF FFFF FF00 00FF F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5FF 0000"            /* ................ */
	$"0000 0000 0000 0000 00FF F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5FF 0000"            /* ................ */
	$"0000 00FF 0000 0000 00FF F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5FF 0000"            /* ................ */
	$"0000 FFFF FF00 0000 00FF F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5FF 0000"            /* ................ */
	$"0000 00FF 0000 0000 00FF F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5FF 0000"            /* ................ */
	$"0000 0000 0000 0000 00FF F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5FF 0000"            /* ................ */
	$"0000 0000 0000 0000 00FF F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5FF 0000"            /* ................ */
	$"0000 0000 0000 0000 00FF F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5FF 0000"            /* ................ */
	$"0000 0000 0000 0000 00FF F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5FF 0000"            /* ................ */
	$"0000 0000 0000 0000 00FF F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5FF 0000"            /* ................ */
	$"0000 FFFF FF00 0000 00FF F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5FF 0000"            /* ................ */
	$"0000 0000 0000 0000 00FF F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5F5 FFFF"            /* ................ */
	$"0000 0000 0000 00FF FFF5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5F5 F5F5"            /* ................ */
	$"FFFF FFFF FFFF FFF5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5F5 F5F5"            /* ................ */
	$"F5F5 F5F5 F5F5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFFF FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FFFF FFFF FFFF FF00 0000"            /* ................ */
};

data 'icl8' (131) {
	$"0000 0000 FFFF FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FF00 0000 0000 0000 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5F5 F5F5"            /* ................ */
	$"F5F5 F5F5 F5F5 FFFF 0000 0000 0000 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5F5 F5F5"            /* ................ */
	$"F5F5 F5F5 F5F5 FF2B FF00 0000 0000 0000"            /* .......+........ */
	$"0000 0000 FFF5 F57F F5F5 F57F 7FF5 F5F5"            /* ................ */
	$"F5FF F5F5 F5F5 FF2B 2BFF 0000 0000 0000"            /* .......++....... */
	$"0000 0000 FFF5 F57F F5F5 F57F F57F F5F5"            /* ................ */
	$"F5FF F5F5 F5F5 FF2B 2B2B FF00 0000 0000"            /* .......+++...... */
	$"0000 0000 FFF5 F57F F5F5 F57F F57F F5F5"            /* ................ */
	$"F5FF F5F5 F5F5 FF2B 2B2B 2BFF 0000 0000"            /* .......++++..... */
	$"0000 0000 FFF5 F57F 7F7F F57F 7FF5 F5F5"            /* ................ */
	$"F5FF FFFF F5F5 FFFF FFFF FFFF FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5F5 F5F5"            /* ................ */
	$"F5F5 F5F5 F5F5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5F5 F5F5"            /* ................ */
	$"F5F5 F5F5 F5F5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5F5 F5F5"            /* ................ */
	$"F5F5 F5F5 F5F5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F57F 7FF5 F57F 7F7F F5F5"            /* ................ */
	$"7F7F F5F5 F5FF FFFF F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F57F F57F F57F F5F5 F57F"            /* ................ */
	$"F5F5 F5F5 F5FF FFF5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F57F F57F F57F 7FF5 F57F"            /* ................ */
	$"F5F5 F5F5 F5FF F5FF F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F57F 7FF5 F57F 7F7F F5F5"            /* ................ */
	$"7F7F F5F5 F5FF FFF5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5F5 F5F5"            /* ................ */
	$"F5F5 F5F5 F5F5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5F5 F5F5"            /* ................ */
	$"F5F5 F5F5 F5F5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5F5 F5F5"            /* ................ */
	$"F5F5 F5F5 F5F5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 7FF5 F5F5 7F7F F5F5"            /* ................ */
	$"F5F5 FFFF F5F5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F57F F57F F57F F57F F5F5"            /* ................ */
	$"F5FF F5F5 F5F5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F57F F57F F57F 7FF5 F5F5"            /* ................ */
	$"F5FF F5F5 F5F5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 7FF5 F57F F57F F5F5"            /* ................ */
	$"F5F5 FFFF F5F5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5F5 F5F5"            /* ................ */
	$"F5F5 F5F5 F5F5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5F5 F5F5"            /* ................ */
	$"F5F5 F5F5 F5F5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5F5 F5F5"            /* ................ */
	$"F5F5 F5F5 F5F5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F57F F5F5 7F7F F5F5"            /* ................ */
	$"F569 F5F5 69F5 6969 69F5 F5F5 FF00 0000"            /* .i..i.iii....... */
	$"0000 0000 FFF5 F5F5 F57F F57F F57F F5F5"            /* ................ */
	$"F569 69F5 69F5 F5F5 69F5 F5F5 FF00 0000"            /* .ii.i...i....... */
	$"0000 0000 FFF5 F5F5 F57F F57F 7FF5 F5F5"            /* ................ */
	$"F569 F569 69F5 F569 F5F5 F5F5 FF00 0000"            /* .i.ii..i........ */
	$"0000 0000 FFF5 F57F 7FF5 F57F F57F F5F5"            /* ................ */
	$"F569 F5F5 69F5 6969 69F5 FFF5 FF00 0000"            /* .i..i.iii....... */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5F5 F5F5"            /* ................ */
	$"F5F5 F5F5 F5F5 F5F5 F5FF 00F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5F5 F5F5"            /* ................ */
	$"F5F5 F5F5 F5F5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFF5 F5F5 F5F5 F5F5 F5F5 F5F5"            /* ................ */
	$"F5F5 F5F5 F5F5 F5F5 F5F5 F5F5 FF00 0000"            /* ................ */
	$"0000 0000 FFFF FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FFFF FFFF FFFF FF00 0000"            /* ................ */
};

data 'ics#' (128) {
	$"1FF0 2008 27C8 2828 2828 2828 2828 27C8"            /* .. .'»(((((((('» */
	$"2008 2428 2E88 2408 2008 2288 2010 1FE0"            /*  .$(.à$. ."à ... */
	$"1FF0 3FF8 3FF8 3FF8 3FF8 3FF8 3FF8 3FF8"            /* ..?.?.?.?.?.?.?. */
	$"3FF8 3FF8 3FF8 3FF8 3FF8 3FF8 3FF0 1FE0"            /* ?.?.?.?.?.?.?... */
};

data 'ics#' (129) {
	$"0000 3FF0 2018 3BB8 2008 27C8 2828 2828"            /* ..?. .;∏ .'»(((( */
	$"2828 2828 2828 27C8 2008 2108 3FF8 0000"            /* (((((('» .!.?... */
	$"FFFF FFFF FFFF FFFF FFFF FFFF FFFF FFFF"            /* ................ */
	$"FFFF FFFF FFFF FFFF FFFF FFFF FFFF FFFF"            /* ................ */
};

data 'ics#' (130) {
	$"7FE0 4030 4028 403C 4004 4544 4FE4 5BF4"            /* ..@0@(@<@.EDO.[. */
	$"4FE4 5FF4 4FE4 5FF4 4FE4 4544 4004 7FFC"            /* O._.O._.O.ED@... */
	$"7FE0 7FF0 7FF8 7FFC 7FFC 7FFC 7FFC 7FFC"            /* ................ */
	$"7FFC 7FFC 7FFC 7FFC 7FFC 7FFC 7FFC 7FFC"            /* ................ */
};

data 'ics#' (131) {
	$"7FE0 4030 5A28 5A3C 4004 56A4 56A4 4004"            /* ..@0Z(Z<@.V§V§@. */
	$"5B54 5B54 4004 55A4 55A4 4004 4004 7FFC"            /* [T[T@.U§U§@.@... */
	$"7FE0 7FF0 7FF8 7FFC 7FFC 7FFC 7FFC 7FFC"            /* ................ */
	$"7FFC 7FFC 7FFC 7FFC 7FFC 7FFC 7FFC 7FFC"            /* ................ */
};

data 'ics4' (128) {
	$"000F FFFF FFFF 0000 00FC CCCC CCCC F000"            /* ..........ÃÃÃÃ.. */
	$"00FC CFFF FFCC F000 00FC F111 11FC F000"            /* ..œ..Ã.......... */
	$"00FC F111 11FC F000 00FC F111 11FC F000"            /* ................ */
	$"00FC F111 11FC F000 00FC CFFF FFCC F000"            /* ..........œ..Ã.. */
	$"00FC CCCC CCCC F000 00FC CFCC CC4C F000"            /* ..ÃÃÃÃ....œÃÃL.. */
	$"00FC FFFC 4CCC F000 00FC CFCC CCCC F000"            /* ....LÃ....œÃÃÃ.. */
	$"00FC CCCC CCCC F000 00FC CCFC FCCC F000"            /* ..ÃÃÃÃ....Ã..Ã.. */
	$"00FC CCCC CCCF 0000 000F FFFF FFF0 0000"            /* ..ÃÃÃœ.......... */
};

data 'ics4' (129) {
	$"0000 0000 0000 0000 00FF FFFF FFFF 0000"            /* ................ */
	$"00FC CCCC CCCF F000 00FF FCFF FCFF F000"            /* ..ÃÃÃœ.......... */
	$"00FC CCCC CCCC F000 00FC CFFF FFCC F000"            /* ..ÃÃÃÃ....œ..Ã.. */
	$"00FC F000 50FC F000 00FC F050 00FC F000"            /* ....P......P.... */
	$"00FC F000 50FC F000 00FC F050 00FC F000"            /* ....P......P.... */
	$"00FC F000 50FC F000 00FC CFFF FFCC F000"            /* ....P.....œ..Ã.. */
	$"00FC CCCC CCCC F000 00FC CCCF CCCC F000"            /* ..ÃÃÃÃ....ÃœÃÃ.. */
	$"00FF FFFF FFFF F000 0000 0000 0000 0000"            /* ................ */
};

data 'ics4' (130) {
	$"0FFF FFFF FFF0 0000 0F0C 0C0C 0CFF 0000"            /* ................ */
	$"0FC0 C0C0 C0FC F000 0F0C 0C0C 0CFF FF00"            /* .¿¿¿¿........... */
	$"0FC0 C0C0 C0C0 CF00 0F0C 0F0F 0F0C 0F00"            /* .¿¿¿¿¿œ......... */
	$"0FC0 FFFF FFF0 CF00 0F0F F0FF FFFF 0F00"            /* .¿....œ......... */
	$"0FC0 FFFF FFF0 CF00 0F0F FFFF FFFF 0F00"            /* .¿....œ......... */
	$"0FC0 FFFF FFF0 CF00 0F0F FFFF FFFF 0F00"            /* .¿....œ......... */
	$"0FC0 FFFF FFF0 CF00 0F0C 0F0F 0F0C 0F00"            /* .¿....œ......... */
	$"0FC0 C0C0 C0C0 CF00 0FFF FFFF FFFF FF00"            /* .¿¿¿¿¿œ......... */
};

data 'ics4' (131) {
	$"0FFF FFFF FFF0 0000 0F0C 0C0C 0CFF 0000"            /* ................ */
	$"0FC6 6060 C0FC F000 0F06 6C6C 0CFF FF00"            /* .∆``¿.....ll.... */
	$"0FC0 C0C0 C0C0 CF00 0F06 066C FCFC 0F00"            /* .¿¿¿¿¿œ....l.... */
	$"0FC6 C660 F0F0 CF00 0F0C 0C0C 0C0C 0F00"            /* .∆∆`..œ......... */
	$"0FC6 6066 CFCF CF00 0F06 6C66 0F0F 0F00"            /* .∆`fœœœ...lf.... */
	$"0FC0 C0C0 C0C0 CF00 0F06 0603 3C3C 0F00"            /* .¿¿¿¿¿œ.....<<.. */
	$"0FC6 C6C3 3030 CF00 0F0C 0C0C 0C0C 0F00"            /* .∆∆√00œ......... */
	$"0FC0 C0C0 C0C0 CF00 0FFF FFFF FFFF FF00"            /* .¿¿¿¿¿œ......... */
};

data 'ics8' (128) {
	$"0000 00FF FFFF FFFF FFFF FFFF 0000 0000"            /* ................ */
	$"0000 FFF6 F6F6 F6F6 F6F6 F6F6 FF00 0000"            /* ................ */
	$"0000 FFF6 F6FF FFFF FFFF F6F6 FF00 0000"            /* ................ */
	$"0000 FFF6 FF29 2929 2929 FFF6 FF00 0000"            /* .....)))))...... */
	$"0000 FFF6 FF29 2929 2929 FFF6 FF00 0000"            /* .....)))))...... */
	$"0000 FFF6 FF29 2929 2929 FFF6 FF00 0000"            /* .....)))))...... */
	$"0000 FFF6 FF29 2929 2929 FFF6 FF00 0000"            /* .....)))))...... */
	$"0000 FFF6 F6FF FFFF FFFF F6F6 FF00 0000"            /* ................ */
	$"0000 FFF6 F6F6 F6F6 F6F6 F6F6 FF00 0000"            /* ................ */
	$"0000 FFF6 F6FF F6F6 F6F6 69F6 FF00 0000"            /* ..........i..... */
	$"0000 FFF6 FFFF FFF6 69F6 F6F6 FF00 0000"            /* ........i....... */
	$"0000 FFF6 F6FF F6F6 F6F6 F6F6 FF00 0000"            /* ................ */
	$"0000 FFF6 F6F6 F6F6 F6F6 F6F6 FF00 0000"            /* ................ */
	$"0000 FFF6 F6F6 FFF6 FFF6 F6F6 FF00 0000"            /* ................ */
	$"0000 FFF6 F6F6 F6F6 F6F6 F6FF 0000 0000"            /* ................ */
	$"0000 00FF FFFF FFFF FFFF FF00 0000 0000"            /* ................ */
};

data 'ics8' (129) {
	$"0000 0000 0000 0000 0000 0000 0000 0000"            /* ................ */
	$"0000 FFFF FFFF FFFF FFFF FFFF 0000 0000"            /* ................ */
	$"0000 FF2B 2B2B 2B2B 2B2B 2BFF FF00 0000"            /* ...++++++++..... */
	$"0000 FFFF FF2B FFFF FF2B FFFF FF00 0000"            /* .....+...+...... */
	$"0000 FF2B 2B2B 2B2B 2B2B 2B2B FF00 0000"            /* ...+++++++++.... */
	$"0000 FF2B 2BFF FFFF FFFF 2B2B FF00 0000"            /* ...++.....++.... */
	$"0000 FF2B FF2A 2A2A 2A2A FF2B FF00 0000"            /* ...+.*****.+.... */
	$"0000 FF2B FF2A 2A2A 2A2A FF2B FF00 0000"            /* ...+.*****.+.... */
	$"0000 FF2B FF2A 2A2A 2A2A FF2B FF00 0000"            /* ...+.*****.+.... */
	$"0000 FF2B FF2A 2A2A 2A2A FF2B FF00 0000"            /* ...+.*****.+.... */
	$"0000 FF2B FF2A 2A2A 2A2A FF2B FF00 0000"            /* ...+.*****.+.... */
	$"0000 FF2B 2BFF FFFF FFFF 2B2B FF00 0000"            /* ...++.....++.... */
	$"0000 FF2B 2B2B 2B2B 2B2B 2B2B FF00 0000"            /* ...+++++++++.... */
	$"0000 FF2B 2B2B 2BFF 2B2B 2B2B FF00 0000"            /* ...++++.++++.... */
	$"0000 FFFF FFFF FFFF FFFF FFFF FF00 0000"            /* ................ */
	$"0000 0000 0000 0000 0000 0000 0000 0000"            /* ................ */
};

data 'ics8' (130) {
	$"00FF FFFF FFFF FFFF FFFF FF00 0000 0000"            /* ................ */
	$"00FF F5F5 F5F5 F5F5 F5F5 FFFF 0000 0000"            /* ................ */
	$"00FF F5F5 F5F5 F5F5 F5F5 FF2B FF00 0000"            /* ...........+.... */
	$"00FF F5F5 F5F5 F5F5 F5F5 FFFF FFFF 0000"            /* ................ */
	$"00FF F5F5 F5F5 F5F5 F5F5 F5F5 F5FF 0000"            /* ................ */
	$"00FF F5F5 F5FF F5FF F5FF F5F5 F5FF 0000"            /* ................ */
	$"00FF F5F5 FFFF FFFF FFFF FFF5 F5FF 0000"            /* ................ */
	$"00FF F5FF FF00 FFFF FFFF FFFF F5FF 0000"            /* ................ */
	$"00FF F5F5 FFFF FFFF FFFF FFF5 F5FF 0000"            /* ................ */
	$"00FF F5FF FFFF FFFF FFFF FFFF F5FF 0000"            /* ................ */
	$"00FF F5F5 FFFF FFFF FFFF FFF5 F5FF 0000"            /* ................ */
	$"00FF F5FF FFFF FFFF FFFF FFFF F5FF 0000"            /* ................ */
	$"00FF F5F5 FFFF FFFF FFFF FFF5 F5FF 0000"            /* ................ */
	$"00FF F5F5 F5FF F5FF F5FF F5F5 F5FF 0000"            /* ................ */
	$"00FF F5F5 F5F5 F5F5 F5F5 F5F5 F5FF 0000"            /* ................ */
	$"00FF FFFF FFFF FFFF FFFF FFFF FFFF 0000"            /* ................ */
};

data 'ics8' (131) {
	$"00FF FFFF FFFF FFFF FFFF FF00 0000 0000"            /* ................ */
	$"00FF F5F5 F5F5 F5F5 F5F5 FFFF 0000 0000"            /* ................ */
	$"00FF F57F 7FF5 FFF5 F5F5 FF2B FF00 0000"            /* ...........+.... */
	$"00FF F57F 7FF5 FFF5 F5F5 FFFF FFFF 0000"            /* ................ */
	$"00FF F5F5 F5F5 F5F5 F5F5 F5F5 F5FF 0000"            /* ................ */
	$"00FF F57F F57F 7FF5 FFF5 FFF5 F5FF 0000"            /* ................ */
	$"00FF F57F F57F 7FF5 FFF5 FFF5 F5FF 0000"            /* ................ */
	$"00FF F5F5 F5F5 F5F5 F5F5 F5F5 F5FF 0000"            /* ................ */
	$"00FF F57F 7FF5 7F7F F5FF F5FF F5FF 0000"            /* ................ */
	$"00FF F57F 7FF5 7F7F F5FF F5FF F5FF 0000"            /* ................ */
	$"00FF F5F5 F5F5 F5F5 F5F5 F5F5 F5FF 0000"            /* ................ */
	$"00FF F57F F57F F569 69F5 69F5 F5FF 0000"            /* .......ii.i..... */
	$"00FF F57F F57F F569 69F5 69F5 F5FF 0000"            /* .......ii.i..... */
	$"00FF F5F5 F5F5 F5F5 F5F5 F5F5 F5FF 0000"            /* ................ */
	$"00FF F5F5 F5F5 F5F5 F5F5 F5F5 F5FF 0000"            /* ................ */
	$"00FF FFFF FFFF FFFF FFFF FFFF FFFF 0000"            /* ................ */
};

resource 'kind' (128) {
	'MGBE',
	0,
	{
		'GBRM', "Game Boy ROM image",
		'SRAM', "Game Boy battery save",
		'BLST', "Gray Brick block cache",
	}
};

resource 'mstr' (100) { "File" };
resource 'mstr' (101) { "Quit" };
resource 'mstr' (102) { "File" };
resource 'mstr' (103) { "Open ROM\0xC9" };

resource 'vers' (1) {
	0x02,
	0x32,
	release,
	0,
	0,
	"2.3.2",
	"2.3.2, \0xa9 Matthew Laux 2013-2026"
};

resource 'vers' (2) {
	0x02,
	0x32,
	release,
	0,
	0,
	"2.3.2",
	"constcast.org"
};
