#!/bin/sh
# Sends build/stackchan_usb_hub.bin to the hub over Wi-Fi.
# Usage: tools/ota.sh [host]   (default stackchan-hub.local)
# The token set with tools/wifi_setup.html is read from .ota-token.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
host=${1:-stackchan-hub.local}
image="$root/build/stackchan_usb_hub.bin"

if [ ! -f "$root/.ota-token" ]; then
  echo "missing $root/.ota-token (save the token from tools/wifi_setup.html)" >&2
  exit 1
fi
token=$(tr -d '[:space:]' < "$root/.ota-token")

curl --fail-with-body --silent --show-error --max-time 120 \
  -H "X-OTA-Token: $token" \
  -H "Content-Type: application/octet-stream" \
  --data-binary "@$image" \
  "http://$host/ota"
