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
            uint8_t buttons = 0, int32_t max_x = 0, int32_t max_y = 0) {
  Frame result;
  result.time_ms = time_ms;
  result.buttons = buttons;
  result.max_x = max_x;
  result.max_y = max_y;
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
  Config config;
  config.redrag_ms = 0;  // Preserve coverage of the original drag behavior.
  Gestures gestures(config);
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

// Leave an established tap-drag at x=660, with its last frame at t+140.
void start_drag(Gestures& gestures, MouseOutput* out, uint32_t t = 1000,
                int32_t max_x = 0, int32_t max_y = 0) {
  gestures.OnFrame(frame(t, {{0, 500, 500}}, 0, max_x, max_y), out);
  lift(gestures, t + 60, out);
  gestures.OnFrame(frame(t + 100, {{0, 500, 500}}, 0, max_x, max_y), out);
  gestures.OnFrame(frame(t + 140, {{0, 660, 500}}, 0, max_x, max_y), out);
}

bool test_distant_landing_releases_click_before_motion() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  gestures.OnFrame(frame(1000, {{0, 500, 500}}), out);
  CHECK(lift(gestures, 1060, out) == 1);
  CHECK(out[0].buttons == 1);
  CHECK(gestures.OnFrame(frame(1150, {{0, 1000, 500}}), out) == 1);
  CHECK(out[0].buttons == 0 && out[0].dx == 0 && out[0].dy == 0);
  CHECK(gestures.next_deadline_ms() == 1300);  // lift timeout, not tap release
  CHECK(gestures.OnFrame(frame(1190, {{0, 1160, 500}}), out) == 1);
  CHECK(out[0].buttons == 0 && out[0].dx == 20);
  CHECK(lift(gestures, 1250, out) == 0);
  CHECK(gestures.next_deadline_ms() == 0);
  return true;
}

bool test_drag_start_requires_nearby_single_finger() {
  MouseOutput out[kMaxOutputs];
  // Exactly 4 mm from the tap is inside the default circular boundary.
  Gestures nearby;
  nearby.OnFrame(frame(1000, {{0, 500, 500}}), out);
  lift(nearby, 1060, out);
  CHECK(nearby.OnFrame(frame(1150, {{0, 648, 500}}), out) == 0);
  CHECK(nearby.OnFrame(frame(1190, {{0, 808, 500}}), out) == 1);
  CHECK(out[0].buttons == 1 && out[0].dx == 20);

  Gestures diagonal;
  diagonal.OnFrame(frame(1000, {{0, 500, 500}}), out);
  lift(diagonal, 1060, out);
  CHECK(diagonal.OnFrame(frame(1150, {{0, 610, 610}}), out) == 1);
  CHECK(out[0].buttons == 0);  // each axis <4 mm, total distance >4 mm

  Gestures two_fingers;
  two_fingers.OnFrame(frame(1000, {{0, 500, 500}}), out);
  lift(two_fingers, 1060, out);
  CHECK(two_fingers.OnFrame(
      frame(1150, {{0, 500, 500}, {1, 520, 500}}), out) == 1);
  CHECK(out[0].buttons == 0);
  CHECK(lift(two_fingers, 1200, out) == 2);
  CHECK(out[0].buttons == 2 && out[1].buttons == 0);

  Config config;
  config.drag_slop_mm = 2;
  Gestures narrower(config);
  narrower.OnFrame(frame(1000, {{0, 500, 500}}), out);
  lift(narrower, 1060, out);
  CHECK(narrower.OnFrame(frame(1150, {{0, 575, 500}}), out) == 1);
  CHECK(out[0].buttons == 0);
  return true;
}

bool test_drag_start_window_expires_even_without_tick() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  gestures.OnFrame(frame(1000, {{0, 500, 500}}), out);
  lift(gestures, 1060, out);
  CHECK(gestures.OnFrame(frame(1260, {{0, 500, 500}}), out) == 1);
  CHECK(out[0].buttons == 0);
  CHECK(gestures.OnFrame(frame(1300, {{0, 660, 500}}), out) == 1);
  CHECK(out[0].buttons == 0 && out[0].dx == 20);
  return true;
}

