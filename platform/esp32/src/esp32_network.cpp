#include "esp32_network.h"

#include <cstdio>
#include <cstring>

#include <esp_event.h>
#include <esp_http_client.h>
#include <esp_netif.h>
#include <esp_netif_sntp.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <middleware/elog/elog.hpp>

namespace embark::platform::esp32 {
namespace {

/// 事件位：只有一个目的 —— 让 connect() 能在「拿到 IP」与「失败」之间醒过来。
constexpr EventBits_t kConnectedBit = BIT0;
constexpr EventBits_t kFailedBit = BIT1;

/// 拨号超时。15 s 是「密码错 / AP 不在 / 信号差」三类共用的上限：再长，App 的
/// own_task 就一直占着任务不放；再短，弱信号下会误报失败。
constexpr TickType_t kConnectTimeout = pdMS_TO_TICKS(15000);

/// 一次 HTTP 的总超时（连接 + 等响应）。与宿主那份（各阶段 5 s）同量级。
constexpr int kHttpTimeoutMs = 10000;

/// 只说明文 HTTP：与宿主同一口径（理由见 include/embark/hal/network.h）。
bool starts_with_ci(const char* text, const char* prefix) noexcept {
  while (*prefix != '\0') {
    if (*text == '\0') {
      return false;
    }
    const char a = (*text >= 'A' && *text <= 'Z') ? static_cast<char>(*text - 'A' + 'a') : *text;
    const char b = (*prefix >= 'A' && *prefix <= 'Z') ? static_cast<char>(*prefix - 'A' + 'a') : *prefix;
    if (a != b) {
      return false;
    }
    ++text;
    ++prefix;
  }
  return true;
}

}  // namespace

/// 事件注册时用的转发器：把 IDF 的回调签名挡在本文件里，头文件不必认识 esp_event。
struct NetworkEventBridge {
  static void wifi(void* arg, esp_event_base_t base, std::int32_t event_id, void* event_data) {
    (void)base;
    (void)event_data;
    static_cast<Esp32Network*>(arg)->on_wifi_event(event_id);
  }
  static void ip(void* arg, esp_event_base_t base, std::int32_t event_id, void* event_data) {
    (void)base;
    (void)event_id;
    static_cast<Esp32Network*>(arg)->on_got_ip(event_data);
  }
};

Esp32Network::~Esp32Network() {
  if (events_ != nullptr) {
    vEventGroupDelete(events_);
    events_ = nullptr;
  }
}

Error Esp32Network::init() noexcept {
  if (events_ != nullptr) {
    return Error::none;  // 幂等
  }

  events_ = xEventGroupCreate();
  if (events_ == nullptr) {
    ELOG_WARN("网络：事件组建不出来 —— 内存不足，网络服务不可用");
    return Error::no_space;
  }

  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ELOG_WARN("网络：esp_netif_init 失败（{}）", esp_err_to_name(err));
    return Error::io_failure;
  }
  // 默认事件循环可能已经被别处建过（真机上目前没有别处），重复建不算错。
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ELOG_WARN("网络：默认事件循环建不起来（{}）", esp_err_to_name(err));
    return Error::io_failure;
  }

  esp_netif_t* netif = esp_netif_create_default_wifi_sta();
  if (netif == nullptr) {
    ELOG_WARN("网络：建不出 STA 网口 —— 网络服务不可用");
    return Error::io_failure;
  }
  netif_ = netif;

  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&init_config);
  if (err != ESP_OK) {
    ELOG_WARN("网络：esp_wifi_init 失败（{}）", esp_err_to_name(err));
    return Error::io_failure;
  }

  // 凭据只存内存：驱动别替我们记 AP —— 那是 App 的事（IPersistence 两个槽）。
  (void)esp_wifi_set_storage(WIFI_STORAGE_RAM);

  err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            &NetworkEventBridge::wifi, this, nullptr);
  if (err != ESP_OK) {
    ELOG_WARN("网络：注册 WIFI_EVENT 失败（{}）", esp_err_to_name(err));
    return Error::io_failure;
  }
  err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            &NetworkEventBridge::ip, this, nullptr);
  if (err != ESP_OK) {
    ELOG_WARN("网络：注册 IP_EVENT 失败（{}）", esp_err_to_name(err));
    return Error::io_failure;
  }

  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) {
    ELOG_WARN("网络：设 STA 模式失败（{}）", esp_err_to_name(err));
    return Error::io_failure;
  }
  err = esp_wifi_start();
  if (err != ESP_OK) {
    ELOG_WARN("网络：esp_wifi_start 失败（{}）", esp_err_to_name(err));
    return Error::io_failure;
  }

  ELOG_INFO("网络：WiFi 就绪（STA 模式；凭据只存内存，不落 NVS）");
  return Error::none;
}

