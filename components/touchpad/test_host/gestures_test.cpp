#include "touchpad_gestures.h"

#include <cstdio>
#include <initializer_list>

using namespace touchpad;

namespace {

const char* current_test = "";
int failures = 0;

#define CHECK(condition)                                                    \
  do {                                                                      \
    if (!(condition)) {                                                     \
      std::printf("FAIL %s:%d: %s\n", current_test, __LINE__, #condition); \
      ++failures;                                                           \
      return false;                                                         \
    }                                                                       \
  } while (false)

struct Point {
  uint8_t id;
  int32_t x;
  int32_t y;
};

Frame frame(uint32_t time_ms, std::initializer_list<Point> points,
            uint8_t buttons = 0) {
  Frame result;
  result.time_ms = time_ms;
  result.buttons = buttons;
  size_t i = 0;
  for (const Point& point : points) {
    result.contacts[i].id = point.id;
    result.contacts[i].touching = true;
    result.contacts[i].x = point.x;
    result.contacts[i].y = point.y;
    ++i;
  }
  result.contact_count = static_cast<uint8_t>(i);
  return result;
}

struct Totals {
  int32_t dx = 0;
  int32_t dy = 0;
  int32_t wheel = 0;
  int32_t pan = 0;
  size_t reports = 0;
  uint8_t last_buttons = 0;
  size_t swipes = 0;
  Swipe last_swipe = Swipe::kNone;
};

void add(Totals& totals, const MouseOutput* out, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    totals.dx += out[i].dx;
    totals.dy += out[i].dy;
    totals.wheel += out[i].wheel;
    totals.pan += out[i].pan;
    totals.last_buttons = out[i].buttons;
    ++totals.reports;
    if (out[i].swipe != Swipe::kNone) {
      ++totals.swipes;
      totals.last_swipe = out[i].swipe;
    }
  }
}

// Slides one finger from (x0, y0) by (dx, dy) over `steps` frames, 1 ms apart.
Totals slide(Gestures& gestures, uint32_t& t, std::initializer_list<Point> start,
             int32_t dx, int32_t dy, int steps) {
  Totals totals;
  MouseOutput out[kMaxOutputs];
  for (int s = 0; s <= steps; ++s) {
    Frame f;
    f.time_ms = t++;
    size_t i = 0;
    for (const Point& p : start) {
      f.contacts[i].id = p.id;
      f.contacts[i].touching = true;
      f.contacts[i].x = p.x + dx * s / steps;
      f.contacts[i].y = p.y + dy * s / steps;
      ++i;
    }
    add(totals, out, gestures.OnFrame(f, out));
  }
  return totals;
}

size_t lift(Gestures& gestures, uint32_t t, MouseOutput* out) {
  return gestures.OnFrame(frame(t, {}), out);
}

bool test_one_finger_moves_pointer() {
  Gestures gestures;
  uint32_t t = 1000;
  const Totals totals = slide(gestures, t, {{0, 500, 500}}, 800, -400, 40);
  // Default gain is 1/8; the first slop's worth is not pointer motion.
  CHECK(totals.dx > 80 && totals.dx <= 100);
  CHECK(totals.dy < -35 && totals.dy >= -50);
  CHECK(totals.wheel == 0);
  CHECK(totals.last_buttons == 0);
  MouseOutput out[kMaxOutputs];
  // A moved touch is not a tap.
  CHECK(lift(gestures, t + 1, out) == 0);
  CHECK(gestures.OnTick(t + 500, out) == 0);
  return true;
}

bool test_tap_clicks_after_drag_window() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  CHECK(gestures.OnFrame(frame(1000, {{0, 500, 500}}), out) == 0);
  CHECK(gestures.OnFrame(frame(1050, {{0, 502, 501}}), out) == 0);
  size_t n = lift(gestures, 1100, out);
  CHECK(n == 1);
  CHECK(out[0].buttons == 0x01);
  CHECK(gestures.next_deadline_ms() == 1300);
  CHECK(gestures.OnTick(1299, out) == 0);
  n = gestures.OnTick(1300, out);
  CHECK(n == 1);
  CHECK(out[0].buttons == 0);
  CHECK(gestures.next_deadline_ms() == 0);
  return true;
}

