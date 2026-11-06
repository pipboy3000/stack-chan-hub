#include <cassert>
#include "motion_diagnostics.h"

int main() {
  MotionDiagnostics metrics;
  touchpad::Frame frame{};
  frame.contacts[0].touching = true;
  frame.contacts[0].id = 1;
  frame.time_ms = UINT32_MAX - 1;
  metrics.OnFrame(frame);
  frame.time_ms = 1;  // wraps by 3 ms
  metrics.OnFrame(frame);
  metrics.OnFrame(frame);  // same millisecond
  frame.time_ms = 10;
  metrics.OnFrame(frame);
  frame.time_ms = 25;
  metrics.OnFrame(frame);
  frame.time_ms = 60;
  metrics.OnFrame(frame);
  frame.time_ms = 300;  // timeout is excluded
  metrics.OnFrame(frame);
  frame.contacts[1].touching = true;  // multi-touch breaks the sequence
  metrics.OnFrame(frame);
  frame.contacts[1].touching = false;
  frame.time_ms = 301;
  metrics.OnFrame(frame);
  assert(metrics.frames == 8);
  assert(metrics.gap_zero == 1 && metrics.gap_5 == 1 && metrics.gap_10 == 1);
  assert(metrics.gap_20 == 1 && metrics.gap_long == 1);
  touchpad::MouseOutput output{};
  metrics.OnOutput(output);
  output.dx = -12; output.dy = 5;
  metrics.OnOutput(output);
  assert(metrics.moves == 1 && metrics.distance == 17);
  metrics.ResetContact();
  ++frame.time_ms;
  metrics.OnFrame(frame);
  assert(metrics.gap_5 == 1);
}
