#!/bin/sh
set -e
cd "$(dirname "$0")"
rgbgfx -u -o tiles.2bpp tiles.png
for f in welcome; do
    rgbasm -i . -o "$f.o" "$f.asm"
    rgblink -o "$f.gb" "$f.o"
    rgbfix -v -p 0xff "$f.gb"
    rm -f "$f.o"
done
