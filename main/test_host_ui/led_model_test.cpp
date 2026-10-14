#include <cstdio>

#include "led_model.h"

namespace {

using namespace led;

int failures = 0;

#define CHECK(condition)                                              \
  do {                                                                \
    if (!(condition)) {                                               \
      std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #condition); \
      ++failures;                                                     \
    }                                                                 \
  } while (false)

Input ok(uint8_t slot) {
  Input input{};
  input.slot = slot;
  input.connected = true;
  return input;
}

void test_host_colour_on_both_sides() {
  for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
    const Pattern pattern = Compute(ok(slot), 750);
    CHECK(pattern.left == kHostColors[slot]);
    CHECK(pattern.right == kHostColors[slot]);
    CHECK(!(kHostColors[slot] == kRed));
  }
  CHECK(Compute(ok(9), 0).left == kHostColors[0]);  // out of range
}

void test_not_connected_blinks_host_colour() {
  Input input = ok(1);
  input.connected = false;
  CHECK(Compute(input, 0).left == kHostColors[1]);
  CHECK(Compute(input, 499).right == kHostColors[1]);
  CHECK(Compute(input, 500).left == kOff);
  CHECK(Compute(input, 999).right == kOff);
  CHECK(Compute(input, 1000).left == kHostColors[1]);
}

void test_errors_are_red_on_their_side() {
  Input input = ok(0);
  input.keyboard_missing = true;
  Pattern pattern = Compute(input, 0);
  CHECK(pattern.left == kRed && pattern.right == kHostColors[0]);

  input = ok(0);
  input.on_battery = true;
  pattern = Compute(input, 0);
  CHECK(pattern.left == kHostColors[0] && pattern.right == kRed);

  // Red does not blink with the link.
  input.keyboard_missing = true;
  input.connected = false;
  pattern = Compute(input, 600);
  CHECK(pattern.left == kRed && pattern.right == kRed);
  input = ok(1);
  input.error = true;
  pattern = Compute(input, 0);
  CHECK(pattern.left == kRed && pattern.right == kHostColors[1]);
}

void test_screen_off_is_dark() {
  Input input = ok(0);
  input.keyboard_missing = true;
  input.on_battery = true;
  input.screen_off = true;
  const Pattern pattern = Compute(input, 0);
  CHECK(pattern.left == kOff && pattern.right == kOff);
}

}  // namespace

int main() {
  test_host_colour_on_both_sides();
  test_not_connected_blinks_host_colour();
  test_errors_are_red_on_their_side();
  test_screen_off_is_dark();
  if (failures != 0) {
    std::printf("%d assertion(s) failed\n", failures);
    return 1;
  }
  std::printf("All tests passed\n");
  return 0;
}
