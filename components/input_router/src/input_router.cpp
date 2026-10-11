#include "input_router.h"

namespace input_router {
namespace {

void clear_actions(Actions& actions) { actions.count = 0; }

void append_action(Actions& actions, ActionType type, uint8_t slot,
                   const KeyboardReport* keyboard = nullptr) {
  if (actions.count >= Actions::kCapacity) {
    return;
  }

  Action& action = actions.items[actions.count++];
  action = Action{};
  action.type = type;
  action.slot = slot;
  action.keyboard = keyboard == nullptr ? KeyboardReport{} : *keyboard;
}

bool is_report_released(const KeyboardReport& report) {
  if (report.modifiers != 0) {
    return false;
  }
  for (uint8_t key : report.keys) {
    if (key != 0) {
      return false;
    }
  }
  return true;
}

int switch_slot_for_usage(uint8_t usage) {
  if (usage < kSwitchUsageFirst || usage > kSwitchUsageLast) {
    return -1;
  }
  return usage - kSwitchUsageFirst;
}

KeyboardReport strip_switch_keys(const KeyboardReport& report) {
  KeyboardReport stripped{};
  stripped.modifiers = report.modifiers;
  size_t next_key = 0;
  for (uint8_t key : report.keys) {
    if (switch_slot_for_usage(key) >= 0 || key == 0) {
      continue;
    }
    stripped.keys[next_key++] = key;
  }
  return stripped;
}

}  // namespace

InputRouter::InputRouter(uint8_t initial_slot)
    : active_slot_(initial_slot < kSlotCount ? initial_slot : 0) {}

void InputRouter::OnUsbKeyboardReport(const KeyboardReport& report,
                                     Actions& out) {
  clear_actions(out);

  for (uint8_t key : report.keys) {
    if (key == kErrorRollOver) {
      return;
    }
  }

  bool switch_down[kSlotCount] = {};
  int first_rising_slot = -1;
  for (uint8_t key : report.keys) {
    const int slot = switch_slot_for_usage(key);
    if (slot < 0) {
      continue;
    }
    switch_down[slot] = true;
    if (!switch_pressed_[slot] && first_rising_slot < 0) {
      first_rising_slot = slot;
    }
  }

  for (int slot = 0; slot < kSlotCount; ++slot) {
    switch_pressed_[slot] = switch_down[slot];
  }
  keyboard_idle_ = is_report_released(report);

  if (first_rising_slot >= 0 &&
      SwitchTo(static_cast<uint8_t>(first_rising_slot), out)) {
    return;
  }

  if (!ReleaseHoldIfIdle()) {
    return;
  }
  if (CanForward()) {
    last_keyboard_ = strip_switch_keys(report);
    append_action(out, ActionType::kSendKeyboard, active_slot_,
                  &last_keyboard_);
  }
}

void InputRouter::OnSelectSlot(uint8_t slot, Actions& out) {
  clear_actions(out);
  if (slot < kSlotCount && SwitchTo(slot, out)) {
    // No switch key has to come up; only keys held right now are suppressed.
    hold_until_released_ = !PhysicallyIdle();
  }
}

bool InputRouter::SwitchTo(uint8_t target_slot, Actions& out) {
  if (target_slot == active_slot_ && link_state_ == LinkState::kConnected) {
    return false;
  }
  if (link_state_ == LinkState::kConnected) {
    append_action(out, ActionType::kSendAllReleased, active_slot_);
  }
  active_slot_ = target_slot;
  link_state_ = LinkState::kConnecting;
  hold_until_released_ = true;
  append_action(out, ActionType::kStartConnect, active_slot_);
  append_action(out, ActionType::kNotifyUi, active_slot_);
  return true;
}

void InputRouter::OnShortcut(uint8_t modifiers, uint8_t key, Actions& out) {
  clear_actions(out);
  if (!ReleaseHoldIfIdle() || !CanForward()) {
    return;
  }
  KeyboardReport press = last_keyboard_;
  press.modifiers |= modifiers;
  size_t slot = 0;
  while (slot < kKeyCount - 1 && press.keys[slot] != 0) {
    ++slot;
  }
  press.keys[slot] = key;
  append_action(out, ActionType::kSendKeyboard, active_slot_, &press);
  append_action(out, ActionType::kSendKeyboard, active_slot_, &last_keyboard_);
}

void InputRouter::OnUsbConsumerReport(uint16_t usage, Actions& out) {
  clear_actions(out);
  consumer_usage_ = usage;
  if (ReleaseHoldIfIdle() && CanForward()) {
    append_action(out, ActionType::kSendConsumer, active_slot_);
    out.items[out.count - 1].usage = usage;
  }
}

void InputRouter::OnUsbSystemReport(uint16_t usage, Actions& out) {
  clear_actions(out);
  system_usage_ = usage;
  if (ReleaseHoldIfIdle() && CanForward()) {
    append_action(out, ActionType::kSendSystem, active_slot_);
    out.items[out.count - 1].usage = usage;
  }
}

void InputRouter::OnUsbMouseReport(const MouseReport& report, Actions& out) {
  clear_actions(out);
  mouse_buttons_ = report.buttons;
  if (ReleaseHoldIfIdle() && CanForward()) {
    append_action(out, ActionType::kSendMouse, active_slot_);
    out.items[out.count - 1].mouse = report;
  }
}

bool InputRouter::PhysicallyIdle() const {
  return keyboard_idle_ && consumer_usage_ == 0 && system_usage_ == 0 &&
         mouse_buttons_ == 0;
}

bool InputRouter::ReleaseHoldIfIdle() {
  if (!hold_until_released_) {
    return true;
  }
  if (!PhysicallyIdle()) {
    return false;
  }
  hold_until_released_ = false;
  return true;
}

bool InputRouter::CanForward() const {
  return link_state_ == LinkState::kConnected && !hold_until_released_;
}

void InputRouter::OnUsbDisconnected(Actions& out) {
  clear_actions(out);
  if (link_state_ == LinkState::kConnected) {
    append_action(out, ActionType::kSendAllReleased, active_slot_);
  }
  for (bool& pressed : switch_pressed_) {
    pressed = false;
  }
  hold_until_released_ = false;
  last_keyboard_ = KeyboardReport{};
  keyboard_idle_ = true;
  consumer_usage_ = 0;
  system_usage_ = 0;
  mouse_buttons_ = 0;
}

void InputRouter::Reset(Actions& out) { OnUsbDisconnected(out); }

void InputRouter::OnBleConnected(uint8_t slot, Actions& out) {
  clear_actions(out);
  if (slot >= kSlotCount || slot != active_slot_) {
    return;
  }
  link_state_ = LinkState::kConnected;
  last_keyboard_ = KeyboardReport{};
  append_action(out, ActionType::kSendAllReleased, active_slot_);
  append_action(out, ActionType::kNotifyUi, active_slot_);
}

void InputRouter::OnBleDisconnected(uint8_t slot, Actions& out) {
  clear_actions(out);
  if (slot >= kSlotCount || slot != active_slot_) {
    return;
  }
  link_state_ = LinkState::kDisconnected;
  append_action(out, ActionType::kNotifyUi, active_slot_);
}

void InputRouter::OnConnectTimeout(uint8_t slot, Actions& out) {
  clear_actions(out);
  if (slot >= kSlotCount || slot != active_slot_ ||
      link_state_ != LinkState::kConnecting) {
    return;
  }
  link_state_ = LinkState::kDisconnected;
  append_action(out, ActionType::kNotifyUi, active_slot_);
}

}  // namespace input_router
