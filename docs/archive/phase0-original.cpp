#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <M5Unified.h>
#include <M5StackChan.h>
#include <Wire.h>
#include <Avatar.h>
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "usb/hid_host.h"
#include "usb/usb_host.h"

namespace {

constexpr char kTag[] = "phase0";
constexpr uint8_t kBootKeyboardProtocol = 1;
QueueHandle_t device_events;
QueueHandle_t keyboard_reports;
std::atomic<uint32_t> dropped_reports{0};
std::atomic<bool> nimble_synced{false};
m5avatar::Avatar avatar;

struct DeviceEvent {
  hid_host_device_handle_t handle;
  bool connected;
};

struct KeyboardReport {
  uint8_t bytes[8];
};

void show_status(const char* message) {
  if (avatar.isDrawing()) {
    avatar.stop();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  M5.Display.fillScreen(BLACK);
  M5.Display.setBrightness(128);
  M5.Display.setTextColor(WHITE, BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(4, 4);
  M5.Display.printf("Voyager Phase 0\n%s", message);
  ESP_LOGI(kTag, "%s", message);
}

void on_hid_device_event(hid_host_device_handle_t handle,
                         hid_host_driver_event_t event, void*) {
  if (event != HID_HOST_DRIVER_EVENT_CONNECTED) {
    return;
  }
  const DeviceEvent item{handle, true};
  if (xQueueSend(device_events, &item, 0) != pdTRUE) {
    ESP_LOGE(kTag, "HID event queue full");
  }
}

void on_hid_interface_event(hid_host_device_handle_t handle,
                            hid_host_interface_event_t event, void*) {
  if (event == HID_HOST_INTERFACE_EVENT_DISCONNECTED) {
    const DeviceEvent item{handle, false};
    xQueueSend(device_events, &item, 0);
    return;
  }
  if (event != HID_HOST_INTERFACE_EVENT_INPUT_REPORT) {
    return;
  }

  uint8_t report[64]{};
  size_t report_size = 0;
  if (hid_host_device_get_raw_input_report_data(handle, report, sizeof(report),
                                                &report_size) != ESP_OK ||
      report_size < 8) {
    return;
  }

  KeyboardReport input{};
  memcpy(input.bytes, report, sizeof(input.bytes));
  if (xQueueSend(keyboard_reports, &input, 0) != pdTRUE) {
    ++dropped_reports;
  }
}

void handle_device_event(const DeviceEvent& item) {
  if (!item.connected) {
    xQueueReset(keyboard_reports);
    show_status("USB disconnected");
    hid_host_device_close(item.handle);
    return;
  }

  hid_host_dev_params_t params{};
  if (hid_host_device_get_params(item.handle, &params) != ESP_OK) {
    show_status("HID params error");
    return;
  }

  ESP_LOGI(kTag, "HID interface=%u protocol=%u subclass=%u",
           params.iface_num, params.proto, params.sub_class);
  if (params.proto != kBootKeyboardProtocol || params.sub_class != 1) {
    return;
  }

  const hid_host_device_config_t device_config = {
      .callback = on_hid_interface_event,
      .callback_arg = nullptr,
  };
  if (hid_host_device_open(item.handle, &device_config) != ESP_OK) {
    show_status("HID open failed");
    return;
  }

  hid_host_dev_info_t info{};
  if (hid_host_get_device_info(item.handle, &info) != ESP_OK) {
    show_status("HID info failed");
    hid_host_device_close(item.handle);
    return;
  }

  esp_err_t status = hid_class_request_set_protocol(item.handle,
                                                     HID_REPORT_PROTOCOL_BOOT);
  if (status == ESP_OK) {
    status = hid_host_device_start(item.handle);
  }
  if (status != ESP_OK) {
    show_status("Boot keyboard failed");
    hid_host_device_close(item.handle);
    return;
  }

  char message[80];
  snprintf(message, sizeof(message), "VID %04X PID %04X\nInterface %u Boot ready",
           info.VID, info.PID, params.iface_num);
  show_status(message);
}

void usb_library_task(void*) {
  while (true) {
    uint32_t flags = 0;
    const esp_err_t status = usb_host_lib_handle_events(portMAX_DELAY, &flags);
    if (status != ESP_OK) {
      ESP_LOGE(kTag, "USB host event: %s", esp_err_to_name(status));
    }
  }
}

void hid_event_task(void*) {
  DeviceEvent event{};
  while (xQueueReceive(device_events, &event, portMAX_DELAY) == pdTRUE) {
    handle_device_event(event);
  }
}

void display_task(void*) {
  KeyboardReport report{};
  while (xQueueReceive(keyboard_reports, &report, portMAX_DELAY) == pdTRUE) {
    const auto* bytes = report.bytes;
    ESP_LOGI(kTag, "keyboard mod=%02x keys=%02x %02x %02x %02x %02x %02x",
             bytes[0], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6],
             bytes[7]);
    M5.Display.fillRect(0, 100, M5.Display.width(), 70, BLACK);
    M5.Display.setCursor(4, 100);
    M5.Display.printf("mod %02X\nkey %02X %02X %02X %02X %02X %02X\ndrop %lu",
                      bytes[0], bytes[2], bytes[3], bytes[4], bytes[5],
                      bytes[6], bytes[7],
                      static_cast<unsigned long>(dropped_reports.load()));
  }
}

void on_nimble_sync() {
  nimble_synced = true;
  ESP_LOGI(kTag, "NimBLE host synchronized");
}

void nimble_host_task(void*) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

}  // namespace

extern "C" void app_main() {
  // The BSP owns M5 initialization and the K151 servo/expander wiring.
  // Initialize NVS first because its servo calibration is stored there.
  if (nvs_flash_init() != ESP_OK) {
    M5.begin();
    show_status("NVS init failed");
    return;
  }
  M5.begin();
  ESP_LOGI(kTag, "Display %dx%d brightness=%u", M5.Display.width(),
           M5.Display.height(), M5.Display.getBrightness());
  ESP_LOGI(kTag, "I2C port=%d SDA=%d SCL=%d initialized=%d",
           M5.In_I2C.getPort(), M5.In_I2C.getSDA(), M5.In_I2C.getSCL(),
           m5gfx::i2c::isInitialized(M5.In_I2C.getPort()));
  ESP_LOGI(kTag, "Wire1 initialized=%d SDA level=%d SCL level=%d",
           i2cIsInit(1), gpio_get_level(GPIO_NUM_12), gpio_get_level(GPIO_NUM_11));
  if (!i2cIsInit(1)) {
    ESP_LOGI(kTag, "Wire1 direct begin=%d", Wire1.begin(12, 11, 100000));
  }
  ESP_LOGI(kTag, "I2C addresses AW=%d AXP=%d BASE=%d",
           M5.In_I2C.scanID(0x58, 100000), M5.In_I2C.scanID(0x34, 100000),
           M5.In_I2C.scanID(0x6F, 100000));
  ESP_LOGI(kTag, "I2C reinit=%d", M5.In_I2C.begin());
  ESP_LOGI(kTag, "I2C addresses after reinit AW=%d AXP=%d BASE=%d",
           M5.In_I2C.scanID(0x58, 100000), M5.In_I2C.scanID(0x34, 100000),
           M5.In_I2C.scanID(0x6F, 100000));
  M5StackChan.begin();
  M5StackChan.showRgbColor(0, 0, 32);
  show_status("Starting...");
  M5.Power.setUsbOutput(true);
  ESP_LOGI(kTag, "VBUS output=%d", M5.Power.getUsbOutput());
  uint8_t power_regs[2]{};
  const bool power_io_ready = M5.In_I2C.readRegister(0x58, 0x02, power_regs,
                                                      sizeof(power_regs), 400000);
  ESP_LOGI(kTag, "AW9523 read=%d port0=%02x port1=%02x", power_io_ready,
           power_regs[0], power_regs[1]);
  if (!M5.Power.getUsbOutput()) {
    M5StackChan.showRgbColor(32, 0, 0);
    show_status("VBUS enable failed");
    return;
  }

  // initArduino() erases an incompatible NVS partition and can release BLE memory.
  // Phase 0 preserves the factory NVS and initializes only what this probe needs.
  if (nimble_port_init() != ESP_OK) {
    show_status("NimBLE init failed");
    return;
  }
  ble_hs_cfg.sync_cb = on_nimble_sync;
  nimble_port_freertos_init(nimble_host_task);
  for (int i = 0; i < 200 && !nimble_synced; ++i) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (!nimble_synced) {
    show_status("NimBLE sync timeout");
    return;
  }
  show_status("NimBLE ready");

  device_events = xQueueCreate(8, sizeof(DeviceEvent));
  ESP_ERROR_CHECK(device_events ? ESP_OK : ESP_ERR_NO_MEM);
  keyboard_reports = xQueueCreate(32, sizeof(KeyboardReport));
  ESP_ERROR_CHECK(keyboard_reports ? ESP_OK : ESP_ERR_NO_MEM);

  usb_host_config_t usb_config{};
  usb_config.skip_phy_setup = false;
  usb_config.intr_flags = ESP_INTR_FLAG_LEVEL1;
  if (usb_host_install(&usb_config) != ESP_OK) {
    show_status("USB Host PHY failed");
    return;
  }
  show_status("USB Host PHY ready");
  M5StackChan.showRgbColor(0, 32, 0);

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
    show_status("USB HID init failed");
    return;
  }
  xTaskCreatePinnedToCore(hid_event_task, "hid_events", 4096, nullptr, 9,
                          nullptr, 1);
  xTaskCreatePinnedToCore(display_task, "display", 4096, nullptr, 2, nullptr,
                          0);
  show_status("Waiting for Voyager");
  avatar.init();
}
