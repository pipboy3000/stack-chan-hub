#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
component_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
binary="$script_dir/gestures_test"
trap 'rm -f "$binary"' EXIT HUP INT TERM

c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I "$component_dir/include" \
  "$component_dir/src/touchpad_gestures.cpp" \
  "$script_dir/gestures_test.cpp" \
  -o "$binary"
"$binary"
