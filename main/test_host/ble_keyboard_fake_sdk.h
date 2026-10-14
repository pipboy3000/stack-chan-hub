#pragma once

// Deliberately only the SDK surface used by ble_keyboard.cpp. GAP operations
// return scripted results; queueing never synthesizes a DISCONNECT callback.
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <mutex>
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(p) (p)->lock()
#define portEXIT_CRITICAL(p) (p)->unlock()

using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int ESP_ERR_NVS_NOT_FOUND = 0x1102;
using nvs_handle_t = uint32_t;
constexpr int NVS_READWRITE = 1;
using esp_event_base_t = const char*;
using esp_event_handler_t = void (*)(void*, esp_event_base_t, int32_t, void*);
struct esp_hidd_dev_t {};
struct esp_hid_raw_report_map_t { const uint8_t* data; uint16_t len; };
struct esp_hid_device_config_t {
  uint16_t vendor_id, product_id, version;
  const char *device_name, *manufacturer_name, *serial_number;
  esp_hid_raw_report_map_t* report_maps;
  uint8_t report_maps_len;
};
constexpr int ESP_HID_APPEARANCE_KEYBOARD = 0x3c1;
constexpr int ESP_HID_TRANSPORT_BLE = 1;
enum esp_hidd_event_t { ESP_HIDD_START_EVENT, ESP_HIDD_OUTPUT_EVENT };
struct esp_hidd_event_data_t {
  struct { uint8_t report_id; uint16_t length; } output;
};
struct ble_addr_t { uint8_t type; uint8_t val[6]; };
constexpr uint16_t BLE_HS_CONN_HANDLE_NONE = 0xffff;
constexpr int BLE_HS_EALREADY = 2;
constexpr int BLE_HS_ENOTCONN = 7;
constexpr int BLE_HS_ESTORE_CAP = 27;
constexpr int BLE_HS_EAUTHEN = 23;
constexpr int BLE_ATT_ERR_INSUFFICIENT_RES = 0x11;
constexpr int BLE_STORE_EVENT_OVERFLOW = 1;
constexpr int BLE_STORE_OBJ_TYPE_CCCD = 3;
constexpr int BLE_STORE_OBJ_TYPE_OUR_SEC = 1, BLE_STORE_OBJ_TYPE_PEER_SEC = 2;
constexpr int BLE_ERR_REM_USER_CONN_TERM = 0x13;
constexpr int BLE_HS_ADV_F_DISC_GEN = 2, BLE_HS_ADV_F_BREDR_UNSUP = 4;
constexpr int BLE_GAP_CONN_MODE_UND = 2, BLE_GAP_DISC_MODE_GEN = 2;
constexpr int BLE_HCI_ADV_FILT_CONN = 2, BLE_HCI_ADV_FILT_NONE = 0;
constexpr int BLE_OWN_ADDR_PUBLIC = 0, BLE_HS_FOREVER = INT32_MAX;
constexpr int BLE_SM_IO_CAP_NO_IO = 3;
constexpr int BLE_SM_PAIR_KEY_DIST_ENC = 1, BLE_SM_PAIR_KEY_DIST_ID = 2;
constexpr int BLE_GAP_REPEAT_PAIRING_RETRY = 1, BLE_GAP_REPEAT_PAIRING_IGNORE = 2;
#define BLE_GAP_ADV_ITVL_MS(ms) ((ms) * 8 / 5)
struct ble_uuid16_t { uint8_t type; uint16_t value; };
#define BLE_UUID16_INIT(value) {16, (value)}
struct ble_hs_adv_fields {
  uint8_t flags;
  uint16_t appearance;
  uint8_t appearance_is_present;
  const uint8_t* name;
  uint8_t name_len, name_is_complete;
  ble_uuid16_t* uuids16;
  uint8_t num_uuids16, uuids16_is_complete;
};
struct ble_gap_adv_params {
  uint8_t conn_mode, disc_mode, filter_policy;
  uint16_t itvl_min, itvl_max;
};
struct ble_gap_conn_desc {
  ble_addr_t peer_id_addr, peer_ota_addr;
  struct { uint8_t encrypted, bonded; } sec_state;
};
enum { BLE_GAP_EVENT_CONNECT, BLE_GAP_EVENT_DISCONNECT,
       BLE_GAP_EVENT_ENC_CHANGE, BLE_GAP_EVENT_REPEAT_PAIRING,
       BLE_GAP_EVENT_ADV_COMPLETE };
