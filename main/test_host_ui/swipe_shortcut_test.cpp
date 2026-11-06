#include <cassert>
#include <initializer_list>
#include "swipe_shortcut.h"

int main() {
  using touchpad::Swipe;
  for (const uint8_t slot : {uint8_t{0}, uint8_t{2}}) {
    const auto back = swipe_shortcut::For(slot, Swipe::kRight);
    const auto forward = swipe_shortcut::For(slot, Swipe::kLeft);
    assert(back.modifiers == 0x08 && back.key == 0x2f);
    assert(forward.modifiers == 0x08 && forward.key == 0x30);
  }
  const auto back = swipe_shortcut::For(1, Swipe::kRight);
  const auto forward = swipe_shortcut::For(1, Swipe::kLeft);
  assert(back.modifiers == 0x04 && back.key == 0x50);
  assert(forward.modifiers == 0x04 && forward.key == 0x4f);
}
