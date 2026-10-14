#!/bin/sh
set -eu
task_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
binary=$(mktemp "${TMPDIR:-/tmp}/ui-model-test.XXXXXX")
trap 'rm -f "$binary"' EXIT HUP INT TERM

for test in ui_model_test led_model_test; do
  c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I "$task_dir/.." "$task_dir/$test.cpp" -o "$binary"
  "$binary"
done
