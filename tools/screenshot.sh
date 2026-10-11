#!/bin/sh
# Saves the hub's LCD canvas (GET /screen, raw RGB565) as a PNG.
# Usage: tools/screenshot.sh [host] [out.png]   (default stackchan-hub.local)
set -eu

host=${1:-stackchan-hub.local}
out=${2:-screen.png}
raw=$(mktemp "${TMPDIR:-/tmp}/hub-screen.XXXXXX")
trap 'rm -f "$raw" "$raw.bmp"' EXIT HUP INT TERM

curl --fail --silent --show-error --max-time 10 -o "$raw" "http://$host/screen"
python3 - "$raw" "$raw.bmp" <<'PY'
import struct, sys
width, height = 320, 240
raw = open(sys.argv[1], "rb").read()
assert len(raw) == width * height * 2, len(raw)
rows = []
for y in range(height - 1, -1, -1):  # BMP rows are bottom-up
    row = bytearray()
    for x in range(width):
        value = (raw[(y * width + x) * 2] << 8) | raw[(y * width + x) * 2 + 1]
        row += bytes(((value & 0x1F) << 3, ((value >> 5) & 0x3F) << 2,
                      (value >> 11) << 3))
    rows.append(bytes(row))
pixels = b"".join(rows)
header = struct.pack("<2sIHHIIiiHHIIiiII", b"BM", 54 + len(pixels), 0, 0, 54,
                     40, width, height, 1, 24, 0, len(pixels), 2835, 2835, 0, 0)
open(sys.argv[2], "wb").write(header + pixels)
PY
sips -s format png "$raw.bmp" --out "$out" >/dev/null
echo "$out"
