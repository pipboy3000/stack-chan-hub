#pragma once

#include <atomic>
#include <cstdint>
#include "touchpad_gestures.h"

// Cumulative counters written by router_task and read by the HTTP task. Frame
// gaps use router processing time, not the physical USB arrival timestamp.
struct MotionDiagnostics {
  std::atomic<uint32_t> frames{0}, gap_zero{0}, gap_5{0}, gap_10{0},
      gap_20{0}, gap_long{0}, moves{0}, distance{0}, mouse_sent{0},
      mouse_retry{0};

  void OnFrame(const touchpad::Frame& frame) {
    const touchpad::Contact* finger = nullptr;
    for (const auto& contact : frame.contacts) {
      if (!contact.touching || !contact.confident) continue;
      if (finger != nullptr) { previous_ = false; return; }
      finger = &contact;
    }
    if (finger == nullptr) { previous_ = false; return; }
    ++frames;
    if (previous_ && finger->id == previous_id_) {
      const uint32_t gap = frame.time_ms - previous_ms_;
      // Do not treat a missing-lift timeout as an uninterrupted movement.
      if (gap <= 150) {
        if (gap == 0) ++gap_zero;
        else if (gap <= 5) ++gap_5;
        else if (gap <= 10) ++gap_10;
        else if (gap <= 20) ++gap_20;
        else ++gap_long;
      }
    }
    previous_ = true;
    previous_id_ = finger->id;
    previous_ms_ = frame.time_ms;
  }

  void OnOutput(const touchpad::MouseOutput& output) {
    if (!output.dx && !output.dy) return;
    ++moves;
    const int32_t x = output.dx, y = output.dy;
    distance += static_cast<uint32_t>((x < 0 ? -x : x) + (y < 0 ? -y : y));
  }

  void ResetContact() { previous_ = false; }

 private:
  bool previous_ = false;
  uint8_t previous_id_ = 0;
  uint32_t previous_ms_ = 0;
};