bool test_redrag_only_holds_at_all_four_edges() {
  for (const Point& end : {Point{0, 148, 700}, Point{0, 1852, 700},
                           Point{0, 700, 148}, Point{0, 700, 1852}}) {
    Config finite;
    finite.redrag_ms = 1500;
    finite.redrag_edge_mm = 4;
    Gestures gestures(finite);
    MouseOutput out[kMaxOutputs];
    start_drag(gestures, out, 1000, 2000, 2000);
    CHECK(gestures.OnFrame(frame(1170, {end}, 0, 2000, 2000), out) == 1);
    CHECK(out[0].buttons == 1);
    CHECK(lift(gestures, 1200, out) == 0);
    CHECK(gestures.next_deadline_ms() == 2700);
    // Re-grip can be far from the edge; landing must not jump the pointer.
    CHECK(gestures.OnFrame(frame(1400, {{0, 1000, 1000}}, 0, 2000, 2000), out) == 0);
    CHECK(gestures.OnFrame(frame(1450, {{0, 1160, 1000}}, 0, 2000, 2000), out) == 1);
    CHECK(out[0].buttons == 1 && out[0].dx == 20 && out[0].dy == 0);
    CHECK(lift(gestures, 1500, out) == 1);  // interior: drop immediately
    CHECK(out[0].buttons == 0);
    CHECK(gestures.next_deadline_ms() == 0);
  }
  return true;
}

bool test_redrag_interior_lift_drops_immediately() {
  Config edge_only;
  edge_only.redrag_edge_mm = 4;
  Gestures gestures(edge_only);
  MouseOutput out[kMaxOutputs];
  start_drag(gestures, out, 1000, 2000, 2000);
  // One unit beyond the 4 mm edge band is already interior.
  CHECK(gestures.OnFrame(frame(1170, {{0, 149, 700}}, 0, 2000, 2000), out) == 1);
  CHECK(lift(gestures, 1200, out) == 1);
  CHECK(out[0].buttons == 0);
  CHECK(gestures.next_deadline_ms() == 0);
  CHECK(gestures.OnTick(2700, out) == 0);
  return true;
}

bool test_redrag_synthetic_lift_uses_last_position_and_bounds() {
  for (const int32_t end_x : {148, 149}) {
    Config finite;
    finite.redrag_ms = 1500;
    finite.redrag_edge_mm = 4;
    Gestures gestures(finite);
    MouseOutput out[kMaxOutputs];
    start_drag(gestures, out, 1000, 2000, 2000);
    gestures.OnFrame(frame(1170, {{0, end_x, 700}}, 0, 2000, 2000), out);
    CHECK(gestures.next_deadline_ms() == 1320);
    const size_t count = gestures.OnTick(1320, out);
    if (end_x == 148) {
      CHECK(count == 0);
      CHECK(gestures.next_deadline_ms() == 2820);
      CHECK(gestures.OnTick(2820, out) == 1);
    } else {
      CHECK(count == 1);
    }
    CHECK(out[0].buttons == 0);
    CHECK(gestures.next_deadline_ms() == 0);
  }
  return true;
}

bool test_redrag_unknown_dimensions_and_configurable_edge() {
  for (const auto& bounds : {Point{0, 0, 2000}, Point{0, 2000, 0}}) {
    Config finite;
    finite.redrag_ms = 1500;
    finite.redrag_edge_mm = 4;
    Gestures gestures(finite);
    MouseOutput out[kMaxOutputs];
    start_drag(gestures, out, 1000, bounds.x, bounds.y);
    CHECK(lift(gestures, 1200, out) == 0);
    CHECK(gestures.next_deadline_ms() == 2700);
  }
  Config config;
  config.redrag_edge_mm = 5;
  config.redrag_ms = 1500;
  Gestures wider(config);
  MouseOutput out[kMaxOutputs];
  start_drag(wider, out, 1000, 2000, 2000);
  wider.OnFrame(frame(1170, {{0, 160, 700}}, 0, 2000, 2000), out);
  CHECK(lift(wider, 1200, out) == 0);
  CHECK(wider.next_deadline_ms() == 2700);
  return true;
}

