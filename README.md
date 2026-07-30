# ![icon](icon_2x.png) Gray Brick - Game Boy emulator for Macintosh System 6

Gray Brick is a Game Boy emulator targeting 68k Macintosh with a focus on
execution speed. The emulator features a JIT compiler that translates Game Boy
code into 68k code for maximum speed.

This project does not make any attempt to be particularly accurate to the GB
hardware - I had to make too many compromises in order to get it working at all.
Despite this, many games work well and run fast enough to be enjoyable.

I have tested this on a Mac Plus with System 6.0.8, a Mac SE/30 with 7.1, and a
Mac IIfx with 7.5.3. A mid-range 68030 can get around 30 FPS with no sound and
frame skip, and a fast 68030 or 68040 can achieve real-time speeds with sound
enabled (and frame skip still on), depending on video settings. Original 8 MHz
68000 Macs get around 8 FPS :( but this is still better than my original
interpreter version which completed one frame every 5-6 seconds.

My goal with this project was to give vintage Mac enthusiasts another fun app
to check out.

![screenshot of gameplay](screenshots/1.png)

## System requirements

* Any 68k Macintosh (68030+ highly recommended)
* Some amount of RAM (see below)
* System 6.0.5 and up

This is a memory-intensive application, and the amount of memory required depends
on how big the games you want to play are. The simplest games require around 
2 MB, with larger games like Pokémon requiring 8 MB for the best performance.

If the emulator runs out of RAM, it will clear all compiled code and try again.
Since each code path is only compiled as it is reached, this gives things like
copyright screens, intro sequences, decompression code, etc. a chance to be
evicted.

If the "working set" of the game exceeds the available RAM, the emulator will
get into a loop of compiling -> clearing -> compiling -> clearing. If this
happens, you can hopefully just quit - I've taken care to ensure it doesn't
crash in this scenario.

Since this emulator translates Game Boy code into 68000 code and then executes
that code directly, anything that would crash the original game, including bugs
in my program, will crash your Mac! So I highly recommend having no other
applications open when using the emulator.

## Releases

Releases are available in the GitHub releases tab in MacBinary format.
Download the app, copy it somewhere on your machine, and run it.
You'll be asked to choose a .gb file (after you open it once, if it has a 
battery save it will be created and the type/creator codes will be set
so you can open it easily again!)

### Default key mappings

WASD for D-pad, L for A, K for B, M for Start, N for Select. Feel free to
customize in the Key Mappings dialog.

## Compatibility

### Games

I've listed a game as "works" if I can run it and play through the first couple
minutes at a reasonable frame rate.

#### Game Boy

| Game | Status |
| - | - |
| Tetris | Works |
| Dr. Mario | Works |
| Alleyway | Works |
| Pokémon Red/Blue | Works |
| Link's Awakening | Works |
| Kaeru no Tame ni Kane wa Naru | Works (disable STAT interrupts for slow parts) |
| Super Mario Land | Works |
| Super Mario Land 2 | Works |
| Metroid II | Works |
| Donkey Kong '94 | Works |
| Final Fantasy Legend | Works |
| Final Fantasy Adventure | Works |
| Everything else | No idea |

#### Game Boy Color

| Game | Status |
| - | - |
| Tetris DX | Works |
| Pokémon Gold/Silver | Works (disable STAT interrupts) |
| Pokémon Crystal | Works (disable STAT interrupts) |
| Link's Awakening DX | Works |
| Dragon Warrior I & II | Works |
| Dragon Warrior III | Works |
| Super Mario Bros. Deluxe | Works |
| Everything else | No idea |

If you try a game, please report whether it works (playable/unplayable but no
crash/crashes the Mac).
If it does NOT work, open an issue and I'll decide whether to fix it based on
how difficult it is to fix :)

### Memory bank controllers

