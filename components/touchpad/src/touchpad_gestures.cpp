#include "touchpad_gestures.h"

namespace touchpad {
namespace {

constexpr uint8_t kLeft = 0x01;
constexpr uint8_t kRight = 0x02;
constexpr uint8_t kMiddle = 0x04;

int32_t abs32(int32_t value) { return value < 0 ? -value : value; }

// Deadlines use 0 as "none", so a real deadline is never 0.
uint32_t deadline(uint32_t time_ms) { return time_ms == 0 ? 1 : time_ms; }

bool reached(uint32_t now, uint32_t when) {
  return static_cast<int32_t>(now - when) >= 0;
}

}  // namespace

Gestures::Gestures(const Config& config) : config_(config) {}

uint8_t Gestures::Buttons() const {
  return pad_buttons_ | (tap_button_ ? kLeft : 0);
}

void Gestures::Emit(MouseOutput* out, size_t& count, int16_t dx, int16_t dy,
                    int16_t wheel, int16_t pan) {
  if (count == kMaxOutputs) {
    return;
  }
  // Callers reuse `out`, so every field is written, including `swipe`.
  MouseOutput& report = out[count++];
  report = MouseOutput{};
  report.buttons = Buttons();
  report.dx = dx;
  report.dy = dy;
  report.wheel = wheel;
  report.pan = pan;
}

int16_t Gestures::Scale(int32_t& remainder, int32_t delta, int32_t num,
                        int32_t den) {
  const int32_t total = delta * num + remainder;
  const int32_t steps = total / den;
  remainder = total - steps * den;
  return static_cast<int16_t>(steps);
}

bool Gestures::ClassifyTwoFinger(uint32_t now, int32_t& dx, int32_t& dy,
                                 MouseOutput* out, size_t& count) {
  if (two_finger_ == TwoFinger::kScroll) {
    return true;
  }
  if (two_finger_ == TwoFinger::kSwiped) {
    return false;
  }
  two_dx_ += dx;
  two_dy_ += dy;
  const int32_t across = abs32(two_dx_);
  const int32_t down = abs32(two_dy_);
  const int32_t slop = config_.tap_slop_mm * config_.units_per_mm;
  if (down > slop && down >= across) {
    two_finger_ = TwoFinger::kScroll;  // mostly vertical: scroll right away
    dx = two_dx_;  // including what was held back while deciding
    dy = two_dy_;
    return true;
  }
  const bool in_time = now - two_finger_start_ms_ <= config_.swipe_max_ms;
  if (in_time && swipe_armed_ &&
      across >= config_.swipe_min_mm * config_.units_per_mm &&
      across >= 2 * down) {
    two_finger_ = TwoFinger::kSwiped;
    swipe_armed_ = false;
    Emit(out, count);
    if (count > 0) {
      out[count - 1].swipe = two_dx_ > 0 ? Swipe::kRight : Swipe::kLeft;
    }
    return false;
  }
  if (!in_time) {
    two_finger_ = TwoFinger::kScroll;  // too slow for a swipe: scroll
    dx = two_dx_;
    dy = two_dy_;
    return true;
  }
  return false;  // still deciding: hold horizontal scrolling back
}

uint32_t Gestures::next_deadline_ms() const {
  const uint32_t lift =
      phase_ == Phase::kIdle ? 0
                             : deadline(last_frame_ms_ + config_.lift_timeout_ms);
  if (tap_release_at_ == 0) {
    return lift;
  }
  if (lift == 0) {
    return tap_release_at_;
  }
  return static_cast<int32_t>(lift - tap_release_at_) < 0 ? lift
                                                           : tap_release_at_;
}

size_t Gestures::OnFrame(const Frame& frame, MouseOutput* out) {
  size_t count = 0;
  const uint32_t now = frame.time_ms;
  last_frame_ms_ = now;

  Track touching[kMaxContacts];
  size_t touching_count = 0;
  for (size_t i = 0; i < kMaxContacts; ++i) {
    const Contact& contact = frame.contacts[i];
    if (contact.touching && contact.confident) {
      // Contacts are tracked by id; a repeated id falls back to the slot
      // number (ids are 3 bits, so slot + 8 cannot clash with a real id).
      uint8_t id = contact.id;
      for (size_t j = 0; j < touching_count; ++j) {
        if (touching[j].id == id) {
          id = static_cast<uint8_t>(8 + i);
          break;
        }
      }
      touching[touching_count++] = {id, contact.x, contact.y};
    }
  }

  const uint8_t previous_buttons = Buttons();
  pad_buttons_ = frame.buttons;

  if (touching_count == 0) {
    if (phase_ != Phase::kIdle) {
      const bool quick = now - touch_start_ms_ <= config_.tap_max_ms;
      if (phase_ == Phase::kTouch && quick) {
        if (max_fingers_ == 1) {
          // Press now, release after the drag window (OnTick).
          tap_button_ = true;
          tap_release_at_ = deadline(now + config_.drag_window_ms);
          Emit(out, count);
        } else {
          const uint8_t click = max_fingers_ == 2 ? kRight : kMiddle;
          pad_buttons_ |= click;
          Emit(out, count);
          pad_buttons_ = frame.buttons;
          Emit(out, count);
        }
      } else if (phase_ == Phase::kDragging) {
        tap_button_ = false;
        Emit(out, count);
        if (drag_tap_ && quick) {
          // Tap, tap: the second tap is a click of its own (double click).
          tap_button_ = true;
          Emit(out, count);
          tap_button_ = false;
          Emit(out, count);
        }
      }
      phase_ = Phase::kIdle;
      track_count_ = 0;
      rem_x_ = rem_y_ = rem_wheel_ = rem_pan_ = 0;
    }
    if (count == 0 && Buttons() != previous_buttons) {
      Emit(out, count);
    }
    return count;
  }

  if (phase_ == Phase::kIdle) {
    touch_start_ms_ = now;
    swipe_armed_ = true;  // one swipe per touch, until all fingers lift
    two_finger_started_ = false;
    max_fingers_ = 0;
    travel_ = 0;
    if (tap_release_at_ != 0) {
      // Touched again right after a tap: keep the button down and drag.
      tap_release_at_ = 0;
      phase_ = Phase::kDragging;
      drag_tap_ = true;
    } else {
      phase_ = Phase::kTouch;
    }
  }
  if (touching_count > max_fingers_) {
    max_fingers_ = touching_count;
  }

  // Same fingers as last frame: move by their average delta. Otherwise only
  // re-baseline, so a finger landing or lifting never jumps the pointer.
  bool same = touching_count == track_count_;
  int32_t sum_dx = 0;
  int32_t sum_dy = 0;
  for (size_t i = 0; same && i < touching_count; ++i) {
    size_t j = 0;
    while (j < track_count_ && tracks_[j].id != touching[i].id) {
      ++j;
    }
    if (j == track_count_) {
      same = false;
      break;
    }
    sum_dx += touching[i].last_x - tracks_[j].last_x;
    sum_dy += touching[i].last_y - tracks_[j].last_y;
  }
  for (size_t i = 0; i < touching_count; ++i) {
    tracks_[i] = touching[i];
  }
  track_count_ = touching_count;
  // The two-finger decision spans the whole touch, so a finger dropping out
  // for a frame neither restarts it nor allows a second swipe.
  if (touching_count >= 2 && !two_finger_started_) {
    two_finger_started_ = true;
    two_finger_ = TwoFinger::kUndecided;
    two_finger_start_ms_ = now;
    two_dx_ = two_dy_ = 0;
  }

  if (same) {
    int32_t dx = sum_dx / static_cast<int32_t>(touching_count);
    int32_t dy = sum_dy / static_cast<int32_t>(touching_count);
    travel_ += abs32(dx) + abs32(dy);
    if (travel_ > config_.tap_slop_mm * config_.units_per_mm) {
      if (phase_ == Phase::kTouch) {
        phase_ = Phase::kMoving;
      }
      drag_tap_ = false;
    }

    const bool pointing = touching_count == 1 && (phase_ == Phase::kMoving ||
                                                  (phase_ == Phase::kDragging &&
                                                   !drag_tap_));
    if (pointing) {
      const int16_t mx =
          Scale(rem_x_, dx, config_.pointer_num, config_.pointer_den);
      const int16_t my =
          Scale(rem_y_, dy, config_.pointer_num, config_.pointer_den);
      if (mx != 0 || my != 0) {
        Emit(out, count, mx, my);
      }
    } else if (touching_count >= 2 && phase_ == Phase::kMoving &&
               ClassifyTwoFinger(now, dx, dy, out, count)) {
      const int16_t y_steps = Scale(rem_wheel_, dy, 1, config_.scroll_step);
      const int16_t x_steps = Scale(rem_pan_, dx, 1, config_.scroll_step);
      // Wheel > 0 scrolls up; natural scrolling makes content follow fingers.
      const int16_t wheel = config_.natural_scroll ? y_steps : -y_steps;
      const int16_t pan = config_.natural_scroll ? -x_steps : x_steps;
      if (wheel != 0 || pan != 0) {
        Emit(out, count, 0, 0, wheel, pan);
      }
    }
  }

  if (count == 0 && Buttons() != previous_buttons) {
    Emit(out, count);
  }
  return count;
}

size_t Gestures::OnTick(uint32_t time_ms, MouseOutput* out) {
  size_t count = 0;
  if (phase_ != Phase::kIdle &&
      reached(time_ms, last_frame_ms_ + config_.lift_timeout_ms)) {
    Frame lifted;
    lifted.buttons = pad_buttons_;
    lifted.time_ms = time_ms;
    count = OnFrame(lifted, out);
  }
  if (tap_release_at_ != 0 && phase_ != Phase::kDragging &&
      reached(time_ms, tap_release_at_)) {
    tap_release_at_ = 0;
    tap_button_ = false;
    Emit(out, count);
  }
  return count;
}

size_t Gestures::Reset(MouseOutput* out) {
  size_t count = 0;
  const bool held = Buttons() != 0;
  phase_ = Phase::kIdle;
  track_count_ = 0;
  pad_buttons_ = 0;
  tap_button_ = false;
  tap_release_at_ = 0;
  drag_tap_ = false;
  rem_x_ = rem_y_ = rem_wheel_ = rem_pan_ = 0;
  if (held) {
    Emit(out, count);
  }
  return count;
}

}  // namespace touchpad
