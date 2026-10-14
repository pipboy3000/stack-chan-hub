#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <M5StackChan.h>

#include "ble_keyboard.h"
#include "dev_link.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hid_descriptor.h"
#include "input_router.h"
#include "led_model.h"
#include "touchpad_gestures.h"
#include "ui_model.h"
#include "ui_view.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "usb/hid_host.h"
#include "usb/usb_host.h"
#include "soc/usb_dwc_struct.h"

namespace {

constexpr char kTag[] = "phase3";
constexpr char kDeviceName[] = "StackChan Hub";
constexpr uint8_t kBootInterfaceSubclass = 1;
constexpr uint8_t kBootKeyboardProtocol = 1;
constexpr uint8_t kAw9523Address = 0x58;
// Host slots (spec 11.2), selected with F21..F23 on the Voyager.
constexpr const char* kSlotNames[input_router::kSlotCount] = {
    "Mac mini", "Linux", "MacBook Air"};
// A switch that has not connected within this is shown as not connected
// (spec 9.7); the slot keeps advertising, so the host may still come.
constexpr uint32_t kConnectTimeoutMs = 5000;
constexpr char kSlotNamespace[] = "hub";

// The active slot survives reboots (spec 11.4).
uint8_t load_active_slot() {
  nvs_handle_t nvs;
  uint8_t slot = 0;
  if (nvs_open(kSlotNamespace, NVS_READONLY, &nvs) == ESP_OK) {
    nvs_get_u8(nvs, "slot", &slot);
    nvs_close(nvs);
  }
  return slot < input_router::kSlotCount ? slot : 0;
}

void save_active_slot(uint8_t slot) {
  nvs_handle_t nvs;
  if (nvs_open(kSlotNamespace, NVS_READWRITE, &nvs) == ESP_OK) {
    nvs_set_u8(nvs, "slot", slot);
    nvs_commit(nvs);
    nvs_close(nvs);
  }
}

uint8_t initial_slot = 0;

// Only ui_task touches M5 (display, touch, I2C). Other tasks post messages.
enum class UiKind : uint8_t { kStatus, kLink, kKeyboard, kAux, kBle };

struct UiMessage {
  UiKind kind;
  char text[96];
  uint8_t report[8];
};

struct DeviceEvent {
  hid_host_device_handle_t handle;
  bool connected;
};

enum class RouterEventKind : uint8_t {
  kUsbReport,
  kUsbConsumer,
  kUsbSystem,
  kUsbMouse,
  kUsbTouch,
  kUsbDisconnected,
  kBleSecured,
  kBleDisconnected,
  // Requests from the LCD menu; `slot` is the host they are about.
  kUiSelectSlot,
  kUiStartPairing,
  kUiUnbind,
  kUiSleep,
};

struct RouterEvent {
  RouterEventKind kind;
  uint8_t slot;  // BLE events: the host slot they belong to
  uint8_t report[8];
  uint16_t usage;
  input_router::MouseReport mouse;
  touchpad::Frame touch;  // time_ms is set by router_task
  TaskHandle_t reply_task;  // sleep preparation acknowledgement
};

// One opened HID interface of the Voyager. Written by hid_event_task before
// hid_host_device_start() and read-only afterwards from the report callback.
struct UsbInterface {
  bool in_use;
  bool boot_keyboard;
  hid_host_device_handle_t handle;
  hid_descriptor::Interface parsed;
};

constexpr size_t kMaxInterfaces = 6;
// About 6 KB each: kept in PSRAM, because internal RAM is needed for USB DMA.
UsbInterface* usb_interfaces = nullptr;

QueueHandle_t ui_messages;
QueueHandle_t device_events;
QueueHandle_t router_events;
std::atomic<uint32_t> dropped_reports{0};
std::atomic<uint32_t> ble_send_failures{0};
// Liveness counters shown on the LCD: USB host mode has no serial log.
std::atomic<uint32_t> usb_input_count{0};
std::atomic<uint32_t> input_activity_count{0};
std::atomic<uint32_t> router_loop_count{0};
std::atomic<uint32_t> ui_loop_count{0};
// Base battery (INA226), sampled by ui_task, which owns the I2C bus.
std::atomic<float> battery_volts{0};
std::atomic<float> battery_milliamps{0};
// Diagnostic snapshots: HTTP never accesses the UI-owned I2C bus.
std::atomic<int> vbus_millivolts{-1};
std::atomic<int> pmic_status{-1};
std::atomic<int> charge_current_register{-1};
std::atomic<int> aw_output_registers{-1};
std::atomic<bool> ble_secured{false};
// Router and USB state for the LCD, which redraws when they change.
std::atomic<uint8_t> router_slot{0};
std::atomic<uint8_t> router_link{0};  // input_router::LinkState
std::atomic<bool> usb_keyboard_ready{false};
// A tap injected over the dev link (x << 16 | y), or -1.
std::atomic<int32_t> remote_tap{-1};

void post_ui(UiKind kind, const char* format, ...) {
  UiMessage message{};
  message.kind = kind;
  va_list args;
  va_start(args, format);
  vsnprintf(message.text, sizeof(message.text), format, args);
  va_end(args);
  xQueueSend(ui_messages, &message, 0);
}

void post_router(const RouterEvent& event) {
  if (xQueueSend(router_events, &event, 0) != pdTRUE &&
      event.kind < RouterEventKind::kBleSecured) {
    ++dropped_reports;
  }
}

void post_router(RouterEventKind kind, const uint8_t* report = nullptr) {
  RouterEvent event{};
  event.kind = kind;
  if (report != nullptr) {
    memcpy(event.report, report, sizeof(event.report));
  }
  post_router(event);
}

// ---------------------------------------------------------------- BLE -----

const char* ble_event_message(ble_keyboard::LinkEvent event);

void on_ble_link(ble_keyboard::LinkEvent event, uint8_t slot) {
  RouterEvent routed{};
  routed.slot = slot;
  switch (event) {
    case ble_keyboard::LinkEvent::kAdvertising:
      break;  // the router's own state drives the display
    case ble_keyboard::LinkEvent::kSecured:
      ble_secured = true;
      routed.kind = RouterEventKind::kBleSecured;
      post_router(routed);
      break;
    case ble_keyboard::LinkEvent::kDisconnected:
      ble_secured = false;
      routed.kind = RouterEventKind::kBleDisconnected;
      post_router(routed);
      break;
    default:  // pairing and unbind results, shown on the host screen
      post_ui(UiKind::kBle, "%s", ble_event_message(event));
      break;
  }
}

// ------------------------------------------------------------- Router -----

const char* link_state_name(input_router::LinkState state) {
  switch (state) {
    case input_router::LinkState::kConnected:
      return "connected";
    case input_router::LinkState::kConnecting:
      return "connecting";
    default:
      return "not connected";
  }
}

void execute(input_router::InputRouter& router,
             const input_router::Actions& actions);

uint32_t now_ms() { return pdTICKS_TO_MS(xTaskGetTickCount()); }

// Pending slot switch: when it times out the router shows "not connected".
uint8_t connect_slot = 0;
uint32_t connect_deadline_ms = 0;  // 0 = none

// Reports wait here until the BLE stack accepts them. BLE sends one batch per
// connection interval (far slower than the Navigator's 1 ms frames), and a
// dropped key release would leave a key stuck on the host, so failed sends
// are retried in order instead of being lost. Mouse motion is merged into an
// unsent mouse report with the same buttons.
struct OutReport {
  enum class Type : uint8_t { kKeyboard, kConsumer, kSystem, kMouse } type;
  uint8_t modifiers;
  uint8_t keys[input_router::kKeyCount];
  uint16_t usage;
  uint8_t buttons;
  int32_t x, y, wheel, pan;
};

constexpr size_t kOutboxSize = 64;
constexpr TickType_t kRetryInterval = pdMS_TO_TICKS(5);
OutReport outbox[kOutboxSize];
size_t outbox_head = 0;
size_t outbox_count = 0;

OutReport& outbox_at(size_t index) {
  return outbox[(outbox_head + index) % kOutboxSize];
}

void outbox_push(const OutReport& report) {
  if (outbox_count == kOutboxSize) {
    // Full while BLE is stalled: the oldest entry is lost.
    outbox_head = (outbox_head + 1) % kOutboxSize;
    --outbox_count;
    ++ble_send_failures;
  }
  outbox_at(outbox_count++) = report;
}

void outbox_clear() { outbox_head = outbox_count = 0; }

int8_t take_step(int32_t& remaining) {
  const int32_t step = remaining > 127 ? 127 : (remaining < -127 ? -127 : remaining);
  remaining -= step;
  return static_cast<int8_t>(step);
}

// Sends queued reports until the stack pushes back. Returns true when empty.
bool outbox_pump() {
  while (outbox_count > 0) {
    OutReport& head = outbox_at(0);
    bool sent = false;
    switch (head.type) {
      case OutReport::Type::kKeyboard:
        sent = ble_keyboard::SendKeyboard(head.modifiers, head.keys);
        break;
      case OutReport::Type::kConsumer:
        sent = ble_keyboard::SendConsumer(head.usage);
        break;
      case OutReport::Type::kSystem:
        sent = ble_keyboard::SendSystem(head.usage);
        break;
      case OutReport::Type::kMouse: {
        // Large merged motion goes out in int8 steps.
        OutReport step = head;
        const int8_t x = take_step(step.x);
        const int8_t y = take_step(step.y);
        const int8_t wheel = take_step(step.wheel);
        const int8_t pan = take_step(step.pan);
        sent = ble_keyboard::SendMouse(head.buttons, x, y, wheel, pan);
        if (sent && (step.x || step.y || step.wheel || step.pan)) {
          head = step;  // the rest of the motion stays queued
          continue;
        }
        break;
      }
    }
    if (!sent) {
      return false;
    }
    outbox_head = (outbox_head + 1) % kOutboxSize;
    --outbox_count;
  }
  return true;
}

void queue_keyboard(uint8_t modifiers, const uint8_t* keys) {
  OutReport report{};
  report.type = OutReport::Type::kKeyboard;
  report.modifiers = modifiers;
  memcpy(report.keys, keys, sizeof(report.keys));
  outbox_push(report);
}

void queue_usage(OutReport::Type type, uint16_t usage) {
  OutReport report{};
  report.type = type;
  report.usage = usage;
  outbox_push(report);
}

void queue_mouse(uint8_t buttons, int32_t x, int32_t y, int32_t wheel,
                 int32_t pan) {
  // outbox_pump runs on this same task, so even the head entry can be merged.
  if (outbox_count > 0) {
    OutReport& last = outbox_at(outbox_count - 1);
    if (last.type == OutReport::Type::kMouse && last.buttons == buttons) {
      last.x += x;
      last.y += y;
      last.wheel += wheel;
      last.pan += pan;
      return;
    }
  }
  OutReport report{};
  report.type = OutReport::Type::kMouse;
  report.buttons = buttons;
  report.x = x;
  report.y = y;
  report.wheel = wheel;
  report.pan = pan;
  outbox_push(report);
}

void execute_action(input_router::InputRouter& router,
                    const input_router::Action& action) {
  using input_router::ActionType;
  static constexpr uint8_t kNoKeys[input_router::kKeyCount] = {};
  switch (action.type) {
    case ActionType::kSendKeyboard:
      queue_keyboard(action.keyboard.modifiers, action.keyboard.keys);
      break;
    case ActionType::kSendConsumer:
      queue_usage(OutReport::Type::kConsumer, action.usage);
      break;
    case ActionType::kSendSystem:
      queue_usage(OutReport::Type::kSystem, action.usage);
      break;
    case ActionType::kSendMouse:
      queue_mouse(action.mouse.buttons, action.mouse.x, action.mouse.y,
                  action.mouse.wheel, action.mouse.pan);
      break;
    case ActionType::kSendAllReleased:
      queue_keyboard(0, kNoKeys);
      queue_usage(OutReport::Type::kConsumer, 0);
      queue_usage(OutReport::Type::kSystem, 0);
      queue_mouse(0, 0, 0, 0, 0);
      break;
    case ActionType::kStartConnect:
      // Give the old host its release (queued just before) a chance to go
      // out; whatever is left belonged to the old host and is dropped.
      for (int i = 0; i < 10 && !outbox_pump(); ++i) {
        vTaskDelay(kRetryInterval);
      }
      outbox_clear();
      save_active_slot(action.slot);
      ble_keyboard::SelectSlot(action.slot);
      connect_slot = action.slot;
      connect_deadline_ms = now_ms() + kConnectTimeoutMs;
      if (connect_deadline_ms == 0) {
        connect_deadline_ms = 1;
      }
      break;
    case ActionType::kNotifyUi:
      router_slot = router.active_slot();
      router_link = static_cast<uint8_t>(router.link_state());
      post_ui(UiKind::kLink, "%u %s: %s", router.active_slot() + 1,
              kSlotNames[router.active_slot()],
              link_state_name(router.link_state()));
      break;
  }
}

void execute(input_router::InputRouter& router,
             const input_router::Actions& actions) {
  for (size_t i = 0; i < actions.count; ++i) {
    execute_action(router, actions.items[i]);
  }
}

int8_t clamp_int8(int32_t value) {
  return static_cast<int8_t>(value > 127 ? 127 : (value < -127 ? -127 : value));
}

void route_touch_outputs(input_router::InputRouter& router,
                         const touchpad::MouseOutput* outputs, size_t count) {
  // Two-finger swipes become browser history shortcuts: Cmd+[ / Cmd+]
  // (HID usages for the US layout's bracket keys).
  static constexpr uint8_t kLeftGui = 0x08;
  static constexpr uint8_t kUsageLeftBracket = 0x2F;
  static constexpr uint8_t kUsageRightBracket = 0x30;
  input_router::Actions actions;
  for (size_t i = 0; i < count; ++i) {
    if (outputs[i].swipe != touchpad::Swipe::kNone) {
      router.OnShortcut(kLeftGui,
                        outputs[i].swipe == touchpad::Swipe::kRight
                            ? kUsageLeftBracket
                            : kUsageRightBracket,
                        actions);
      execute(router, actions);
      ESP_LOGI(kTag, "swipe %s",
               outputs[i].swipe == touchpad::Swipe::kRight ? "back" : "forward");
      post_ui(UiKind::kAux, "swipe %s",
              outputs[i].swipe == touchpad::Swipe::kRight ? "back" : "forward");
      continue;
    }
    input_router::MouseReport report;
    report.buttons = outputs[i].buttons;
    report.x = clamp_int8(outputs[i].dx);
    report.y = clamp_int8(outputs[i].dy);
    report.wheel = clamp_int8(outputs[i].wheel);
    report.pan = clamp_int8(outputs[i].pan);
    router.OnUsbMouseReport(report, actions);
    execute(router, actions);
  }
}

// Logs a touch frame whenever the set of touching fingers changes.
void log_touch_frame(const touchpad::Frame& frame) {
  static uint8_t last_mask = 0;
  uint8_t mask = 0;
  for (size_t i = 0; i < touchpad::kMaxContacts; ++i) {
    const touchpad::Contact& contact = frame.contacts[i];
    if (contact.touching) {
      mask |= 1u << (contact.id & 7);
    }
  }
  if (mask == last_mask) {
    return;
  }
  last_mask = mask;
  ESP_LOGI(kTag,
           "touch ids=%02x count=%u max=%ld,%ld c0=%u/%d/%d(%ld,%ld) "
           "c1=%u/%d/%d(%ld,%ld)",
           mask, frame.contact_count, static_cast<long>(frame.max_x),
           static_cast<long>(frame.max_y), frame.contacts[0].id,
           frame.contacts[0].touching, frame.contacts[0].confident,
           static_cast<long>(frame.contacts[0].x),
           static_cast<long>(frame.contacts[0].y), frame.contacts[1].id,
           frame.contacts[1].touching, frame.contacts[1].confident,
           static_cast<long>(frame.contacts[1].x),
           static_cast<long>(frame.contacts[1].y));
}

void router_task(void*) {
  input_router::InputRouter router(initial_slot);
  // The saved slot advertises from boot; show it as connecting until the
  // host connects or the timeout passes.
  connect_slot = initial_slot;
  connect_deadline_ms = now_ms() + kConnectTimeoutMs;
  router_slot = router.active_slot();
  router_link = static_cast<uint8_t>(input_router::LinkState::kConnecting);
  post_ui(UiKind::kLink, "%u %s: connecting", initial_slot + 1,
          kSlotNames[initial_slot]);
  touchpad::Gestures gestures;
  touchpad::MouseOutput touch_outputs[touchpad::kMaxOutputs];
  input_router::Actions actions;
  RouterEvent event{};
  while (true) {
    ++router_loop_count;
    TickType_t wait = outbox_count > 0 ? kRetryInterval : portMAX_DELAY;
    for (const uint32_t deadline :
         {gestures.next_deadline_ms(), connect_deadline_ms}) {
      if (deadline == 0) {
        continue;
      }
      const int32_t until = static_cast<int32_t>(deadline - now_ms());
      const TickType_t deadline_wait =
          until <= 0 ? 0 : pdMS_TO_TICKS(static_cast<uint32_t>(until));
      if (deadline_wait < wait) {
        wait = deadline_wait;
      }
    }
    if (xQueueReceive(router_events, &event, wait) != pdTRUE) {
      if (connect_deadline_ms != 0 &&
          static_cast<int32_t>(now_ms() - connect_deadline_ms) >= 0) {
        connect_deadline_ms = 0;
        router.OnConnectTimeout(connect_slot, actions);
        execute(router, actions);
      }
      route_touch_outputs(router, touch_outputs,
                          gestures.OnTick(now_ms(), touch_outputs));
      outbox_pump();
      continue;
    }
    switch (event.kind) {
      case RouterEventKind::kUsbReport: {
        for (const uint8_t key : event.report) {
          if (key != 0) {
            ++input_activity_count;
            break;
          }
        }
        input_router::KeyboardReport report;
        report.modifiers = event.report[0];
        memcpy(report.keys, &event.report[2], input_router::kKeyCount);
        router.OnUsbKeyboardReport(report, actions);
        break;
      }
      case RouterEventKind::kUsbConsumer:
        if (event.usage != 0) ++input_activity_count;
        router.OnUsbConsumerReport(event.usage, actions);
        break;
      case RouterEventKind::kUsbSystem:
        if (event.usage != 0) ++input_activity_count;
        router.OnUsbSystemReport(event.usage, actions);
        break;
      case RouterEventKind::kUsbMouse:
        if (event.mouse.buttons || event.mouse.x || event.mouse.y ||
            event.mouse.wheel || event.mouse.pan) ++input_activity_count;
        router.OnUsbMouseReport(event.mouse, actions);
        break;
      case RouterEventKind::kUsbTouch:
        if (event.touch.buttons) ++input_activity_count;
        for (const auto& contact : event.touch.contacts) {
          if (contact.touching && contact.confident) {
            ++input_activity_count;
            break;
          }
        }
        event.touch.time_ms = now_ms();
        log_touch_frame(event.touch);
        route_touch_outputs(router, touch_outputs,
                            gestures.OnFrame(event.touch, touch_outputs));
        actions.count = 0;
        break;
      case RouterEventKind::kUsbDisconnected:
        gestures.Reset(touch_outputs);  // the router releases everything
        router.OnUsbDisconnected(actions);
        break;
      case RouterEventKind::kBleSecured:
        if (event.slot == connect_slot) {
          connect_deadline_ms = 0;
        }
        router.OnBleConnected(event.slot, actions);
        break;
      case RouterEventKind::kBleDisconnected:
        outbox_clear();  // nothing reaches the host; it gets a release later
        router.OnBleDisconnected(event.slot, actions);
        break;
      case RouterEventKind::kUiSelectSlot:
        router.OnSelectSlot(event.slot, actions);
        break;
      case RouterEventKind::kUiStartPairing:
        // The window belongs to the selected slot, so select it first.
        router.OnSelectSlot(event.slot, actions);
        execute(router, actions);
        actions.count = 0;
        if (!ble_keyboard::StartPairing(event.slot)) {
          post_ui(UiKind::kBle, "ペアリングを開始できません");
        }
        break;
      case RouterEventKind::kUiUnbind:
        // The host being removed gets its release before the link drops.
        actions.count = 0;
        if (event.slot == router.active_slot()) {
          router.Reset(actions);
          execute(router, actions);
          for (int i = 0; i < 10 && !outbox_pump(); ++i) {
            vTaskDelay(kRetryInterval);
          }
          outbox_clear();
          actions.count = 0;
        }
        if (!ble_keyboard::Unbind(event.slot)) {
          post_ui(UiKind::kBle, "解除を開始できません");
        }
        break;
      case RouterEventKind::kUiSleep: {
        // Drop pending motion/presses, then send only the final neutral state.
        outbox_clear();
        gestures.Reset(touch_outputs);
        router.Reset(actions);
        execute(router, actions);
        for (int i = 0; i < 10 && !outbox_pump(); ++i) {
          vTaskDelay(kRetryInterval);
        }
        const bool released = outbox_count == 0 || !ble_secured;
        outbox_clear();
        actions.count = 0;
        // Stop accepting input before UI shuts the transport down. A failed
        // release is recovered by reboot; never leave input running into sleep.
        if (!released) {
          ESP_LOGE(kTag, "sleep release was not accepted; restarting");
          esp_restart();
        }
        xTaskNotifyGive(event.reply_task);
        vTaskSuspend(nullptr);
        break;
      }
    }
    execute(router, actions);
    outbox_pump();
  }
}

// ---------------------------------------------------------------- USB -----

void on_hid_device_event(hid_host_device_handle_t handle,
                         hid_host_driver_event_t event, void*) {
  if (event != HID_HOST_DRIVER_EVENT_CONNECTED) {
    return;
  }
  const DeviceEvent item{handle, true};
  xQueueSend(device_events, &item, 0);
}

// Non-keyboard reports can arrive every millisecond; the LCD gets a sample.
void post_ui_throttled(const char* format, ...) {
  static std::atomic<TickType_t> last_post{0};
  const TickType_t now = xTaskGetTickCount();
  if (now - last_post.load() < pdMS_TO_TICKS(100)) {
    return;
  }
  last_post = now;
  UiMessage message{};
  message.kind = UiKind::kAux;
  va_list args;
  va_start(args, format);
  vsnprintf(message.text, sizeof(message.text), format, args);
  va_end(args);
  xQueueSend(ui_messages, &message, 0);
}

UsbInterface* find_interface(hid_host_device_handle_t handle) {
  for (size_t i = 0; i < kMaxInterfaces; ++i) {
    UsbInterface& usb = usb_interfaces[i];
    if (usb.in_use && usb.handle == handle) {
      return &usb;
    }
  }
  return nullptr;
}

UsbInterface* allocate_interface(hid_host_device_handle_t handle) {
  for (size_t i = 0; i < kMaxInterfaces; ++i) {
    UsbInterface& usb = usb_interfaces[i];
    if (!usb.in_use) {
      // Fields are set in place; `parsed` is filled by hid_descriptor::Parse.
      usb.in_use = true;
      usb.boot_keyboard = false;
      usb.handle = handle;
      return &usb;
    }
  }
  return nullptr;
}

void on_hid_interface_event(hid_host_device_handle_t handle,
                            hid_host_interface_event_t event, void* arg) {
  if (event == HID_HOST_INTERFACE_EVENT_DISCONNECTED) {
    const DeviceEvent item{handle, false};
    xQueueSend(device_events, &item, 0);
    return;
  }
  if (event != HID_HOST_INTERFACE_EVENT_INPUT_REPORT) {
    return;
  }
  ++usb_input_count;

  const auto* usb = static_cast<const UsbInterface*>(arg);
  uint8_t report[64]{};
  size_t report_size = 0;
  if (usb == nullptr ||
      hid_host_device_get_raw_input_report_data(handle, report, sizeof(report),
                                                &report_size) != ESP_OK) {
    return;
  }

  if (usb->boot_keyboard) {
    if (report_size < 8) {
      return;
    }
    post_router(RouterEventKind::kUsbReport, report);
    // The LCD copy is best effort and never holds up input.
    UiMessage message{};
    message.kind = UiKind::kKeyboard;
    memcpy(message.report, report, sizeof(message.report));
    xQueueSend(ui_messages, &message, 0);
    return;
  }

  const bool has_id = usb->parsed.uses_report_ids;
  if (report_size < (has_id ? 1u : 0u)) {
    return;
  }
  const uint8_t report_id = has_id ? report[0] : 0;
  const uint8_t* payload = has_id ? &report[1] : report;
  const size_t payload_size = has_id ? report_size - 1 : report_size;
  const hid_descriptor::InputReport* info = usb->parsed.Find(report_id);
  if (info == nullptr) {
    return;
  }

  RouterEvent routed{};
  switch (info->kind) {
    case hid_descriptor::ReportKind::kConsumer:
    case hid_descriptor::ReportKind::kSystem:
      routed.kind = info->kind == hid_descriptor::ReportKind::kConsumer
                       ? RouterEventKind::kUsbConsumer
                       : RouterEventKind::kUsbSystem;
      routed.usage = static_cast<uint16_t>(
          hid_descriptor::Extract(payload, payload_size, info->usage));
      post_router(routed);
      post_ui_throttled("%s %04X",
                        routed.kind == RouterEventKind::kUsbConsumer ? "consumer"
                                                                     : "system",
                        routed.usage);
      break;
    case hid_descriptor::ReportKind::kMouse:
      routed.kind = RouterEventKind::kUsbMouse;
      routed.mouse.buttons = static_cast<uint8_t>(
          hid_descriptor::Extract(payload, payload_size, info->buttons));
      routed.mouse.x = clamp_int8(
          hid_descriptor::Extract(payload, payload_size, info->x));
      routed.mouse.y = clamp_int8(
          hid_descriptor::Extract(payload, payload_size, info->y));
      routed.mouse.wheel = clamp_int8(
          hid_descriptor::Extract(payload, payload_size, info->wheel));
      routed.mouse.pan = clamp_int8(
          hid_descriptor::Extract(payload, payload_size, info->pan));
      post_router(routed);
      post_ui_throttled("mouse b%02X x%d y%d w%d", routed.mouse.buttons,
                        routed.mouse.x, routed.mouse.y, routed.mouse.wheel);
      break;
    case hid_descriptor::ReportKind::kDigitizer: {
      routed.kind = RouterEventKind::kUsbTouch;
      uint8_t touching = 0;
      for (size_t i = 0;
           i < info->finger_count && i < touchpad::kMaxContacts; ++i) {
        const hid_descriptor::Finger& finger = info->fingers[i];
        routed.touch.max_x = finger.x.logical_max;
        routed.touch.max_y = finger.y.logical_max;
        touchpad::Contact& contact = routed.touch.contacts[i];
        contact.touching =
            hid_descriptor::Extract(payload, payload_size, finger.tip) != 0;
        contact.confident =
            !finger.confidence.present ||
            hid_descriptor::Extract(payload, payload_size, finger.confidence);
        contact.id = static_cast<uint8_t>(
            hid_descriptor::Extract(payload, payload_size, finger.contact_id));
        contact.x = hid_descriptor::Extract(payload, payload_size, finger.x);
        contact.y = hid_descriptor::Extract(payload, payload_size, finger.y);
        touching += contact.touching;
      }
      routed.touch.contact_count = static_cast<uint8_t>(
          hid_descriptor::Extract(payload, payload_size, info->contact_count));
      routed.touch.buttons = static_cast<uint8_t>(
          hid_descriptor::Extract(payload, payload_size, info->buttons));
      post_router(routed);
      post_ui_throttled("touch %u x%ld y%ld b%u", touching,
                        static_cast<long>(routed.touch.contacts[0].x),
                        static_cast<long>(routed.touch.contacts[0].y),
                        routed.touch.buttons);
      break;
    }
    default:
      // NKRO keyboard and vendor reports are not forwarded.
      post_ui_throttled("id %u %s ignored", report_id,
                        info->kind == hid_descriptor::ReportKind::kDigitizer
                            ? "digitizer"
                            : "report");
      break;
  }
}

// A precision touchpad reports as a plain mouse until the host selects the
// touchpad input mode; only then do its contact reports carry gestures.
void enable_touchpad_mode(const UsbInterface& usb, uint8_t iface_num) {
  const uint8_t mode_id = usb.parsed.input_mode_report_id;
  if (mode_id == 0) {
    return;
  }
  uint8_t input_mode[2] = {mode_id, 3};
  const esp_err_t mode_status = hid_class_request_set_report(
      usb.handle, HID_REPORT_TYPE_FEATURE, mode_id, input_mode,
      sizeof(input_mode));
  esp_err_t switch_status = ESP_OK;
  if (const uint8_t switch_id = usb.parsed.selective_report_id) {
    uint8_t surface_and_button[2] = {switch_id, 0x03};
    switch_status = hid_class_request_set_report(
        usb.handle, HID_REPORT_TYPE_FEATURE, switch_id, surface_and_button,
        sizeof(surface_and_button));
  }
  ESP_LOGI(kTag, "interface %u touchpad mode: %s / %s", iface_num,
           esp_err_to_name(mode_status), esp_err_to_name(switch_status));
  post_ui_throttled("touchpad mode %s", mode_status == ESP_OK ? "on" : "failed");
}

void close_interface(UsbInterface* usb) {
  hid_host_device_close(usb->handle);
  usb->in_use = false;
}

void handle_device_event(const DeviceEvent& item) {
  if (!item.connected) {
    UsbInterface* usb = find_interface(item.handle);
    if (usb != nullptr && usb->boot_keyboard) {
      post_router(RouterEventKind::kUsbDisconnected);
      usb_keyboard_ready = false;
      post_ui(UiKind::kStatus, "USB disconnected");
    }
    if (usb != nullptr) {
      close_interface(usb);
    } else {
      hid_host_device_close(item.handle);
    }
    return;
  }

  hid_host_dev_params_t params{};
  if (hid_host_device_get_params(item.handle, &params) != ESP_OK) {
    post_ui(UiKind::kStatus, "HID params error");
    return;
  }
  UsbInterface* usb = allocate_interface(item.handle);
  if (usb == nullptr) {
    post_ui(UiKind::kStatus, "Too many HID interfaces");
    return;
  }
  usb->boot_keyboard = params.sub_class == kBootInterfaceSubclass &&
                       params.proto == kBootKeyboardProtocol;

  const hid_host_device_config_t device_config = {
      .callback = on_hid_interface_event,
      .callback_arg = usb,
  };
  if (hid_host_device_open(item.handle, &device_config) != ESP_OK) {
    usb->in_use = false;
    post_ui(UiKind::kStatus, "HID open failed (if %u)", params.iface_num);
    return;
  }

  if (!usb->boot_keyboard) {
    size_t length = 0;
    const uint8_t* descriptor =
        hid_host_get_report_descriptor(item.handle, &length);
    if (descriptor == nullptr ||
        !hid_descriptor::Parse(descriptor, length, usb->parsed)) {
      ESP_LOGW(kTag, "interface %u: report descriptor unusable",
               params.iface_num);
      close_interface(usb);
      return;
    }
    bool forwardable = false;
    for (size_t i = 0; i < usb->parsed.count; ++i) {
      const auto kind = usb->parsed.reports[i].kind;
      forwardable |= kind == hid_descriptor::ReportKind::kConsumer ||
                     kind == hid_descriptor::ReportKind::kSystem ||
                     kind == hid_descriptor::ReportKind::kMouse ||
                     kind == hid_descriptor::ReportKind::kDigitizer;
    }
    ESP_LOGI(kTag, "interface %u: %u input reports, forwardable=%d",
             params.iface_num, static_cast<unsigned>(usb->parsed.count),
             forwardable);
    if (!forwardable) {
      close_interface(usb);  // e.g. the vendor (Oryx) interface
      return;
    }
    if (hid_host_device_start(item.handle) != ESP_OK) {
      post_ui(UiKind::kStatus, "HID start failed (if %u)", params.iface_num);
      close_interface(usb);
      return;
    }
    enable_touchpad_mode(*usb, params.iface_num);
    return;
  }

  hid_host_dev_info_t info{};
  if (hid_host_get_device_info(item.handle, &info) != ESP_OK) {
    post_ui(UiKind::kStatus, "HID info failed");
    close_interface(usb);
    return;
  }

  esp_err_t status =
      hid_class_request_set_protocol(item.handle, HID_REPORT_PROTOCOL_BOOT);
  if (status == ESP_OK) {
    status = hid_host_device_start(item.handle);
  }
  if (status != ESP_OK) {
    post_ui(UiKind::kStatus, "Boot keyboard failed\n%s",
            esp_err_to_name(status));
    close_interface(usb);
    return;
  }
  usb_keyboard_ready = true;
  post_ui(UiKind::kStatus, "VID %04X PID %04X\nInterface %u Boot ready",
          info.VID, info.PID, params.iface_num);
}

void usb_library_task(void*) {
  while (true) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(portMAX_DELAY, &flags);
  }
}

