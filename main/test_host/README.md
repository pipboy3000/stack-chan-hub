Run `sh main/test_host/run.sh` from the repository root. Both host binaries use
C++17, `-Wall -Wextra -Werror`, AddressSanitizer and UndefinedBehaviorSanitizer.
Temporary SDK include wrappers and executables are removed on exit.

`peer_slots_test.cpp` tests the address/admission/storage model.
`ble_keyboard_test.cpp` includes the actual `../ble_keyboard.cpp`, so its host
queue handlers and public send functions execute unchanged. The shared
`ble_keyboard_fake_sdk.h` supplies only the SDK types and calls needed to compile
that source. No production test hooks or copied state-machine implementation are
used.

The 33 BLE cases cover selection round trips during termination, delayed GAP
callbacks, terminate EALREADY/ENOTCONN/other failures, latest-slot reconnection
with encrypted/bonded identity, stale secured flags, advertising stop/start
errors, and CCCD resource exhaustion. Management coverage includes closed-window
admission and key-store rejection, explicit pairing and countdown, timeout before
timer dispatch, cancellation/selection and late encryption, ordinary reconnect
and repeat-pairing policy, target-only unbinding after disconnect, store/NVS/
termination failures, request queue limits, deadline wraparound, and explicit
repair of a registered slot whose bond is missing (including key-store failure). PeerSlots
also covers idempotent unbinding and restoration of the resulting mapping.

SDK assumptions are based on the local ESP-IDF NimBLE API: terminate EALREADY
means termination is already pending; advertising stop EALREADY means no active
advertisement; advertising start EALREADY means an advertisement already exists.
The fixture scripts return codes, exposes one connection descriptor, accepts NVS
writes and HID sends with scripted failure cases, and records SDK calls. Queue puts do not invoke callbacks;
tests explicitly drain selection and deliver GAP callbacks in the chosen order.
NVS failures are injected return codes, not a reboot/power-loss storage model.
The fixture uses a mutex for snapshot/request access but does not simulate
ESP32 task scheduling or real NimBLE SMP/key-distribution timing;
firmware compilation and physical-device verification remain separate checks.
