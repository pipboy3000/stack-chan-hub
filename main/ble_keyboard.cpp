#include "ble_keyboard.h"
#include "peer_slots.h"

#include <atomic>
#include <cstring>

#include "esp_hidd.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nvs.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "store/config/ble_store_config.h"

extern "C" void ble_store_config_init(void);

namespace ble_keyboard {
namespace {

constexpr char kTag[] = "ble_keyboard";
constexpr uint8_t kKeyboardReportId = 1;
constexpr uint8_t kConsumerReportId = 2;
constexpr uint8_t kSystemReportId = 3;
constexpr uint8_t kMouseReportId = 4;
constexpr uint16_t kHidServiceUuid = 0x1812;

// Boot-compatible keyboard: 8-bit modifiers, reserved byte, 6 key array
// (full 0x00-0xFF usage range), and the 5 LED output bits for Phase 4.
// Then Consumer Control and System Control (one 16-bit usage each) and a
// 5-button mouse with X, Y, wheel and AC Pan.
constexpr uint8_t kReportMap[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, kKeyboardReportId,  // Report ID
    0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,        //   Usage Minimum (Left Control)
    0x29, 0xE7,        //   Usage Maximum (Right GUI)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Var, Abs)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Const)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x05,        //   Usage Maximum (Kana)
    0x91, 0x02,        //   Output (Data, Var, Abs)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x01,        //   Output (Const)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x00,  //   Usage Maximum (255)
    0x81, 0x00,        //   Input (Data, Array, Abs)
    0xC0,              // End Collection

    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, kConsumerReportId,  // Report ID
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (0x3FF)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,  //   Usage Maximum (0x3FF)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data, Array, Abs)
    0xC0,              // End Collection

    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x80,        // Usage (System Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, kSystemReportId,  // Report ID
    0x16, 0x81, 0x00,  //   Logical Minimum (0x81)
    0x26, 0x8F, 0x00,  //   Logical Maximum (0x8F)
    0x1A, 0x81, 0x00,  //   Usage Minimum (System Power Down)
    0x2A, 0x8F, 0x00,  //   Usage Maximum (0x8F)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data, Array, Abs)
    0xC0,              // End Collection

    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, kMouseReportId,  // Report ID
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (1)
    0x29, 0x05,        //     Usage Maximum (5)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x05,        //     Report Count (5)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data, Var, Abs)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x03,        //     Report Size (3)
    0x81, 0x01,        //     Input (Const)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x06,        //     Input (Data, Var, Rel)
    0x05, 0x0C,        //     Usage Page (Consumer)
    0x0A, 0x38, 0x02,  //     Usage (AC Pan)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x06,        //     Input (Data, Var, Rel)
    0xC0,              //   End Collection
    0xC0,              // End Collection
};

esp_hid_raw_report_map_t report_maps[] = {
    {.data = kReportMap, .len = sizeof(kReportMap)},
};

esp_hid_device_config_t hid_config = {
    .vendor_id = 0x16C0,
    .product_id = 0x05DF,
    .version = 0x0100,
    .device_name = nullptr,
    .manufacturer_name = "StackChan",
    .serial_number = "0001",
    .report_maps = report_maps,
    .report_maps_len = 1,
};

esp_hidd_dev_t* hid_device = nullptr;
LinkCallback link_callback = nullptr;
std::atomic<bool> secured{false};
std::atomic<uint16_t> conn_handle{BLE_HS_CONN_HANDLE_NONE};
std::atomic<bool> disconnect_pending{false};

// Plan B: one public identity, independent bonds for three selected peers.
std::atomic<uint8_t> active_slot{0};
std::atomic<uint8_t> link_slot{0};
std::atomic<bool> host_ready{false};
uint8_t advertised_slot = 0;
const char* name_prefix = "StackChan Hub";
PeerSlots peer_slots;
nvs_handle_t slots_nvs = 0;
bool migrate_legacy_bonds = false;
ble_npl_event select_event{};
ble_npl_event management_event{};
ble_npl_callout pairing_timer{};
constexpr uint8_t kNoSlot = 0xff;
bool pairing_active = false;
uint8_t pairing_slot = kNoSlot;
uint32_t pairing_deadline = 0;
bool pairing_link = false;
uint8_t unbind_slot = kNoSlot;
enum class RequestKind : uint8_t { kPair, kStop, kUnbind };
struct Request { RequestKind kind; uint8_t slot; uint16_t seconds; };
constexpr size_t kRequestCapacity = 8;
Request requests[kRequestCapacity]{};
size_t request_head = 0, request_count = 0;
portMUX_TYPE management_lock = portMUX_INITIALIZER_UNLOCKED;
SlotSnapshot snapshots[PeerSlots::kCount]{};
uint32_t snapshot_deadlines[PeerSlots::kCount]{};
ManagementError slot_errors[PeerSlots::kCount]{};

bool pairing_allowed(uint8_t slot) {
  return pairing_active && pairing_slot == slot && slot == active_slot &&
      static_cast<int32_t>(pairing_deadline - ble_npl_time_get()) > 0;
}

void publish_slots() {
  portENTER_CRITICAL(&management_lock);
  for (uint8_t slot = 0; slot < PeerSlots::kCount; ++slot) {
    SlotSnapshot info{};
    info.valid = true;
    if (const auto* peer = peer_slots.Get(slot)) {
      info.registered = true;
      info.address_type = peer->address.type;
      memcpy(info.address, peer->address.bytes.data(), sizeof(info.address));
    }
    info.pairing = pairing_allowed(slot);
    info.unbinding = unbind_slot == slot;
    info.last_error = slot_errors[slot];
    snapshots[slot] = info;
    snapshot_deadlines[slot] = pairing_deadline;
  }
  portEXIT_CRITICAL(&management_lock);
}

void notify(LinkEvent event, uint8_t slot) {
  publish_slots();
  if (link_callback != nullptr) {
    link_callback(event, slot);
  }
}

PeerAddress peer_address(const ble_addr_t& address) {
  PeerAddress peer;
  peer.type = address.type & 1;  // PUBLIC_ID/RANDOM_ID -> identity type
  memcpy(peer.bytes.data(), address.val, peer.bytes.size());
  return peer;
}

ble_addr_t ble_address(const PeerAddress& peer) {
  ble_addr_t address{};
  address.type = peer.type;
  memcpy(address.val, peer.bytes.data(), peer.bytes.size());
  return address;
}

void log_peer(const char* label, const ble_addr_t& address) {
  ESP_LOGI(kTag, "%s type=%u %02X:%02X:%02X:%02X:%02X:%02X", label,
           address.type, address.val[5], address.val[4], address.val[3],
           address.val[2], address.val[1], address.val[0]);
}

bool persist_slots(const PeerSlots& slots) {
  const auto& data = slots.DataForStorage();
  esp_err_t rc = nvs_set_blob(slots_nvs, "peers", data.data(), sizeof(data));
  if (rc == ESP_OK) rc = nvs_commit(slots_nvs);
  if (rc != ESP_OK) ESP_LOGE(kTag, "save peers: %s", esp_err_to_name(rc));
  return rc == ESP_OK;
}

bool load_slots() {
  esp_err_t rc = nvs_open("ble_slots_b1", NVS_READWRITE, &slots_nvs);
  if (rc != ESP_OK) return false;
  PeerSlots::Data data{};
  size_t size = sizeof(data);
  rc = nvs_get_blob(slots_nvs, "peers", data.data(), &size);
  if (rc == ESP_ERR_NVS_NOT_FOUND) {
    migrate_legacy_bonds = true;
    return true;
  }
  if (rc != ESP_OK || size != sizeof(data) || !peer_slots.Restore(data)) {
    ESP_LOGE(kTag, "invalid slot storage: %s", esp_err_to_name(rc));
    return false;
  }
  return true;
}

bool migrate_bonds() {
  if (!migrate_legacy_bonds) return true;
  ble_addr_t peers[PeerSlots::kCount]{};
  int count = 0;
  int rc = ble_store_util_bonded_peers(peers, &count, PeerSlots::kCount);
  if (rc != 0) return false;
  for (int i = 0; i < count; ++i) {
    rc = ble_store_util_delete_peer(&peers[i]);
    if (rc != 0) {
      ESP_LOGE(kTag, "legacy bond removal rc=%d", rc);
      return false;
    }
  }
  if (!persist_slots(peer_slots)) return false;
  migrate_legacy_bonds = false;
  ESP_LOGI(kTag, "plan B migration: removed %d legacy bonds", count);
  return true;
}

// Bonds and subscriptions are never evicted for another host. A full store
// fails the peer's request, so make it visible instead of failing silently.
int on_store_status(ble_store_status_event* event, void*) {
  if (event->event_code == BLE_STORE_EVENT_OVERFLOW) {
    ESP_LOGE(kTag, "store full: object type %d rejected",
             event->overflow.obj_type);
    if (event->overflow.obj_type == BLE_STORE_OBJ_TYPE_CCCD) {
      // This IDF version passes CCCD persistence failures straight to ATT.
      // BLE_HS_ESTORE_CAP (27) is not a defined ATT error; report resources.
      return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
  }
  return BLE_HS_ESTORE_CAP;
}

int on_gap_event(ble_gap_event* event, void*);
void close_pairing(LinkEvent reason);
void finish_unbind();

// All GAP changes run on the NimBLE host queue, including UI/router requests.
void start_advertising() {
  if (!host_ready || conn_handle != BLE_HS_CONN_HANDLE_NONE) return;
  int rc = ble_gap_adv_stop();
  if (rc != 0 && rc != BLE_HS_EALREADY) {
    ESP_LOGE(kTag, "adv stop rc=%d", rc);
    if (pairing_active) {
      slot_errors[pairing_slot] = ManagementError::kAdvertising;
      close_pairing(LinkEvent::kPairingFailed);
    }
    return;
  }
  if (!migrate_bonds()) {
    ESP_LOGE(kTag, "bond migration failed; advertising stopped");
    if (pairing_active) {
      slot_errors[pairing_slot] = ManagementError::kStorage;
      close_pairing(LinkEvent::kPairingFailed);
    }
    return;
  }
  const uint8_t slot = active_slot;
  const auto* peer = peer_slots.Get(slot);
  if (unbind_slot == slot || (peer == nullptr && !pairing_allowed(slot))) return;
  ble_addr_t accept{};
  if (peer != nullptr) {
    accept = ble_address(peer->address);
    log_peer("selected peer", accept);
    ble_store_key_sec key{};
    key.peer_addr = accept;
    ble_store_value_sec our{}, their{};
    const int our_rc = ble_store_read_our_sec(&key, &our);
    const int their_rc = ble_store_read_peer_sec(&key, &their);
    // Log only presence flags, never key material.
    ESP_LOGI(kTag, "slot %u bond our_rc=%d ltk=%u peer_rc=%d irk=%u",
             slot + 1, our_rc, our_rc == 0 && our.ltk_present,
             their_rc, their_rc == 0 && their.irk_present);
  }
  rc = ble_gap_wl_set(peer != nullptr ? &accept : nullptr,
                     peer != nullptr ? 1 : 0);
  if (rc != 0) {
    ESP_LOGE(kTag, "accept list rc=%d", rc);
    if (pairing_active) {
      slot_errors[pairing_slot] = ManagementError::kAdvertising;
      close_pairing(LinkEvent::kPairingFailed);
    }
    return;  // Never fall back to allowing inactive hosts.
  }
  ble_svc_gap_device_name_set(name_prefix);
  const char* name = name_prefix;

  ble_hs_adv_fields fields{};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.appearance = ESP_HID_APPEARANCE_KEYBOARD;
  fields.appearance_is_present = 1;
  fields.name = reinterpret_cast<const uint8_t*>(name);
  fields.name_len = strlen(name);
  fields.name_is_complete = 1;
  ble_uuid16_t hid_uuid = BLE_UUID16_INIT(kHidServiceUuid);
  fields.uuids16 = &hid_uuid;
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;
  rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(kTag, "adv fields rc=%d", rc);
    if (pairing_active) {
      slot_errors[pairing_slot] = ManagementError::kAdvertising;
      close_pairing(LinkEvent::kPairingFailed);
    }
    return;
  }

  ble_gap_adv_params params{};
  params.conn_mode = BLE_GAP_CONN_MODE_UND;
  params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  params.filter_policy = peer != nullptr ? BLE_HCI_ADV_FILT_CONN
                                        : BLE_HCI_ADV_FILT_NONE;
  params.itvl_min = BLE_GAP_ADV_ITVL_MS(30);
  params.itvl_max = BLE_GAP_ADV_ITVL_MS(50);
  rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, &params,
                         on_gap_event, nullptr);
  if (rc != 0) {
    ESP_LOGE(kTag, "adv start rc=%d", rc);
    if (pairing_allowed(slot)) {
      slot_errors[slot] = ManagementError::kAdvertising;
      close_pairing(LinkEvent::kPairingFailed);
    }
    return;
  }
  advertised_slot = slot;
  ESP_LOGI(kTag, "advertising slot %u as %s (%s)", slot + 1, name,
           peer != nullptr ? "selected peer only" : "pairing");
  notify(LinkEvent::kAdvertising, slot);
}

void complete_disconnect() {
  const uint8_t slot = link_slot;
  secured = false;
  conn_handle = BLE_HS_CONN_HANDLE_NONE;
  disconnect_pending = false;
  pairing_link = false;
  notify(LinkEvent::kDisconnected, slot);
  if (unbind_slot != kNoSlot) finish_unbind();
  start_advertising();
}

bool request_disconnect(uint16_t handle) {
  if (disconnect_pending) return true;
  secured = false;
  disconnect_pending = true;
  const int rc = ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
  // NimBLE's EALREADY means this same link is already being terminated.
  if (rc == 0 || rc == BLE_HS_EALREADY) return true;
  disconnect_pending = false;
  ESP_LOGE(kTag, "terminate handle=%u rc=%d", handle, rc);
  if (rc == BLE_HS_ENOTCONN && handle == conn_handle) {
    complete_disconnect();
    return true;
  } else if (unbind_slot != kNoSlot) {
    const uint8_t slot = unbind_slot;
    unbind_slot = kNoSlot;
    slot_errors[slot] = ManagementError::kDisconnect;
    notify(LinkEvent::kUnbindFailed, slot);
  }
  return false;
}

void close_pairing(LinkEvent reason) {
  if (!pairing_active) return;
  const uint8_t slot = pairing_slot;
  pairing_active = false;
  pairing_slot = kNoSlot;
  ble_npl_callout_stop(&pairing_timer);
  if (reason != LinkEvent::kPairingComplete && pairing_link &&
      conn_handle != BLE_HS_CONN_HANDLE_NONE) {
    ble_gap_conn_desc desc{};
    if (ble_gap_conn_find(conn_handle, &desc) == 0 &&
        peer_slots.Allows(slot, peer_address(desc.peer_id_addr)) &&
        ble_store_util_delete_peer(&desc.peer_id_addr) != 0) {
      slot_errors[slot] = ManagementError::kStorage;
      reason = LinkEvent::kPairingFailed;
    }
  }
  if (pairing_link && conn_handle != BLE_HS_CONN_HANDLE_NONE &&
      !request_disconnect(conn_handle)) {
    slot_errors[slot] = ManagementError::kDisconnect;
    reason = LinkEvent::kPairingFailed;
  }
  notify(reason, slot);
}

void pairing_expired(ble_npl_event*) {
  if (pairing_active && !pairing_allowed(pairing_slot)) {
    close_pairing(pairing_slot != active_slot ? LinkEvent::kPairingStopped
                                             : LinkEvent::kPairingTimedOut);
    start_advertising();
  }
}

void finish_unbind() {
  const uint8_t slot = unbind_slot;
  if (slot >= PeerSlots::kCount) return;
  PeerSlots updated = peer_slots;
  bool ok = true;
  if (const auto* peer = peer_slots.Get(slot)) {
    const auto address = ble_address(peer->address);
    ok = ble_store_util_delete_peer(&address) == 0;
  }
  if (ok) ok = updated.Unbind(slot) && persist_slots(updated);
  if (ok) peer_slots = updated;
  unbind_slot = kNoSlot;
  slot_errors[slot] = ok ? ManagementError::kNone : ManagementError::kStorage;
  notify(ok ? LinkEvent::kUnbound : LinkEvent::kUnbindFailed, slot);
}

void apply_management(ble_npl_event*) {
  for (;;) {
    Request request{};
    portENTER_CRITICAL(&management_lock);
    const bool available = request_count != 0;
    if (available) {
      request = requests[request_head];
      request_head = (request_head + 1) % kRequestCapacity;
      --request_count;
    }
    portEXIT_CRITICAL(&management_lock);
    if (!available) return;
    const uint8_t slot = request.slot;
    if (request.kind == RequestKind::kStop) {
      close_pairing(LinkEvent::kPairingStopped);
      start_advertising();
    } else if (request.kind == RequestKind::kPair) {
      if (slot != active_slot || unbind_slot != kNoSlot || disconnect_pending) {
        slot_errors[slot] = slot != active_slot ? ManagementError::kNotSelected
                                              : ManagementError::kBusy;
        notify(LinkEvent::kPairingFailed, slot);
        continue;
      }
      close_pairing(LinkEvent::kPairingStopped);
      // Cancelling an incomplete older attempt can start a disconnect.
      if (disconnect_pending) {
        slot_errors[slot] = ManagementError::kBusy;
        notify(LinkEvent::kPairingFailed, slot);
        continue;
      }
      pairing_active = true;
      pairing_slot = slot;
      pairing_deadline = ble_npl_time_get() +
          ble_npl_time_ms_to_ticks32(request.seconds * 1000u);
      slot_errors[slot] = ManagementError::kNone;
      if (ble_npl_callout_reset(&pairing_timer,
          ble_npl_time_ms_to_ticks32(request.seconds * 1000u)) != 0) {
        slot_errors[slot] = ManagementError::kBusy;
        close_pairing(LinkEvent::kPairingFailed);
        continue;
      }
      start_advertising();
      if (pairing_active) notify(LinkEvent::kPairingStarted, slot);
    } else {
      if (unbind_slot != kNoSlot) {
        slot_errors[slot] = ManagementError::kBusy;
        notify(LinkEvent::kUnbindFailed, slot);
        continue;
      }
      unbind_slot = slot;
      slot_errors[slot] = ManagementError::kNone;
      if (pairing_active && pairing_slot == slot) {
        close_pairing(LinkEvent::kPairingStopped);
      }
      publish_slots();
      if (conn_handle != BLE_HS_CONN_HANDLE_NONE && link_slot == slot) {
        // Defence in depth; Router's normal release/barrier remains required.
        const uint8_t keys[6]{};
        SendKeyboard(0, keys);
        SendConsumer(0);
        SendSystem(0);
        SendMouse(0, 0, 0, 0, 0);
        request_disconnect(conn_handle);
      } else {
        finish_unbind();
        start_advertising();
      }
    }
  }
}

ble_store_write_fn* original_store_write = nullptr;
int gated_store_write(int type, const ble_store_value* value) {
  const bool security_keys = type == BLE_STORE_OBJ_TYPE_OUR_SEC ||
                             type == BLE_STORE_OBJ_TYPE_PEER_SEC;
  if (security_keys) {
    if (!pairing_allowed(active_slot) || disconnect_pending ||
        !peer_slots.Allows(active_slot, peer_address(value->sec.peer_addr))) {
      return BLE_HS_EAUTHEN;
    }
  }
  const int rc = original_store_write != nullptr ? original_store_write(type, value)
                                                : BLE_HS_ESTORE_CAP;
  // NimBLE's persist_keys does not propagate store failures into ENC_CHANGE.
  if (security_keys && rc != 0) slot_errors[active_slot] = ManagementError::kStorage;
  if (security_keys && rc == 0) pairing_link = true;
  return rc;
}

bool enqueue_request(Request request) {
  if (!host_ready) return false;
  portENTER_CRITICAL(&management_lock);
  const bool room = request_count < kRequestCapacity;
  if (room) {
    requests[(request_head + request_count) % kRequestCapacity] = request;
    ++request_count;
  }
  portEXIT_CRITICAL(&management_lock);
  if (room) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &management_event);
  return room;
}

