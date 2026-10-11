// Minimal HID report descriptor parser (spec v0.4, chapter 10).
//
// Finds the input reports of one USB HID interface, classifies each by its
// top-level application collection, and locates the fields the hub forwards:
// mouse buttons/X/Y/wheel/pan, the Consumer/System Control usage, and the
// contact slots of a precision touchpad.
// Pure logic: no ESP-IDF, heap allocation or I/O.
#pragma once

#include <cstddef>
#include <cstdint>

namespace hid_descriptor {

enum class ReportKind : uint8_t {
  kUnknown,
  kKeyboard,
  kMouse,
  kConsumer,
  kSystem,
  kDigitizer,
  kVendor,
};

// Location of one field inside an input report, excluding the report ID byte.
struct Field {
  bool present = false;
  uint16_t bit_offset = 0;
  uint8_t bit_size = 0;
  bool is_signed = false;
  int32_t logical_max = 0;
};

// One contact slot of a digitizer (touchpad) report.
struct Finger {
  Field tip;         // Tip Switch: finger touching
  Field confidence;  // Confidence: not a palm
  Field contact_id;
  Field x;
  Field y;
};

struct InputReport {
  uint8_t report_id = 0;  // 0 when the interface uses no report IDs.
  ReportKind kind = ReportKind::kUnknown;
  uint16_t bit_length = 0;  // Payload length, excluding the report ID byte.
  // Mouse: buttons is a bitmap field (one bit per button, first button LSB).
  Field buttons;
  Field x;
  Field y;
  Field wheel;
  Field pan;
  // Consumer / System Control: the first data field (the pressed usage).
  Field usage;
  // Digitizer: contact slots in report order, plus the frame-level fields.
  static constexpr size_t kMaxFingers = 5;
  Finger fingers[kMaxFingers];
  uint8_t finger_count = 0;
  Field contact_count;
  Field scan_time;
};

struct Interface {
  static constexpr size_t kMaxReports = 16;
  InputReport reports[kMaxReports];
  size_t count = 0;
  bool uses_report_ids = false;
  // Precision touchpad configuration feature reports (0 = absent).
  uint8_t input_mode_report_id = 0;        // Input Mode: 3 = touchpad
  uint8_t selective_report_id = 0;         // Surface / Button Switch

  // Returns the input report for `report_id`, or nullptr.
  const InputReport* Find(uint8_t report_id) const;
};

// Parses `descriptor`. Returns false on malformed input or overflow.
bool Parse(const uint8_t* descriptor, size_t length, Interface& out);

// Reads `field` from a report payload (the bytes after the report ID).
// Returns 0 when the field is absent or outside `length`.
int32_t Extract(const uint8_t* payload, size_t length, const Field& field);

}  // namespace hid_descriptor
