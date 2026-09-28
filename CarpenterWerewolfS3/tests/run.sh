#!/bin/sh
# Host-side test harness for the parts of the firmware that are pure logic:
# DSP tables, effects, sequencer, and every synth engine's DSP + panel-as-
# data/adapter layer -- all seventeen engines: CS-80, Minimoog, Prophet-5,
# Prophet-10, ARP 2600, ARP Odyssey, Mellotron, Oberheim Four-Voice/SEM,
# Jupiter-8, System 100, the Moog Modular wildcard, Oberheim OB-X, PPG
# Wave 2.3, Korg MS-20, Yamaha DX7, ARP/Solina String Ensemble and
# Sequential Prophet-VS (see engine.cpp's file banner for the full roster --
# nothing is staged/stubbed any more).
#
# These translation units only need a handful of Arduino symbols, which
# shim/Arduino.h supplies, so they build and run natively on any machine — no
# board, no toolchain, no upload cycle.  Everything hardware-bound (I2S, U8g2,
# FatFS, MIDI PIO-UART) is deliberately out of scope here: it is type-checked
# against the real libraries by tests/syntax_check.sh, which compiles every
# translation unit INCLUDING ui.cpp, controls.cpp and the sketch, and is
# covered end to end by building for the target.
#
# Built with ASan+UBSan and -Wextra -Wshadow on every run, not just as a
# one-off check: this is the only sanitizer coverage this codebase gets
# without real hardware, so it stays on permanently rather than being a
# manual step someone has to remember to run.
#
#   ./tests/run.sh
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
SRC=$DIR/..

# ── Vendored Synth_Dexed (the DX7 engine) ───────────────────────────────────
# Built once into tests/.dexed/ and cached, because it is ~17 translation
# units of third-party code that never changes between runs.
#
# Warnings off (-w) and UndefinedBehaviorSanitizer off -- ASan stays on.  Both
# exclusions are for the same reason and neither is a shrug:
#
#  - MSFA's FM core is fixed-point DSP that deliberately relies on WRAPPING
#    signed arithmetic (fm_op_kernel.cpp's phase accumulation, dx7note.cpp's
#    level maths).  That is undefined behaviour by the letter of the standard
#    and correct-by-construction in practice on every compiler this will ever
#    see; UBSan reports it every audio block, which would bury this project's
#    own diagnostics.  One non-arithmetic report is also real and also benign:
#    Dexed's constructor calls setMonoMode(false), which reads `monoMode`
#    before assigning it -- both branches of that read end with mono off.
#  - -Wall -Wextra -Wshadow on vendored upstream source produces hundreds of
#    lines about someone else's code.
#
# ASan is kept because it catches the class of bug that WOULD matter here: a
# buffer this project sized wrong being overrun by the library.
DEXED=$SRC/extras/Synth_Dexed/src
DEXOBJ=$DIR/.dexed
SAN="-fsanitize=address -fno-omit-frame-pointer"
mkdir -p "$DEXOBJ"
for f in "$DEXED"/*.cpp; do
    o="$DEXOBJ/$(basename "$f").o"
    [ -f "$o" ] && [ "$o" -nt "$f" ] ||         g++ -c -g -O1 $SAN -std=gnu++17 -w -I "$DIR/shim" -I "$DEXED" -o "$o" "$f"
done
o="$DEXOBJ/arm_math.c.o"
[ -f "$o" ] && [ "$o" -nt "$DEXED/arm_math.c" ] ||     gcc -c -g -O1 $SAN -w -I "$DIR/shim" -I "$DEXED" -o "$o" "$DEXED/arm_math.c"

g++ -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
    -std=gnu++17 -Wall -Wextra -Wshadow \
    -I "$DIR/shim" -I "$DEXED" -I "$SRC" -o "$DIR/test" \
    "$DIR/test.cpp" \
    "$SRC/dsp_tables.cpp" "$SRC/cs80_params.cpp" "$SRC/cs80_voice.cpp" \
    "$SRC/sequencer.cpp" "$SRC/fx.cpp" \
    "$SRC/engine.cpp" "$SRC/engine_cs80.cpp" "$SRC/minimoog.cpp" \
    "$SRC/prophet5.cpp" "$SRC/prophet10.cpp" "$SRC/arp2600.cpp" "$SRC/odyssey.cpp" \
    "$SRC/mellotron.cpp" \
    "$SRC/oberheim.cpp" "$SRC/jupiter8.cpp" "$SRC/system100.cpp" "$SRC/moogmodular.cpp" \
    "$SRC/obx.cpp" "$SRC/ppgwave.cpp" \
    "$SRC/ms20.cpp" "$SRC/dx7.cpp" "$SRC/dx7_rom.cpp" \
    "$SRC/stringens.cpp" "$SRC/prophetvs.cpp" \
    "$SRC/stub_engine.cpp" "$DEXOBJ"/*.o -lm
"$DIR/test"
