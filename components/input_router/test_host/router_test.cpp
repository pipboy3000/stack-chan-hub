#include "input_router.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>

using namespace input_router;

namespace {

const char* current_test = "";
int failures = 0;

#define CHECK(condition)                                                    \
  do {                                                                      \
    if (!(condition)) {                                                     \
      std::printf("FAIL %s:%d: %s\n", current_test, __LINE__, #condition); \
      ++failures;                                                           \
      return false;                                                        \
    }                                                                       \
  } while (false)

KeyboardReport report(uint8_t modifiers,
                      std::initializer_list<uint8_t> keys) {
  KeyboardReport result{};
  result.modifiers = modifiers;
  size_t i = 0;
  for (uint8_t key : keys) {
    if (i >= kKeyCount) {
      break;
    }
    result.keys[i++] = key;
  }
  return result;
}

bool action_is(const Actions& actions, size_t index, ActionType type,
              uint8_t slot) {
  return index < actions.count && actions.items[index].type == type &&
         actions.items[index].slot == slot;
}

void connect(InputRouter& router, uint8_t slot) {
  Actions actions{};
  router.OnBleConnected(slot, actions);
}

bool test_special_key_removal_and_compaction() {
  InputRouter router(1);
  connect(router, 1);
  Actions actions{};
  router.OnUsbKeyboardReport(report(0, {0x71}), actions);
  CHECK(actions.count == 1);
  CHECK(action_is(actions, 0, ActionType::kSendKeyboard, 1));
  CHECK(actions.items[0].keyboard.keys[0] == 0);

  router.OnUsbKeyboardReport(report(0, {0x04, 0x71, 0x06}), actions);
  CHECK(actions.count == 1);
  CHECK(action_is(actions, 0, ActionType::kSendKeyboard, 1));
  CHECK(actions.items[0].keyboard.keys[0] == 0x04);
  CHECK(actions.items[0].keyboard.keys[1] == 0x06);
  for (size_t i = 2; i < kKeyCount; ++i) {
    CHECK(actions.items[0].keyboard.keys[i] == 0);
  }
  return true;
}

bool test_modifier_is_preserved_with_switch_usage() {
  InputRouter router(1);
  connect(router, 1);
  Actions actions{};
  router.OnUsbKeyboardReport(report(0x01, {0x71, 0x06}), actions);
  CHECK(actions.count == 1);
  CHECK(action_is(actions, 0, ActionType::kSendKeyboard, 1));
  CHECK(actions.items[0].keyboard.modifiers == 0x01);
  CHECK(actions.items[0].keyboard.keys[0] == 0x06);
  for (size_t i = 1; i < kKeyCount; ++i) {
    CHECK(actions.items[0].keyboard.keys[i] == 0);
  }
  return true;
}

bool test_switch_edge_hold_release_and_repress() {
  InputRouter router(0);
  connect(router, 0);
  Actions actions{};
  router.OnUsbKeyboardReport(report(0, {0x71}), actions);
  CHECK(actions.count == 3);
  CHECK(action_is(actions, 0, ActionType::kSendAllReleased, 0));
  CHECK(action_is(actions, 1, ActionType::kStartConnect, 1));
  CHECK(action_is(actions, 2, ActionType::kNotifyUi, 1));

  router.OnUsbKeyboardReport(report(0, {0x71}), actions);
  CHECK(actions.count == 0);
  router.OnUsbKeyboardReport(report(0, {}), actions);
  CHECK(actions.count == 0);
  router.OnUsbKeyboardReport(report(0, {0x71}), actions);
  CHECK(actions.count == 2);
  CHECK(action_is(actions, 0, ActionType::kStartConnect, 1));
  CHECK(action_is(actions, 1, ActionType::kNotifyUi, 1));
  return true;
}

bool test_phantom_does_not_clear_held_switch_edge() {
  InputRouter router(0);
  connect(router, 0);
  Actions actions{};
  router.OnUsbKeyboardReport(report(0, {0x71}), actions);
  CHECK(actions.count == 3);
  CHECK(action_is(actions, 1, ActionType::kStartConnect, 1));

  router.OnUsbKeyboardReport(
      report(0, {kErrorRollOver, kErrorRollOver, kErrorRollOver,
                 kErrorRollOver, kErrorRollOver, kErrorRollOver}),
      actions);
  CHECK(actions.count == 0);
  router.OnUsbKeyboardReport(report(0, {0x71}), actions);
  CHECK(actions.count == 0);
  CHECK(router.active_slot() == 1);
  return true;
}

bool test_phantom_produces_no_actions() {
  InputRouter router(0);
  connect(router, 0);
  Actions actions{};
  router.OnUsbKeyboardReport(report(0, {0x04}), actions);
  CHECK(actions.count == 1);
  CHECK(action_is(actions, 0, ActionType::kSendKeyboard, 0));

  router.OnUsbKeyboardReport(
      report(0, {kErrorRollOver, kErrorRollOver, kErrorRollOver,
                 kErrorRollOver, kErrorRollOver, kErrorRollOver}),
      actions);
  CHECK(actions.count == 0);
  return true;
}

bool test_phantom_does_not_release_forwarding_hold() {
  InputRouter router(0);
  connect(router, 0);
  Actions actions{};
  router.OnUsbKeyboardReport(report(0, {0x71}), actions);
  CHECK(router.forwarding_on_hold());
  router.OnBleConnected(1, actions);
  CHECK(router.forwarding_on_hold());

  router.OnUsbKeyboardReport(
      report(0, {kErrorRollOver, kErrorRollOver, kErrorRollOver,
                 kErrorRollOver, kErrorRollOver, kErrorRollOver}),
      actions);
  CHECK(actions.count == 0);
  CHECK(router.forwarding_on_hold());
  router.OnUsbKeyboardReport(report(0x01, {0x06}), actions);
  CHECK(actions.count == 0);
  CHECK(router.forwarding_on_hold());
  return true;
}

bool test_switch_action_order_with_connected_old_slot() {
  InputRouter router(0);
  connect(router, 0);
  Actions actions{};
  router.OnUsbKeyboardReport(report(0, {0x72}), actions);
  CHECK(actions.count == 3);
  CHECK(action_is(actions, 0, ActionType::kSendAllReleased, 0));
  CHECK(action_is(actions, 1, ActionType::kStartConnect, 2));
  CHECK(action_is(actions, 2, ActionType::kNotifyUi, 2));
  return true;
}

bool test_switch_skips_release_when_old_slot_disconnected() {
  InputRouter router(0);
  Actions actions{};
  router.OnUsbKeyboardReport(report(0, {0x71}), actions);
  CHECK(actions.count == 2);
  CHECK(action_is(actions, 0, ActionType::kStartConnect, 1));
  CHECK(action_is(actions, 1, ActionType::kNotifyUi, 1));
  return true;
}

bool test_switch_hold_survives_ble_connect_until_all_keys_release() {
  InputRouter router(0);
  connect(router, 0);
  Actions actions{};
  router.OnUsbKeyboardReport(report(0, {0x71}), actions);
  CHECK(actions.count == 3);

  router.OnBleConnected(1, actions);
  CHECK(router.link_state() == LinkState::kConnected);
  CHECK(router.forwarding_on_hold());
  CHECK(actions.count == 2);
  CHECK(action_is(actions, 0, ActionType::kSendAllReleased, 1));
  CHECK(action_is(actions, 1, ActionType::kNotifyUi, 1));

  router.OnUsbKeyboardReport(report(0x01, {0x06}), actions);
  CHECK(actions.count == 0);
  CHECK(router.forwarding_on_hold());
  router.OnUsbKeyboardReport(report(0, {}), actions);
  CHECK(actions.count == 1);
  CHECK(action_is(actions, 0, ActionType::kSendKeyboard, 1));
  CHECK(actions.items[0].keyboard.modifiers == 0);
  CHECK(actions.items[0].keyboard.keys[0] == 0);
  CHECK(!router.forwarding_on_hold());

  router.OnUsbKeyboardReport(report(0, {0x06}), actions);
  CHECK(actions.count == 1);
  CHECK(actions.items[0].keyboard.keys[0] == 0x06);
  return true;
}

bool test_active_ble_connected_sends_release_and_notifies() {
  InputRouter router(2);
  Actions actions{};
  router.OnBleConnected(2, actions);
  CHECK(router.link_state() == LinkState::kConnected);
  CHECK(actions.count == 2);
  CHECK(action_is(actions, 0, ActionType::kSendAllReleased, 2));
  CHECK(action_is(actions, 1, ActionType::kNotifyUi, 2));
  return true;
}

bool test_non_active_ble_events_are_ignored() {
  InputRouter router(0);
  Actions actions{};
  router.OnUsbKeyboardReport(report(0, {0x70}), actions);
  CHECK(router.link_state() == LinkState::kConnecting);
  router.OnBleConnected(1, actions);
  CHECK(actions.count == 0);
  router.OnBleDisconnected(1, actions);
  CHECK(actions.count == 0);
  router.OnConnectTimeout(1, actions);
  CHECK(actions.count == 0);
  CHECK(router.active_slot() == 0);
  CHECK(router.link_state() == LinkState::kConnecting);
  return true;
}

bool test_unconnected_input_is_dropped_but_switches_work() {
  InputRouter router(0);
  Actions actions{};
  router.OnUsbKeyboardReport(report(0, {0x04}), actions);
  CHECK(actions.count == 0);
  router.OnUsbKeyboardReport(report(0, {0x70}), actions);
  CHECK(actions.count == 2);
  CHECK(action_is(actions, 0, ActionType::kStartConnect, 0));
  router.OnUsbKeyboardReport(report(0, {0x04}), actions);
  CHECK(actions.count == 0);
  router.OnUsbKeyboardReport(report(0, {}), actions);
  router.OnUsbKeyboardReport(report(0, {0x72}), actions);
  CHECK(actions.count == 2);
  CHECK(action_is(actions, 0, ActionType::kStartConnect, 2));
  CHECK(action_is(actions, 1, ActionType::kNotifyUi, 2));
  return true;
}

bool test_timeout_disconnects_and_router_remains_switchable() {
  InputRouter router(1);
  Actions actions{};
  router.OnUsbKeyboardReport(report(0, {0x71}), actions);
  CHECK(router.link_state() == LinkState::kConnecting);
  router.OnConnectTimeout(1, actions);
  CHECK(router.link_state() == LinkState::kDisconnected);
  CHECK(actions.count == 1);
  CHECK(action_is(actions, 0, ActionType::kNotifyUi, 1));

  router.OnUsbKeyboardReport(report(0, {}), actions);
  router.OnUsbKeyboardReport(report(0, {0x72}), actions);
  CHECK(router.active_slot() == 2);
  CHECK(router.link_state() == LinkState::kConnecting);
  CHECK(actions.count == 2);
  CHECK(action_is(actions, 0, ActionType::kStartConnect, 2));
  return true;
}

bool test_same_slot_connected_is_noop_and_disconnected_retries() {
  InputRouter connected_router(0);
  connect(connected_router, 0);
  Actions actions{};
  connected_router.OnUsbKeyboardReport(report(0, {0x70}), actions);
  CHECK(actions.count == 1);
  CHECK(action_is(actions, 0, ActionType::kSendKeyboard, 0));

  InputRouter disconnected_router(1);
  disconnected_router.OnUsbKeyboardReport(report(0, {0x71}), actions);
  CHECK(actions.count == 2);
  CHECK(action_is(actions, 0, ActionType::kStartConnect, 1));
  CHECK(action_is(actions, 1, ActionType::kNotifyUi, 1));
  return true;
}

bool test_usb_disconnect_and_reset_release_and_clear_state() {
  InputRouter router(0);
  connect(router, 0);
  Actions actions{};
  router.OnUsbKeyboardReport(report(0, {0x71}), actions);
  CHECK(actions.count == 3);
  CHECK(router.link_state() == LinkState::kConnecting);

  router.OnUsbDisconnected(actions);
  CHECK(actions.count == 0);
  CHECK(router.link_state() == LinkState::kConnecting);
  CHECK(!router.forwarding_on_hold());

  // F22 was pressed when USB disconnected; it must be a new edge afterward.
  router.OnUsbKeyboardReport(report(0, {0x71}), actions);
  CHECK(actions.count == 2);
  CHECK(action_is(actions, 0, ActionType::kStartConnect, 1));
  CHECK(action_is(actions, 1, ActionType::kNotifyUi, 1));

  router.OnBleConnected(1, actions);
  CHECK(actions.count == 2);
  CHECK(action_is(actions, 0, ActionType::kSendAllReleased, 1));
  router.Reset(actions);
  CHECK(actions.count == 1);
  CHECK(action_is(actions, 0, ActionType::kSendAllReleased, 1));
  CHECK(router.link_state() == LinkState::kConnected);
  CHECK(!router.forwarding_on_hold());
  router.OnUsbDisconnected(actions);
  CHECK(actions.count == 1);
  CHECK(action_is(actions, 0, ActionType::kSendAllReleased, 1));
  CHECK(router.link_state() == LinkState::kConnected);
  CHECK(!router.forwarding_on_hold());
  return true;
}

bool test_multiple_switches_choose_first_usage_in_report_order() {
  InputRouter router(0);
  connect(router, 0);
  Actions actions{};
  router.OnUsbKeyboardReport(report(0, {0x72, 0x70}), actions);
  CHECK(router.active_slot() == 2);
  CHECK(actions.count == 3);
  CHECK(action_is(actions, 1, ActionType::kStartConnect, 2));

  router.OnUsbKeyboardReport(report(0, {}), actions);
  router.OnUsbKeyboardReport(report(0, {0x70, 0x72}), actions);
  CHECK(router.active_slot() == 0);
  CHECK(actions.count == 2);
  CHECK(action_is(actions, 0, ActionType::kStartConnect, 0));
  return true;
}

bool test_action_capacity_and_invalid_slots() {
  InputRouter router(0);
  Actions actions{};
  router.OnUsbKeyboardReport(report(0, {0x70, 0x71, 0x72}), actions);
  CHECK(actions.count <= Actions::kCapacity);
  router.OnBleConnected(255, actions);
  CHECK(actions.count == 0);
  router.OnBleDisconnected(255, actions);
  CHECK(actions.count == 0);
  router.OnConnectTimeout(255, actions);
  CHECK(actions.count == 0);
  InputRouter invalid_initial_slot(255);
  CHECK(invalid_initial_slot.active_slot() == 0);
  return true;
}

bool test_consumer_system_mouse_forwarded_when_connected() {
  InputRouter router(0);
  connect(router, 0);
  Actions actions{};
  router.OnUsbConsumerReport(0x00E9, actions);
  CHECK(actions.count == 1);
  CHECK(action_is(actions, 0, ActionType::kSendConsumer, 0));
  CHECK(actions.items[0].usage == 0x00E9);

  router.OnUsbSystemReport(0x0082, actions);
  CHECK(actions.count == 1);
  CHECK(action_is(actions, 0, ActionType::kSendSystem, 0));
  CHECK(actions.items[0].usage == 0x0082);

  MouseReport mouse;
  mouse.buttons = 1;
  mouse.x = -5;
  mouse.y = 7;
  router.OnUsbMouseReport(mouse, actions);
  CHECK(actions.count == 1);
  CHECK(action_is(actions, 0, ActionType::kSendMouse, 0));
  CHECK(actions.items[0].mouse.buttons == 1);
  CHECK(actions.items[0].mouse.x == -5);
  CHECK(actions.items[0].mouse.y == 7);
  return true;
}

bool test_consumer_system_mouse_dropped_when_not_connected() {
  InputRouter router(0);
  Actions actions{};
  router.OnUsbConsumerReport(0x00E9, actions);
  CHECK(actions.count == 0);
  router.OnUsbSystemReport(0x0082, actions);
  CHECK(actions.count == 0);
  MouseReport mouse;
  mouse.x = 3;
  router.OnUsbMouseReport(mouse, actions);
  CHECK(actions.count == 0);
  return true;
}

bool test_hold_waits_for_consumer_and_mouse_buttons() {
  InputRouter router(0);
  connect(router, 0);
  Actions actions{};
  // Volume key and left button held while switching to slot 1.
  router.OnUsbConsumerReport(0x00E9, actions);
  MouseReport pressed;
  pressed.buttons = 1;
  router.OnUsbMouseReport(pressed, actions);
  router.OnUsbKeyboardReport(report(0, {0x71}), actions);
  CHECK(router.forwarding_on_hold());
  connect(router, 1);

  // Keyboard idle, but consumer and button still down: nothing forwarded.
  router.OnUsbKeyboardReport(report(0, {}), actions);
  CHECK(actions.count == 0);
  MouseReport motion = pressed;
  motion.x = 10;
  router.OnUsbMouseReport(motion, actions);
  CHECK(actions.count == 0);
  router.OnUsbConsumerReport(0, actions);
  CHECK(actions.count == 0);
  CHECK(router.forwarding_on_hold());

  // Releasing the last button makes it idle; that report is forwarded.
  router.OnUsbMouseReport(MouseReport{}, actions);
  CHECK(!router.forwarding_on_hold());
  CHECK(actions.count == 1);
  CHECK(action_is(actions, 0, ActionType::kSendMouse, 1));
  CHECK(actions.items[0].mouse.buttons == 0);
  return true;
}

bool test_mouse_motion_without_buttons_does_not_block_hold_release() {
  InputRouter router(0);
  connect(router, 0);
  Actions actions{};
  router.OnUsbKeyboardReport(report(0, {0x71}), actions);
  connect(router, 1);
  MouseReport motion;
  motion.x = 4;
  // Keyboard still holds F22: motion is dropped.
  router.OnUsbMouseReport(motion, actions);
  CHECK(actions.count == 0);
  router.OnUsbKeyboardReport(report(0, {}), actions);
  CHECK(!router.forwarding_on_hold());
  router.OnUsbMouseReport(motion, actions);
  CHECK(actions.count == 1);
  CHECK(action_is(actions, 0, ActionType::kSendMouse, 1));
  return true;
}

bool test_usb_disconnect_clears_consumer_and_mouse_state() {
  InputRouter router(0);
  connect(router, 0);
  Actions actions{};
  router.OnUsbConsumerReport(0x00E9, actions);
  router.OnUsbKeyboardReport(report(0, {0x71}), actions);
  CHECK(router.forwarding_on_hold());
  router.OnUsbDisconnected(actions);
  CHECK(!router.forwarding_on_hold());
  connect(router, 1);
  router.OnUsbConsumerReport(0x00EA, actions);
  CHECK(actions.count == 1);
  CHECK(action_is(actions, 0, ActionType::kSendConsumer, 1));
  return true;
}

bool test_shortcut_adds_to_held_keys_and_restores() {
  InputRouter router(0);
  connect(router, 0);
  Actions actions{};
  router.OnUsbKeyboardReport(report(0x02, {0x04}), actions);  // Shift+A held
  router.OnShortcut(0x08, 0x2F, actions);                     // Cmd+[
  CHECK(actions.count == 2);
  CHECK(action_is(actions, 0, ActionType::kSendKeyboard, 0));
  CHECK(actions.items[0].keyboard.modifiers == 0x0A);
  CHECK(actions.items[0].keyboard.keys[0] == 0x04);
  CHECK(actions.items[0].keyboard.keys[1] == 0x2F);
  CHECK(actions.items[1].keyboard.modifiers == 0x02);
  CHECK(actions.items[1].keyboard.keys[0] == 0x04);
  CHECK(actions.items[1].keyboard.keys[1] == 0);
  return true;
}

bool test_shortcut_dropped_when_not_forwarding() {
  InputRouter router(0);
  Actions actions{};
  router.OnShortcut(0x08, 0x2F, actions);
  CHECK(actions.count == 0);
  connect(router, 0);
  router.OnUsbKeyboardReport(report(0, {0x71}), actions);  // switch: hold
  connect(router, 1);
  router.OnShortcut(0x08, 0x2F, actions);
  CHECK(actions.count == 0);  // F22 still physically down
  return true;
}

bool test_select_slot_from_ui() {
  InputRouter router(0);
  Actions actions{};
  connect(router, 0);
  router.OnSelectSlot(0, actions);
  CHECK(actions.count == 0);  // already connected
  router.OnSelectSlot(3, actions);
  CHECK(actions.count == 0);  // out of range
  router.OnUsbKeyboardReport(report(0, {0x04}), actions);  // A held
  router.OnSelectSlot(1, actions);
  CHECK(actions.count == 3);
  CHECK(actions.items[0].type == ActionType::kSendAllReleased);
  CHECK(actions.items[0].slot == 0);
  CHECK(actions.items[1].type == ActionType::kStartConnect);
  CHECK(actions.items[1].slot == 1);
  CHECK(actions.items[2].type == ActionType::kNotifyUi);
  CHECK(router.active_slot() == 1);
  connect(router, 1);
  router.OnUsbKeyboardReport(report(0, {0x04, 0x05}), actions);
  CHECK(actions.count == 0);  // A is still down: on hold
  router.OnUsbKeyboardReport(report(0, {}), actions);
  CHECK(actions.count == 1);
  // Nothing held: the next key goes straight to the new host.
  router.OnSelectSlot(0, actions);
  connect(router, 0);
  router.OnUsbKeyboardReport(report(0, {0x04}), actions);
  CHECK(actions.count == 1);
  CHECK(actions.items[0].type == ActionType::kSendKeyboard);
  CHECK(actions.items[0].slot == 0);
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
  run_test("select_slot_from_ui", test_select_slot_from_ui);
  run_test("special_key_removal_and_compaction",
           test_special_key_removal_and_compaction);
  run_test("modifier_is_preserved_with_switch_usage",
           test_modifier_is_preserved_with_switch_usage);
  run_test("switch_edge_hold_release_and_repress",
           test_switch_edge_hold_release_and_repress);
  run_test("phantom_does_not_clear_held_switch_edge",
           test_phantom_does_not_clear_held_switch_edge);
  run_test("phantom_produces_no_actions", test_phantom_produces_no_actions);
  run_test("phantom_does_not_release_forwarding_hold",
           test_phantom_does_not_release_forwarding_hold);
  run_test("switch_action_order_with_connected_old_slot",
           test_switch_action_order_with_connected_old_slot);
  run_test("switch_skips_release_when_old_slot_disconnected",
           test_switch_skips_release_when_old_slot_disconnected);
  run_test("switch_hold_survives_ble_connect_until_all_keys_release",
           test_switch_hold_survives_ble_connect_until_all_keys_release);
  run_test("active_ble_connected_sends_release_and_notifies",
           test_active_ble_connected_sends_release_and_notifies);
  run_test("non_active_ble_events_are_ignored",
           test_non_active_ble_events_are_ignored);
  run_test("unconnected_input_is_dropped_but_switches_work",
           test_unconnected_input_is_dropped_but_switches_work);
  run_test("timeout_disconnects_and_router_remains_switchable",
           test_timeout_disconnects_and_router_remains_switchable);
  run_test("same_slot_connected_is_noop_and_disconnected_retries",
           test_same_slot_connected_is_noop_and_disconnected_retries);
  run_test("usb_disconnect_and_reset_release_and_clear_state",
           test_usb_disconnect_and_reset_release_and_clear_state);
  run_test("multiple_switches_choose_first_usage_in_report_order",
           test_multiple_switches_choose_first_usage_in_report_order);
  run_test("action_capacity_and_invalid_slots",
           test_action_capacity_and_invalid_slots);
  run_test("consumer_system_mouse_forwarded_when_connected",
           test_consumer_system_mouse_forwarded_when_connected);
  run_test("consumer_system_mouse_dropped_when_not_connected",
           test_consumer_system_mouse_dropped_when_not_connected);
  run_test("hold_waits_for_consumer_and_mouse_buttons",
           test_hold_waits_for_consumer_and_mouse_buttons);
  run_test("mouse_motion_without_buttons_does_not_block_hold_release",
           test_mouse_motion_without_buttons_does_not_block_hold_release);
  run_test("usb_disconnect_clears_consumer_and_mouse_state",
           test_usb_disconnect_clears_consumer_and_mouse_state);
  run_test("shortcut_adds_to_held_keys_and_restores",
           test_shortcut_adds_to_held_keys_and_restores);
  run_test("shortcut_dropped_when_not_forwarding",
           test_shortcut_dropped_when_not_forwarding);

  if (failures != 0) {
    std::printf("%d test assertion(s) failed\n", failures);
    return 1;
  }
  std::printf("All tests passed\n");
  return 0;
}