bool test_observed_tap_then_distant_stroke_is_not_drag() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  gestures.OnFrame(frame(69156518, {{0, 1000, 1093}}), out);
  CHECK(gestures.OnFrame(frame(69156570, {{0, 1006, 1089}}), out) == 0);
  CHECK(lift(gestures, 69156575, out) == 1);
  CHECK(out[0].buttons == 1);
  // Use the measured re-landing coordinates at a 130 ms interval. The actual
  // excerpt has a 536 ms gap; this variant exercises the drag-start window.
  CHECK(gestures.OnFrame(frame(69156705, {{0, 501, 740}}), out) == 1);
  CHECK(out[0].buttons == 0 && out[0].dx == 0 && out[0].dy == 0);
  CHECK(gestures.OnFrame(frame(69156745, {{0, 1593, 780}}), out) == 1);
  CHECK(out[0].buttons == 0 && out[0].dx > 0);
  CHECK(lift(gestures, 69156923, out) == 0);
  CHECK(gestures.next_deadline_ms() == 0);
  Gestures original_timing;
  original_timing.OnFrame(frame(69156518, {{0, 1000, 1093}}), out);
  original_timing.OnFrame(frame(69156570, {{0, 1006, 1089}}), out);
  lift(original_timing, 69156575, out);
  CHECK(original_timing.OnFrame(frame(69157111, {{0, 501, 740}}), out) == 1);
  CHECK(out[0].buttons == 0);  // deadline expired even without an OnTick call
  CHECK(original_timing.OnFrame(frame(69157320, {{0, 1593, 780}}), out) == 1);
  CHECK(out[0].buttons == 0 && out[0].dx > 0);
  CHECK(lift(original_timing, 69157329, out) == 0);
  return true;
}

bool test_redrag_repositions_repeatedly_without_jump() {
  Config finite;
  finite.redrag_ms = 1500;
  Gestures gestures(finite);
  MouseOutput out[kMaxOutputs];
  start_drag(gestures, out);
  CHECK(lift(gestures, 1200, out) == 0);
  CHECK(gestures.next_deadline_ms() == 2700);
  CHECK(gestures.OnTick(2699, out) == 0);
  CHECK(gestures.OnFrame(frame(2699, {{0, 100, 500}}), out) == 0);
  CHECK(gestures.next_deadline_ms() == 2849);
  CHECK(gestures.OnFrame(frame(2730, {{0, 260, 500}}), out) == 1);
  CHECK(out[0].buttons == 1 && out[0].dx == 20 && out[0].dy == 0);
  CHECK(lift(gestures, 2740, out) == 0);
  CHECK(gestures.next_deadline_ms() == 4240);
  CHECK(gestures.OnFrame(frame(3000, {{1, 1500, 900}}), out) == 0);
  CHECK(gestures.OnFrame(frame(3040, {{1, 1340, 900}}), out) == 1);
  CHECK(out[0].buttons == 1 && out[0].dx == -20 && out[0].dy == 0);
  CHECK(lift(gestures, 3100, out) == 0);
  CHECK(gestures.next_deadline_ms() == 4600);
  CHECK(gestures.OnTick(4600, out) == 1);
  CHECK(out[0].buttons == 0);
  return true;
}

bool test_redrag_expires_once() {
  Config config;
  config.redrag_ms = 500;
  Gestures gestures(config);
  MouseOutput out[kMaxOutputs];
  start_drag(gestures, out);
  CHECK(lift(gestures, 1200, out) == 0);
  // Further no-contact frames must not extend the hold.
  CHECK(lift(gestures, 1300, out) == 0);
  CHECK(gestures.next_deadline_ms() == 1700);
  CHECK(gestures.OnTick(1699, out) == 0);
  CHECK(gestures.OnTick(1700, out) == 1);
  CHECK(out[0].buttons == 0 && out[0].dx == 0);
  CHECK(gestures.next_deadline_ms() == 0);
  CHECK(gestures.OnTick(1701, out) == 0);
  return true;
}

bool test_redrag_tap_drops_without_click() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  start_drag(gestures, out);
  CHECK(lift(gestures, 1200, out) == 0);
  CHECK(gestures.OnFrame(frame(1400, {{0, 100, 500}}), out) == 0);
  CHECK(gestures.OnFrame(frame(1450, {{0, 102, 501}}), out) == 0);
  CHECK(lift(gestures, 1580, out) == 1);  // inclusive tap time limit
  CHECK(out[0].buttons == 0 && out[0].dx == 0 && out[0].dy == 0);
  CHECK(gestures.next_deadline_ms() == 0);
  CHECK(gestures.OnTick(4000, out) == 0);
  // Dropping must clear the continuation distinction for future double taps.
  CHECK(gestures.OnFrame(frame(5000, {{0, 500, 500}}), out) == 0);
  CHECK(lift(gestures, 5060, out) == 1);
  CHECK(gestures.OnFrame(frame(5100, {{0, 500, 500}}), out) == 0);
  CHECK(lift(gestures, 5160, out) == 3);
  return true;
}

