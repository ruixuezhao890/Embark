#ifndef EMBARK_PLATFORM_HOST_HOST_NETWORK_H
#define EMBARK_PLATFORM_HOST_HOST_NETWORK_H

/**
 * 宿主 · 网络服务（issue 10）
 *
 * 直通宿主 socket（Windows = WinSock，Linux/macOS = BSD socket），只说明文 HTTP。
 * `connect()` 恒成功、直接进 `connected`：宿主上「连哪个 AP」没有意义，但**状态机必须
 * 与真机同形** —— App 里 `state() == connected` 这条判断两边都要跑得到，否则宿主验不出
 * 真机路径。
 *
 * `sntp_sync()` 返回 `unsupported`：宿主本来就有真实时钟，对时是设备上的问题。
 *
 * 阻塞语义与真机一致：`http_get` 会卡到响应收完或超时（默认各阶段 5 s）。
 */
#include <cstddef>
#include <cstdint>
#include <string>

#include <embark/hal/network.h>

namespace embark::platform::host {

class HostNetwork final : public hal::INetwork {
 public:
  /// 状态枚举在 hal 里（INetwork::state 的返回类型）；类内起个别名，成员与实现里才好写。
  using NetState = hal::NetState;

  HostNetwork() = default;
  ~HostNetwork() override;

  HostNetwork(const HostNetwork&) = delete;
  HostNetwork& operator=(const HostNetwork&) = delete;

  Error init() noexcept override;
  [[nodiscard]] NetState state() const noexcept override;
  Error connect(const char* ssid, const char* password) noexcept override;
  Error disconnect() noexcept override;
  Error http_get(const char* url, etl::span<char> body_out, std::size_t& body_len) noexcept override;
  [[nodiscard]] int last_http_status() const noexcept override;
  Error sntp_sync(std::uint32_t timeout_ms) noexcept override;

  /// 当前「连着」的 SSID（宿主观测用，启动日志会打一行）。没连时是空串。
  [[nodiscard]] const std::string& ssid() const noexcept;

 private:
  bool stack_ready_ = false;
  NetState state_ = NetState::off;
  std::string ssid_;
  int http_status_ = 0;
};

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_HOST_NETWORK_H */
