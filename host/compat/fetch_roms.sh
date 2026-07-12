#!/bin/bash
# downloads the test ROMs the compat scoreboard uses into compat/roms/.
# game ROMs are expected in the repo root and are not fetched here.
set -e
cd "$(dirname "$0")"
mkdir -p roms/blargg roms/mooneye roms/acid2
cd roms

BLARGG_BASE="https://gbdev.gg8.se/files/roms/blargg-gb-tests"
for t in cpu_instrs instr_timing mem_timing; do
    if [ ! -f "blargg/$t.gb" ]; then
        echo "fetching blargg/$t"
        curl -sL "$BLARGG_BASE/$t.zip" -o /tmp/gb6_$t.zip
        unzip -o -q -j /tmp/gb6_$t.zip "*$t.gb" -d blargg/
        rm /tmp/gb6_$t.zip
    fi
done

MTS="mts-20240926-1737-443f6e1"
if [ ! -d "mooneye/acceptance" ]; then
    echo "fetching mooneye $MTS"
    curl -sL "https://gekkio.fi/files/mooneye-test-suite/$MTS/$MTS.tar.xz" \
        -o /tmp/gb6_mts.tar.xz
    tar -xf /tmp/gb6_mts.tar.xz -C mooneye --strip-components 1
    rm /tmp/gb6_mts.tar.xz
fi

if [ ! -f "acid2/dmg-acid2.gb" ]; then
    echo "fetching dmg-acid2"
    curl -sL "https://github.com/mattcurrie/dmg-acid2/releases/download/v1.0/dmg-acid2.gb" \
        -o acid2/dmg-acid2.gb
fi

echo "roms ready"
