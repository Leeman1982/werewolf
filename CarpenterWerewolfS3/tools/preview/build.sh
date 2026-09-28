#!/bin/sh
# Render every panel screen to tools/preview/out/*.png through the firmware's
# real ui.cpp + gfx.cpp (see preview.cpp). Needs g++, python3 + Pillow, and
# the U8g2 C sources: taken from tests/.u8g2 (tests/syntax_check.sh clones it)
# or cloned here on first use.
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
SRC=$DIR/../..
U8G2=$SRC/tests/.u8g2
[ -d "$U8G2" ] || git clone --depth 1 -q https://github.com/olikraus/u8g2.git "$U8G2"
CLIB=$U8G2/csrc

OBJ=$DIR/build
mkdir -p "$OBJ" "$DIR/out"
for f in "$CLIB"/u8g2_*.c "$CLIB"/u8x8_*.c; do
    o="$OBJ/$(basename "$f").o"
    [ -f "$o" ] && [ "$o" -nt "$f" ] || gcc -c -O1 -w -I "$CLIB" -o "$o" "$f"
done

# The DX7 engine's Synth_Dexed objects, as tests/run.sh builds them.
DEXED=$SRC/extras/Synth_Dexed/src
for f in "$DEXED"/*.cpp; do
    o="$OBJ/dexed_$(basename "$f").o"
    [ -f "$o" ] && [ "$o" -nt "$f" ] || g++ -c -O1 -std=gnu++17 -w -I "$SRC/tests/shim" -I "$DEXED" -o "$o" "$f"
done
o="$OBJ/dexed_arm_math.c.o"
[ -f "$o" ] && [ "$o" -nt "$DEXED/arm_math.c" ] || gcc -c -O1 -w -I "$SRC/tests/shim" -I "$DEXED" -o "$o" "$DEXED/arm_math.c"

g++ -O1 -g -std=gnu++17 -Wall -Wextra -Wno-unused-parameter \
    -I "$SRC/tests/shim" -I "$CLIB" -isystem "$DEXED" -I "$SRC" -o "$OBJ/preview" \
    "$DIR/preview.cpp" "$SRC/ui.cpp" "$SRC/gfx.cpp" \
    "$SRC/dsp_tables.cpp" "$SRC/cs80_params.cpp" "$SRC/cs80_voice.cpp" \
    "$SRC/sequencer.cpp" "$SRC/fx.cpp" "$SRC/arps.cpp" \
    "$SRC/engine.cpp" "$SRC/engine_cs80.cpp" "$SRC/minimoog.cpp" \
    "$SRC/prophet5.cpp" "$SRC/prophet10.cpp" "$SRC/arp2600.cpp" "$SRC/odyssey.cpp" \
    "$SRC/mellotron.cpp" "$SRC/oberheim.cpp" "$SRC/jupiter8.cpp" "$SRC/system100.cpp" \
    "$SRC/moogmodular.cpp" "$SRC/obx.cpp" "$SRC/ppgwave.cpp" \
    "$SRC/ms20.cpp" "$SRC/dx7.cpp" "$SRC/dx7_rom.cpp" \
    "$SRC/stringens.cpp" "$SRC/prophetvs.cpp" "$SRC/stub_engine.cpp" \
    "$OBJ"/u8*.o "$OBJ"/dexed_*.o -lm

cd "$DIR" && "$OBJ/preview"
python3 - "$DIR/out" <<'EOF'
import sys, glob, os
from PIL import Image
d = sys.argv[1]
scale = int(os.environ.get('SCALE', '2'))
for p in sorted(glob.glob(os.path.join(d, '*.ppm'))):
    im = Image.open(p)
    im.resize((im.width * scale, im.height * scale), Image.NEAREST).save(p[:-4] + '.png')
    os.remove(p)
EOF
echo "wrote $DIR/out/*.png"
