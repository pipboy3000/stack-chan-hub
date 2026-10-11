#include "dev_link.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "driver/usb_serial_jtag.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "nvs.h"

namespace dev_link {
namespace {

constexpr char kTag[] = "dev_link";
constexpr char kNamespace[] = "devlink";
constexpr char kHostname[] = "stackchan-hub";
constexpr uint16_t kLogPort = 5140;
constexpr size_t kMaxSsid = 33;
constexpr size_t kMaxPassword = 65;
constexpr size_t kMaxToken = 65;

std::atomic<State> link_state{State::kNotConfigured};
char ip_text[16] = "";
char ota_token[kMaxToken] = "";
std::atomic<bool> serial_running{false};
std::atomic<StatusWriter> status_writer{nullptr};
std::atomic<ScreenReader> screen_reader{nullptr};
std::atomic<TapHandler> tap_handler{nullptr};
std::atomic<bool> serial_stop{false};
TaskHandle_t serial_task_handle = nullptr;

// ------------------------------------------------------------ settings -----

bool load_string(nvs_handle_t nvs, const char* key, char* out, size_t size) {
  size_t length = size;
  return nvs_get_str(nvs, key, out, &length) == ESP_OK && out[0] != '\0';
}

bool load_settings(char* ssid, char* password) {
  nvs_handle_t nvs;
  if (nvs_open(kNamespace, NVS_READONLY, &nvs) != ESP_OK) {
    return false;
  }
  const bool ok = load_string(nvs, "ssid", ssid, kMaxSsid) &&
                  load_string(nvs, "token", ota_token, kMaxToken);
  if (ok && !load_string(nvs, "pass", password, kMaxPassword)) {
    password[0] = '\0';  // open network
  }
  nvs_close(nvs);
  return ok;
}

bool save_settings(const char* ssid, const char* password, const char* token) {
  nvs_handle_t nvs;
  if (nvs_open(kNamespace, NVS_READWRITE, &nvs) != ESP_OK) {
    return false;
  }
  const bool ok = nvs_set_str(nvs, "ssid", ssid) == ESP_OK &&
                  nvs_set_str(nvs, "pass", password) == ESP_OK &&
                  nvs_set_str(nvs, "token", token) == ESP_OK &&
                  nvs_commit(nvs) == ESP_OK;
  nvs_close(nvs);
  return ok;
}

// ------------------------------------------------------------ UDP logs -----

int log_socket = -1;
vprintf_like_t original_vprintf = nullptr;

// Mirrors every log line to UDP broadcast; `nc -kul 5140` on the Mac shows it.
int log_to_udp(const char* format, va_list args) {
  va_list copy;
  va_copy(copy, args);
  const int written = original_vprintf(format, args);
  if (log_socket >= 0 && link_state == State::kConnected) {
    char line[256];
    const int length = vsnprintf(line, sizeof(line), format, copy);
    if (length > 0) {
      sockaddr_in to{};
      to.sin_family = AF_INET;
      to.sin_port = htons(kLogPort);
      to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
      sendto(log_socket, line,
             length < static_cast<int>(sizeof(line)) ? length : sizeof(line) - 1,
             MSG_DONTWAIT, reinterpret_cast<sockaddr*>(&to), sizeof(to));
    }
  }
  va_end(copy);
  return written;
}

void start_udp_log() {
  log_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (log_socket < 0) {
    return;
  }
  const int enable = 1;
  setsockopt(log_socket, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(enable));
  original_vprintf = esp_log_set_vprintf(log_to_udp);
}

// ------------------------------------------------------------------ OTA -----

bool token_matches(httpd_req_t* request) {
  char token[kMaxToken] = "";
  if (httpd_req_get_hdr_value_str(request, "X-OTA-Token", token,
                                  sizeof(token)) != ESP_OK) {
    return false;
  }
  // Constant-time comparison over the stored token's length.
  const size_t length = strlen(ota_token);
  uint8_t diff = strlen(token) != length;
  for (size_t i = 0; i < length; ++i) {
    diff |= token[i] ^ ota_token[i];
  }
  return diff == 0;
}

void restart_task(void*) {
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_restart();
}

esp_err_t handle_ota(httpd_req_t* request) {
  if (!token_matches(request)) {
    httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "bad token");
    return ESP_OK;
  }
  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
  esp_ota_handle_t ota = 0;
  if (target == nullptr ||
      esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
    httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                        "ota begin failed");
    return ESP_OK;
  }
  ESP_LOGI(kTag, "OTA to %s, %d bytes", target->label, request->content_len);

