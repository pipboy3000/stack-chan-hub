// Base LEDs: which host is selected, and what needs attention.
//
// Left side: the input path. Right side: the supply. Both show the colour of
// the selected host while all is well, so red always means "look at the LCD".
// Pure logic (no M5 or I/O) so it runs in host unit tests.
#pragma once

#include <cstdint>

namespace led {

constexpr int kSlotCount = 3;

struct Color {
  uint8_t r, g, b;
  constexpr bool operator==(const Color& other) const {
    return r == other.r && g == other.g && b == other.b;
  }
};

constexpr Color kOff{0, 0, 0};
constexpr Color kRed{32, 0, 0};
// One colour per host; none of them close to red.
constexpr Color kHostColors[kSlotCount] = {
    {0, 0, 32},     // blue
    {0, 32, 0},     // green
    {20, 20, 20},   // white
};
constexpr uint32_t kBlinkPeriodMs = 1000;

struct Pattern {
  Color left;
  Color right;
  constexpr bool operator==(const Pattern& other) const {
    return left == other.left && right == other.right;
  }
};

struct Input {
  uint8_t slot;
  bool connected;         // BLE link to the selected host is up
  bool keyboard_missing;  // USB host runs, but no keyboard is attached
  bool error;             // USB start or BLE management failed
  bool on_battery;        // neither external supply nor charging
  bool screen_off;        // idle: everything dark
};

constexpr Pattern Compute(const Input& input, uint32_t now_ms) {
  if (input.screen_off) {
    return {kOff, kOff};
  }
  // Not connected (switching, pairing, host asleep): the host colour blinks.
  const bool lit = input.connected || now_ms % kBlinkPeriodMs < kBlinkPeriodMs / 2;
  const Color host =
      lit ? kHostColors[input.slot < kSlotCount ? input.slot : 0] : kOff;
  return {input.keyboard_missing || input.error ? kRed : host,
          input.on_battery ? kRed : host};
}

}  // namespace led
