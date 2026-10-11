// Compile the real implementation, including its private host-queue handlers.
// SDK calls alone are faked; no state-machine implementation is duplicated.
#include "ble_keyboard_fake_sdk.h"
#include "../ble_keyboard.cpp"

#include <algorithm>
#include <utility>

using namespace ble_keyboard;
namespace {
using Notification = std::pair<LinkEvent, uint8_t>;
std::vector<Notification> notifications;
constexpr uint16_t kHandle = 42;
const PeerAddress kPeer0{0, {1, 2, 3, 4, 5, 6}};
const PeerAddress kPeer1{0, {2, 3, 4, 5, 6, 7}};
const uint8_t kKeys[6] = {4};

void record(LinkEvent event, uint8_t slot) { notifications.emplace_back(event, slot); }
int count(LinkEvent event) {
  return std::count_if(notifications.begin(), notifications.end(),
                       [event](const Notification& n) { return n.first == event; });
}
void reset() {
  fake_sdk::reset();
  pairing_active = pairing_link = false;
  pairing_slot = unbind_slot = kNoSlot;
  pairing_deadline = 0;
  request_head = request_count = 0;
  for (auto& error : slot_errors) error = ManagementError::kNone;
  ble_npl_callout_init(&pairing_timer, nimble_port_get_dflt_eventq(), pairing_expired, nullptr);
  notifications.clear();
  peer_slots = PeerSlots{};
  assert(peer_slots.Bind(0, kPeer0));
  assert(peer_slots.Bind(1, kPeer1));
  secured = false;
  disconnect_pending = false;
  conn_handle = BLE_HS_CONN_HANDLE_NONE;
  active_slot = link_slot = advertised_slot = 0;
  host_ready = true;
  migrate_legacy_bonds = false;
  link_callback = record;
  hid_device = &fake_sdk::device;
  sent_count = failed_count = 0;
  fake_sdk::connection.peer_id_addr = ble_address(kPeer0);
  fake_sdk::connection.sec_state = {1, 1};
  publish_slots();
}
void connect(uint16_t handle = kHandle) {
  ble_gap_event event{};
  event.type = BLE_GAP_EVENT_CONNECT;
  event.connect.conn_handle = handle;
  assert(on_gap_event(&event, nullptr) == 0);
}
void disconnect() {
  ble_gap_event event{};
  event.type = BLE_GAP_EVENT_DISCONNECT;
  assert(on_gap_event(&event, nullptr) == 0);
}
void select(uint8_t slot) {
  SelectSlot(slot);
  apply_selection(nullptr);  // Drain the fake host queue explicitly.
}
void connected0() {
  reset();
  connect();
  assert(secured && conn_handle == kHandle && count(LinkEvent::kSecured) == 1);
  notifications.clear();
}

void selection_round_trip_waits_for_disconnect() {
  connected0();
  select(1);
  assert(disconnect_pending && !secured && fake_sdk::terminate_calls == 1);
  assert(fake_sdk::terminated_handle == kHandle);
  assert(fake_sdk::terminated_reason == BLE_ERR_REM_USER_CONN_TERM);
  select(0);
  secure_link(kHandle);
  connect();  // A delayed CONNECT must not revive a terminating link either.
  ble_gap_event event{};
  event.type = BLE_GAP_EVENT_ENC_CHANGE;
  event.enc_change.conn_handle = kHandle;
  on_gap_event(&event, nullptr);
  event.type = BLE_GAP_EVENT_REPEAT_PAIRING;
  event.repeat_pairing.conn_handle = kHandle;
  assert(on_gap_event(&event, nullptr) == BLE_GAP_REPEAT_PAIRING_IGNORE);
  assert(fake_sdk::delete_calls == 0);
  assert(!SendKeyboard(0, kKeys) && fake_sdk::hid_calls == 0);
  assert(disconnect_pending && !secured && count(LinkEvent::kSecured) == 0);
  assert(fake_sdk::terminate_calls == 1 && fake_sdk::adv_start_calls == 0);
  disconnect();
  assert(!disconnect_pending && conn_handle == BLE_HS_CONN_HANDLE_NONE);
  assert(count(LinkEvent::kDisconnected) == 1 && advertised_slot == 0);
  connect();
  assert(secured && count(LinkEvent::kSecured) == 1);
  assert(SendKeyboard(0, kKeys) && fake_sdk::hid_calls == 1);
}

void already_terminating_is_pending() {
  connected0();
  fake_sdk::terminate_rc = BLE_HS_EALREADY;
  select(1);
  apply_selection(nullptr);
  assert(disconnect_pending && conn_handle == kHandle);
  assert(fake_sdk::terminate_calls == 1 && fake_sdk::error_logs == 0);
  assert(!SendKeyboard(0, kKeys) && fake_sdk::hid_calls == 0);
  disconnect();
  assert(!disconnect_pending && advertised_slot == 1);
}

void reconnect_uses_latest_slot_and_identity() {
  connected0();
  select(1);
  select(2);
  select(1);
  disconnect();
  assert(advertised_slot == 1 && fake_sdk::whitelist_count == 1);
  assert(peer_address(fake_sdk::whitelist) == kPeer1);
  assert(notifications.back() == Notification(LinkEvent::kAdvertising, 1));
  fake_sdk::connection.peer_id_addr = ble_address(kPeer1);
  fake_sdk::connection.sec_state = {0, 1};
  connect(43);
  assert(link_slot == 1 && !secured && fake_sdk::security_calls == 1);
  assert(!SendKeyboard(0, kKeys));
  fake_sdk::connection.sec_state = {1, 0};
  secure_link(43);
  assert(!secured);  // Encryption alone does not satisfy the bonded contract.
  fake_sdk::connection.sec_state = {1, 1};
  secure_link(43);
  assert(secured && notifications.back() == Notification(LinkEvent::kSecured, 1));
  assert(SendKeyboard(0, kKeys) && fake_sdk::hid_calls == 1 && SentCount() == 1);
}

void stale_secured_link_cannot_send() {
  connected0();
  active_slot = 1;
  assert(secured && link_slot != active_slot);
  assert(!SendKeyboard(0, kKeys));
  assert(fake_sdk::hid_calls == 0 && SentCount() == 0 && FailedCount() == 0);
}

void pending_link_cannot_send_even_with_stale_secured_flag() {
  connected0();
  disconnect_pending = true;
  assert(secured && link_slot == active_slot);
  assert(!SendKeyboard(0, kKeys));
  assert(fake_sdk::hid_calls == 0 && SentCount() == 0 && FailedCount() == 0);
}

void missing_link_clears_tracking_and_advertises() {
  connected0();
  fake_sdk::terminate_rc = BLE_HS_ENOTCONN;
  select(1);
  assert(!disconnect_pending && !secured && conn_handle == BLE_HS_CONN_HANDLE_NONE);
  assert(count(LinkEvent::kDisconnected) == 1 && advertised_slot == 1);
  assert(fake_sdk::adv_start_calls == 1 && fake_sdk::error_logs == 1);
  assert(notifications.front() == Notification(LinkEvent::kDisconnected, 0));
}

void other_terminate_failure_preserves_link() {
  connected0();
  fake_sdk::terminate_rc = 99;
  select(1);
  assert(!disconnect_pending && !secured && conn_handle == kHandle);
  assert(fake_sdk::error_logs == 1 && count(LinkEvent::kDisconnected) == 0);
  assert(fake_sdk::adv_start_calls == 0);
  assert(!SendKeyboard(0, kKeys) && fake_sdk::hid_calls == 0);
}

void adv_stop_failure_aborts_update() {
  reset();
  active_slot = 1;
  fake_sdk::adv_stop_rc = 99;
  start_advertising();
  assert(fake_sdk::adv_stop_calls == 1 && fake_sdk::error_logs == 1);
  assert(fake_sdk::whitelist_calls == 0 && fake_sdk::adv_fields_calls == 0);
  assert(fake_sdk::adv_start_calls == 0 && advertised_slot == 0);
  assert(count(LinkEvent::kAdvertising) == 0);
}

void adv_stop_already_stopped_is_benign() {
  reset();
  active_slot = 1;
  fake_sdk::adv_stop_rc = BLE_HS_EALREADY;
  start_advertising();
  assert(fake_sdk::whitelist_calls == 1 && fake_sdk::adv_start_calls == 1);
  assert(advertised_slot == 1 && fake_sdk::error_logs == 0);
  assert(notifications.back() == Notification(LinkEvent::kAdvertising, 1));
}

void adv_start_already_active_does_not_claim_new_slot() {
  reset();
  active_slot = 1;
  fake_sdk::adv_start_rc = BLE_HS_EALREADY;
  start_advertising();
  assert(fake_sdk::adv_start_calls == 1 && advertised_slot == 0);
  assert(count(LinkEvent::kAdvertising) == 0);
}

void cccd_overflow_returns_att_resource_error() {
  reset();
  ble_store_status_event event{};
  event.event_code = BLE_STORE_EVENT_OVERFLOW;
  event.overflow.obj_type = BLE_STORE_OBJ_TYPE_CCCD;
  assert(on_store_status(&event, nullptr) == 0x11);
  event.overflow.obj_type = 1;
  assert(on_store_status(&event, nullptr) == BLE_HS_ESTORE_CAP);
}

void manage() { apply_management(nullptr); }
void pair(uint8_t slot, uint16_t seconds = 60) {
  assert(StartPairing(slot, seconds)); manage();
}
int accept_store(int, const ble_store_value*) { ++fake_sdk::store_calls; return 0; }

void unknown_slot_is_silent_and_rejects_late_connection() {
  reset(); select(2);
  assert(fake_sdk::adv_start_calls == 0);
  advertised_slot = 2;  // delayed connection from an old open advertisement
  fake_sdk::connection.peer_id_addr = ble_address(PeerAddress{0, {9,8,7,6,5,4}});
  connect();
  assert(disconnect_pending && !secured && peer_slots.Get(2) == nullptr);
  assert(fake_sdk::security_calls == 0);
}

void explicit_pairing_binds_and_closes_window() {
  reset(); select(2); pair(2);
  assert(count(LinkEvent::kPairingStarted) == 1 && fake_sdk::whitelist_count == 0);
  auto info = SlotInfo(2);
  assert(info.valid && !info.registered && info.pairing && info.pairing_seconds_remaining == 60);
  fake_sdk::now = 1501;
  assert(SlotInfo(2).pairing_seconds_remaining == 59);
  const PeerAddress newcomer{0, {9,8,7,6,5,4}};
  fake_sdk::connection.peer_id_addr = ble_address(newcomer);
  connect();
  assert(secured && count(LinkEvent::kPairingComplete) == 1);
  assert(count(LinkEvent::kSecured) == 1 && !SlotInfo(2).pairing);
  assert(SlotInfo(2).registered && peer_slots.Get(2)->address == newcomer);
  assert(notifications[notifications.size()-2].first == LinkEvent::kPairingComplete);
}

void timeout_blocks_keys_and_late_encryption_before_timer_runs() {
  reset(); select(2); pair(2, 1);
  fake_sdk::connection.peer_id_addr = ble_address(PeerAddress{0, {9,8,7,6,5,4}});
  fake_sdk::connection.sec_state = {0,0}; connect();
  fake_sdk::now = 1000;
  assert(!SlotInfo(2).pairing);
  original_store_write = accept_store;
  ble_store_value value{}; value.sec.peer_addr = fake_sdk::connection.peer_id_addr;
  assert(gated_store_write(BLE_STORE_OBJ_TYPE_OUR_SEC, &value) == BLE_HS_EAUTHEN);
  fake_sdk::connection.sec_state = {1,1}; secure_link(kHandle);
  assert(!secured && peer_slots.Get(2) == nullptr);
  pairing_expired(nullptr);
  assert(count(LinkEvent::kPairingTimedOut) == 1);
  disconnect();
  assert(peer_slots.Get(2) == nullptr);
}

void cancel_and_switch_close_windows() {
  reset(); select(2); pair(2);
  assert(StopPairing()); manage();
  assert(!SlotInfo(2).pairing && count(LinkEvent::kPairingStopped) == 1);
  pair(2); select(1);
  assert(!SlotInfo(2).pairing && count(LinkEvent::kPairingStopped) == 2);
  assert(fake_sdk::whitelist_count == 1 && peer_address(fake_sdk::whitelist) == kPeer1);
}

void registered_reconnect_needs_no_window_and_repeat_is_rejected() {
  connected0();
  ble_gap_event event{}; event.type = BLE_GAP_EVENT_REPEAT_PAIRING;
  event.repeat_pairing.conn_handle = kHandle;
  assert(on_gap_event(&event, nullptr) == BLE_GAP_REPEAT_PAIRING_IGNORE);
  assert(fake_sdk::delete_calls == 0 && SendKeyboard(0,kKeys));
  pair(0);
  assert(on_gap_event(&event,nullptr) == BLE_GAP_REPEAT_PAIRING_RETRY);
  assert(fake_sdk::delete_calls == 1 && pairing_link && !secured);
  fake_sdk::connection.sec_state = {1,1}; secure_link(kHandle);
  assert(count(LinkEvent::kPairingComplete) == 1 && secured);
}

void cancel_registered_window_preserves_normal_connection() {
  connected0(); pair(0);
  assert(StopPairing()); manage();
  assert(fake_sdk::terminate_calls == 0 && fake_sdk::delete_calls == 0);
  assert(SendKeyboard(0,kKeys));
}

void pairing_failure_does_not_claim_acceptance() {
  reset(); select(2); fake_sdk::adv_start_rc = 99; pair(2);
  assert(!SlotInfo(2).pairing && count(LinkEvent::kPairingStarted) == 0);
  assert(count(LinkEvent::kPairingFailed) == 1);
  assert(SlotInfo(2).last_error == ManagementError::kAdvertising);
  reset(); pair(1);
  assert(SlotInfo(1).last_error == ManagementError::kNotSelected);
  assert(count(LinkEvent::kPairingFailed) == 1);
}

void security_storage_gate_preserves_cccd_and_other_hosts() {
  reset(); original_store_write = accept_store;
  ble_store_value value{}; value.sec.peer_addr = ble_address(kPeer0);
  assert(gated_store_write(BLE_STORE_OBJ_TYPE_OUR_SEC,&value) == BLE_HS_EAUTHEN);
  assert(gated_store_write(BLE_STORE_OBJ_TYPE_CCCD,&value) == 0);
  pair(0);
  assert(gated_store_write(BLE_STORE_OBJ_TYPE_OUR_SEC,&value) == 0);
  value.sec.peer_addr = ble_address(kPeer1);
  assert(gated_store_write(BLE_STORE_OBJ_TYPE_PEER_SEC,&value) == BLE_HS_EAUTHEN);
  assert(fake_sdk::store_calls == 2);
}

void unbind_waits_for_disconnect_and_removes_only_target() {
  connected0(); assert(Unbind(0)); manage();
  assert(SlotInfo(0).unbinding && fake_sdk::delete_calls == 0);
  assert(fake_sdk::hid_calls == 4 && disconnect_pending);
  disconnect();
  assert(count(LinkEvent::kUnbound) == 1 && !SlotInfo(0).registered);
  assert(SlotInfo(1).registered && peer_slots.Get(1)->address == kPeer1);
  assert(peer_address(fake_sdk::deleted_peer) == kPeer0);
  assert(fake_sdk::adv_start_calls == 0); // empty slot no longer auto-pairs
}

void unbind_failures_keep_mapping_and_allow_retry() {
  reset(); fake_sdk::delete_rc = 99; assert(Unbind(1)); manage();
  assert(SlotInfo(1).registered && count(LinkEvent::kUnbindFailed) == 1);
  fake_sdk::delete_rc = 0; fake_sdk::nvs_commit_rc = 99;
  assert(Unbind(1)); manage();
  assert(SlotInfo(1).registered && count(LinkEvent::kUnbindFailed) == 2);
  fake_sdk::nvs_commit_rc = 0; assert(Unbind(1)); manage();
  assert(!SlotInfo(1).registered && SlotInfo(0).registered);
  assert(count(LinkEvent::kUnbound) == 1);
  connected0(); fake_sdk::terminate_rc = 99; assert(Unbind(0)); manage();
  assert(count(LinkEvent::kUnbindFailed) == 1 && fake_sdk::delete_calls == 0);
  assert(SlotInfo(0).last_error == ManagementError::kDisconnect);
}

void invalid_queue_and_empty_unbind() {
  reset(); assert(!SlotInfo(3).valid);
  assert(!StartPairing(3) && !StartPairing(0,0) && !StartPairing(0,301));
  assert(!Unbind(3)); host_ready = false; assert(!StopPairing()); host_ready = true;
  for(size_t i=0;i<kRequestCapacity;++i) assert(StopPairing());
  assert(!StopPairing()); manage(); assert(StopPairing()); manage();
  assert(Unbind(2)); manage(); assert(count(LinkEvent::kUnbound) == 1);
}

void missing_bond_cannot_silently_pair() {
  reset(); fake_sdk::bond_read_rc = 99; connect();
  assert(disconnect_pending && !secured && fake_sdk::security_calls == 0);
}

void cancel_incomplete_attempt_disconnects_and_cleans_target() {
  reset(); select(2); pair(2);
  const PeerAddress newcomer{0, {9,8,7,6,5,4}};
  fake_sdk::connection.peer_id_addr = ble_address(newcomer);
  fake_sdk::connection.sec_state = {0,0}; connect();
  assert(StopPairing()); manage();
  assert(disconnect_pending && fake_sdk::delete_calls == 1);
  assert(peer_address(fake_sdk::deleted_peer) == newcomer);
  fake_sdk::connection.sec_state = {1,1}; secure_link(kHandle);
  assert(!secured && peer_slots.Get(2) == nullptr && SlotInfo(0).registered);
}

void failed_registration_is_not_reported_as_secured() {
  reset(); select(2); pair(2); fake_sdk::nvs_set_rc = 99;
  fake_sdk::connection.peer_id_addr = ble_address(PeerAddress{0,{9,8,7,6,5,4}});
  connect();
  assert(!secured && !SlotInfo(2).registered && !SlotInfo(2).pairing);
  assert(count(LinkEvent::kPairingFailed) == 1 && count(LinkEvent::kSecured) == 0);
  assert(fake_sdk::delete_calls == 1 && disconnect_pending);
  assert(SlotInfo(2).last_error == ManagementError::kStorage);
}

int fail_store(int, const ble_store_value*) { return 99; }
void key_store_failure_cannot_bind_even_if_nimble_reports_bonded() {
  reset(); select(2); pair(2);
  original_store_write = fail_store;
  ble_store_value value{}; value.sec.peer_addr = ble_address(PeerAddress{0,{9,8,7,6,5,4}});
  assert(gated_store_write(BLE_STORE_OBJ_TYPE_OUR_SEC,&value) == 99);
  fake_sdk::connection.peer_id_addr = value.sec.peer_addr; connect();
  assert(count(LinkEvent::kPairingFailed) == 1 && count(LinkEvent::kSecured) == 0);
  assert(!SlotInfo(2).registered && disconnect_pending);
}

void registered_timeout_keeps_connection_and_window_survives_normal_encryption() {
  connected0(); pair(0,1); secure_link(kHandle);
  assert(SlotInfo(0).pairing && count(LinkEvent::kPairingComplete) == 0);
  fake_sdk::now = 1000; pairing_expired(nullptr);
  assert(count(LinkEvent::kPairingTimedOut) == 1 && secured);
  assert(fake_sdk::terminate_calls == 0 && fake_sdk::delete_calls == 0);
  assert(SendKeyboard(0,kKeys));
}

void inactive_unbind_preserves_active_link() {
  connected0(); assert(Unbind(1)); manage();
  assert(secured && conn_handle == kHandle && SendKeyboard(0,kKeys));
  assert(count(LinkEvent::kUnbound) == 1 && fake_sdk::terminate_calls == 0);
  assert(SlotInfo(0).registered && !SlotInfo(1).registered);
  assert(peer_address(fake_sdk::deleted_peer) == kPeer1);
}

void repeat_delete_failure_is_visible() {
  connected0(); pair(0); fake_sdk::delete_rc = 99;
  ble_gap_event event{}; event.type = BLE_GAP_EVENT_REPEAT_PAIRING;
  event.repeat_pairing.conn_handle = kHandle;
  assert(on_gap_event(&event,nullptr) == BLE_GAP_REPEAT_PAIRING_IGNORE);
  assert(count(LinkEvent::kPairingFailed) == 1 && !SlotInfo(0).pairing);
  assert(SlotInfo(0).registered && SlotInfo(1).registered);
  assert(SlotInfo(0).last_error == ManagementError::kStorage);
}

void deadline_wrap_and_delayed_request_selection() {
  reset(); select(2); fake_sdk::now = UINT32_MAX - 499; pair(2,1);
  assert(SlotInfo(2).pairing_seconds_remaining == 1);
  fake_sdk::now = 500; pairing_expired(nullptr);
  assert(count(LinkEvent::kPairingTimedOut) == 1 && !SlotInfo(2).pairing);
  assert(StartPairing(2)); SelectSlot(1); manage(); apply_selection(nullptr);
  assert(count(LinkEvent::kPairingFailed) == 1 && !pairing_active);
}

void registered_missing_bond_can_explicitly_repair() {
  reset(); fake_sdk::bond_read_rc = 99; pair(0);
  fake_sdk::connection.sec_state = {0,0}; connect();
  assert(pairing_link && !secured);
  fake_sdk::bond_read_rc = 0;
  fake_sdk::connection.sec_state = {1,1}; secure_link(kHandle);
  assert(count(LinkEvent::kPairingComplete) == 1 && secured);
  assert(SlotInfo(0).registered && !SlotInfo(0).pairing);
}

void registered_missing_bond_store_failure_is_not_secured() {
  reset(); fake_sdk::bond_read_rc = 99; pair(0);
  fake_sdk::connection.sec_state = {0,0}; connect();
  original_store_write = fail_store;
  ble_store_value value{}; value.sec.peer_addr = ble_address(kPeer0);
  assert(gated_store_write(BLE_STORE_OBJ_TYPE_OUR_SEC,&value) == 99);
  fake_sdk::bond_read_rc = 0;
  fake_sdk::connection.sec_state = {1,1}; secure_link(kHandle);
  assert(!secured && count(LinkEvent::kPairingFailed) == 1);
  assert(count(LinkEvent::kSecured) == 0 && SlotInfo(0).registered);
  assert(SlotInfo(0).last_error == ManagementError::kStorage);
}

void cancel_termination_failure_is_visible() {
  reset(); select(2); pair(2);
  fake_sdk::connection.peer_id_addr = ble_address(PeerAddress{0,{9,8,7,6,5,4}});
  fake_sdk::connection.sec_state = {0,0}; connect();
  fake_sdk::terminate_rc = 99;
  assert(StopPairing()); manage();
  assert(!SlotInfo(2).pairing && count(LinkEvent::kPairingFailed) == 1);
  assert(SlotInfo(2).last_error == ManagementError::kDisconnect);
  assert(!secured && count(LinkEvent::kPairingStopped) == 0);
}
}  // namespace

