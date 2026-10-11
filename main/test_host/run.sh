#!/bin/sh
set -eu
task_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
task_tmp=$(mktemp -d "${TMPDIR:-/tmp}/ble-host-tests.XXXXXX")
trap 'rm -rf "$task_tmp"' EXIT HUP INT TERM

# All fake SDK includes share one small fixture; production source is compiled
# unchanged and resolves its SDK headers from this disposable include tree.
for header in esp_hidd.h esp_log.h freertos/FreeRTOS.h host/ble_att.h \
  host/ble_hs.h host/ble_store.h host/util/util.h nimble/nimble_port.h \
  nvs.h nimble/nimble_port_freertos.h services/gap/ble_svc_gap.h \
  store/config/ble_store_config.h; do
  mkdir -p "$task_tmp/include/$(dirname "$header")"
  printf '#include "ble_keyboard_fake_sdk.h"\n' > "$task_tmp/include/$header"
done

for test in peer_slots ble_keyboard; do
  c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I "$task_tmp/include" -I "$task_dir" -I "$task_dir/.." \
    "$task_dir/${test}_test.cpp" -o "$task_tmp/$test"
  "$task_tmp/$test"
done