void hid_event_task(void*) {
  DeviceEvent event{};
  while (xQueueReceive(device_events, &event, portMAX_DELAY) == pdTRUE) {
    handle_device_event(event);
  }
}

// Switches the shared USB PHY from Serial-JTAG to the OTG host, so serial
// logging and auto-reset flashing stop working until the next reset.
bool start_usb_host() {
  dev_link::StopSerial();  // the USB PHY is about to leave Serial-JTAG mode
  usb_host_config_t usb_config{};
  usb_config.skip_phy_setup = false;
  usb_config.intr_flags = ESP_INTR_FLAG_LEVEL1;
  if (usb_host_install(&usb_config) != ESP_OK) {
    post_ui(UiKind::kStatus, "USB Host install failed");
    return false;
  }
  xTaskCreatePinnedToCore(usb_library_task, "usb_events", 4096, nullptr, 10,
                          nullptr, 1);

  const hid_host_driver_config_t hid_config = {
      .create_background_task = true,
      .task_priority = 9,
      .stack_size = 4096,
      .core_id = 1,
      .callback = on_hid_device_event,
      .callback_arg = nullptr,
  };
  if (hid_host_install(&hid_config) != ESP_OK) {
    post_ui(UiKind::kStatus, "USB HID install failed");
    return false;
  }
  xTaskCreatePinnedToCore(hid_event_task, "hid_events", 8192, nullptr, 9,
                          nullptr, 1);
  post_ui(UiKind::kStatus, "Waiting for Voyager");
  return true;
}

