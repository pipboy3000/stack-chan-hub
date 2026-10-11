// LCD menu: draws the current ui::Model screen.
//
// Everything is drawn into an off-screen canvas (PSRAM) and pushed in one go,
// so a redraw does not flicker. Only ui_task may call into this.
#pragma once

#include <cstddef>
#include <cstdint>

#include "ui_model.h"

namespace ui {

enum class Link : uint8_t { kDisconnected, kConnecting, kConnected };
enum class Keyboard : uint8_t { kStopped, kWaiting, kReady };

// What the screens show besides the model state. Redrawn when it changes;
// compared bytewise, so zero it with memset before filling it in.
struct Status {
  uint8_t active_slot;
  Link link;
  Keyboard keyboard;
  bool registered[kSlotCount];
  int8_t battery_percent;  // -1 = unknown
  bool charging;
  bool external_power;
  char notice[64];  // one home line for something to act on
  // Host shown on Screen::kHost.
  uint8_t host_address[6];  // NimBLE byte order
  uint16_t pairing_seconds;
  char host_message[64];
};

// `slot_names` must outlive the view. False if the canvas cannot be allocated.
bool Begin(const char* const* slot_names);

// `diagnostics` is newline-separated text for Screen::kDiagnostics.
void Draw(const Model& model, const Status& status, const char* diagnostics,
          uint32_t now_ms);

// Raw canvas pixels (RGB565, big-endian, kWidth x kHeight) for a screenshot.
const uint8_t* Pixels(size_t* size);

}  // namespace ui