Esp32Network::NetState Esp32Network::state() const noexcept { return state_; }

int Esp32Network::last_http_status() const noexcept { return http_status_; }

void Esp32Network::on_wifi_event(std::int32_t event_id) noexcept {
  if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
    // 只有「正在拨号」或「已经连着」时才把状态翻成 failed：主动 disconnect() 自己也会
    // 触发这条事件，那时状态已经是 off，不能又翻回 failed。
    if (state_ == NetState::connecting || state_ == NetState::connected) {
      state_ = NetState::failed;
      ip_[0] = '\0';
      xEventGroupSetBits(events_, kFailedBit);
    }
  }
  // WIFI_EVENT_STA_CONNECTED（链路起来但还没 IP）不改状态：真正的「能用」以拿到 IP 为准。
}

void Esp32Network::on_got_ip(void* event_data) noexcept {
  if (event_data == nullptr) {
    return;
  }
  const auto* event = static_cast<const ip_event_got_ip_t*>(event_data);
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(&event->ip_info.ip.addr);
  // esp_ip4_addr_t 是小端存的：字节 0 是最低位的八位组。
  std::snprintf(ip_, sizeof(ip_), "%u.%u.%u.%u", static_cast<unsigned>(bytes[0]),
                static_cast<unsigned>(bytes[1]), static_cast<unsigned>(bytes[2]),
                static_cast<unsigned>(bytes[3]));
  state_ = NetState::connected;
  xEventGroupSetBits(events_, kConnectedBit);
  ELOG_INFO("网络：已连接 {}（IP {}）", ssid_, ip_);
}

Error Esp32Network::connect(const char* ssid, const char* password) noexcept {
  if (events_ == nullptr) {
    return Error::not_ready;
  }
  if (ssid == nullptr || *ssid == '\0') {
    return Error::invalid_argument;
  }

  // 先把上一次的链路断掉（幂等）：这样下面的「清位 → connecting」不会被自己触发的
  // DISCONNECTED 事件抢在前面改成 failed。
  (void)esp_wifi_disconnect();
  xEventGroupClearBits(events_, kConnectedBit | kFailedBit);
  ip_[0] = '\0';
  std::strncpy(ssid_, ssid, sizeof(ssid_) - 1);
  ssid_[sizeof(ssid_) - 1] = '\0';
  state_ = NetState::connecting;

  wifi_config_t config = {};
  std::strncpy(reinterpret_cast<char*>(config.sta.ssid), ssid, sizeof(config.sta.ssid) - 1);
  if (password != nullptr) {
    std::strncpy(reinterpret_cast<char*>(config.sta.password), password,
                 sizeof(config.sta.password) - 1);
  }
  // 不设 authmode 门槛：开放 AP 与 WPA2 都要能连（真机上的使用场景是「用户自己填的家里 AP」）。
  config.sta.threshold.authmode = WIFI_AUTH_OPEN;

  esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
  if (err != ESP_OK) {
    ELOG_WARN("网络：写 STA 配置失败（{}）", esp_err_to_name(err));
    state_ = NetState::failed;
    return Error::io_failure;
  }
  err = esp_wifi_connect();
  if (err != ESP_OK) {
    ELOG_WARN("网络：esp_wifi_connect 失败（{}）", esp_err_to_name(err));
    state_ = NetState::failed;
    return Error::io_failure;
  }

  const EventBits_t bits =
      xEventGroupWaitBits(events_, kConnectedBit | kFailedBit, pdTRUE, pdFALSE, kConnectTimeout);
  if ((bits & kConnectedBit) != 0U) {
    return Error::none;
  }

  // 超时与「密码错」在这一层不细分：调用方能做的事是一样的（让用户检查一遍再试）。
  ELOG_WARN("网络：拨号 {} 失败或超时（密码错 / AP 不在 / 信号差）", ssid_);
  state_ = NetState::failed;
  (void)esp_wifi_disconnect();
  return Error::timeout;
}