// ----------------------------------------------------------------- UI -----

// Above this the USB VBUS line is fed from outside (Mac or the base supply),
// so the OTG boost must stay off to avoid two sources on one line.
constexpr int kExternalVbusMillivolts = 4000;
constexpr uint8_t kBrightness[ui::kBrightnessLevels] = {40, 128, 255};
constexpr uint8_t kDimmedBrightness = 10;
// INA226 on the battery line: negative is charging.
constexpr float kChargingMilliamps = -20.0f;

struct BatteryStatus {
  int percent = -1;
};

BatteryStatus read_battery_status() {
  BatteryStatus battery;
  auto& pmic = M5.Power.Axp2101;
  if (M5.Power.getType() != m5::Power_Class::pmic_t::pmic_axp2101 ||
      !pmic.isEnabled()) {
    return battery;
  }

  // The convenience getters turn failed I2C reads into zero. Read their
  // registers with the checked API so an unavailable battery stays unknown.
  uint8_t status[2]{};
  if (!pmic.readRegister(0x00, status, sizeof(status)) ||
      !(status[0] & 0x08)) {  // battery present
    return battery;
  }
  uint8_t percent = 0;
  if (pmic.readRegister(0xA4, &percent, 1) && percent <= 100) {
    battery.percent = percent;
  }
  return battery;
}

