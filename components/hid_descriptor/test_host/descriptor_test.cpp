#include "hid_descriptor.h"

#include <cstdio>

#include "voyager_descriptors.h"

using namespace hid_descriptor;

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

bool test_boot_keyboard_interface() {
  Interface parsed;
  CHECK(Parse(kVoyagerInterface0, sizeof(kVoyagerInterface0), parsed));
  CHECK(!parsed.uses_report_ids);
  CHECK(parsed.count == 1);
  CHECK(parsed.reports[0].report_id == 0);
  CHECK(parsed.reports[0].kind == ReportKind::kKeyboard);
  CHECK(parsed.reports[0].bit_length == 64);
  return true;
}

bool test_vendor_interface() {
  Interface parsed;
  CHECK(Parse(kVoyagerInterface1, sizeof(kVoyagerInterface1), parsed));
  CHECK(parsed.count == 1);
  CHECK(parsed.reports[0].kind == ReportKind::kVendor);
  CHECK(parsed.input_mode_report_id == 0);
  return true;
}

bool test_extra_keys_interface() {
  Interface parsed;
  CHECK(Parse(kVoyagerInterface2, sizeof(kVoyagerInterface2), parsed));
  CHECK(parsed.uses_report_ids);

  const InputReport* system = parsed.Find(3);
  CHECK(system != nullptr);
  CHECK(system->kind == ReportKind::kSystem);
  CHECK(system->usage.present);
  CHECK(system->usage.bit_offset == 0);
  CHECK(system->usage.bit_size == 16);

  const InputReport* consumer = parsed.Find(4);
  CHECK(consumer != nullptr);
  CHECK(consumer->kind == ReportKind::kConsumer);
  CHECK(consumer->usage.bit_offset == 0);
  CHECK(consumer->usage.bit_size == 16);

  const InputReport* nkro = parsed.Find(6);
  CHECK(nkro != nullptr);
  CHECK(nkro->kind == ReportKind::kKeyboard);
  CHECK(nkro->bit_length == 8 + 240);
  return true;
}

bool test_navigator_interface() {
  Interface parsed;
  CHECK(Parse(kVoyagerInterface3, sizeof(kVoyagerInterface3), parsed));
  CHECK(parsed.uses_report_ids);

  const InputReport* touchpad = parsed.Find(1);
  CHECK(touchpad != nullptr);
  CHECK(touchpad->kind == ReportKind::kDigitizer);

  const InputReport* mouse = parsed.Find(6);
  CHECK(mouse != nullptr);
  CHECK(mouse->kind == ReportKind::kMouse);
  CHECK(mouse->bit_length == 24);
  CHECK(mouse->buttons.present);
  CHECK(mouse->buttons.bit_offset == 0);
  CHECK(mouse->buttons.bit_size == 3);
  CHECK(mouse->x.present && mouse->x.bit_offset == 8 && mouse->x.bit_size == 8);
  CHECK(mouse->x.is_signed);
  CHECK(mouse->y.present && mouse->y.bit_offset == 16);
  CHECK(!mouse->wheel.present);

  // Feature-only reports are not listed as inputs.
  CHECK(parsed.Find(2) == nullptr);
  CHECK(parsed.Find(4) == nullptr);
  return true;
}

bool test_extract_values() {
  Interface parsed;
  CHECK(Parse(kVoyagerInterface3, sizeof(kVoyagerInterface3), parsed));
  const InputReport* mouse = parsed.Find(6);
  CHECK(mouse != nullptr);
  const uint8_t payload[] = {0x05, 0xFB, 0x07};  // left+middle, x=-5, y=7
  CHECK(Extract(payload, sizeof(payload), mouse->buttons) == 5);
  CHECK(Extract(payload, sizeof(payload), mouse->x) == -5);
  CHECK(Extract(payload, sizeof(payload), mouse->y) == 7);
  CHECK(Extract(payload, sizeof(payload), mouse->wheel) == 0);
  // Truncated payload reads as absent.
  CHECK(Extract(payload, 2, mouse->y) == 0);

  CHECK(Parse(kVoyagerInterface2, sizeof(kVoyagerInterface2), parsed));
  const uint8_t volume_up[] = {0xE9, 0x00};
  CHECK(Extract(volume_up, sizeof(volume_up), parsed.Find(4)->usage) == 0xE9);
  return true;
}

