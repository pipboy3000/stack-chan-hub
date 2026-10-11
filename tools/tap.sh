#!/bin/sh
# Taps the hub's LCD at (x, y) over Wi-Fi. Usage: tools/tap.sh x y [host]
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
token=$(tr -d '[:space:]' < "$root/.ota-token")
curl --fail-with-body --silent --show-error --max-time 10 -X POST \
  -H "X-OTA-Token: $token" "http://${3:-stackchan-hub.local}/tap?x=$1&y=$2"