struct ble_gap_event {
  uint8_t type;
  struct { int status; uint16_t conn_handle; } connect;
  struct { int reason; } disconnect;
  struct { int status; uint16_t conn_handle; } enc_change;
  struct { uint16_t conn_handle; } repeat_pairing;
};
using ble_gap_event_fn = int(ble_gap_event*, void*);
struct ble_store_key_sec { ble_addr_t peer_addr; };
struct ble_store_value_sec { uint8_t ltk_present, irk_present; ble_addr_t peer_addr; };
union ble_store_value { ble_store_value_sec sec; };
using ble_store_write_fn = int(int, const ble_store_value*);
struct ble_store_status_event {
  int event_code;
  struct { int obj_type; } overflow;
};
struct ble_hs_cfg_t {
  uint8_t sm_io_cap, sm_bonding, sm_mitm, sm_sc, sm_our_key_dist, sm_their_key_dist;
  int (*store_status_cb)(ble_store_status_event*, void*);
  ble_store_write_fn* store_write_cb;
};
inline ble_hs_cfg_t ble_hs_cfg{};
struct ble_npl_event { void (*callback)(ble_npl_event*); };
struct ble_npl_eventq {};
struct ble_npl_callout { ble_npl_event event; bool armed; uint32_t deadline; };

namespace fake_sdk {
inline int terminate_rc, adv_stop_rc, adv_start_rc;
inline int delete_rc, nvs_set_rc, nvs_commit_rc, bond_read_rc, store_calls;
inline uint32_t now;
inline ble_addr_t deleted_peer{};
inline int terminate_calls, adv_stop_calls, whitelist_calls, adv_fields_calls;
inline int adv_start_calls, hid_calls, error_logs, security_calls, delete_calls;
inline uint16_t terminated_handle;
inline uint8_t terminated_reason;
inline ble_gap_conn_desc connection{};
inline ble_addr_t whitelist{};
inline uint8_t whitelist_count;
inline esp_hidd_dev_t device;
inline void log(bool error, const char*, const char*, ...) {
  if (error) ++error_logs;
}
inline void reset() {
  terminate_rc = adv_stop_rc = adv_start_rc = 0;
  delete_rc = nvs_set_rc = nvs_commit_rc = bond_read_rc = store_calls = 0;
  now = 0;
  terminate_calls = adv_stop_calls = whitelist_calls = adv_fields_calls = 0;
  adv_start_calls = hid_calls = error_logs = security_calls = delete_calls = 0;
  connection = {};
  whitelist = {};
  whitelist_count = 0;
}
}  // namespace fake_sdk
#define ESP_LOGI(...) fake_sdk::log(false, __VA_ARGS__)
#define ESP_LOGW(...) fake_sdk::log(false, __VA_ARGS__)
#define ESP_LOGE(...) fake_sdk::log(true, __VA_ARGS__)
inline const char* esp_err_to_name(int) { return "fake SDK error"; }
inline int nvs_open(const char*, int, nvs_handle_t* h) { *h = 1; return 0; }
inline int nvs_get_blob(nvs_handle_t, const char*, void*, size_t*) { return ESP_ERR_NVS_NOT_FOUND; }
inline int nvs_set_blob(nvs_handle_t, const char*, const void*, size_t) { return fake_sdk::nvs_set_rc; }
inline int nvs_commit(nvs_handle_t) { return fake_sdk::nvs_commit_rc; }
inline int ble_gap_adv_stop() { ++fake_sdk::adv_stop_calls; return fake_sdk::adv_stop_rc; }
inline int ble_gap_wl_set(const ble_addr_t* addresses, uint8_t count) {
  ++fake_sdk::whitelist_calls;
  fake_sdk::whitelist_count = count;
  if (count) fake_sdk::whitelist = *addresses;
  return 0;
}
inline int ble_gap_adv_set_fields(const ble_hs_adv_fields*) {
  ++fake_sdk::adv_fields_calls; return 0;
}
inline int ble_gap_adv_start(uint8_t, const ble_addr_t*, int32_t,
                             const ble_gap_adv_params*, ble_gap_event_fn*, void*) {
  ++fake_sdk::adv_start_calls; return fake_sdk::adv_start_rc;
}
inline int ble_gap_terminate(uint16_t handle, uint8_t reason) {
  ++fake_sdk::terminate_calls;
  fake_sdk::terminated_handle = handle;
  fake_sdk::terminated_reason = reason;
  return fake_sdk::terminate_rc;
}
inline int ble_gap_conn_find(uint16_t, ble_gap_conn_desc* desc) {
  *desc = fake_sdk::connection; return 0;
}
inline int ble_gap_security_initiate(uint16_t) { ++fake_sdk::security_calls; return 0; }
inline int ble_store_util_bonded_peers(ble_addr_t*, int* count, int) { *count = 0; return 0; }
inline int ble_store_util_delete_peer(const ble_addr_t* address) {
  ++fake_sdk::delete_calls; fake_sdk::deleted_peer = *address; return fake_sdk::delete_rc;
}
inline int ble_store_read_our_sec(const ble_store_key_sec*, ble_store_value_sec* value) {
  value->ltk_present = 1; return fake_sdk::bond_read_rc;
}
inline int ble_store_read_peer_sec(const ble_store_key_sec*, ble_store_value_sec* value) {
  value->irk_present = 1; return 0;
}
inline int ble_svc_gap_device_name_set(const char*) { return 0; }
inline void ble_svc_gap_device_appearance_set(uint16_t) {}
inline void ble_npl_event_init(ble_npl_event* event, void (*callback)(ble_npl_event*), void*) {
  event->callback = callback;
}
inline ble_npl_eventq* nimble_port_get_dflt_eventq() { static ble_npl_eventq queue; return &queue; }
inline void ble_npl_eventq_put(ble_npl_eventq*, ble_npl_event*) {}
inline uint32_t ble_npl_time_get() { return fake_sdk::now; }
inline uint32_t ble_npl_time_ms_to_ticks32(uint32_t ms) { return ms; }
inline uint32_t ble_npl_time_ticks_to_ms32(uint32_t ticks) { return ticks; }
inline int ble_npl_callout_init(ble_npl_callout* callout, ble_npl_eventq*,
                                 void (*callback)(ble_npl_event*), void*) {
  callout->event.callback = callback; callout->armed = false; return 0;
}
inline int ble_npl_callout_reset(ble_npl_callout* callout, uint32_t ticks) {
  callout->armed = true; callout->deadline = fake_sdk::now + ticks; return 0;
}
inline void ble_npl_callout_stop(ble_npl_callout* callout) { callout->armed = false; }
inline int nimble_port_init() { return 0; }
inline int sleep_stop_rc = 0, sleep_deinit_rc = 0;
inline int sleep_stop_calls = 0, sleep_deinit_calls = 0;
inline void (*sleep_stop_hook)() = nullptr;
inline int nimble_port_stop() {
  ++sleep_stop_calls;
  if (sleep_stop_rc == 0 && sleep_stop_hook) sleep_stop_hook();
  return sleep_stop_rc;
}
inline int nimble_port_deinit() { ++sleep_deinit_calls; return sleep_deinit_rc; }
inline void nimble_port_run() {}
inline void nimble_port_freertos_deinit() {}
inline void nimble_port_freertos_init(void (*)(void*)) {}
extern "C" inline void ble_store_config_init() {}
inline int esp_hidd_dev_init(const esp_hid_device_config_t*, int,
                             esp_event_handler_t, esp_hidd_dev_t** device) {
  *device = &fake_sdk::device; return 0;
}
inline int esp_hidd_dev_input_set(esp_hidd_dev_t*, size_t, size_t, uint8_t*, size_t) {
  ++fake_sdk::hid_calls; return 0;
}