// Samples the supply values for the LCD and GET /status.
BatteryStatus sample_power() {
  uint8_t status = 0;
  uint8_t adc[2]{};
  auto& pmic = M5.Power.Axp2101;
  const bool status_ok = pmic.readRegister(0x00, &status, 1);
  pmic_status = status_ok ? status : -1;
  int vbus = -1;
  if (status_ok) {
    if (!(status & 0x20)) {
      vbus = 0;  // PMIC input not valid; not a physical USB-C measurement.
    } else if (pmic.readRegister(0x38, adc, sizeof(adc))) {
      const int raw = ((adc[0] & 0x3f) << 8) | adc[1];
      if (raw < 16384 - 32) vbus = raw;
    }
  }
  vbus_millivolts = vbus;
  uint8_t charge = 0;
  charge_current_register = pmic.readRegister(0x62, &charge, 1) ? charge : -1;
  uint8_t outputs[2]{};
  aw_output_registers = M5.In_I2C.readRegister(
      kAw9523Address, 0x02, outputs, sizeof(outputs), 400000)
      ? (outputs[0] | (outputs[1] << 8)) : -1;
  battery_volts = M5StackChan.getBatteryVoltage();
  battery_milliamps = M5StackChan.getBatteryCurrent() * 1000.0f;
  return read_battery_status();
}