bool test_redrag_reset_releases_hold() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  start_drag(gestures, out);
  CHECK(lift(gestures, 1200, out) == 0);
  CHECK(gestures.Reset(out) == 1);
  CHECK(out[0].buttons == 0);
  CHECK(gestures.next_deadline_ms() == 0);
  CHECK(gestures.Reset(out) == 0);
  CHECK(gestures.OnTick(2700, out) == 0);
  return true;
}

bool test_redrag_stationary_long_touch_releases() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  start_drag(gestures, out);
  CHECK(lift(gestures, 1200, out) == 0);
  CHECK(gestures.OnFrame(frame(1400, {{0, 100, 500}}), out) == 0);
  CHECK(lift(gestures, 1581, out) == 1);  // not a quick drop or moved drag
  CHECK(out[0].buttons == 0);
  CHECK(gestures.next_deadline_ms() == 0);
  return true;
}

bool test_redrag_tap_slop_boundary_is_not_a_drop_tap() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  start_drag(gestures, out);
  CHECK(lift(gestures, 1200, out) == 0);
  CHECK(gestures.OnFrame(frame(1400, {{0, 100, 500}}), out) == 0);
  CHECK(gestures.OnFrame(frame(1450, {{0, 137, 500}}), out) == 0);
  CHECK(lift(gestures, 1500, out) == 1);
  CHECK(out[0].buttons == 0);  // release only: no double click
  CHECK(gestures.next_deadline_ms() == 0);
  return true;
}

bool test_three_finger_tap_middle_clicks() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  CHECK(gestures.OnFrame(
      frame(1000, {{0, 100, 500}, {1, 500, 500}, {2, 900, 500}}), out) == 0);
  CHECK(lift(gestures, 1060, out) == 2);
  CHECK(out[0].buttons == 4 && out[1].buttons == 0);
  CHECK(gestures.next_deadline_ms() == 0);
  return true;
}

bool test_redrag_missing_lift_enters_hold() {
  Config finite;
  finite.redrag_ms = 1500;
  Gestures gestures(finite);
  MouseOutput out[kMaxOutputs];
  start_drag(gestures, out);
  CHECK(gestures.next_deadline_ms() == 1290);
  CHECK(gestures.OnTick(1290, out) == 0);
  CHECK(gestures.next_deadline_ms() == 2790);
  CHECK(gestures.OnTick(2790, out) == 1);
  CHECK(out[0].buttons == 0);
  return true;
}

bool test_redrag_late_frame_cannot_resume() {
  Config finite;
  finite.redrag_ms = 1500;
  Gestures gestures(finite);
  MouseOutput out[kMaxOutputs];
  start_drag(gestures, out);
  CHECK(lift(gestures, 1200, out) == 0);
  CHECK(gestures.OnFrame(frame(2700, {{0, 100, 500}}), out) == 1);
  CHECK(out[0].buttons == 0);
  CHECK(gestures.OnFrame(frame(2740, {{0, 260, 500}}), out) == 1);
  CHECK(out[0].buttons == 0 && out[0].dx == 20);
  CHECK(lift(gestures, 2800, out) == 0);
  CHECK(gestures.next_deadline_ms() == 0);
  return true;
}

bool test_physical_drag_does_not_enter_redrag() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  CHECK(gestures.OnFrame(frame(1000, {{0, 500, 500}}, 1), out) == 1);
  CHECK(gestures.OnFrame(frame(1040, {{0, 660, 500}}, 1), out) == 1);
  CHECK(gestures.OnFrame(frame(1100, {}, 1), out) == 0);
  CHECK(gestures.next_deadline_ms() == 0);
  CHECK(lift(gestures, 1150, out) == 1);
  CHECK(out[0].buttons == 0);
  CHECK(gestures.OnTick(3000, out) == 0);
  return true;
}