bool test_wheel_and_pan_mouse() {
  // Typical 5-button mouse with wheel and AC Pan (4-byte usage with page).
  const uint8_t descriptor[] = {
      0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00,
      0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01, 0x95, 0x05,
      0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x03, 0x81, 0x01, 0x05, 0x01,
      0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08,
      0x95, 0x03, 0x81, 0x06, 0x0B, 0x38, 0x02, 0x0C, 0x00, 0x95, 0x01, 0x81,
      0x06, 0xC0, 0xC0,
  };
  Interface parsed;
  CHECK(Parse(descriptor, sizeof(descriptor), parsed));
  const InputReport* mouse = parsed.Find(2);
  CHECK(mouse != nullptr);
  CHECK(mouse->kind == ReportKind::kMouse);
  CHECK(mouse->buttons.bit_size == 5);
  CHECK(mouse->wheel.present && mouse->wheel.bit_offset == 24);
  CHECK(mouse->pan.present && mouse->pan.bit_offset == 32);
  const uint8_t payload[] = {0x01, 0x02, 0xFE, 0xFF, 0x01};
  CHECK(Extract(payload, sizeof(payload), mouse->wheel) == -1);
  CHECK(Extract(payload, sizeof(payload), mouse->pan) == 1);
  return true;
}

bool test_malformed_input_is_rejected() {
  Interface parsed;
  const uint8_t truncated[] = {0x05, 0x01, 0x09};
  CHECK(!Parse(truncated, sizeof(truncated), parsed));
  const uint8_t pop_without_push[] = {0xB4};
  CHECK(!Parse(pop_without_push, sizeof(pop_without_push), parsed));
  return true;
}

bool test_navigator_touchpad_contacts() {
  Interface parsed;
  CHECK(Parse(kVoyagerInterface3, sizeof(kVoyagerInterface3), parsed));
  const InputReport* pad = parsed.Find(1);
  CHECK(pad != nullptr);
  CHECK(pad->bit_length == 120);
  CHECK(pad->finger_count == 2);
  for (int i = 0; i < 2; ++i) {
    const Finger& finger = pad->fingers[i];
    const uint16_t base = i * 48;
    CHECK(finger.confidence.present && finger.confidence.bit_offset == base);
    CHECK(finger.tip.present && finger.tip.bit_offset == base + 1);
    CHECK(finger.contact_id.bit_offset == base + 8);
    CHECK(finger.contact_id.bit_size == 3);
    CHECK(finger.x.bit_offset == base + 16 && finger.x.bit_size == 16);
    CHECK(finger.y.bit_offset == base + 32 && finger.y.bit_size == 16);
    CHECK(finger.x.logical_max == 0x800);
    CHECK(!finger.x.is_signed);
  }
  CHECK(pad->scan_time.bit_offset == 96 && pad->scan_time.bit_size == 16);
  CHECK(pad->contact_count.bit_offset == 112);
  CHECK(pad->contact_count.bit_size == 4);
  CHECK(pad->buttons.bit_offset == 116 && pad->buttons.bit_size == 3);
  CHECK(parsed.input_mode_report_id == 4);
  CHECK(parsed.selective_report_id == 5);

  // Finger 1 touching at (0x400, 0x123), contact id 2, one contact.
  uint8_t payload[15] = {};
  payload[6] = 0x03;
  payload[7] = 0x02;
  payload[8] = 0x00;
  payload[9] = 0x04;
  payload[10] = 0x23;
  payload[11] = 0x01;
  payload[14] = 0x01;
  CHECK(Extract(payload, sizeof(payload), pad->fingers[1].tip) == 1);
  CHECK(Extract(payload, sizeof(payload), pad->fingers[1].contact_id) == 2);
  CHECK(Extract(payload, sizeof(payload), pad->fingers[1].x) == 0x400);
  CHECK(Extract(payload, sizeof(payload), pad->fingers[1].y) == 0x123);
  CHECK(Extract(payload, sizeof(payload), pad->fingers[0].tip) == 0);
  CHECK(Extract(payload, sizeof(payload), pad->contact_count) == 1);
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
  run_test("boot_keyboard_interface", test_boot_keyboard_interface);
  run_test("vendor_interface", test_vendor_interface);
  run_test("extra_keys_interface", test_extra_keys_interface);
  run_test("navigator_interface", test_navigator_interface);
  run_test("extract_values", test_extract_values);
  run_test("wheel_and_pan_mouse", test_wheel_and_pan_mouse);
  run_test("malformed_input_is_rejected", test_malformed_input_is_rejected);
  run_test("navigator_touchpad_contacts", test_navigator_touchpad_contacts);
  if (failures != 0) {
    std::printf("%d test assertion(s) failed\n", failures);
    return 1;
  }
  std::printf("All tests passed\n");
  return 0;
}
