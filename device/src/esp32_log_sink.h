/**
 * Embark ESP32-S3 · 日志后端（issues/11）
 *
 * spec §8：日志后端只管把**一整条已格式化记录**送出去（格式化由 elog 完成，
 * 攒整行与串行化由 embark::LogSinkBinder 完成）。真机的出口就是 IDF 控制台
 * （UART0，115200），所以这里直接写 stdout —— IDF 把 stdout 接到了 UART 上，
 * 不需要我们自己装 VFS 驱动。
 *
 * 两条纪律：
 *   - 不阻塞太久：写一条 + fflush 就走（串口 115200 下 200 字节约 17 ms，
 *     这是日志本该付的代价；框架自己不会对日志做重试或排队）；
 *   - 不许在这里再打日志（会递归）。
 */
#ifndef EMBARK_PLATFORM_ESP32_LOG_SINK_H
#define EMBARK_PLATFORM_ESP32_LOG_SINK_H

#include <cstddef>

#include <embark/hal/log_sink.h>

namespace embark::platform::esp32 {

class Esp32LogSink final : public hal::ILogSink {
 public:
  Esp32LogSink() noexcept = default;
  Esp32LogSink(const Esp32LogSink&) = delete;
  Esp32LogSink& operator=(const Esp32LogSink&) = delete;
  ~Esp32LogSink() override = default;

  /// 幂等。真机的控制台是 IDF 装好的，这里没有失败路径。
  bool init() noexcept override;

  void write(const char* data, std::size_t size) noexcept override;
  void flush() noexcept override;
};

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_LOG_SINK_H */
