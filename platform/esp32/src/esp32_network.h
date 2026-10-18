/**
 * @file esp32_network.h
 * @brief 真机 · 网络服务（issue 10）：esp_wifi STA + esp_http_client + SNTP
 *
 * 与宿主（platform/host/host_network.h）同形：同一个 `INetwork` 接口、同一套状态机。
 * 差异只在能不能真的拨号、以及 http_get 底下是谁在发包：
 *   - 宿主：直连 socket，connect 恒成功（没有对端可拨）。
 *   - 真机：esp_wifi 真拨号，靠事件回调把「拿到 IP / 失败」翻成状态位。
 *
 * ## 线程纪律（与 hal::INetwork 头注释同一条）
 * `connect` / `http_get` / `sntp_sync` 都是**阻塞**的：必须跑在 App 自己
 * `BackgroundPolicy::own_task` 的任务里，结果用 `Framework::post()` 回 UI 线程。
 * 本类内部不加锁 —— 它的调用方已经保证单线程使用（一次只跑一个网络操作）。
 *
 * ## 凭据不落 NVS
 * `esp_wifi_set_storage(WIFI_STORAGE_RAM)`：驱动不替我们记住 AP。记住上次连的是谁
 * 是 App 的事（写进 IPersistence 的两个槽：SSID 一槽、口令一槽，见 network.h 头注释）。
 */
#ifndef EMBARK_PLATFORM_ESP32_NETWORK_H
#define EMBARK_PLATFORM_ESP32_NETWORK_H

#include <cstddef>
#include <cstdint>

#include <embark/hal/network.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

namespace embark::platform::esp32 {

class Esp32Network final : public hal::INetwork {
 public:
  /// 状态枚举在 hal 里（INetwork::state 的返回类型）；类内起个别名，成员与实现里才好写。
  using NetState = hal::NetState;

  Esp32Network() = default;
  ~Esp32Network() override;

  Esp32Network(const Esp32Network&) = delete;
  Esp32Network& operator=(const Esp32Network&) = delete;

  Error init() noexcept override;
  [[nodiscard]] NetState state() const noexcept override;
  Error connect(const char* ssid, const char* password) noexcept override;
  Error disconnect() noexcept override;
  Error http_get(const char* url, etl::span<char> body_out, std::size_t& body_len) noexcept override;
  [[nodiscard]] int last_http_status() const noexcept override;
  Error sntp_sync(std::uint32_t timeout_ms) noexcept override;

  /// 协议栈是否起来了（init 成功）。没起来时所有操作返回 not_ready。
  [[nodiscard]] bool ready() const noexcept { return events_ != nullptr; }

  /// 当前 IP（点分十进制；没连上是空串）。启动日志与 --net-test 会打它。
  [[nodiscard]] const char* ip_address() const noexcept { return ip_; }

 private:
  friend struct NetworkEventBridge;

  /// 事件回调的落点（真正的注册在 .cpp 里，好让 esp_event 的类型不出本文件）。
  void on_wifi_event(std::int32_t event_id) noexcept;
  void on_got_ip(void* event_data) noexcept;

  EventGroupHandle_t events_ = nullptr;  // 位 0 = 拿到 IP，位 1 = 失败
  void* netif_ = nullptr;  // esp_netif_t*（IDF 头不进本文件，同 esp32_audio 的 i2s 句柄）
  NetState state_ = NetState::off;
  char ssid_[33] = {};
  char ip_[16] = {};
  int http_status_ = 0;
};

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_NETWORK_H */