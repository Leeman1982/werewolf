#!/usr/bin/env bash
# ============================================================================
#  tests/build_s3.sh — compile the sketch for the ESP32-S3, three ways.
#
#  tests/run.sh builds and runs the DSP on the host; this closes the other
#  half by compiling EVERY translation unit, the hardware layer included,
#  with the real arduino-esp32 toolchain, in the three board configurations
#  that change what gets compiled:
#    1. USB-OTG (TinyUSB) + OPI PSRAM   -- the recommended setup, USB DRIVE on
#    2. Hardware CDC/JTAG + OPI PSRAM   -- USB DRIVE compiled out
#    3. PSRAM disabled                  -- builds; stops at boot with a message
#
#  Needs arduino-cli with the esp32 core (3.x) and the U8g2 library:
#    arduino-cli core install esp32:esp32
#    arduino-cli lib install U8g2
#  ARDUINO_CLI overrides the binary (default: arduino-cli on PATH).
# ============================================================================
set -u
cd "$(dirname "$0")/.."
ROOT="$PWD"
CLI="${ARDUINO_CLI:-arduino-cli}"
BASE="esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CPUFreq=240"
fail=0
for v in "USBMode=default,CDCOnBoot=cdc,PSRAM=opi" \
         "USBMode=hwcdc,CDCOnBoot=cdc,PSRAM=opi" \
         "USBMode=default,CDCOnBoot=default,PSRAM=disabled"; do
    echo "== $v"
    out="$("$CLI" compile --fqbn "$BASE,$v" --warnings default \
            --library "$ROOT/extras/Synth_Dexed" \
            --build-path "${TMPDIR:-/tmp}/wolf-build-${v//[,=]/_}" "$ROOT" 2>&1)"
    rc=$?
    echo "$out" | grep -E ' error|warning: |Sketch uses|Global variables' \
                | grep -v 'Synth_Dexed/src' || true
    [ $rc -eq 0 ] || { echo "   FAILED"; fail=1; }
done
[ $fail -eq 0 ] && echo "ALL BUILDS OK" || echo "BUILD FAILURES"
exit $fail
