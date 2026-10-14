// Precision touchpad gestures -> relative mouse reports.
//
// The Navigator touchpad reports absolute contacts once it is switched to
// touchpad input mode. macOS cannot take those over BLE HID, so the hub turns
// them into mouse motion, clicks, drags and scrolling here.
// Pure logic: no ESP-IDF, heap allocation or I/O.
#pragma once

#include <cstddef>
#include <cstdint>

namespace touchpad {

constexpr size_t kMaxContacts = 5;
constexpr uint32_t kRedragNoLimit = UINT32_MAX;

struct Contact {
  uint8_t id = 0;
  bool touching = false;
  bool confident = true;  // false for palms
  int32_t x = 0;
  int32_t y = 0;
};

// One digitizer report. `time_ms` is the host time when it arrived.
struct Frame {
  Contact contacts[kMaxContacts];
  uint8_t contact_count = 0;
  uint8_t buttons = 0;  // physical pad buttons, bit 0 = left
  uint32_t time_ms = 0;
  int32_t max_x = 0;  // logical coordinate maxima; 0 = unknown
  int32_t max_y = 0;
};

struct Config {
  // Pad resolution in logical units per millimetre.
  int32_t units_per_mm = 37;
  // Pointer counts = logical delta * pointer_num / pointer_den.
  int32_t pointer_num = 1;
  int32_t pointer_den = 8;
  // Two-finger travel per wheel/pan step, in logical units.
  int32_t scroll_step = 60;
  // true: fingers moving up scroll the content up (touchpad style).
  bool natural_scroll = true;
  // A touch is a tap when it ends within this time and moves less than
  // tap_slop_mm.
  uint32_t tap_max_ms = 180;
  int32_t tap_slop_mm = 1;
  // After a one-finger tap the button stays down this long; touching again
  // nearby within it turns the tap into a drag.
  uint32_t drag_window_ms = 200;
  // A second one-finger touch must land this close to the previous tap.
  int32_t drag_slop_mm = 4;
  // After a moved tap-drag lifts, keep the button down (drag lock).
  // Zero disables continuation; kRedragNoLimit holds until resumed/dropped.
  // Other values bound the hold without changing the drag-start window.
  uint32_t redrag_ms = kRedragNoLimit;
  // Non-zero: only hold when the finger lifts this close to a pad edge.
  int32_t redrag_edge_mm = 0;
  // With fingers down but no frame for this long, the touch counts as lifted.
  uint32_t lift_timeout_ms = 150;
  // A two-finger horizontal flick covering swipe_min_mm within swipe_max_ms
  // (and at least twice as far across as down) is a swipe, not a scroll.
  // Until that is decided, horizontal scrolling is held back.
  int32_t swipe_min_mm = 12;
  uint32_t swipe_max_ms = 350;
};

enum class Swipe : uint8_t {
  kNone,
  kRight,  // fingers moved left to right (macOS: Back)
  kLeft,   // fingers moved right to left (macOS: Forward)
};

struct MouseOutput {
  uint8_t buttons = 0;
  int16_t dx = 0;
  int16_t dy = 0;
  int16_t wheel = 0;
  int16_t pan = 0;
  Swipe swipe = Swipe::kNone;  // set on a report that only carries a swipe
};

// Upper bound of outputs produced by one call.
constexpr size_t kMaxOutputs = 4;

class Gestures {
 public:
  explicit Gestures(const Config& config = Config{});

  // Feeds one frame. Writes up to kMaxOutputs reports, returns the count.
  size_t OnFrame(const Frame& frame, MouseOutput* out);

  // Completes time-based gestures (tap/re-drag release and missing lifts).
  size_t OnTick(uint32_t time_ms, MouseOutput* out);

  // Next time OnTick has work, or 0 when nothing is pending.
  uint32_t next_deadline_ms() const;

  // Drops all state, e.g. when the device is unplugged. Returns a release
  // report in `out` when a button was held.
  size_t Reset(MouseOutput* out);

 private:
  struct Track {
    uint8_t id;
    int32_t last_x;
    int32_t last_y;
  };

  enum class Phase : uint8_t {
    kIdle,          // no finger
    kTouch,         // fingers down, tap still possible
    kMoving,        // fingers moved: pointer or scroll, no tap
    kDragging,      // second touch after a tap: left button held
  };

  // What a two-finger movement turned out to be.
  enum class TwoFinger : uint8_t { kUndecided, kScroll, kSwiped };

  uint8_t Buttons() const;
  bool NearTap(const Track& contact) const;
  bool AtEdge() const;
  void Emit(MouseOutput* out, size_t& count, int16_t dx = 0, int16_t dy = 0,
            int16_t wheel = 0, int16_t pan = 0);
  static int16_t Scale(int32_t& remainder, int32_t delta, int32_t num,
                       int32_t den);
  // Decides whether a two-finger movement is a scroll or a swipe. Returns
  // true when the movement should scroll, with `dx`/`dy` widened to include
  // anything held back while deciding; emits the swipe itself.
  bool ClassifyTwoFinger(uint32_t now, int32_t& dx, int32_t& dy,
                         MouseOutput* out, size_t& count);

  Config config_;
  Phase phase_ = Phase::kIdle;
  Track tracks_[kMaxContacts] = {};
  size_t track_count_ = 0;
  uint8_t max_fingers_ = 0;
  uint32_t touch_start_ms_ = 0;
  uint32_t last_frame_ms_ = 0;
  int32_t travel_ = 0;  // summed |dx| + |dy| since touch start
  bool drag_tap_ = false;  // the drag touch itself may still be a tap
  bool redrag_ = false;  // holding or continuing a drag after repositioning
  int32_t tap_x_ = 0;
  int32_t tap_y_ = 0;
  int32_t max_x_ = 0;
  int32_t max_y_ = 0;
  uint8_t pad_buttons_ = 0;
  bool tap_button_ = false;
  uint32_t tap_release_at_ = 0;
  int32_t rem_x_ = 0;
  int32_t rem_y_ = 0;
  int32_t rem_wheel_ = 0;
  int32_t rem_pan_ = 0;
  TwoFinger two_finger_ = TwoFinger::kUndecided;
  uint32_t two_finger_start_ms_ = 0;
  int32_t two_dx_ = 0;
  int32_t two_dy_ = 0;
  bool swipe_armed_ = false;
  bool two_finger_started_ = false;
};

}  // namespace touchpad
