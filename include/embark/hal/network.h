/**
 * HAL · 网络服务（ADR 0019 的公共资源，issue 10）
 *
 * 一句话：**连上一个 AP，发一个 HTTP GET，再把墙钟对一次**。天气、OTA、对时都建立在
 * 这三件事上；「什么时候去拉数据」「拉到之后怎么解析」「断网了要不要重试」全归 App
 * （ADR 0019 决策 3：业务服务不进基建）。
 *
 * ## 范围：今天只说明文 HTTP
 *
 * `http_get("http://…")` 可以，`https://` 一律返回 `unsupported`。这不是懒：宿主侧的
 * TLS 需要引入一份实现（OpenSSL/mbedTLS），那是一次独立的依赖决策；而「真机能上 HTTPS、
 * 宿主不能」的**不对称能力**比「两边都只支持 HTTP」更糟 —— App 在宿主上验不出真机的行为。
 * 要 HTTPS 就两边一起做，另开一票。
 *
 * ## 线程纪律（这条决定它怎么用）
 *
 * `http_get` 与 `sntp_sync` 都是**阻塞**的：DNS + TCP + 几 KB 响应，几十毫秒到几秒。
 * `onBackgroundTick` 跑在唯一的 UI 任务循环里（include/embark/framework.h），在那里调它
 * 就是整屏卡住。正确用法只有一种：
 *
 *   1. App 声明自己的后台任务是独立任务（BackgroundPolicy::own_task）；
 *   2. 在那个任务的入口里调 `http_get`，把结果存进自己的成员；
 *   3. 用 `Framework::post()` 把「数据到了」这条消息发回 UI 线程去刷控件。
 *
 * ## 为什么 body 写进调用方给的 span
 *
 * 本平台的内核与 App 是零堆的（spec §10）：服务层不替调用方 malloc 一块「返回给你的
 * 响应体」，那样调用方还得管释放。所以形状是「你给缓冲，我填满并告诉你多长」——
 * 缓冲不够就返回 `no_space`，一个字节都不写。响应头不进 body，只看 `last_http_status()`。
 *
 * ## 凭据不归这一层管
 *
 * `connect(ssid, password)` 只负责拨号，**不负责记住它们**。「上次连的哪个 AP」是 App 的
 * 事，用 `IPersistence` 存两格：`persistence_max_value_bytes` 是 64，而 WiFi 密码最长 63
 * 字符 —— 密码单独一格刚好装得下（不需要结尾 NUL，`IPersistence` 是按长度读写的），
 * SSID（≤32）另存一格。把两者挤进一格会正好卡在上限（见 issue 10 的容量决策 (b)）。
 */
#ifndef EMBARK_HAL_NETWORK_H
#define EMBARK_HAL_NETWORK_H

#include <cstddef>
#include <cstdint>

#include <embark/error.h>
#include <middleware/etl/span.h>

namespace embark::hal {

/**
 * 连接状态。`off` = 没拨号（初始 / disconnect 之后）；`connecting` = 拨号中；
 * `connected` = 拿到地址、可以发请求；`failed` = 拨号失败（密码错 / AP 不在 / 超时）。
 *
 * `failed` 之后再 `connect()` 是允许的（重试），不需要先 `disconnect()`。
 */
enum class NetState : std::uint8_t { off, connecting, connected, failed };

class INetwork {
 public:
  virtual ~INetwork() = default;

  /**
   * 打开协议栈（宿主 = 初始化 socket 层，真机 = 初始化 netif + WiFi 驱动）。幂等。
   *
   * 失败语义：unsupported = 这个平台没有网络能力（后端没编进来）；io_failure = 协议栈
   * 起不来。调用方**不该**把这里的失败当致命错误 —— 没有网络的板子照样要能跑，
   * 接口在这种情况下所有 `connect()` 都返回 `not_ready`。
   */
  virtual Error init() noexcept = 0;

  [[nodiscard]] virtual NetState state() const noexcept = 0;

  /**
   * 连一个 AP。**阻塞到拿到地址或失败**（真机上典型 1~5 秒）。
   *
   * 失败语义：not_ready = `init()` 没成过；invalid_argument = ssid 为空；timeout = 在
   * 后端自己的超时里没连上（密码错、AP 不在、信号太差都走这条，接口不细分）。
   * 已经 connected 时再连另一个 AP：先断开再连（不报错）。
   */
  virtual Error connect(const char* ssid, const char* password) noexcept = 0;

  /// 断开并回到 `off`（幂等：本来就没连也算成功）。
  virtual Error disconnect() noexcept = 0;

  /**
   * 发一个 **明文 HTTP** GET，把响应体写进 `body_out`，实际字节数写进 `body_len`。
   *
   * 失败语义：not_ready = 没连上（**不自动拨号**，调用方自己决定用什么凭据）；
   * unsupported = ws://https:// 之类本层不认的 URL；invalid_argument = url 为空或太长；
   * not_found = DNS 解析不出来；timeout = 连上但对端没响应；io_failure = 传输中断；
   * no_space = 响应体比 `body_out` 大（**此时一个字节都没写**，`body_len` 置 0）。
   *
   * 注意：**HTTP 4xx/5xx 不是 Error** —— 那是「服务器收到了并回了一个状态码」，属于正常
   * 完成；看 `last_http_status()`。只有传输层的问题才是 Error。
   */
  virtual Error http_get(const char* url, etl::span<char> body_out,
                         std::size_t& body_len) noexcept = 0;

  /// 最近一次 `http_get` 的 HTTP 状态码（200 / 404 / 500 …）。还没发过请求时是 0。
  [[nodiscard]] virtual int last_http_status() const noexcept = 0;

  /**
   * 做一次 SNTP 对时，成功即把系统墙钟校准到网络时间（`ITime::epoch_ms()` 之后返回
   * 的是校准过的值）。**阻塞到拿到答案或超时**（通常 100~500 ms）。
   *
   * 失败语义：not_ready = 没连上；timeout = `timeout_ms` 内没收到应答；
   * unsupported = 这个后端不提供对时（宿主就是这种：它本来就有真实时钟）。
   */
  virtual Error sntp_sync(std::uint32_t timeout_ms) noexcept = 0;
};

}  // namespace embark::hal

#endif /* EMBARK_HAL_NETWORK_H */