MBCs 1-5 are all supported, including battery saves. Saves are stored in files
matching the internal ROM name (for example, "ZELDA" for Link's Awakening).

## Limitations

* ~~Audio sounds like crap on System 6~~ fixed in 2.1.0
  - ~~If anyone knows how to use `SndPlayDoubleBuffer()` on System 6 and not~~
    ~~have a ton of popping noises, let me know~~
  - ~~Chances are if your machine is fast enough to try audio you're on System 7~~
    ~~anyway~~
* Memory management could be better
  - Clear unused blocks instead of everything
* ~~LCD rendering is... yeah (see below)~~ this is better now in 2.0.0!

## Options

![screenshot of options menu](screenshots/4.png) ![screenshot of key mappings dialog](screenshots/3.png)

### Enable Audio

I would say a 68030 at 25 MHz is the minimum requirement for smooth audio
emulation.

This option requires either Sound Manager 3.0 (best) or the "enhanced Sound Manager",
and an Apple Sound Chip. Pretty much all Macs except the original compacts have the ASC. 
System 6, 7.0, and 7.1 come with the "enhanced Sound Manager" which in my experience
leads to a lot of clicking in the audio. If you're getting clicking, install
the Sound Manager 3.0 extension and it should sound better. System 7.5 and up
already have Sound Manager 3.0.

### Enable STAT Interrupts

#### Background info on rendering

A real Game Boy is constantly visiting each pixel on the screen, reading the
state of video RAM and hardware registers at the moment that pixel is reached,
and using this information to decide what color it should be. When the bottom
of the screen is reached, there is a short period of time where the program can
transfer new image data to video RAM and move sprites around before the drawing
process starts again. When this period starts, the program is interrupted
and the CPU jumps to an interrupt handler where it makes its updates.

This process is prohibitively slow on a 68k Mac, so instead, a log is kept of
each time the LCD parameters (scroll, palettes, etc.) are changed throughout
the frame, then rendered all at once at the end based on how the registers
were set at each logged point.

#### So what

Games that update the LCD parameters mid-frame (usually for status bars or
special effects) use STAT interrupts to achieve this. This increases the
amount of work the emulator has to do.

Leave this option on by default and turn it off if the frame rate tanks.
In the worst case, some games (Pokémon GSC is an example) fire STAT
interrupts on every line (144 times per frame), even when no special effect
is active, which is way too slow on 68k. You'll want to turn this option off
for games that do this.

### Limit to 60 FPS

This is pretty much for 68040s only :)

### Frame skip

Copying the Game Boy pixels to your Mac's screen is a pretty expensive operation.
This preference allows one in every 2, 3, 4, or 5 frames to actually be drawn
to the screen. Skipped frames are still emulated - only the actual drawing is
skipped. This can drastically increase performance on slow machines.

### Scale

Choose between 1x or 2x rendering based on your preference, 2x is slower.

### Run as GBC

For games that support both the original Game Boy and the Game Boy Color, this
option allows you to choose which system you want to run it as. 

## Building

This project uses the Retro68 GCC port. Do this:

```
#!/bin/sh
mkdir -p build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../../Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
make
cd ..
```

...replacing the path to `CMAKE_TOOLCHAIN_FILE` with wherever you have it installed.
Or run `system6/compile.sh` which contains exactly that code.

## Project structure

Here are some interesting files:

* `system6/`
  - `emulator.c` - entry point, event handling, offscreen buffer setup
  - `jit.c` - JIT context setup, calls into compiler, glue code for generated
    machine code
  - `dispatcher_asm.c` - machine language routines for chaining between blocks
    and patching blocks to jump directly to the next one
  - `lcd_mac.c` - output functions for B&W and color monitors
  - `audio_mac.c` - hooks up GB audio driver to the Sound Manager
  - `dialogs.c` - open dialog, preferences dialog, key mappings dialog, alert dialogs

* `src/`
  - `dmg.c` - memory read/write functions, general "sync hardware" function
  - `lcd.c` - background and sprite rendering
  - `mbc.c` - memory bank controllers
  - `audio.c` - audio driver

* `compiler/`
  - `compiler.c` - entry point

The separation between `src/` and `system6/` is largely a carryover from when
I had an ImGui debug version that ran on modern OSes while developing the
interpreter version of the emulator.

## LLM statement

I used Claude Code to generate compiler tests, quickly generate 68k instruction
emitter functions, and heavily comment my emitted instruction sequences with
the corresponding assembly syntax and offsets, because they're confusing.

All LLM usage was done by individually reviewing every edit before accepting,
there was no use of autonomous agents or vibe coding.

* `src/`, `system6/`, and `compiler/` (main app) are ~0% generated
* `compiler/tests/` is ~80% generated
* `host/` (test harness for running on modern machines) is ~100% generated

## License

GNU GPL version 3.