bool test_long_press_is_not_a_tap() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  gestures.OnFrame(frame(1000, {{0, 500, 500}}), out);
  CHECK(lift(gestures, 1500, out) == 0);
  return true;
}

bool test_two_finger_tap_right_clicks() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  gestures.OnFrame(frame(1000, {{0, 500, 500}}), out);
  gestures.OnFrame(frame(1010, {{0, 500, 500}, {1, 900, 500}}), out);
  const size_t n = lift(gestures, 1080, out);
  CHECK(n == 2);
  CHECK(out[0].buttons == 0x02);
  CHECK(out[1].buttons == 0);
  CHECK(gestures.next_deadline_ms() == 0);
  return true;
}

bool test_tap_then_touch_drags() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  gestures.OnFrame(frame(1000, {{0, 500, 500}}), out);
  lift(gestures, 1080, out);  // press
  uint32_t t = 1150;          // within the 200 ms window
  const Totals totals = slide(gestures, t, {{0, 500, 500}}, 600, 0, 30);
  CHECK(totals.dx > 50);
  CHECK(totals.last_buttons == 0x01);
  // The tap window (ends 1280) passing mid-drag does not release the button;
  // the last frame was at 1180, so the lift timeout (1330) is not reached.
  CHECK(t - 1 == 1180);
  CHECK(gestures.OnTick(1300, out) == 0);
  const size_t n = lift(gestures, t + 1, out);
  CHECK(n == 1);
  CHECK(out[0].buttons == 0);
  return true;
}

bool test_double_tap_double_clicks() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  gestures.OnFrame(frame(1000, {{0, 500, 500}}), out);
  CHECK(lift(gestures, 1060, out) == 1);  // press
  gestures.OnFrame(frame(1150, {{0, 501, 500}}), out);
  const size_t n = lift(gestures, 1210, out);
  CHECK(n == 3);  // release, press, release
  CHECK(out[0].buttons == 0);
  CHECK(out[1].buttons == 0x01);
  CHECK(out[2].buttons == 0);
  CHECK(gestures.next_deadline_ms() == 0);
  return true;
}

bool test_two_finger_scroll_natural() {
  Gestures gestures;
  uint32_t t = 1000;
  // Fingers move down 600 units: natural scrolling -> wheel up (positive).
  Totals totals =
      slide(gestures, t, {{0, 500, 500}, {1, 900, 500}}, 0, 600, 30);
  CHECK(totals.dx == 0 && totals.dy == 0);
  CHECK(totals.wheel >= 8 && totals.wheel <= 10);
  CHECK(totals.pan == 0);
  MouseOutput out[kMaxOutputs];
  lift(gestures, t + 1, out);

  // Fingers move right slowly (1 s): past the swipe window it scrolls, and
  // natural scrolling pans left (negative). What was held back during the
  // first 350 ms is scrolled once decided, so nearly all travel counts.
  Gestures sideways;
  t = 3000;
  totals = slide(sideways, t, {{0, 500, 500}, {1, 500, 900}}, 600, 0, 1000);
  CHECK(totals.pan <= -8 && totals.pan >= -10);
  CHECK(totals.wheel == 0);
  CHECK(totals.swipes == 0);
  return true;
}

bool test_fast_two_finger_flick_is_a_swipe() {
  Gestures gestures;
  uint32_t t = 1000;
  // 600 units (about 16 mm) left to right in 100 ms.
  Totals totals =
      slide(gestures, t, {{0, 300, 500}, {1, 300, 900}}, 600, 40, 100);
  CHECK(totals.swipes == 1);
  CHECK(totals.last_swipe == Swipe::kRight);
  CHECK(totals.pan == 0 && totals.wheel == 0);
  // Moving on after the swipe does not scroll or swipe again.
  totals = slide(gestures, t, {{0, 900, 540}, {1, 900, 940}}, 300, 0, 50);
  CHECK(totals.swipes == 0 && totals.pan == 0);
  MouseOutput out[kMaxOutputs];
  CHECK(lift(gestures, t, out) == 0);  // no tap either

  Gestures leftward;
  t = 5000;
  totals = slide(leftward, t, {{0, 1500, 500}, {1, 1500, 900}}, -600, 0, 100);
  CHECK(totals.swipes == 1);
  CHECK(totals.last_swipe == Swipe::kLeft);
  return true;
}