bool stored_bond(const PeerAddress& peer) {
  ble_store_key_sec key{};
  key.peer_addr = ble_address(peer);
  ble_store_value_sec bond{};
  return ble_store_read_our_sec(&key, &bond) == 0 && bond.ltk_present;
}

bool admit_link(uint16_t handle, const ble_gap_conn_desc& desc) {
  if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    conn_handle = handle;
    link_slot = advertised_slot;
  }
  const uint8_t slot = link_slot;
  const auto* registered = peer_slots.Get(slot);
  if (handle != conn_handle || slot != active_slot ||
      unbind_slot == slot ||
      !peer_slots.Allows(slot, peer_address(desc.peer_id_addr)) ||
      (!pairing_allowed(slot) &&
       (registered == nullptr || !stored_bond(registered->address)))) {
    ESP_LOGW(kTag, "reject peer on slot %u", slot + 1);
    request_disconnect(handle);
    return false;
  }
  if (registered == nullptr ||
      (pairing_allowed(slot) && !stored_bond(registered->address))) {
    pairing_link = true;
  }
  return true;
}

void secure_link(uint16_t handle) {
  if (disconnect_pending) return;
  ble_gap_conn_desc desc{};
  if (ble_gap_conn_find(handle, &desc) != 0 || !admit_link(handle, desc) ||
      !desc.sec_state.encrypted || !desc.sec_state.bonded) return;
  const uint8_t slot = link_slot;
  if (pairing_link && (slot_errors[slot] == ManagementError::kStorage ||
                      !stored_bond(peer_address(desc.peer_id_addr)))) {
    slot_errors[slot] = ManagementError::kStorage;
    close_pairing(LinkEvent::kPairingFailed);
    request_disconnect(handle);
    return;
  }
  if (peer_slots.Get(slot) == nullptr) {
    PeerSlots updated = peer_slots;
    if (!updated.Bind(slot, peer_address(desc.peer_id_addr)) ||
        !persist_slots(updated)) {
      // A rejected late/orphan registration must not leave a new bond behind.
      slot_errors[slot] = ManagementError::kStorage;
      close_pairing(LinkEvent::kPairingFailed);
      request_disconnect(handle);
      return;
    }
    peer_slots = updated;
    const auto& address = desc.peer_id_addr;
    ESP_LOGI(kTag, "paired slot %u peer type=%u %02X:%02X:%02X:%02X:%02X:%02X",
             slot + 1, address.type, address.val[5], address.val[4],
             address.val[3], address.val[2], address.val[1], address.val[0]);
  }
  if (disconnect_pending || slot != active_slot) return;
  if (pairing_allowed(slot) && pairing_link) {
    pairing_link = false;
    close_pairing(LinkEvent::kPairingComplete);
  }
  if (!secured.exchange(true)) notify(LinkEvent::kSecured, slot);
}

