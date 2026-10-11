#include "peer_slots.h"

#include <cassert>
#include <cstdio>

using ble_keyboard::PeerAddress;
using ble_keyboard::PeerSlots;

int main() {
  const PeerAddress mac{0, {1, 2, 3, 4, 5, 6}};
  const PeerAddress linux{0, {2, 3, 4, 5, 6, 7}};
  const PeerAddress air{1, {3, 4, 5, 6, 7, 0xc8}};
  PeerSlots slots;
  assert(slots.Allows(0, mac));
  assert(slots.Bind(0, mac));
  assert(slots.Allows(0, mac) && !slots.Allows(0, linux));
  assert(!slots.Allows(1, mac) && !slots.Bind(1, mac));
  assert(slots.Bind(1, linux));
  assert(slots.Bind(2, air));
  assert(slots.Allows(0, mac) && !slots.Allows(0, linux));
  assert(slots.Allows(1, linux) && !slots.Allows(1, mac));
  assert(slots.Bind(0, mac));  // re-pairing Mac preserves Linux's assignment
  assert(slots.Allows(1, linux));
  assert(!slots.Bind(0, linux));
  assert(!slots.Allows(3, mac) && !slots.Bind(3, mac));
  PeerSlots rebooted;
  assert(rebooted.Restore(slots.DataForStorage()));
  assert(rebooted.Allows(0, mac) && rebooted.Allows(1, linux));
  auto corrupt = slots.DataForStorage();
  corrupt[1].address = mac;
  assert(!rebooted.Restore(corrupt));
  assert(rebooted.Allows(1, linux));  // failed restore is atomic
  corrupt = slots.DataForStorage();
  corrupt[1].valid = 2;
  assert(!rebooted.Restore(corrupt));
  PeerSlots empty;
  // A new host may connect with an RPA, but it must disclose its identity
  // during pairing before that address can be saved in a slot.
  const PeerAddress rpa{1, {1, 2, 3, 4, 5, 0x40}};
  assert(empty.Allows(0, rpa));
  assert(!empty.Bind(0, rpa));
  assert(!empty.Bind(0, PeerAddress{}));
  assert(!empty.Bind(0, PeerAddress{2, mac.bytes}));
  assert(!empty.Bind(0, PeerAddress{1, {1, 2, 3, 4, 5, 0x40}}));
  assert(slots.Unbind(1));
  assert(slots.Get(1) == nullptr && slots.Get(0) != nullptr && slots.Get(2) != nullptr);
  assert(slots.Unbind(1) && !slots.Unbind(3));
  PeerSlots after_unbind;
  assert(after_unbind.Restore(slots.DataForStorage()) && after_unbind.Get(1) == nullptr);
  assert(slots.Bind(1, PeerAddress{0, {9,8,7,6,5,4}}));
  puts("peer_slots: PASS (admission, isolation, re-pairing, restore, corrupt data)");
}