  static char buffer[4096];
  int remaining = request->content_len;
  while (remaining > 0) {
    const int received = httpd_req_recv(
        request, buffer,
        remaining < static_cast<int>(sizeof(buffer)) ? remaining
                                                     : sizeof(buffer));
    if (received == HTTPD_SOCK_ERR_TIMEOUT) {
      continue;
    }
    if (received <= 0 || esp_ota_write(ota, buffer, received) != ESP_OK) {
      esp_ota_abort(ota);
      httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                          "receive or write failed");
      return ESP_OK;
    }
    remaining -= received;
  }

  const esp_err_t end = esp_ota_end(ota);
  if (end != ESP_OK || esp_ota_set_boot_partition(target) != ESP_OK) {
    httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                        end == ESP_ERR_OTA_VALIDATE_FAILED ? "image invalid"
                                                           : "ota end failed");
    return ESP_OK;
  }
  httpd_resp_sendstr(request, "ok, restarting\n");
  ESP_LOGI(kTag, "OTA done, restarting into %s", target->label);
  xTaskCreate(restart_task, "ota_restart", 2048, nullptr, 5, nullptr);
  return ESP_OK;
}

esp_err_t handle_status(httpd_req_t* request) {
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_app_desc_t* app = esp_app_get_description();
  char body[512];
  int length = snprintf(
      body, sizeof(body), "partition %s\nbuilt %s %s\nfree heap %lu\n",
      running->label, app->date, app->time,
      static_cast<unsigned long>(esp_get_free_heap_size()));
  if (const StatusWriter writer = status_writer.load();
      writer != nullptr && length > 0 && length < static_cast<int>(sizeof(body))) {
    writer(body + length, sizeof(body) - length);
  }
  httpd_resp_sendstr(request, body);
  return ESP_OK;
}

esp_err_t handle_screen(httpd_req_t* request) {
  size_t size = 0;
  const ScreenReader reader = screen_reader.load();
  const uint8_t* pixels = reader != nullptr ? reader(&size) : nullptr;
  if (pixels == nullptr) {
    httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "no screen");
    return ESP_OK;
  }
  httpd_resp_set_type(request, "application/octet-stream");
  return httpd_resp_send(request, reinterpret_cast<const char*>(pixels), size);
}

esp_err_t handle_tap(httpd_req_t* request) {
  if (!token_matches(request)) {
    httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "bad token");
    return ESP_OK;
  }
  char query[32];
  char x[8];
  char y[8];
  const TapHandler handler = tap_handler.load();
  if (handler == nullptr ||
      httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "x", x, sizeof(x)) != ESP_OK ||
      httpd_query_key_value(query, "y", y, sizeof(y)) != ESP_OK) {
    httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "x and y required");
    return ESP_OK;
  }
  handler(atoi(x), atoi(y));
  httpd_resp_sendstr(request, "ok\n");
  return ESP_OK;
}

void start_http_server() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.recv_wait_timeout = 10;
  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &config) != ESP_OK) {
    ESP_LOGE(kTag, "HTTP server failed");
    return;
  }
  const httpd_uri_t ota = {.uri = "/ota",
                           .method = HTTP_POST,
                           .handler = handle_ota,
                           .user_ctx = nullptr};
  const httpd_uri_t status = {.uri = "/status",
                              .method = HTTP_GET,
                              .handler = handle_status,
                              .user_ctx = nullptr};
  httpd_register_uri_handler(server, &ota);
  const httpd_uri_t screen = {.uri = "/screen",
                              .method = HTTP_GET,
                              .handler = handle_screen,
                              .user_ctx = nullptr};
  httpd_register_uri_handler(server, &status);
  const httpd_uri_t tap = {.uri = "/tap",
                           .method = HTTP_POST,
                           .handler = handle_tap,
                           .user_ctx = nullptr};
  httpd_register_uri_handler(server, &screen);
  httpd_register_uri_handler(server, &tap);
}

// ---------------------------------------------------------------- Wi-Fi -----

void on_wifi_event(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    link_state = State::kConnecting;
    ip_text[0] = '\0';
    esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const auto* event = static_cast<ip_event_got_ip_t*>(data);
    snprintf(ip_text, sizeof(ip_text), IPSTR, IP2STR(&event->ip_info.ip));
    link_state = State::kConnected;
    ESP_LOGI(kTag, "connected as %s (%s.local)", ip_text, kHostname);
  }
}