int on_gap_event(ble_gap_event* event, void*) {
  ble_gap_conn_desc desc{};
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      ESP_LOGI(kTag, "connect status=%d", event->connect.status);
      if (event->connect.status != 0) {
        start_advertising();
        return 0;
      }
      if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
        log_peer("connected identity", desc.peer_id_addr);
        log_peer("connected air address", desc.peer_ota_addr);
        if (!admit_link(event->connect.conn_handle, desc) || disconnect_pending) {
          return 0;
        }
        // Encryption can finish before IDF's delayed CONNECT callback.
        secure_link(event->connect.conn_handle);
        // macOS's Bluetooth UI may only connect/discover services until the
        // peripheral requests security. Do this only for an admitted peer,
        // leaving an already-encrypted or in-progress procedure alone.
        if (!desc.sec_state.encrypted) {
          const int rc = ble_gap_security_initiate(event->connect.conn_handle);
          if (rc != 0 && rc != BLE_HS_EALREADY) {
            ESP_LOGW(kTag, "security request rc=%d", rc);
          }
        }
      }
      return 0;

    case BLE_GAP_EVENT_DISCONNECT: {
      ESP_LOGI(kTag, "disconnect reason=0x%x", event->disconnect.reason);
      complete_disconnect();
      return 0;
    }

    case BLE_GAP_EVENT_ENC_CHANGE:
      ESP_LOGI(kTag, "encryption status=%d", event->enc_change.status);
      if (event->enc_change.status == 0) {
        secure_link(event->enc_change.conn_handle);
      } else if (pairing_allowed(link_slot)) {
        slot_errors[link_slot] = ManagementError::kSecurity;
        close_pairing(LinkEvent::kPairingFailed);
      }
      return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
      // Replace only the admitted peer's bond; never another slot's keys.
      if (pairing_allowed(link_slot) && !disconnect_pending &&
          ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0 &&
          admit_link(event->repeat_pairing.conn_handle, desc)) {
        if (ble_store_util_delete_peer(&desc.peer_id_addr) != 0) {
          slot_errors[link_slot] = ManagementError::kStorage;
          close_pairing(LinkEvent::kPairingFailed);
          return BLE_GAP_REPEAT_PAIRING_IGNORE;
        }
        pairing_link = true;
        secured = false;
        return BLE_GAP_REPEAT_PAIRING_RETRY;
      }
      return BLE_GAP_REPEAT_PAIRING_IGNORE;

    case BLE_GAP_EVENT_ADV_COMPLETE:
      if (conn_handle == BLE_HS_CONN_HANDLE_NONE) start_advertising();
      return 0;
    default:
      return 0;
  }
}

