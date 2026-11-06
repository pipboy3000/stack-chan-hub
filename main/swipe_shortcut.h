#pragma once

#include <cstdint>
#include "touchpad_gestures.h"

namespace swipe_shortcut {
struct Shortcut {
  uint8_t modifiers;
  uint8_t key;
};

// Slots are Mac mini, Linux, and MacBook Air, in that order.
constexpr Shortcut For(uint8_t slot, touchpad::Swipe swipe) {
  const bool back = swipe == touchpad::Swipe::kRight;
  return slot == 1 ? Shortcut{0x04, static_cast<uint8_t>(back ? 0x50 : 0x4f)}
                   : Shortcut{0x08, static_cast<uint8_t>(back ? 0x2f : 0x30)};
}
}  // namespace swipe_shortcut
