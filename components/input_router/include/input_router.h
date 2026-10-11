// Input Router (spec v0.4, chapter 9).
//
// Pure logic: no ESP-IDF, FreeRTOS, heap allocation or I/O, so it builds and
// runs in host unit tests. The caller feeds events in and executes the
// returned Actions in order (send BLE reports, start a connection, update UI).
#pragma once

#include <cstddef>
#include <cstdint>

namespace input_router {

constexpr int kSlotCount = 3;
constexpr int kKeyCount = 6;

// HID usages of the slot switch keys (F21..F23 -> slot 0..2).
constexpr uint8_t kSwitchUsageFirst = 0x70;
constexpr uint8_t kSwitchUsageLast = 0x72;
// Boot keyboards fill every key slot with this usage when too many keys are down.
constexpr uint8_t kErrorRollOver = 0x01;

// Boot keyboard report without the reserved byte.
struct KeyboardReport {
  uint8_t modifiers = 0;
  uint8_t keys[kKeyCount] = {};
};

// Relative mouse report. Buttons are bit 0 = left, 1 = right, 2 = middle...
struct MouseReport {
  uint8_t buttons = 0;
  int8_t x = 0;
  int8_t y = 0;
  int8_t wheel = 0;
  int8_t pan = 0;
};

enum class LinkState : uint8_t {
  kDisconnected,
  kConnecting,
  kConnected,
};

enum class ActionType : uint8_t {
  // Send `keyboard` to `slot`.
  kSendKeyboard,
  // Send the Consumer Control usage `usage` (0 = released) to `slot`.
  kSendConsumer,
  // Send the System Control usage `usage` (0 = released) to `slot`.
  kSendSystem,
  // Send `mouse` to `slot`.
  kSendMouse,
  // Send neutral Keyboard, Consumer, System and Mouse reports to `slot`.
  kSendAllReleased,
  // Drop any current BLE link and start connecting to `slot`.
  kStartConnect,
  // Active slot or link state changed; UI should redraw from the router state.
  kNotifyUi,
};

struct Action {
  ActionType type;
  uint8_t slot;
  KeyboardReport keyboard;  // Valid only for kSendKeyboard.
  uint16_t usage;           // Valid only for kSendConsumer / kSendSystem.
  MouseReport mouse;        // Valid only for kSendMouse.
};

// Fixed-capacity output buffer; one event produces at most kCapacity actions.
struct Actions {
  static constexpr size_t kCapacity = 8;
  Action items[kCapacity];
  size_t count = 0;
};

class InputRouter {
 public:
  explicit InputRouter(uint8_t initial_slot = 0);

  // A keyboard report arrived from USB.
  //  - F21..F23 are removed and the remaining keys are packed to the front
  //    (zero-filled). Modifiers are kept.
  //  - A switch fires once on the rising edge of a switch key. If several
  //    switch keys rise in one report, the first one in `keys` order wins.
  //  - On a switch: kSendAllReleased(old slot) if the old link was connected,
  //    then kStartConnect(new slot), state becomes kConnecting, kNotifyUi.
  //    Switching to the already active slot restarts the connection only
  //    when it is not connected; otherwise it is a no-op.
  //  - After a switch, forwarding of every report type stays on hold until
  //    the Voyager is physically idle: the last keyboard report is all zero
  //    (raw, including switch keys), and the last Consumer usage, System
  //    usage and mouse buttons are all 0. The report that makes it idle is
  //    forwarded. The hold survives the connection being established.
  //  - Otherwise the stripped report is sent with kSendKeyboard only when the
  //    active link is kConnected and no hold is pending; it is dropped
  //    otherwise and never replayed later.
  //  - A phantom report (any key equal to kErrorRollOver) means the key state
  //    is unknown: it is ignored entirely (no actions, switch edge memory and
  //    hold unchanged), so a held switch key does not fire again afterwards.
  void OnUsbKeyboardReport(const KeyboardReport& report, Actions& out);

  // The slot was chosen on the LCD: the same switch as a rising F21..F23.
  // Keys held on the Voyager at that moment stay on hold until released.
  // An out-of-range slot is ignored.
  void OnSelectSlot(uint8_t slot, Actions& out);

  // Consumer Control / System Control usage from USB (0 = released).
  // Forwarded as kSendConsumer / kSendSystem when the active link is
  // kConnected and no hold is pending; dropped otherwise.
  void OnUsbConsumerReport(uint16_t usage, Actions& out);
  void OnUsbSystemReport(uint16_t usage, Actions& out);

  // Mouse report from USB (mouse keys or the Navigator). Forwarded as
  // kSendMouse under the same conditions; motion during a hold is dropped.
  void OnUsbMouseReport(const MouseReport& report, Actions& out);

  // A gesture asks for a keyboard shortcut (e.g. Cmd+[ for Back). Sent as
  // two kSendKeyboard actions on top of the keys currently held: first with
  // `modifiers` and `key` added, then the held state again. Dropped under
  // the same conditions as other reports.
  void OnShortcut(uint8_t modifiers, uint8_t key, Actions& out);

  // Voyager unplugged or the USB host was reset: kSendAllReleased(active) if
  // connected, forget the previously pressed switch keys and any hold, and
  // treat every input as physically idle.
  void OnUsbDisconnected(Actions& out);

  // Full router reset: same releases as OnUsbDisconnected, state is kept.
  void Reset(Actions& out);

  // The BLE link to `slot` is up. If `slot` is the active slot:
  // state becomes kConnected, kSendAllReleased(slot), kNotifyUi.
  // Links to other slots are ignored.
  void OnBleConnected(uint8_t slot, Actions& out);

  // The BLE link to `slot` dropped. If `slot` is the active slot:
  // state becomes kDisconnected, kNotifyUi.
  void OnBleDisconnected(uint8_t slot, Actions& out);

  // Connecting to `slot` timed out. If `slot` is the active slot and the
  // state is kConnecting: state becomes kDisconnected, kNotifyUi.
  void OnConnectTimeout(uint8_t slot, Actions& out);

  uint8_t active_slot() const { return active_slot_; }
  LinkState link_state() const { return link_state_; }
  bool forwarding_on_hold() const { return hold_until_released_; }

 private:
  uint8_t active_slot_;
  LinkState link_state_ = LinkState::kDisconnected;
  bool hold_until_released_ = false;
  bool switch_pressed_[kSlotCount] = {};
  // Last physical state per report type, for the hold release.
  bool keyboard_idle_ = true;
  uint16_t consumer_usage_ = 0;
  uint16_t system_usage_ = 0;
  uint8_t mouse_buttons_ = 0;

  // Last keyboard report forwarded to the host, for shortcuts.
  KeyboardReport last_keyboard_;

  // Switches unless `slot` is already connected; true if it switched.
  bool SwitchTo(uint8_t slot, Actions& out);
  bool PhysicallyIdle() const;
  // Updates the hold after a physical state change; true if the report that
  // caused it may be forwarded.
  bool ReleaseHoldIfIdle();
  bool CanForward() const;
};

}  // namespace input_router