void apply_selection(ble_npl_event*) {
  if (!host_ready) return;
  if (pairing_active && pairing_slot != active_slot) {
    close_pairing(LinkEvent::kPairingStopped);
  }
  if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
    if (disconnect_pending) return;
    if (link_slot != active_slot) {
      request_disconnect(conn_handle);
    } else {
      secure_link(conn_handle);
    }
    return;
  }
  start_advertising();
}

void on_hidd_event(void*, esp_event_base_t, int32_t id, void* data) {
  const auto* param = static_cast<esp_hidd_event_data_t*>(data);
  switch (static_cast<esp_hidd_event_t>(id)) {
    case ESP_HIDD_START_EVENT:
      // Posted once the NimBLE host has synced.
      host_ready = true;
      ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &select_event);
      break;
    case ESP_HIDD_OUTPUT_EVENT:
      // LED output reports are forwarded to USB in Phase 4.
      ESP_LOGI(kTag, "output report id=%u len=%u", param->output.report_id,
               static_cast<unsigned>(param->output.length));
      break;
    default:
      break;
  }
}

void host_task(void*) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

}  // namespace

bool Start(const char* device_name, uint8_t slot, LinkCallback callback) {
  if (slot >= PeerSlots::kCount || !load_slots()) return false;
  link_callback = callback;
  name_prefix = device_name;
  active_slot = slot;
  hid_config.device_name = device_name;

  if (nimble_port_init() != ESP_OK) {
    ESP_LOGE(kTag, "nimble_port_init failed");
    return false;
  }
  ble_npl_event_init(&select_event, apply_selection, nullptr);
  ble_npl_event_init(&management_event, apply_management, nullptr);
  if (ble_npl_callout_init(&pairing_timer, nimble_port_get_dflt_eventq(),
                           pairing_expired, nullptr) != 0) return false;
  publish_slots();
  ble_svc_gap_device_appearance_set(ESP_HID_APPEARANCE_KEYBOARD);

  // PoC pairing is Just Works with bonding (spec 11.5); privacy stays off.
  ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 0;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist =
      BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  // Three slots fit in three bonds. Refuse overflow instead of evicting a PC.
  ble_hs_cfg.store_status_cb = on_store_status;

  // esp_hid registers the HID service and takes over the sync/reset callbacks.
  if (esp_hidd_dev_init(&hid_config, ESP_HID_TRANSPORT_BLE, on_hidd_event,
                        &hid_device) != ESP_OK) {
    ESP_LOGE(kTag, "esp_hidd_dev_init failed");
    return false;
  }
  ble_store_config_init();
  original_store_write = ble_hs_cfg.store_write_cb;
  ble_hs_cfg.store_write_cb = gated_store_write;
  nimble_port_freertos_init(host_task);
  return true;
}