const char* reset_reason_name(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power on";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_DEEPSLEEP:
      return "deep sleep";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT WDT";
    case ESP_RST_TASK_WDT:
      return "TASK WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_USB:
      return "usb";
    default:
      return "other";
  }
}

// How the USB side is running; saved so a software reset (OTA) resumes it.
enum class UsbRun : uint8_t { kNone, kInternal, kExternal, kWithoutUsb };

constexpr char kHubNamespace[] = "hub";

uint8_t load_setting(const char* key, uint8_t fallback) {
  nvs_handle_t nvs;
  uint8_t value = fallback;
  if (nvs_open(kHubNamespace, NVS_READONLY, &nvs) == ESP_OK) {
    nvs_get_u8(nvs, key, &value);
    nvs_close(nvs);
  }
  return value;
}

void save_setting(const char* key, uint8_t value) {
  nvs_handle_t nvs;
  if (nvs_open(kHubNamespace, NVS_READWRITE, &nvs) == ESP_OK) {
    nvs_set_u8(nvs, key, value);
    nvs_commit(nvs);
    nvs_close(nvs);
  }
}

// Sets the Voyager supply for `setting`. With the boost on, the PMIC does not
// see VBUS, so an external supply can only be detected while it is off.
// `notice` gets a line for the home screen when the request was not followed.
UsbRun apply_power(ui::PowerSetting setting, char* notice, size_t size) {
  notice[0] = '\0';
  const bool boost_on = M5.Power.getUsbOutput();
  if (setting == ui::PowerSetting::kExternal) {
    if (boost_on) {
      M5.Power.setUsbOutput(false);
    }
    return UsbRun::kExternal;
  }
  if (boost_on) {
    return UsbRun::kInternal;  // automatic: decided again at the next start
  }
  if (M5.Power.getVBUSVoltage() > kExternalVbusMillivolts) {
    if (setting == ui::PowerSetting::kInternal) {
      snprintf(notice, size, "外部5Vがあるため外部給電で動作中");
    }
    return UsbRun::kExternal;
  }
  M5.Power.setUsbOutput(true);
  if (!M5.Power.getUsbOutput()) {
    snprintf(notice, size, "キーボードへの給電に失敗しました");
    return UsbRun::kExternal;
  }
  return UsbRun::kInternal;
}

