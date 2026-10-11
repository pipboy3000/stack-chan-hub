#pragma once

#include <array>
#include <cstdint>

namespace ble_keyboard {

// Only public/static identity addresses are persisted, never rotating RPAs.
struct PeerAddress {
  uint8_t type = 0;
  std::array<uint8_t, 6> bytes{};

  bool operator==(const PeerAddress& other) const {
    return type == other.type && bytes == other.bytes;
  }
};

class PeerSlots {
 public:
  static constexpr uint8_t kCount = 3;
  struct Entry {
    uint8_t valid = 0;
    PeerAddress address{};
  };
  using Data = std::array<Entry, kCount>;

  const Entry* Get(uint8_t slot) const {
    return slot < kCount && data_[slot].valid ? &data_[slot] : nullptr;
  }

  bool Allows(uint8_t slot, const PeerAddress& peer) const {
    if (slot >= kCount || peer.type > 1 ||
        peer.bytes == std::array<uint8_t, 6>{}) return false;
    if (const auto* entry = Get(slot)) return entry->address == peer;
    // A known PC must not capture another slot during initial pairing.
    for (const auto& entry : data_) {
      if (entry.valid && entry.address == peer) return false;
    }
    return true;
  }

  bool Bind(uint8_t slot, const PeerAddress& peer) {
    if (!ValidAddress(peer) || !Allows(slot, peer)) return false;
    data_[slot] = {1, peer};
    return true;
  }

  bool Unbind(uint8_t slot) {
    if (slot >= kCount) return false;
    data_[slot] = {};
    return true;
  }

  const Data& DataForStorage() const { return data_; }

  bool Restore(const Data& data) {
    PeerSlots restored;
    for (uint8_t slot = 0; slot < kCount; ++slot) {
      if (data[slot].valid > 1) return false;
      if (data[slot].valid && !restored.Bind(slot, data[slot].address)) {
        return false;
      }
    }
    data_ = restored.data_;
    return true;
  }

 private:
  static bool ValidAddress(const PeerAddress& peer) {
    if (peer.type > 1) return false;
    if (peer.type == 1 && (peer.bytes[5] & 0xc0) != 0xc0) return false;
    return peer.bytes != std::array<uint8_t, 6>{};
  }

  Data data_{};
};

static_assert(sizeof(PeerSlots::Entry) == 8, "NVS entry must have no padding");

}  // namespace ble_keyboard