bool test_vertical_scroll_is_not_delayed() {
  Gestures gestures;
  uint32_t t = 1000;
  // Short, fast vertical movement scrolls at once.
  const Totals totals =
      slide(gestures, t, {{0, 500, 500}, {1, 900, 500}}, 20, 240, 40);
  CHECK(totals.wheel >= 3);
  CHECK(totals.swipes == 0);
  return true;
}

bool test_one_finger_flick_is_not_a_swipe() {
  Gestures gestures;
  uint32_t t = 1000;
  const Totals totals = slide(gestures, t, {{0, 300, 500}}, 800, 0, 50);
  CHECK(totals.swipes == 0);
  CHECK(totals.dx > 80);
  return true;
}

bool test_traditional_scroll_direction() {
  Config config;
  config.natural_scroll = false;
  Gestures gestures(config);
  uint32_t t = 1000;
  const Totals totals =
      slide(gestures, t, {{0, 500, 500}, {1, 900, 500}}, 0, 600, 30);
  CHECK(totals.wheel <= -8);
  return true;
}

bool test_finger_change_does_not_jump() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  uint32_t t = 1000;
  slide(gestures, t, {{0, 500, 500}}, 200, 0, 10);
  // A second finger lands far away, then the first lifts: no pointer jump.
  size_t n = gestures.OnFrame(frame(t++, {{0, 700, 500}, {1, 1800, 1800}}), out);
  CHECK(n == 0);
  n = gestures.OnFrame(frame(t++, {{1, 1800, 1800}}), out);
  CHECK(n == 0);
  return true;
}

bool test_physical_button_passes_through() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  size_t n = gestures.OnFrame(frame(1000, {{0, 500, 500}}, 0x01), out);
  CHECK(n == 1);
  CHECK(out[0].buttons == 0x01);
  n = gestures.OnFrame(frame(1010, {{0, 500, 500}}, 0x00), out);
  CHECK(n == 1);
  CHECK(out[0].buttons == 0);
  return true;
}

bool test_palm_is_ignored() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  Frame palm = frame(1000, {{0, 500, 500}});
  palm.contacts[0].confident = false;
  CHECK(gestures.OnFrame(palm, out) == 0);
  palm.time_ms = 1050;
  CHECK(gestures.OnFrame(palm, out) == 0);
  CHECK(lift(gestures, 1080, out) == 0);  // no tap either
  return true;
}

bool test_missing_lift_frame_times_out() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  gestures.OnFrame(frame(1000, {{0, 500, 500}}), out);
  gestures.OnFrame(frame(1050, {{0, 500, 500}}), out);
  // No more frames: the touch is lifted after 150 ms and counts as a tap
  // only if short enough (it is not: 200 ms > 180 ms).
  CHECK(gestures.next_deadline_ms() == 1200);
  CHECK(gestures.OnTick(1200, out) == 0);
  CHECK(gestures.next_deadline_ms() == 0);
  return true;
}

bool test_reset_releases_held_button() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  gestures.OnFrame(frame(1000, {{0, 500, 500}}), out);
  lift(gestures, 1050, out);  // tap press, release pending
  const size_t n = gestures.Reset(out);
  CHECK(n == 1);
  CHECK(out[0].buttons == 0);
  CHECK(gestures.next_deadline_ms() == 0);
  return true;
}

bool test_duplicate_contact_ids_are_told_apart() {
  // Both slots report id 0 (seen from the Navigator): slot order tells the
  // fingers apart, so two still fingers do not look like a fast movement.
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  Totals totals;
  for (uint32_t t = 1000; t < 1100; t += 5) {
    add(totals, out,
        gestures.OnFrame(frame(t, {{0, 300, 500}, {0, 1500, 500}}), out));
  }
  CHECK(totals.swipes == 0);
  CHECK(totals.pan == 0 && totals.wheel == 0 && totals.dx == 0);
  return true;
}