bool test_redrag_deadline_wraparound() {
  Config finite;
  finite.redrag_ms = 1500;
  Gestures gestures(finite);
  MouseOutput out[kMaxOutputs];
  start_drag(gestures, out, UINT32_MAX - 300);
  CHECK(lift(gestures, UINT32_MAX - 100, out) == 0);
  CHECK(gestures.next_deadline_ms() == 1399);
  CHECK(gestures.OnTick(1398, out) == 0);
  CHECK(gestures.OnTick(1399, out) == 1);
  CHECK(out[0].buttons == 0);
  CHECK(gestures.next_deadline_ms() == 0);
  return true;
}

void move_drag_to_edge(Gestures& gestures, MouseOutput* out,
                       uint32_t base = 1000) {
  start_drag(gestures, out, base, 2048, 2048);
  gestures.OnFrame(frame(base + 170, {{0, 2048, 1000}}, 0, 2048, 2048), out);
}

// Default: the drag stays locked wherever the finger lifts, until a tap.
bool test_default_drag_lock_holds_after_interior_lift() {
  CHECK(Config{}.redrag_edge_mm == 0);
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  start_drag(gestures, out, 1000, 2048, 2048);
  CHECK(lift(gestures, 1200, out) == 0);  // mid-pad lift keeps the button
  CHECK(gestures.next_deadline_ms() == 0);
  CHECK(gestures.OnTick(61200, out) == 0);
  // Any later stroke continues the drag, and may end mid-pad again.
  CHECK(gestures.OnFrame(frame(61300, {{0, 900, 900}}, 0, 2048, 2048), out) ==
        0);
  CHECK(gestures.OnFrame(frame(61350, {{0, 1060, 900}}, 0, 2048, 2048),
                         out) == 1);
  CHECK(out[0].buttons == 1 && out[0].dx == 20);
  CHECK(lift(gestures, 61400, out) == 0);
  // A tap drops, without a click of its own.
  CHECK(gestures.OnFrame(frame(62000, {{0, 300, 300}}, 0, 2048, 2048), out) ==
        0);
  CHECK(lift(gestures, 62060, out) == 1);
  CHECK(out[0].buttons == 0);
  CHECK(gestures.OnTick(63000, out) == 0);
  return true;
}

bool test_unlimited_redrag_waits_and_resumes_across_wrap() {
  CHECK(Config{}.redrag_ms == kRedragNoLimit);
  CHECK(kRedragNoLimit == UINT32_MAX);
  for (const uint32_t base : {1000u, UINT32_MAX - 2000u}) {
    Config edge_only;
    edge_only.redrag_edge_mm = 4;
    Gestures gestures(edge_only);
    MouseOutput out[kMaxOutputs];
    move_drag_to_edge(gestures, out, base);
    CHECK(lift(gestures, base + 200, out) == 0);
    CHECK(gestures.next_deadline_ms() == 0);
    CHECK(gestures.OnTick(base + 10200, out) == 0);  // 10 seconds
    CHECK(gestures.OnTick(base + 3600200, out) == 0);  // one hour
    CHECK(gestures.next_deadline_ms() == 0);
    CHECK(gestures.OnFrame(
        frame(base + 3600300, {{0, 700, 700}}, 0, 2048, 2048), out) == 0);
    CHECK(gestures.OnFrame(
        frame(base + 3600350, {{0, 860, 700}}, 0, 2048, 2048), out) == 1);
    CHECK(out[0].buttons == 1 && out[0].dx == 20 && out[0].dy == 0);
    CHECK(lift(gestures, base + 3600400, out) == 1);  // interior drop
    CHECK(out[0].buttons == 0);
    CHECK(gestures.next_deadline_ms() == 0);
  }
  return true;
}

bool test_unlimited_redrag_tap_or_long_stationary_touch_drops() {
  for (const uint32_t duration : {70u, 181u}) {
    Gestures gestures;
    MouseOutput out[kMaxOutputs];
    move_drag_to_edge(gestures, out);
    CHECK(lift(gestures, 1200, out) == 0);
    CHECK(gestures.OnFrame(frame(1400, {{0, 200, 200}}, 0, 2048, 2048), out) == 0);
    CHECK(lift(gestures, 1400 + duration, out) == 1);
    CHECK(out[0].buttons == 0 && out[0].dx == 0 && out[0].dy == 0);
    CHECK(gestures.next_deadline_ms() == 0);
    CHECK(gestures.OnTick(3600000, out) == 0);
  }
  return true;
}

