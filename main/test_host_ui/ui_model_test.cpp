#include <cstdio>

#include "ui_model.h"

namespace {

using namespace ui;

int failures = 0;

#define CHECK(condition)                                              \
  do {                                                                \
    if (!(condition)) {                                               \
      std::printf("FAIL %s:%d: %s\n", __func__, __LINE__, #condition); \
      ++failures;                                                     \
    }                                                                 \
  } while (false)

Command touch(Model& model, const Rect& rect, uint32_t now) {
  return model.OnTouch(rect.x + rect.w / 2, rect.y + rect.h / 2, now);
}

void test_boot_countdown_starts_with_setting() {
  Model model;
  model.Restore(PowerSetting::kExternal, 1);
  model.Begin(1000, true);
  CHECK(model.screen() == Screen::kBoot);
  CHECK(model.BootSecondsLeft(1000) == 3);
  CHECK(model.BootSecondsLeft(3001) == 1);
  CHECK(model.OnTick(3999).type == CommandType::kNone);
  const Command command = model.OnTick(4000);
  CHECK(command.type == CommandType::kStart);
  CHECK(command.value == static_cast<uint8_t>(PowerSetting::kExternal));
  CHECK(model.screen() == Screen::kHome);
  CHECK(model.OnTick(9000).type == CommandType::kNone);  // only once
}

void test_boot_manual_choices() {
  for (int choice = 0; choice < 3; ++choice) {
    Model model;
    model.Begin(0, true);
    CHECK(model.OnTouch(10, 10, 500).type == CommandType::kNone);
    CHECK(model.screen() == Screen::kBootManual);
    CHECK(model.OnTick(60000).type == CommandType::kNone);  // waits forever
    CHECK(!model.Dimmed(60000));
    const Command command = touch(model, ChoiceButton(choice), 60000);
    CHECK(model.screen() == Screen::kHome);
    if (choice == 2) {
      CHECK(command.type == CommandType::kStartWithoutUsb);
    } else {
      CHECK(command.type == CommandType::kStart);
      CHECK(command.value == choice + 1);
    }
  }
}

void test_resume_skips_boot() {
  Model model;
  model.Begin(0, false);
  CHECK(model.screen() == Screen::kHome);
  CHECK(model.OnTick(5000).type == CommandType::kNone);
}

void test_home_slot_buttons() {
  Model model;
  model.Begin(0, false);
  Command command = touch(model, SlotButton(1, 2), 100);
  CHECK(command.type == CommandType::kSelectSlot && command.value == 1);
  // The third slot is not offered until it is in use.
  CHECK(model.OnTouch(310, 200, 200).value == 1);
  model.SetHomeSlots(3);
  command = model.OnTouch(310, 200, 300);
  CHECK(command.type == CommandType::kSelectSlot && command.value == 2);
  CHECK(model.OnTouch(160, 100, 400).type == CommandType::kNone);
}

void test_dimmed_touch_only_wakes() {
  Model model;
  model.Begin(0, false);
  CHECK(!model.Dimmed(29999));
  CHECK(model.Dimmed(30000));
  model.TakeDirty();
  model.OnTick(30000);
  CHECK(model.TakeDirty());
  CHECK(touch(model, SlotButton(1, 2), 40000).type == CommandType::kNone);
  CHECK(!model.Dimmed(40001));
  CHECK(touch(model, SlotButton(1, 2), 40100).type ==
        CommandType::kSelectSlot);
  model.Wake(100000);
  CHECK(!model.Dimmed(100001));
}

void test_screen_turns_off_when_idle() {
  Model model;
  model.Begin(0, false);
  CHECK(!model.ScreenOff(Model::kOffAfterMs - 1));
  CHECK(model.ScreenOff(Model::kOffAfterMs));
  // Typing keeps the screen on, but dimmed.
  model.Activity(200000);
  CHECK(model.Dimmed(Model::kOffAfterMs));
  CHECK(!model.ScreenOff(200000 + Model::kOffAfterMs - 1));
  CHECK(model.ScreenOff(200000 + Model::kOffAfterMs));
  // Input brings a dark screen back, still dimmed.
  model.Activity(600000);
  CHECK(!model.ScreenOff(600001));
  CHECK(model.Dimmed(600001));
  // The first touch on a dark screen only wakes it.
  CHECK(model.ScreenOff(1000000));
  CHECK(touch(model, SlotButton(1, 2), 1000000).type == CommandType::kNone);
  CHECK(!model.ScreenOff(1000001) && !model.Dimmed(1000001));
  // Turning off and on redraws.
  model.OnTick(1000001);
  model.TakeDirty();
  model.OnTick(1000000 + Model::kOffAfterMs);
  CHECK(model.TakeDirty());
}

void test_screen_stays_on_while_pairing() {
  Model model;
  model.Begin(0, false);
  touch(model, kSettingsButton, 10);
  touch(model, ListRow(kRowHosts), 20);
  touch(model, ListRow(0), 30);
  model.SetHostState(false, true, false);
  CHECK(!model.ScreenOff(Model::kOffAfterMs + 30));
}

void test_settings_rows() {
  Model model;
  model.Begin(0, false);
  touch(model, kSettingsButton, 10);
  CHECK(model.screen() == Screen::kSettings);
  Command command = touch(model, ListRow(kRowBrightness), 30);
  CHECK(command.type == CommandType::kSetBrightness && command.value == 2);
  command = touch(model, ListRow(kRowBrightness), 40);
  CHECK(command.value == 0);
  touch(model, ListRow(kRowDiagnostics), 50);
  CHECK(model.screen() == Screen::kDiagnostics);
  touch(model, kBackButton, 60);
  CHECK(model.screen() == Screen::kSettings);
  touch(model, ListRow(kRowPower), 70);
  CHECK(model.screen() == Screen::kPower);
  command = touch(model, ChoiceButton(2), 80);
  CHECK(command.type == CommandType::kSetPower && command.value == 2);
  CHECK(model.power() == PowerSetting::kExternal);
  CHECK(model.screen() == Screen::kSettings);
  touch(model, kBackButton, 90);
  CHECK(model.screen() == Screen::kHome);
}

void test_restart_needs_two_taps() {
  Model model;
  model.Begin(0, false);
  touch(model, kSettingsButton, 10);
  CHECK(touch(model, ListRow(kRowRestart), 100).type == CommandType::kNone);
  CHECK(model.RestartArmed(200));
  CHECK(touch(model, ListRow(kRowRestart), 200).type ==
        CommandType::kRestart);
  // Too slow: armed again instead of restarting.
  CHECK(touch(model, ListRow(kRowRestart), 300).type == CommandType::kNone);
  model.OnTick(3400);
  CHECK(!model.RestartArmed(3400));
  CHECK(touch(model, ListRow(kRowRestart), 3500).type == CommandType::kNone);
  // Another row in between disarms.
  touch(model, ListRow(kRowBrightness), 3600);
  CHECK(touch(model, ListRow(kRowRestart), 3700).type == CommandType::kNone);
}

void test_sleep_confirmation_and_layout() {
  CHECK(ListRow(kRowSleep).y + ListRow(kRowSleep).h <= kHeight);
  Model model;
  model.Begin(0, false);
  touch(model, kSettingsButton, 10);
  CHECK(touch(model, ListRow(kRowSleep), 100).type == CommandType::kNone);
  CHECK(model.SleepArmed(200) && !model.RestartArmed(200));
  CHECK(touch(model, ListRow(kRowSleep), 200).type == CommandType::kSleep);
  CHECK(!model.SleepArmed(201));
  touch(model, ListRow(kRowSleep), 300);
  model.OnTick(3300);
  CHECK(!model.SleepArmed(3300));
  CHECK(touch(model, ListRow(kRowSleep), 3300).type == CommandType::kNone);
  touch(model, ListRow(kRowBrightness), 3400);
  CHECK(!model.SleepArmed(3400));
  CHECK(touch(model, ListRow(kRowSleep), 3500).type == CommandType::kNone);
  // Restart and sleep require their own confirmation, never cross-confirm.
  CHECK(touch(model, ListRow(kRowRestart), 3600).type == CommandType::kNone);
  CHECK(!model.SleepArmed(3600) && model.RestartArmed(3600));
  CHECK(touch(model, ListRow(kRowSleep), 3700).type == CommandType::kNone);
  CHECK(!model.RestartArmed(3700));
  touch(model, kBackButton, 3800);
  CHECK(!model.SleepArmed(3800));
  touch(model, kSettingsButton, 3900);
  // A first tap on a dark screen only wakes, and does not arm sleep.
  CHECK(touch(model, ListRow(kRowSleep), 400000).type == CommandType::kNone);
  CHECK(!model.SleepArmed(400000));
}

void open_host(Model& model, int slot) {
  model.Begin(0, false);
  touch(model, kSettingsButton, 10);
  touch(model, ListRow(kRowHosts), 20);
  CHECK(model.screen() == Screen::kHosts);
  touch(model, ListRow(slot), 30);
  CHECK(model.screen() == Screen::kHost);
  CHECK(model.host() == slot);
}

void test_pairing_flow() {
  Model model;
  open_host(model, 2);
  model.SetHostState(false, false, false);
  // An unregistered host has nothing to unbind.
  CHECK(touch(model, ChoiceButton(2), 40).type == CommandType::kNone);
  CHECK(model.screen() == Screen::kHost);
  Command command = touch(model, ChoiceButton(1), 50);
  CHECK(command.type == CommandType::kStartPairing && command.value == 2);
  model.SetHostState(false, true, false);
  CHECK(!model.Dimmed(500000));
  command = touch(model, ChoiceButton(1), 60);
  CHECK(command.type == CommandType::kStopPairing);
  // Back closes an open window.
  command = touch(model, kBackButton, 70);
  CHECK(command.type == CommandType::kStopPairing);
  CHECK(model.screen() == Screen::kHosts);
}

void test_unbind_needs_confirmation() {
  Model model;
  open_host(model, 1);
  model.SetHostState(true, false, false);
  CHECK(touch(model, ChoiceButton(2), 40).type == CommandType::kNone);
  CHECK(model.screen() == Screen::kConfirmUnbind);
  touch(model, ChoiceButton(2), 50);  // cancel
  CHECK(model.screen() == Screen::kHost);
  touch(model, ChoiceButton(2), 60);
  const Command command = touch(model, ChoiceButton(1), 70);
  CHECK(command.type == CommandType::kUnbind && command.value == 1);
  CHECK(model.screen() == Screen::kHost);
  // Nothing is offered while the request runs.
  model.SetHostState(true, false, true);
  CHECK(touch(model, ChoiceButton(1), 80).type == CommandType::kNone);
  CHECK(touch(model, ChoiceButton(2), 90).type == CommandType::kNone);
  CHECK(model.screen() == Screen::kHost);
}

}  // namespace

int main() {
  test_boot_countdown_starts_with_setting();
  test_boot_manual_choices();
  test_resume_skips_boot();
  test_home_slot_buttons();
  test_dimmed_touch_only_wakes();
  test_screen_turns_off_when_idle();
  test_screen_stays_on_while_pairing();
  test_settings_rows();
  test_restart_needs_two_taps();
  test_sleep_confirmation_and_layout();
  test_pairing_flow();
  test_unbind_needs_confirmation();
  if (failures != 0) {
    std::printf("%d assertion(s) failed\n", failures);
    return 1;
  }
  std::printf("All tests passed\n");
  return 0;
}