void SelectSlot(uint8_t slot) {
  if (slot >= PeerSlots::kCount) return;
  active_slot = slot;
  secured = false;
  ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &select_event);
}

bool StartPairing(uint8_t slot, uint16_t timeout_s) {
  if (slot >= PeerSlots::kCount || timeout_s == 0 || timeout_s > 300) return false;
  return enqueue_request({RequestKind::kPair, slot, timeout_s});
}

bool StopPairing() {
  return enqueue_request({RequestKind::kStop, 0, 0});
}

bool Unbind(uint8_t slot) {
  if (slot >= PeerSlots::kCount) return false;
  return enqueue_request({RequestKind::kUnbind, slot, 0});
}

bool StopForSleep() {
  // Prevent disconnect callbacks and queued requests from restarting adverts.
  if (!host_ready.exchange(false)) return false;
  secured = false;
  if (nimble_port_stop() != ESP_OK) return false;
  // NimBLE can finish its stop procedure after a disconnect timeout too.
  // Require the GAP disconnect callback before declaring a graceful stop.
  if (conn_handle != BLE_HS_CONN_HANDLE_NONE) return false;
  ble_npl_callout_stop(&pairing_timer);
  return nimble_port_deinit() == ESP_OK;
}

SlotSnapshot SlotInfo(uint8_t slot) {
  if (slot >= PeerSlots::kCount) return {};
  portENTER_CRITICAL(&management_lock);
  SlotSnapshot info = snapshots[slot];
  const uint32_t deadline = snapshot_deadlines[slot];
  portEXIT_CRITICAL(&management_lock);
  if (info.pairing) {
    const int32_t ticks = static_cast<int32_t>(deadline - ble_npl_time_get());
    const uint32_t ms = ticks > 0 ? ble_npl_time_ticks_to_ms32(ticks) : 0;
    info.pairing_seconds_remaining = (ms + 999) / 1000;
    info.pairing = ms > 0;
  }
  return info;
}