bool test_one_swipe_per_touch_even_if_fingers_flicker() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  Totals totals;
  uint32_t t = 1000;
  int32_t x = 300;
  // Two fingers flick right while the second one keeps dropping out.
  for (int i = 0; i < 60; ++i, t += 5, x += 30) {
    if (i % 3 == 2) {
      add(totals, out, gestures.OnFrame(frame(t, {{0, x, 500}}), out));
    } else {
      add(totals, out,
          gestures.OnFrame(frame(t, {{0, x, 500}, {1, x, 900}}), out));
    }
  }
  CHECK(totals.swipes == 1);
  // After lifting, a new touch may swipe again.
  lift(gestures, t, out);
  t += 100;
  totals = slide(gestures, t, {{0, 300, 500}, {1, 300, 900}}, 600, 0, 100);
  CHECK(totals.swipes == 1);
  return true;
}

bool test_reused_output_buffer_does_not_repeat_swipe() {
  // The firmware passes the same output array to every call.
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  Totals totals;
  uint32_t t = 1000;
  for (int s = 0; s <= 100; ++s, ++t) {
    add(totals, out,
        gestures.OnFrame(frame(t, {{0, 300 + 6 * s, 500}, {1, 300 + 6 * s, 900}}),
                         out));
  }
  CHECK(totals.swipes == 1);
  add(totals, out, lift(gestures, t++, out));
  // One finger afterwards: pointer motion only, never another swipe.
  Totals pointer;
  for (int s = 0; s <= 40; ++s, ++t) {
    add(pointer, out, gestures.OnFrame(frame(t, {{0, 500 + 20 * s, 500}}), out));
  }
  CHECK(pointer.swipes == 0);
  CHECK(pointer.dx > 50);
  return true;
}

void run_test(const char* name, bool (*test)()) {
  current_test = name;
  const int failures_before = failures;
  if (test() && failures == failures_before) {
    std::printf("PASS %s\n", name);
  } else {
    std::printf("FAIL %s\n", name);
  }
}

}  // namespace

int main() {
  run_test("one_finger_moves_pointer", test_one_finger_moves_pointer);
  run_test("tap_clicks_after_drag_window", test_tap_clicks_after_drag_window);
  run_test("long_press_is_not_a_tap", test_long_press_is_not_a_tap);
  run_test("two_finger_tap_right_clicks", test_two_finger_tap_right_clicks);
  run_test("tap_then_touch_drags", test_tap_then_touch_drags);
  run_test("double_tap_double_clicks", test_double_tap_double_clicks);
  run_test("two_finger_scroll_natural", test_two_finger_scroll_natural);
  run_test("traditional_scroll_direction", test_traditional_scroll_direction);
  run_test("fast_two_finger_flick_is_a_swipe",
           test_fast_two_finger_flick_is_a_swipe);
  run_test("vertical_scroll_is_not_delayed",
           test_vertical_scroll_is_not_delayed);
  run_test("one_finger_flick_is_not_a_swipe",
           test_one_finger_flick_is_not_a_swipe);
  run_test("duplicate_contact_ids_are_told_apart",
           test_duplicate_contact_ids_are_told_apart);
  run_test("one_swipe_per_touch_even_if_fingers_flicker",
           test_one_swipe_per_touch_even_if_fingers_flicker);
  run_test("reused_output_buffer_does_not_repeat_swipe",
           test_reused_output_buffer_does_not_repeat_swipe);
  run_test("finger_change_does_not_jump", test_finger_change_does_not_jump);
  run_test("physical_button_passes_through",
           test_physical_button_passes_through);
  run_test("palm_is_ignored", test_palm_is_ignored);
  run_test("missing_lift_frame_times_out", test_missing_lift_frame_times_out);
  run_test("reset_releases_held_button", test_reset_releases_held_button);
  if (failures != 0) {
    std::printf("%d test assertion(s) failed\n", failures);
    return 1;
  }
  std::printf("All tests passed\n");
  return 0;
}
