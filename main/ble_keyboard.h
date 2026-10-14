// BLE HID keyboard, consumer/system control and mouse (HID over GATT) on
// NimBLE via esp_hid.
#pragma once

#include <cstdint>

namespace ble_keyboard {

enum class LinkEvent : uint8_t {
  kAdvertising,
  // Link is encrypted and bonded, so the host accepts input reports.
  kSecured,
  kDisconnected,
  kPairingStarted,
  kPairingStopped,   // Cancelled or closed by selecting another slot.
  kPairingTimedOut,
  kPairingComplete,  // Bond and slot mapping saved; followed by kSecured.
  kPairingFailed,
  kUnbound,
  kUnbindFailed,
};

enum class ManagementError : uint8_t {
  kNone, kNotSelected, kBusy, kAdvertising, kSecurity, kStorage, kDisconnect,
};

struct SlotSnapshot {
  bool valid = false;       // false for an invalid slot number
  bool registered = false;
  uint8_t address_type = 0; // public=0, static random=1
  uint8_t address[6]{};     // NimBLE byte order (display indices 5..0)
  bool pairing = false;
  uint16_t pairing_seconds_remaining = 0;
  bool unbinding = false;
  ManagementError last_error = ManagementError::kNone;
};

// Called from NimBLE / esp_hid tasks; keep it short and non-blocking.
// `slot` is the host slot the event belongs to.
using LinkCallback = void (*)(LinkEvent event, uint8_t slot);

// Initializes the controller, NimBLE host, GAP/GATT and the HID service, then
// starts the host task. One public identity is shared by all slots. Each slot
// stores a different bonded peer; only the selected peer may reconnect. An
// unregistered slot stays silent until StartPairing. Pairing windows are not
// restored after restart. Registered hosts reconnect without opening a window.
bool Start(const char* device_name, uint8_t slot, LinkCallback callback);

// Drops the old link and advertises to the peer registered in `slot`.
void SelectSlot(uint8_t slot);

// All slots are zero-based. Management calls enqueue onto the NimBLE host task.
// true means queued, NOT completed. false means invalid argument, host not ready,
// or request queue full; no callback follows a rejected request.
// Accepted calls produce the matching LinkEvent; inspect SlotInfo.last_error on
// failure. Callbacks must be forwarded to the UI, never block the host task.
// StartPairing requires the UI to select `slot` through the Router first, so old
// host releases and the held-key barrier are preserved. timeout_s is 1..300
// (default 60). Only one window exists. Another slot selection closes it.
// A registered slot accepts re-pairing ONLY from its existing identity; opening
// a window never deletes a registration. PC replacement is confirmed Unbind,
// wait for kUnbound, then StartPairing. The PC may need its old bond removed too.
// Outside a window no initial/repeat pairing or new security-key storage is
// accepted; ordinary encrypted reconnection of registered PCs remains allowed.
bool StartPairing(uint8_t slot, uint16_t timeout_s = 60);
// kPairingStopped (current window slot); no active window -> queued no-op.
// Cancelling/timing out an incomplete pairing disconnects it, never a normal
// already-secured registered connection. A late success must not bind a slot.
bool StopPairing();
// UI must request Router all-released before Unbind of its current host.
// Disconnects the target before deleting ONLY its keys/CCCD and slot mapping.
// Other slots are preserved. Already empty slots succeed idempotently.
// NimBLE store and slot NVS are not atomic: partial failure reports kUnbindFailed
// and retains the in-memory mapping for retry (reboot may reflect partial
// storage changes). Do not offer a replacement until kUnbound.
bool Unbind(uint8_t slot);
// Terminal shutdown before deep sleep; call from UI, never a NimBLE callback.
// Caller must first quiesce Router and send all-released. Stops advertising,
// gracefully disconnects peers and deinitializes host/controller without
// deleting bonds or slots. false means shutdown failed; reboot before reuse.
bool StopForSleep();
// Thread-safe value copy; never exposes Host-owned storage. Remaining time is
// rounded up. Snapshot may lag a queued request until its Host handler runs.
SlotSnapshot SlotInfo(uint8_t slot);

// Sends one boot-layout keyboard report (modifiers, reserved, 6 keys).
// Returns false when no secured link is up.
bool SendKeyboard(uint8_t modifiers, const uint8_t keys[6]);

// Consumer / System Control usage (0 = released).
bool SendConsumer(uint16_t usage);
bool SendSystem(uint16_t usage);

// Relative mouse: buttons bit 0 = left; x, y, wheel and pan in -127..127.
bool SendMouse(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan);

// Input reports handed to the stack / rejected by it, for diagnostics.
uint32_t SentCount();
uint32_t FailedCount();

}  // namespace ble_keyboard