void start_wifi(const char* ssid, const char* password) {
  esp_netif_init();
  esp_event_loop_create_default();
  esp_netif_t* netif = esp_netif_create_default_wifi_sta();
  esp_netif_set_hostname(netif, kHostname);

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&init) != ESP_OK) {
    ESP_LOGE(kTag, "Wi-Fi init failed");
    return;
  }
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event,
                             nullptr);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event,
                             nullptr);

  wifi_config_t config{};
  strncpy(reinterpret_cast<char*>(config.sta.ssid), ssid,
          sizeof(config.sta.ssid));
  strncpy(reinterpret_cast<char*>(config.sta.password), password,
          sizeof(config.sta.password));
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_set_config(WIFI_IF_STA, &config);
  // Modem sleep keeps BLE coexistence working; input stays on BLE anyway.
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  link_state = State::kConnecting;
  esp_wifi_start();

  if (mdns_init() == ESP_OK) {
    mdns_hostname_set(kHostname);
    mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
  }
  start_http_server();
  start_udp_log();
}

// --------------------------------------------------------------- serial -----

void reply(const char* text) {
  usb_serial_jtag_write_bytes(text, strlen(text), pdMS_TO_TICKS(50));
}

// One command per line, fields separated by tabs:
//   wifi<TAB>ssid<TAB>password<TAB>ota-token   store and restart
//   status                                     print the link state
void handle_line(char* line) {
  char* fields[4] = {};
  size_t count = 0;
  for (char* cursor = line; count < 4;) {
    fields[count++] = cursor;
    char* tab = strchr(cursor, '\t');
    if (tab == nullptr) {
      break;
    }
    *tab = '\0';
    cursor = tab + 1;
  }
  if (strcmp(fields[0], "status") == 0) {
    char text[96];
    snprintf(text, sizeof(text), "status %s %s\n",
             link_state == State::kConnected    ? "connected"
             : link_state == State::kConnecting ? "connecting"
                                                : "not-configured",
             ip_text);
    reply(text);
    return;
  }
  if (strcmp(fields[0], "wifi") == 0 && count == 4 && fields[1][0] != '\0' &&
      strlen(fields[1]) < kMaxSsid && strlen(fields[2]) < kMaxPassword &&
      strlen(fields[3]) >= 16 && strlen(fields[3]) < kMaxToken) {
    if (save_settings(fields[1], fields[2], fields[3])) {
      reply("ok saved, restarting\n");
      xTaskCreate(restart_task, "cfg_restart", 2048, nullptr, 5, nullptr);
    } else {
      reply("error nvs\n");
    }
    return;
  }
  reply("error usage: wifi<TAB>ssid<TAB>password<TAB>token(16+) | status\n");
}

void serial_task(void*) {
  char line[192];
  size_t length = 0;
  while (!serial_stop) {
    uint8_t byte = 0;
    if (usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(100)) != 1) {
      continue;
    }
    if (byte == '\n' || byte == '\r') {
      if (length > 0) {
        line[length] = '\0';
        handle_line(line);
        length = 0;
      }
    } else if (length + 1 < sizeof(line)) {
      line[length++] = static_cast<char>(byte);
    }
  }
  usb_serial_jtag_driver_uninstall();
  serial_running = false;
  vTaskDelete(nullptr);
}

void start_serial() {
  usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
  if (usb_serial_jtag_driver_install(&config) != ESP_OK) {
    return;
  }
  serial_running = true;
  xTaskCreate(serial_task, "provision", 4096, nullptr, 3, &serial_task_handle);
}

}  // namespace

void Start() {
  start_serial();
  char ssid[kMaxSsid] = "";
  char password[kMaxPassword] = "";
  if (!load_settings(ssid, password)) {
    ESP_LOGI(kTag, "no Wi-Fi settings; provision with tools/wifi_setup.html");
    return;
  }
  start_wifi(ssid, password);
}

void StopSerial() {
  if (!serial_running) {
    return;
  }
  serial_stop = true;
  while (serial_running) {
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void SetStatusWriter(StatusWriter writer) { status_writer = writer; }
void SetScreenReader(ScreenReader reader) { screen_reader = reader; }
void SetTapHandler(TapHandler handler) { tap_handler = handler; }

State state() { return link_state; }

const char* ip_address() { return ip_text; }

}  // namespace dev_link
