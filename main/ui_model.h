// LCD menu: screen transitions and touch hit-testing.
//
// Pure logic (no M5, FreeRTOS or I/O) so it runs in host unit tests. The
// caller feeds touches and ticks in, executes the returned Command and draws
// the current screen (ui_view) from this state.
#pragma once

#include <cstdint>

namespace ui {

constexpr int kWidth = 320;
constexpr int kHeight = 240;
constexpr int kSlotCount = 3;

struct Rect {
  int16_t x, y, w, h;
  constexpr bool Contains(int px, int py) const {
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

// Layout shared by hit-testing and drawing.
constexpr int kHeaderHeight = 36;
constexpr Rect kSettingsButton{256, 0, 64, 40};  // home, top right
constexpr Rect kBackButton{0, 0, 96, kHeaderHeight};
constexpr int kSlotBarTop = 152;
constexpr Rect SlotButton(int index, int count) {
  const int16_t width = static_cast<int16_t>(kWidth / count);
  return {static_cast<int16_t>(index * width), kSlotBarTop,
          index == count - 1 ? static_cast<int16_t>(kWidth - index * width)
                             : width,
          kHeight - kSlotBarTop};
}
// Full-width list row below the header.
constexpr int kRowHeight = 34;
constexpr Rect ListRow(int index) {
  return {0, static_cast<int16_t>(kHeaderHeight + index * kRowHeight), kWidth,
          kRowHeight};
}
// Large stacked buttons (boot choice, power choice, host actions).
constexpr Rect ChoiceButton(int index) {
  return {24, static_cast<int16_t>(72 + index * 54), kWidth - 48, 44};
}

enum class Screen : uint8_t {
  kBoot,        // countdown to the automatic start
  kBootManual,  // choose how to start
  kHome,
  kSettings,
  kPower,       // keyboard power setting
  kDiagnostics,
  kHosts,       // host list
  kHost,        // one host: pair / unbind
  kConfirmUnbind,
};

enum class PowerSetting : uint8_t { kAuto, kInternal, kExternal };
constexpr int kBrightnessLevels = 3;

enum SettingsRow : int {
  kRowHosts,
  kRowPower,
  kRowBrightness,
  kRowDiagnostics,
  kRowRestart,
  kSettingsRowCount,
};

enum class CommandType : uint8_t {
  kNone,
  kStart,          // value: PowerSetting to start the USB host with
  kStartWithoutUsb,
  kSelectSlot,     // value: slot
  kSetPower,       // value: PowerSetting
  kSetBrightness,  // value: level
  kStartPairing,   // value: slot
  kStopPairing,
  kUnbind,         // value: slot
  kRestart,
};

struct Command {
  CommandType type = CommandType::kNone;
  uint8_t value = 0;
};

class Model {
 public:
  static constexpr uint32_t kBootCountdownMs = 3000;
  static constexpr uint32_t kDimAfterMs = 30000;
  static constexpr uint32_t kRestartArmMs = 3000;

  // `countdown` false skips the boot screens (resume after a software reset).
  void Begin(uint32_t now_ms, bool countdown) {
    screen_ = countdown ? Screen::kBoot : Screen::kHome;
    boot_started_ms_ = last_touch_ms_ = now_ms;
    dirty_ = true;
  }

  Command OnTouch(int x, int y, uint32_t now_ms) {
    const bool was_dimmed = Dimmed(now_ms);
    last_touch_ms_ = now_ms;
    if (was_dimmed) {
      dirty_ = true;  // the first touch only wakes the screen
      return {};
    }
    const bool restart_armed = RestartArmed(now_ms);
    restart_armed_ms_ = 0;
    dirty_ = true;
    switch (screen_) {
      case Screen::kBoot:
        screen_ = Screen::kBootManual;
        return {};
      case Screen::kBootManual:
        if (ChoiceButton(0).Contains(x, y)) {
          return Leave(Screen::kHome, CommandType::kStart,
                       static_cast<uint8_t>(PowerSetting::kInternal));
        }
        if (ChoiceButton(1).Contains(x, y)) {
          return Leave(Screen::kHome, CommandType::kStart,
                       static_cast<uint8_t>(PowerSetting::kExternal));
        }
        if (ChoiceButton(2).Contains(x, y)) {
          return Leave(Screen::kHome, CommandType::kStartWithoutUsb, 0);
        }
        return {};
      case Screen::kHome:
        if (kSettingsButton.Contains(x, y)) {
          screen_ = Screen::kSettings;
          return {};
        }
        for (int i = 0; i < home_slots_; ++i) {
          if (SlotButton(i, home_slots_).Contains(x, y)) {
            return {CommandType::kSelectSlot, static_cast<uint8_t>(i)};
          }
        }
        return {};
      case Screen::kSettings:
        if (kBackButton.Contains(x, y)) {
          screen_ = Screen::kHome;
          return {};
        }
        return OnSettingsRow(RowAt(x, y, kSettingsRowCount), restart_armed,
                             now_ms);
      case Screen::kPower:
        if (kBackButton.Contains(x, y)) {
          screen_ = Screen::kSettings;
          return {};
        }
        for (int i = 0; i < 3; ++i) {
          if (ChoiceButton(i).Contains(x, y)) {
            power_ = static_cast<PowerSetting>(i);
            return Leave(Screen::kSettings, CommandType::kSetPower,
                         static_cast<uint8_t>(i));
          }
        }
        return {};
      case Screen::kDiagnostics:
        if (kBackButton.Contains(x, y)) {
          screen_ = Screen::kSettings;
        }
        return {};
      case Screen::kHosts: {
        if (kBackButton.Contains(x, y)) {
          screen_ = Screen::kSettings;
          return {};
        }
        const int row = RowAt(x, y, kSlotCount);
        if (row >= 0) {
          host_ = static_cast<uint8_t>(row);
          screen_ = Screen::kHost;
        }
        return {};
      }
      case Screen::kHost:
        if (kBackButton.Contains(x, y)) {
          screen_ = Screen::kHosts;
          // Leaving the screen closes an open window: it must stay visible.
          return host_pairing_ ? Command{CommandType::kStopPairing, host_}
                               : Command{};
        }
        if (host_busy_) {
          return {};
        }
        if (ChoiceButton(1).Contains(x, y)) {
          return host_pairing_ ? Command{CommandType::kStopPairing, host_}
                               : Command{CommandType::kStartPairing, host_};
        }
        if (ChoiceButton(2).Contains(x, y) && host_registered_ &&
            !host_pairing_) {
          screen_ = Screen::kConfirmUnbind;
        }
        return {};
      case Screen::kConfirmUnbind:
        if (ChoiceButton(1).Contains(x, y)) {
          return Leave(Screen::kHost, CommandType::kUnbind, host_);
        }
        if (ChoiceButton(2).Contains(x, y) || kBackButton.Contains(x, y)) {
          screen_ = Screen::kHost;
        }
        return {};
    }
    return {};
  }

  Command OnTick(uint32_t now_ms) {
    if (screen_ == Screen::kBoot &&
        now_ms - boot_started_ms_ >= kBootCountdownMs) {
      return Leave(Screen::kHome, CommandType::kStart,
                   static_cast<uint8_t>(power_));
    }
    if (restart_armed_ms_ != 0 && !RestartArmed(now_ms)) {
      restart_armed_ms_ = 0;
      dirty_ = true;
    }
    const bool dimmed = Dimmed(now_ms);
    if (dimmed != was_dimmed_) {
      was_dimmed_ = dimmed;
      dirty_ = true;
    }
    return {};
  }

  // Lights the screen for something the user should see (slot switch, error).
  void Wake(uint32_t now_ms) {
    last_touch_ms_ = now_ms;
    dirty_ = true;
  }

  // The screen never dims while a choice or a pairing window is waiting.
  bool Dimmed(uint32_t now_ms) const {
    if (screen_ == Screen::kBoot || screen_ == Screen::kBootManual ||
        (screen_ == Screen::kHost && host_pairing_)) {
      return false;
    }
    return now_ms - last_touch_ms_ >= kDimAfterMs;
  }

  int BootSecondsLeft(uint32_t now_ms) const {
    const uint32_t elapsed = now_ms - boot_started_ms_;
    return elapsed >= kBootCountdownMs
               ? 0
               : static_cast<int>((kBootCountdownMs - elapsed + 999) / 1000);
  }

  bool RestartArmed(uint32_t now_ms) const {
    return restart_armed_ms_ != 0 &&
           now_ms - restart_armed_ms_ < kRestartArmMs;
  }

  // State of the host shown on kHost, refreshed by the caller.
  void SetHostState(bool registered, bool pairing, bool busy) {
    if (registered != host_registered_ || pairing != host_pairing_ ||
        busy != host_busy_) {
      host_registered_ = registered;
      host_pairing_ = pairing;
      host_busy_ = busy;
      dirty_ = true;
    }
  }

  // Slots offered on the home screen (2, or 3 once the third is in use).
  void SetHomeSlots(int count) {
    count = count < 2 ? 2 : (count > kSlotCount ? kSlotCount : count);
    if (count != home_slots_) {
      home_slots_ = count;
      dirty_ = true;
    }
  }

  void Restore(PowerSetting power, uint8_t brightness) {
    power_ = power;
    brightness_ = brightness < kBrightnessLevels ? brightness : 1;
  }

  void MarkDirty() { dirty_ = true; }
  bool TakeDirty() {
    const bool dirty = dirty_;
    dirty_ = false;
    return dirty;
  }

  Screen screen() const { return screen_; }
  uint8_t host() const { return host_; }
  bool host_registered() const { return host_registered_; }
  bool host_pairing() const { return host_pairing_; }
  bool host_busy() const { return host_busy_; }
  int home_slots() const { return home_slots_; }
  PowerSetting power() const { return power_; }
  uint8_t brightness() const { return brightness_; }

 private:
  static int RowAt(int x, int y, int count) {
    for (int i = 0; i < count; ++i) {
      if (ListRow(i).Contains(x, y)) {
        return i;
      }
    }
    return -1;
  }

  Command Leave(Screen next, CommandType type, uint8_t value) {
    screen_ = next;
    dirty_ = true;
    return {type, value};
  }

  Command OnSettingsRow(int row, bool restart_armed, uint32_t now_ms) {
    switch (row) {
      case kRowHosts:
        screen_ = Screen::kHosts;
        return {};
      case kRowPower:
        screen_ = Screen::kPower;
        return {};
      case kRowBrightness:
        brightness_ = static_cast<uint8_t>((brightness_ + 1) %
                                           kBrightnessLevels);
        return {CommandType::kSetBrightness, brightness_};
      case kRowDiagnostics:
        screen_ = Screen::kDiagnostics;
        return {};
      case kRowRestart:
        if (restart_armed) {
          return {CommandType::kRestart, 0};
        }
        restart_armed_ms_ = now_ms == 0 ? 1 : now_ms;  // second tap confirms
        return {};
      default:
        return {};
    }
  }

  Screen screen_ = Screen::kHome;
  uint32_t boot_started_ms_ = 0;
  uint32_t last_touch_ms_ = 0;
  uint32_t restart_armed_ms_ = 0;  // 0 = not armed
  bool was_dimmed_ = false;
  bool dirty_ = true;
  uint8_t host_ = 0;
  bool host_registered_ = false;
  bool host_pairing_ = false;
  bool host_busy_ = false;
  int home_slots_ = 2;
  PowerSetting power_ = PowerSetting::kAuto;
  uint8_t brightness_ = 1;
};

}  // namespace ui