namespace {

std::atomic<uint32_t> sent_count{0};
std::atomic<uint32_t> failed_count{0};

bool send(uint8_t report_id, uint8_t* report, size_t length) {
  if (!secured || disconnect_pending || link_slot != active_slot ||
      hid_device == nullptr) {
    return false;
  }
  const bool ok = esp_hidd_dev_input_set(hid_device, 0, report_id, report,
                                         length) == ESP_OK;
  ++(ok ? sent_count : failed_count);
  return ok;
}

}  // namespace

uint32_t SentCount() { return sent_count; }
uint32_t FailedCount() { return failed_count; }

bool SendKeyboard(uint8_t modifiers, const uint8_t keys[6]) {
  uint8_t report[8] = {modifiers, 0};
  memcpy(&report[2], keys, 6);
  return send(kKeyboardReportId, report, sizeof(report));
}

bool SendConsumer(uint16_t usage) {
  uint8_t report[2] = {static_cast<uint8_t>(usage),
                       static_cast<uint8_t>(usage >> 8)};
  return send(kConsumerReportId, report, sizeof(report));
}

bool SendSystem(uint16_t usage) {
  uint8_t report[2] = {static_cast<uint8_t>(usage),
                       static_cast<uint8_t>(usage >> 8)};
  return send(kSystemReportId, report, sizeof(report));
}

bool SendMouse(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan) {
  uint8_t report[5] = {static_cast<uint8_t>(buttons & 0x1F),
                       static_cast<uint8_t>(x), static_cast<uint8_t>(y),
                       static_cast<uint8_t>(wheel), static_cast<uint8_t>(pan)};
  return send(kMouseReportId, report, sizeof(report));
}

}  // namespace ble_keyboard