// Base LEDs 0-5 are on the left, 6-11 on the right.
void show_leds(const led::Pattern& pattern) {
  for (uint8_t i = 0; i < 12; ++i) {
    const led::Color& color = i < 6 ? pattern.left : pattern.right;
    M5StackChan.setRgbColor(i, color.r, color.g, color.b);
  }
  M5StackChan.refreshRgb();
}

const char* ble_event_message(ble_keyboard::LinkEvent event) {
  switch (event) {
    case ble_keyboard::LinkEvent::kPairingComplete:
      return "ペアリングしました";
    case ble_keyboard::LinkEvent::kPairingTimedOut:
      return "時間切れです";
    case ble_keyboard::LinkEvent::kPairingFailed:
      return "ペアリングに失敗しました";
    case ble_keyboard::LinkEvent::kUnbound:
      return "登録を解除しました";
    case ble_keyboard::LinkEvent::kUnbindFailed:
      return "解除に失敗しました（やり直せます）";
    default:
      return "";
  }
}

void post_router_slot(RouterEventKind kind, uint8_t slot) {
  RouterEvent event{};
  event.kind = kind;
  event.slot = slot;
  post_router(event);
}

void ui_task(void*) {
  // Temporary supply-load comparison; leave charge voltage/input limits intact.
  uint8_t before = 0;
  const bool before_ok = M5.Power.Axp2101.readRegister(0x62, &before, 1);
  M5.Power.setChargeCurrent(200);
  uint8_t after = 0;
  const bool after_ok = M5.Power.Axp2101.readRegister(0x62, &after, 1);
  ESP_LOGI(kTag, "charge diagnostic 200mA: reg62 before=%d after=%d",
           before_ok ? before : -1, after_ok ? after : -1);

  ui::Model model;
  model.Restore(static_cast<ui::PowerSetting>(load_setting("power", 0) % 3),
                load_setting("bright", 1));
  M5.Display.setBrightness(kBrightness[model.brightness()]);
  M5.Display.fillScreen(TFT_BLACK);
  const bool view_ok = ui::Begin(kSlotNames);
  if (!view_ok) {
    ESP_LOGE(kTag, "UI canvas allocation failed");
  }

  // Resume the saved supply mode after software reset or manual hibernation.
  const auto last_run = static_cast<UsbRun>(load_setting("run", 0));
  const bool resuming = (esp_reset_reason() == ESP_RST_SW ||
                         esp_reset_reason() == ESP_RST_DEEPSLEEP) &&
                        (last_run == UsbRun::kInternal ||
                         last_run == UsbRun::kExternal ||
                         (esp_reset_reason() == ESP_RST_DEEPSLEEP &&
                          last_run == UsbRun::kWithoutUsb));
  // Otherwise VBUS stays off until the start: the Mac may be on the USB-C.
  if (!(resuming && last_run == UsbRun::kInternal)) {
    M5.Power.setUsbOutput(false);
  }

  UsbRun run = UsbRun::kNone;
  bool usb_started = false;
  bool usb_start_failed = false;
  char start_notice[64] = "";
  char usb_status[96] = "";
  char aux[96] = "";
  char host_message[64] = "";
  char diagnostics[512] = "";
  // Starts the USB host on the first call; later calls only move the supply.
  auto start = [&](ui::PowerSetting setting) {
    run = apply_power(setting, start_notice, sizeof(start_notice));
    save_setting("run", static_cast<uint8_t>(run));
    if (!usb_started) {
      usb_started = start_usb_host();
      usb_start_failed = !usb_started;
      if (!usb_started) {
        snprintf(start_notice, sizeof(start_notice),
                 "USBホストを開始できません");
      }
    }
  };

  model.Begin(now_ms(), !resuming);
  if (resuming) {
    if (last_run == UsbRun::kWithoutUsb) {
      run = last_run;
    } else {
      start(last_run == UsbRun::kInternal ? ui::PowerSetting::kInternal
                                         : ui::PowerSetting::kExternal);
    }
  }

  ui::Status shown;
  memset(&shown, 0xff, sizeof(shown));
  BatteryStatus battery;
  ble_keyboard::SlotSnapshot slots[ui::kSlotCount];
  UiMessage message{};
  TickType_t last_sample = 0;
  TickType_t last_periodic_draw = 0;
  bool was_dimmed = false;
  bool was_off = false;
  uint32_t seen_inputs = input_activity_count;
  led::Pattern shown_leds{led::kRed, led::kRed};
  show_leds(shown_leds);
  while (true) {
    ++ui_loop_count;
    const uint32_t now = now_ms();
    M5StackChan.update();

    if (const uint32_t inputs = input_activity_count; inputs != seen_inputs) {
      seen_inputs = inputs;
      model.Activity(now);
    }
    ui::Command command = model.OnTick(now);
    const auto touch = M5.Touch.getDetail();
    if (command.type == ui::CommandType::kNone && touch.wasPressed()) {
      command = model.OnTouch(touch.x, touch.y, now);
    }
    if (const int32_t tap = remote_tap.exchange(-1);
        command.type == ui::CommandType::kNone && tap >= 0) {
      command = model.OnTouch(tap >> 16, tap & 0xffff, now);
    }
    switch (command.type) {
      case ui::CommandType::kNone:
        break;
      case ui::CommandType::kStart:
        start(static_cast<ui::PowerSetting>(command.value));
        break;
      case ui::CommandType::kStartWithoutUsb:
        run = UsbRun::kWithoutUsb;
        save_setting("run", static_cast<uint8_t>(run));
        break;
      case ui::CommandType::kSelectSlot:
        post_router_slot(RouterEventKind::kUiSelectSlot, command.value);
        break;
      case ui::CommandType::kSetPower:
        save_setting("power", command.value);
        if (usb_started) {
          start(static_cast<ui::PowerSetting>(command.value));
        }
        break;
      case ui::CommandType::kSetBrightness:
        save_setting("bright", command.value);
        break;
      case ui::CommandType::kStartPairing:
        host_message[0] = '\0';
        post_router_slot(RouterEventKind::kUiStartPairing, command.value);
        break;
      case ui::CommandType::kStopPairing:
        ble_keyboard::StopPairing();
        break;
      case ui::CommandType::kUnbind:
        host_message[0] = '\0';
        post_router_slot(RouterEventKind::kUiUnbind, command.value);
        break;
      case ui::CommandType::kRestart:
        esp_restart();
        break;
      case ui::CommandType::kSleep: {
        RouterEvent sleep{};
        sleep.kind = RouterEventKind::kUiSleep;
        sleep.reply_task = xTaskGetCurrentTaskHandle();
        // Unlike ordinary LCD requests, sleep cannot proceed without an ack.
        if (xQueueSend(router_events, &sleep, pdMS_TO_TICKS(100)) != pdTRUE) {
          snprintf(start_notice, sizeof(start_notice), "休止を開始できません");
          model.Wake(now);
          break;
        }
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) == 0 ||
            !ble_keyboard::StopForSleep()) {
          ESP_LOGE(kTag, "sleep shutdown failed; restarting");
          esp_restart();
        }
        const esp_err_t wifi_stop = esp_wifi_stop();
        if (wifi_stop != ESP_OK && wifi_stop != ESP_ERR_WIFI_NOT_INIT) {
          ESP_LOGE(kTag, "sleep Wi-Fi stop failed; restarting");
          esp_restart();
        }
        show_leds({led::kOff, led::kOff});
        M5.Display.setBrightness(0);
        if (run == UsbRun::kInternal) M5.Power.setUsbOutput(false);
        M5.Power.deepSleep(M5.Power.sleep_no_timer, true);
        // A wake-pin/I2C failure can make deepSleep return. BLE is stopped and
        // Router is parked, so reboot to restore operation rather than hang.
        ESP_LOGE(kTag, "deep sleep did not start; restarting");
        esp_restart();
        break;
      }
    }

    while (xQueueReceive(ui_messages, &message, 0) == pdTRUE) {
      switch (message.kind) {
        case UiKind::kStatus:
          memcpy(usb_status, message.text, sizeof(usb_status));
          model.Wake(now);
          break;
        case UiKind::kLink:
          model.Wake(now);
          break;
        case UiKind::kKeyboard:
          break;
        case UiKind::kAux:
          memcpy(aux, message.text, sizeof(aux));
          break;
        case UiKind::kBle:
          memcpy(host_message, message.text, sizeof(host_message) - 1);
          model.Wake(now);
          break;
      }
    }

    const TickType_t ticks = xTaskGetTickCount();
    if (ticks - last_sample >= pdMS_TO_TICKS(500) || last_sample == 0) {
      last_sample = ticks;
      battery = sample_power();
      for (uint8_t slot = 0; slot < ui::kSlotCount; ++slot) {
        slots[slot] = ble_keyboard::SlotInfo(slot);
      }
    }

    ui::Status status;
    memset(&status, 0, sizeof(status));
    status.active_slot = router_slot;
    status.link = static_cast<ui::Link>(router_link.load());
    status.keyboard = !usb_started
                          ? ui::Keyboard::kStopped
                          : (usb_keyboard_ready ? ui::Keyboard::kReady
                                                : ui::Keyboard::kWaiting);
    for (uint8_t slot = 0; slot < ui::kSlotCount; ++slot) {
      status.registered[slot] = slots[slot].registered;
    }
    status.battery_percent = static_cast<int8_t>(battery.percent);
    status.charging = battery_milliamps < kChargingMilliamps;
    status.external_power = vbus_millivolts > kExternalVbusMillivolts;
    if (start_notice[0] != '\0') {
      snprintf(status.notice, sizeof(status.notice), "%s", start_notice);
    } else if (run == UsbRun::kWithoutUsb) {
      snprintf(status.notice, sizeof(status.notice),
               "USB書き込みモード（再起動で解除）");
    } else if (status.keyboard == ui::Keyboard::kWaiting) {
      snprintf(status.notice, sizeof(status.notice),
               "キーボードが見つかりません");
    }
    const auto& host = slots[model.host()];
    memcpy(status.host_address, host.address, sizeof(status.host_address));
    status.pairing_seconds = host.pairing_seconds_remaining;
    memcpy(status.host_message, host_message, sizeof(status.host_message));
    model.SetHostState(host.registered, host.pairing, host.unbinding);
    model.SetHomeSlots(slots[2].registered || status.active_slot == 2 ? 3 : 2);

    const bool dimmed = model.Dimmed(now);
    const bool off = model.ScreenOff(now);
    if (dimmed != was_dimmed || off != was_off ||
        command.type == ui::CommandType::kSetBrightness) {
      was_dimmed = dimmed;
      was_off = off;
      M5.Display.setBrightness(
          off ? 0 : (dimmed ? kDimmedBrightness
                            : kBrightness[model.brightness()]));
    }

    led::Input leds{};
    leds.slot = status.active_slot;
    leds.connected = status.link == ui::Link::kConnected;
    leds.keyboard_missing = status.keyboard == ui::Keyboard::kWaiting;
    leds.error = usb_start_failed ||
        slots[status.active_slot].last_error != ble_keyboard::ManagementError::kNone;
    leds.on_battery = !status.external_power && !status.charging;
    leds.screen_off = off;
    if (const led::Pattern pattern = led::Compute(leds, now);
        !(pattern == shown_leds)) {
      shown_leds = pattern;
      show_leds(pattern);
    }

    // Screens with a clock or live values are redrawn on a timer as well.
    const bool live = model.screen() == ui::Screen::kBoot ||
                      model.screen() == ui::Screen::kDiagnostics;
    const bool periodic =
        live && ticks - last_periodic_draw >= pdMS_TO_TICKS(250);
    if (view_ok && (model.TakeDirty() || periodic ||
                    memcmp(&status, &shown, sizeof(status)) != 0)) {
      last_periodic_draw = ticks;
      shown = status;
      if (model.screen() == ui::Screen::kDiagnostics) {
        char first_line[48];
        snprintf(first_line, sizeof(first_line), "%.*s",
                 static_cast<int>(strcspn(usb_status, "\n")), usb_status);
        snprintf(diagnostics, sizeof(diagnostics),
                 "Wi-Fi %s\nVBUS %dmV  boost %s\n"
                 "battery %.2fV %.0fmA %d%%\nUSB %s\n"
                 "in %lu  drop %lu\nBLE sent %lu failed %lu\n"
                 "%s\nreset %s  heap %lu",
                 dev_link::state() == dev_link::State::kConnected
                     ? dev_link::ip_address()
                     : "-",
                 vbus_millivolts.load(),
                 run == UsbRun::kInternal ? "on" : "off",
                 battery_volts.load(), battery_milliamps.load(),
                 battery.percent, first_line,
                 static_cast<unsigned long>(usb_input_count.load()),
                 static_cast<unsigned long>(dropped_reports.load()),
                 static_cast<unsigned long>(ble_keyboard::SentCount()),
                 static_cast<unsigned long>(ble_keyboard::FailedCount()),
                 aux, reset_reason_name(esp_reset_reason()),
                 static_cast<unsigned long>(
                     heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
      }
      ui::Draw(model, status, diagnostics, now);
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

}  // namespace

// Liveness counters for GET /status.
int write_status(char* out, int size) {
  usb_host_lib_info_t usb_info{};
  const bool host_installed = usb_host_lib_info(&usb_info) == ESP_OK;
  // Read-only controller snapshot for this ESP32-S3 diagnostic build.
  const uint32_t port = host_installed ? USB_DWC.hprt_reg.val : 0;
  return snprintf(out, size,
                  "usb %lu\nrouter %lu\nble sent %lu failed %lu\nui %lu\n"
                  "dropped %lu\nsecured %d\ninternal free %lu largest dma %lu\n"
                  "reset %s\nbattery %.2fV %.0fmA\n"
                  "vbus_mV %d pmic_status %d\notg %d boost %d aw_outputs %d\n"
                  "charge_diag_mA 200 charge_reg62 %d\n"
                  "usb_devices %d clients %d hprt %08lx\n",
                  static_cast<unsigned long>(usb_input_count.load()),
                  static_cast<unsigned long>(router_loop_count.load()),
                  static_cast<unsigned long>(ble_keyboard::SentCount()),
                  static_cast<unsigned long>(ble_keyboard::FailedCount()),
                  static_cast<unsigned long>(ui_loop_count.load()),
                  static_cast<unsigned long>(dropped_reports.load()),
                  ble_secured.load() ? 1 : 0,
                  static_cast<unsigned long>(
                      heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned long>(heap_caps_get_largest_free_block(
                      MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)),
                  reset_reason_name(esp_reset_reason()),
                  battery_volts.load(), battery_milliamps.load(),
                  vbus_millivolts.load(), pmic_status.load(),
                  aw_output_registers.load() < 0 ? -1 :
                      !!(aw_output_registers.load() & 0x20),
                  aw_output_registers.load() < 0 ? -1 :
                      !!(aw_output_registers.load() & 0x8000),
                  aw_output_registers.load(), charge_current_register.load(),
                  host_installed ? usb_info.num_devices : -1,
                  host_installed ? usb_info.num_clients : -1,
                  static_cast<unsigned long>(port));
}

extern "C" void app_main() {
  const esp_err_t nvs_status = nvs_flash_init();
  if (nvs_status != ESP_OK) {
    ESP_LOGE(kTag, "NVS init failed: %s", esp_err_to_name(nvs_status));
    return;
  }

  usb_interfaces = static_cast<UsbInterface*>(heap_caps_calloc(
      kMaxInterfaces, sizeof(UsbInterface), MALLOC_CAP_SPIRAM));
  ui_messages = xQueueCreate(32, sizeof(UiMessage));
  device_events = xQueueCreate(8, sizeof(DeviceEvent));
  router_events = xQueueCreate(64, sizeof(RouterEvent));
  if (usb_interfaces == nullptr || ui_messages == nullptr ||
      device_events == nullptr ||
      router_events == nullptr) {
    ESP_LOGE(kTag, "Queue allocation failed");
    return;
  }

  // Logging is synchronous and runs on the input path: NimBLE logs every
  // notification, and esp_hid logs every failed send (retried from the
  // outbox and counted in /status instead).
  esp_log_level_set("NimBLE", ESP_LOG_WARN);
  esp_log_level_set("NIMBLE_HIDD", ESP_LOG_NONE);

  initial_slot = load_active_slot();
  M5StackChan.begin();
  dev_link::SetStatusWriter(write_status);
  dev_link::SetScreenReader(ui::Pixels);
  dev_link::SetTapHandler([](int x, int y) {
    if (x >= 0 && x < ui::kWidth && y >= 0 && y < ui::kHeight) {
      remote_tap = (x << 16) | y;
    }
  });
  dev_link::Start();
  xTaskCreatePinnedToCore(router_task, "router", 4096, nullptr, 8, nullptr, 1);
  xTaskCreatePinnedToCore(ui_task, "ui", 8192, nullptr, 2, nullptr, 0);
  if (!ble_keyboard::Start(kDeviceName, initial_slot, on_ble_link)) {
    post_ui(UiKind::kLink, "BLE init failed");
  }
}
