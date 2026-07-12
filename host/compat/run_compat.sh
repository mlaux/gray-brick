#!/bin/bash
# gb6 compatibility scoreboard (PLAN.md phase 1 milestone 4).
#
# runs blargg serial tests, mooneye acceptance tests (pass = fibonacci in
# B..L), and game/rendering frame-hash tests through gb6run, in both
# dispatcher modes. output is deterministic text, one line per test, meant
# to be committed as baseline.txt and diffed by `make compat`.
#
# game tests record the SET of distinct frame hashes over a trailing
# window: screens that oscillate between renderings (frame-at-a-time
# strategy, see dmg-acid2) produce a stable multi-hash set instead of a
# flaky single hash.

cd "$(dirname "$0")"
GB6RUN=../gb6run
ROOT=../..
OUT="${1:-results.txt}"

# dispatcher configs; divergence between them is itself a finding
CONFIGS="${CONFIGS:-default chain}"
CPU="${CPU:-68020}"

: > "$OUT"

cfg_args() {
    [ "$1" = chain ] && echo "--chain"
}

emit() {
    echo "$@" >> "$OUT"
    echo "$@"
}

# blargg: run to a fixed frame cap, report the serial text
blargg_test() {
    local cfg=$1 name=$2 rom=$3 frames=$4
    if [ ! -f "$rom" ]; then
        emit "[$cfg] blargg/$name SKIP (rom missing)"
        return
    fi
    local serial
    serial=$($GB6RUN "$rom" --frames "$frames" --serial \
        --until-serial "Passed" $(cfg_args "$cfg") 2>/dev/null \
        | tr '\n' ' ' | tr -s ' ')
    emit "[$cfg] blargg/$name: $serial"
}

# mooneye: fixed frames, pass = fib sequence in B..L
mooneye_test() {
    local cfg=$1 name=$2 rom=$3
    if [ ! -f "$rom" ]; then
        emit "[$cfg] mooneye/$name SKIP (rom missing)"
        return
    fi
    local state status
    state=$($GB6RUN "$rom" --frames 300 --dump-state $(cfg_args "$cfg") \
        2>/dev/null | grep '^state')
    status=$?
    if [ -z "$state" ]; then
        emit "[$cfg] mooneye/$name CRASH ($status)"
    elif echo "$state" | grep -q 'BC=0305 DE=080d HL=1522'; then
        emit "[$cfg] mooneye/$name PASS"
    else
        emit "[$cfg] mooneye/$name FAIL $(echo "$state" \
            | sed 's/state //; s/ IE=.*//')"
    fi
}

# game/rendering: distinct frame hashes over the last <window> rendered
# frames, sorted - stable for static AND oscillating screens
hashset_test() {
    local cfg=$1 name=$2 rom=$3 frames=$4 window=$5
    if [ ! -f "$rom" ]; then
        emit "[$cfg] game/$name SKIP (rom missing)"
        return
    fi
    local hashes
    hashes=$($GB6RUN "$rom" --frames "$frames" --hash-frames \
        $(cfg_args "$cfg") 2>/dev/null | tail -n "$window" \
        | awk '{print $3}' | sort -u | paste -sd, -)
    emit "[$cfg] game/$name hashes=$hashes"
}

# same, but with scripted joypad input
input_test() {
    local cfg=$1 name=$2 rom=$3 script=$4 frames=$5 window=$6
    if [ ! -f "$rom" ]; then
        emit "[$cfg] input/$name SKIP (rom missing)"
        return
    fi
    local hashes
    hashes=$($GB6RUN "$rom" --frames "$frames" --hash-frames \
        --input "$script" $(cfg_args "$cfg") 2>/dev/null \
        | tail -n "$window" | awk '{print $3}' | sort -u | paste -sd, -)
    emit "[$cfg] input/$name hashes=$hashes"
}

for cfg in $CONFIGS; do
    emit "== config: $CPU $cfg =="

    blargg_test "$cfg" cpu_instrs   roms/blargg/cpu_instrs.gb   4500
    blargg_test "$cfg" instr_timing roms/blargg/instr_timing.gb 1200
    blargg_test "$cfg" mem_timing   roms/blargg/mem_timing.gb   1500

    for t in \
        acceptance/instr/daa \
        acceptance/bits/reg_f \
        acceptance/if_ie_registers \
        acceptance/ei_timing \
        acceptance/halt_ime0_ei \
        acceptance/halt_ime1_timing \
        acceptance/rapid_di_ei \
        acceptance/timer/div_write \
        acceptance/timer/tima_reload \
        acceptance/ppu/intr_2_mode0_timing \
        acceptance/ppu/stat_irq_blocking \
        acceptance/ppu/lcdon_timing-GS \
        ; do
        mooneye_test "$cfg" "$t" "roms/mooneye/$t.gb"
    done

    hashset_test "$cfg" dmg-acid2 roms/acid2/dmg-acid2.gb 300 60

    hashset_test "$cfg" tetris   "$ROOT/tetris.gb"    1500 120
    hashset_test "$cfg" dk94     "$ROOT/dk.gb"        1200 120
    hashset_test "$cfg" drmario  "$ROOT/drmario.gb"   900  120
    hashset_test "$cfg" zeldadx  "$ROOT/zeldadx.gb"   900  120
    hashset_test "$cfg" sml2     "$ROOT/sml2.gb"      900  120
    hashset_test "$cfg" pokegold "$ROOT/pokegold.gbc" 900  120
    hashset_test "$cfg" dw3      "$ROOT/dw3.gbc"      900  120
    hashset_test "$cfg" ffl      "$ROOT/ffl.gb"       600  120

    input_test "$cfg" tetris-start "$ROOT/tetris.gb" scripts/tetris_start.txt 1000 100
    input_test "$cfg" ffl-start    "$ROOT/ffl.gb"    scripts/ffl_start.txt    600  100
done