bool test_unlimited_redrag_reset_releases() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  move_drag_to_edge(gestures, out);
  CHECK(lift(gestures, 1200, out) == 0);
  CHECK(gestures.next_deadline_ms() == 0);
  CHECK(gestures.Reset(out) == 1);
  CHECK(out[0].buttons == 0);
  CHECK(gestures.Reset(out) == 0);
  CHECK(gestures.OnTick(3600000, out) == 0);
  CHECK(gestures.OnFrame(frame(3600100, {{0, 200, 200}}), out) == 0);
  CHECK(gestures.OnFrame(frame(3600150, {{0, 360, 200}}), out) == 1);
  CHECK(out[0].buttons == 0 && out[0].dx == 20);
  return true;
}

bool test_unlimited_redrag_synthetic_lift_has_no_hold_deadline() {
  Gestures gestures;
  MouseOutput out[kMaxOutputs];
  move_drag_to_edge(gestures, out);
  CHECK(gestures.next_deadline_ms() == 1320);
  CHECK(gestures.OnTick(1320, out) == 0);
  CHECK(gestures.next_deadline_ms() == 0);
  CHECK(gestures.OnTick(3601320, out) == 0);
  CHECK(gestures.Reset(out) == 1);
  CHECK(out[0].buttons == 0);
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
  run_test("distant_landing_releases_click_before_motion",
           test_distant_landing_releases_click_before_motion);
  run_test("drag_start_requires_nearby_single_finger",
           test_drag_start_requires_nearby_single_finger);
  run_test("drag_start_window_expires_even_without_tick",
           test_drag_start_window_expires_even_without_tick);
  run_test("redrag_only_holds_at_all_four_edges",
           test_redrag_only_holds_at_all_four_edges);
  run_test("default_drag_lock_holds_after_interior_lift",
           test_default_drag_lock_holds_after_interior_lift);
  run_test("redrag_interior_lift_drops_immediately",
           test_redrag_interior_lift_drops_immediately);
  run_test("redrag_synthetic_lift_uses_last_position_and_bounds",
           test_redrag_synthetic_lift_uses_last_position_and_bounds);
  run_test("redrag_unknown_dimensions_and_configurable_edge",
           test_redrag_unknown_dimensions_and_configurable_edge);
  run_test("observed_tap_then_distant_stroke_is_not_drag",
           test_observed_tap_then_distant_stroke_is_not_drag);
  run_test("redrag_repositions_repeatedly_without_jump",
           test_redrag_repositions_repeatedly_without_jump);
  run_test("redrag_expires_once", test_redrag_expires_once);
  run_test("redrag_tap_drops_without_click", test_redrag_tap_drops_without_click);
  run_test("redrag_reset_releases_hold", test_redrag_reset_releases_hold);
  run_test("redrag_stationary_long_touch_releases",
           test_redrag_stationary_long_touch_releases);
  run_test("redrag_tap_slop_boundary_is_not_a_drop_tap",
           test_redrag_tap_slop_boundary_is_not_a_drop_tap);
  run_test("three_finger_tap_middle_clicks", test_three_finger_tap_middle_clicks);
  run_test("redrag_missing_lift_enters_hold", test_redrag_missing_lift_enters_hold);
  run_test("redrag_late_frame_cannot_resume", test_redrag_late_frame_cannot_resume);
  run_test("physical_drag_does_not_enter_redrag",
           test_physical_drag_does_not_enter_redrag);
  run_test("redrag_deadline_wraparound", test_redrag_deadline_wraparound);
  run_test("unlimited_redrag_waits_and_resumes_across_wrap",
           test_unlimited_redrag_waits_and_resumes_across_wrap);
  run_test("unlimited_redrag_tap_or_long_stationary_touch_drops",
           test_unlimited_redrag_tap_or_long_stationary_touch_drops);
  run_test("unlimited_redrag_reset_releases", test_unlimited_redrag_reset_releases);
  run_test("unlimited_redrag_synthetic_lift_has_no_hold_deadline",
           test_unlimited_redrag_synthetic_lift_has_no_hold_deadline);
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