Error Esp32Network::disconnect() noexcept {
  if (events_ == nullptr) {
    return Error::none;
  }
  state_ = NetState::off;  // 先翻状态：这样下面那条 DISCONNECTED 事件不会把它改成 failed
  ip_[0] = '\0';
  ssid_[0] = '\0';
  (void)esp_wifi_disconnect();
  xEventGroupClearBits(events_, kConnectedBit | kFailedBit);
  return Error::none;
}

Error Esp32Network::http_get(const char* url, etl::span<char> body_out,
                             std::size_t& body_len) noexcept {
  body_len = 0;
  http_status_ = 0;

  if (state_ != NetState::connected) {
    return Error::not_ready;
  }
  if (url == nullptr || *url == '\0') {
    return Error::invalid_argument;
  }
  if (!starts_with_ci(url, "http://")) {
    return Error::unsupported;  // https / ws 同上：这一层不做 TLS
  }

  esp_http_client_config_t config = {};
  config.url = url;
  config.method = HTTP_METHOD_GET;
  config.timeout_ms = kHttpTimeoutMs;
  config.buffer_size = 1024;
  config.buffer_size_tx = 512;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    return Error::io_failure;
  }

  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    ELOG_WARN("网络：连不上 {}（{}）", url, esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return Error::io_failure;
  }
  if (esp_http_client_fetch_headers(client) < 0) {
    ELOG_WARN("网络：{} 的响应头收不下来", url);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return Error::io_failure;
  }

  // 4xx/5xx **不是** Error（见 hal/network.h）：调用方要能读到错误页正文与状态码。
  http_status_ = esp_http_client_get_status_code(client);

  std::size_t used = 0;
  bool overflow = false;
  char chunk[256];
  while (true) {
    const int got = esp_http_client_read(client, chunk, static_cast<int>(sizeof(chunk)));
    if (got < 0) {
      ELOG_WARN("网络：读 {} 的响应体失败（已收 {} 字节）", url, used);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return Error::io_failure;
    }
    if (got == 0) {
      break;
    }
    const auto count = static_cast<std::size_t>(got);
    if (used + count > body_out.size()) {
      overflow = true;
      break;
    }
    std::memcpy(body_out.data() + used, chunk, count);
    used += count;
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (overflow) {
    ELOG_WARN("网络：响应体超过调用方缓冲 {} 字节（no_space）", body_out.size());
    return Error::no_space;
  }
  body_len = used;
  return Error::none;
}

Error Esp32Network::sntp_sync(std::uint32_t timeout_ms) noexcept {
  if (state_ != NetState::connected) {
    return Error::not_ready;
  }
  if (timeout_ms == 0U) {
    timeout_ms = 5000U;  // 0 当「用默认」：真机上没人真想立刻超时
  }

  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  esp_err_t err = esp_netif_sntp_init(&config);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ELOG_WARN("网络：SNTP 起不来（{}）", esp_err_to_name(err));
    return Error::io_failure;
  }
  err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms));
  esp_netif_sntp_deinit();
  if (err != ESP_OK) {
    ELOG_WARN("网络：对时超时（{} ms）—— 时间保持原样", timeout_ms);
    return Error::timeout;
  }
  ELOG_INFO("网络：对时完成（系统时间已按 SNTP 校正）");
  return Error::none;
}

}  // namespace embark::platform::esp32