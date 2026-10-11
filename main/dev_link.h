// Development link over Wi-Fi: OTA firmware updates and UDP log forwarding.
//
// Wi-Fi credentials and the OTA token are provisioned over the USB serial
// console (tools/wifi_setup.html, Web Serial) while the CoreS3 USB-C is still
// in Serial-JTAG mode. Without credentials Wi-Fi stays off.
#pragma once

#include <cstddef>
#include <cstdint>

namespace dev_link {

enum class State : uint8_t {
  kNotConfigured,
  kConnecting,
  kConnected,
};

// Starts the serial provisioning listener and, when credentials are stored,
// Wi-Fi, mDNS (stackchan-hub.local), the OTA HTTP server and UDP logging.
void Start();

// Stops the serial listener before the USB PHY is handed to the USB host.
void StopSerial();

// Appends application status lines to GET /status.
using StatusWriter = int (*)(char* out, int size);
void SetStatusWriter(StatusWriter writer);

// Serves the LCD canvas at GET /screen (raw RGB565, see tools/screenshot.sh).
using ScreenReader = const uint8_t* (*)(size_t* size);
void SetScreenReader(ScreenReader reader);

// POST /tap?x=..&y=.. (with the OTA token) taps the LCD, for checking screens
// without touching the device.
using TapHandler = void (*)(int x, int y);
void SetTapHandler(TapHandler handler);

State state();
// Dotted IPv4 address when connected, otherwise "".
const char* ip_address();

}  // namespace dev_link
