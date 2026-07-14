#!/usr/bin/env bash
# Flash a GM SoundFont (.sf2) into the WerewolfAmyS3 "sf2" partition.
#
#   usage: scripts/flash_soundfont.sh <font.sf2> [port]
#
# The partition lives at 0x700000 and is 8 MB (see
# partitions_werewolf_16MB.csv) — the SoundFont must be <= 8388608 bytes.
set -euo pipefail

SF2="${1:?usage: $0 <font.sf2> [port]}"
PORT="${2:-}"
OFFSET=0x700000
MAX=8388608

SIZE=$(stat -c%s "$SF2" 2>/dev/null || stat -f%z "$SF2")
if [ "$SIZE" -gt "$MAX" ]; then
  echo "error: $SF2 is $SIZE bytes; the sf2 partition holds $MAX" >&2
  exit 1
fi

ARGS=(--chip esp32s3 --baud 921600)
[ -n "$PORT" ] && ARGS+=(--port "$PORT")

if command -v esptool.py >/dev/null; then ESPTOOL=esptool.py
elif command -v esptool >/dev/null;    then ESPTOOL=esptool
else
  echo "error: esptool not found (pip install esptool)" >&2
  exit 1
fi

echo "flashing $SF2 ($SIZE bytes) to $OFFSET ..."
"$ESPTOOL" "${ARGS[@]}" write_flash "$OFFSET" "$SF2"
echo "done — reboot the board; the SETUP page should show 'SoundFont: loaded'."
