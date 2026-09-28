#!/usr/bin/env bash
# ============================================================================
#  tests/syntax_check.sh — type-check the HARDWARE layer on the host.
#
#  tests/run.sh builds and *runs* the engine DSP natively, but it can only do
#  that by excluding everything hardware-bound: display.h, ui.cpp, controls.*,
#  midi.cpp, storage.cpp and the sketch itself never see a compiler until the
#  Arduino IDE gets them. That gap has shipped at least one build break (a
#  U8g2 panel class has no default constructor, so `U8g2Real _u8g2;` in
#  display.h was a deleted-function error the host tests could never notice).
#
#  This script closes it: every translation unit, including the .ino, is run
#  through `g++ -fsyntax-only` against the REAL U8g2 and Synth_Dexed headers
#  plus a declarations-only shim for the arduino-pico core. Nothing is linked
#  or run -- the point is purely that the types, method signatures,
#  constructor arguments and `u8g2_font_*` symbol names are all real.
#
#  Note the two shim directories are deliberately separate and not
#  interchangeable: tests/shim/ is the tiny DSP-only Arduino stand-in run.sh
#  uses, while tests/hwshim/ here is the wider host-side core (pinMode,
#  analogRead, Wire, I2S, FatFS, rp2040, the Pico SDK macros) that only the
#  hardware layer needs.
#
#  Usage: tests/syntax_check.sh          (clones U8g2 into tests/.u8g2 once)
# ============================================================================
set -u
cd "$(dirname "$0")/.."
ROOT="$PWD"
U8G2="$ROOT/tests/.u8g2"

if [ ! -d "$U8G2" ]; then
    echo "fetching U8g2 headers into tests/.u8g2 ..."
    git clone --depth 1 -q https://github.com/olikraus/u8g2.git "$U8G2" || {
        echo "FAILED to clone U8g2 -- syntax check needs its real headers."; exit 1; }
fi

CXXFLAGS="-fsyntax-only -std=gnu++17 -DARDUINO=200 -Wall -Wextra"
# -isystem, not -I, for the two vendored third-party trees: it keeps their own
# warnings out of a report that exists to show THIS project's diagnostics.
INCS="-isystem $U8G2/cppsrc -isystem $U8G2/csrc \
      -isystem $ROOT/extras/Synth_Dexed/src \
      -I $ROOT/tests/hwshim -I $ROOT"

# The .ino is C++ but g++ won't treat that extension as such; the Arduino
# build's own prototype auto-generation is not needed here because the sketch
# already declares everything before use.
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cp "$ROOT/CarpenterSynth.ino" "$TMP/CarpenterSynth_ino.cpp"

fail=0
for f in "$ROOT"/*.cpp "$TMP/CarpenterSynth_ino.cpp"; do
    name="$(basename "$f")"
    out="$(g++ $CXXFLAGS $INCS "$f" 2>&1)"
    if [ -n "$out" ]; then
        echo "--- $name"
        echo "$out"
        echo "$out" | grep -q ' error: ' && fail=1
    fi
done

if [ $fail -ne 0 ]; then
    echo "SYNTAX CHECK FAILED"
    exit 1
fi
echo "syntax check OK — all translation units and the sketch type-check against real U8g2"