int main() {
  selection_round_trip_waits_for_disconnect();
  already_terminating_is_pending();
  reconnect_uses_latest_slot_and_identity();
  stale_secured_link_cannot_send();
  pending_link_cannot_send_even_with_stale_secured_flag();
  missing_link_clears_tracking_and_advertises();
  other_terminate_failure_preserves_link();
  adv_stop_failure_aborts_update();
  adv_stop_already_stopped_is_benign();
  adv_start_already_active_does_not_claim_new_slot();
  cccd_overflow_returns_att_resource_error();
  unknown_slot_is_silent_and_rejects_late_connection();
  explicit_pairing_binds_and_closes_window();
  timeout_blocks_keys_and_late_encryption_before_timer_runs();
  cancel_and_switch_close_windows();
  registered_reconnect_needs_no_window_and_repeat_is_rejected();
  cancel_registered_window_preserves_normal_connection();
  pairing_failure_does_not_claim_acceptance();
  security_storage_gate_preserves_cccd_and_other_hosts();
  unbind_waits_for_disconnect_and_removes_only_target();
  unbind_failures_keep_mapping_and_allow_retry();
  invalid_queue_and_empty_unbind();
  missing_bond_cannot_silently_pair();
  cancel_incomplete_attempt_disconnects_and_cleans_target();
  failed_registration_is_not_reported_as_secured();
  key_store_failure_cannot_bind_even_if_nimble_reports_bonded();
  registered_timeout_keeps_connection_and_window_survives_normal_encryption();
  inactive_unbind_preserves_active_link();
  repeat_delete_failure_is_visible();
  deadline_wrap_and_delayed_request_selection();
  registered_missing_bond_can_explicitly_repair();
  registered_missing_bond_store_failure_is_not_secured();
  cancel_termination_failure_is_visible();
  puts("ble_keyboard: PASS (33 real-implementation regression cases)");
}
