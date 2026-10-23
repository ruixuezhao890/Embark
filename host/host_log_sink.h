/**
 * 宿主 · 日志后端（issues/04）
 *
 * 写 stderr 而不是 stdout：宿主程序的 stdout 可能被当成"程序的输出"被解析，日志混进去
 * 就麻烦了。Windows 上 stderr 是无缓冲的，日志能立刻看到（issue 02 踩过 stdout 块缓冲
 * 导致进程被杀后日志全空的坑）。
 */
#ifndef EMBARK_PLATFORM_HOST_LOG_SINK_H
#define EMBARK_PLATFORM_HOST_LOG_SINK_H

#include <cstdio>

#include <embark/hal/log_sink.h>

namespace embark::platform::host {

class HostLogSink final : public hal::ILogSink {
 public:
  [[nodiscard]] bool init() noexcept override;

  void write(const char* data, std::size_t size) noexcept override;

  void flush() noexcept override;
};

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_LOG_SINK_H */
