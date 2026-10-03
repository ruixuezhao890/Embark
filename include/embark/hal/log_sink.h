/**
 * HAL · 日志后端（spec §7、§8）
 *
 * 上游 elog 的 sink 是一个"写字节"的函数指针（elog.hpp:85），不负责格式化 —— 格式化
 * 在 elog 内部做完，最后一整条记录才交到这里。所以后端只管把字节送出去：
 * 真机送 UART / RTT，宿主送 stderr 或文件。
 */
#ifndef EMBARK_HAL_LOG_SINK_H
#define EMBARK_HAL_LOG_SINK_H

#include <cstddef>

namespace embark::hal {

class ILogSink {
 public:
  virtual ~ILogSink() = default;

  /// 打开后端。日志后端失败不致命：返回 false 之后框架仍可跑（丢掉日志）。
  virtual bool init() noexcept = 0;

  /// 写一整条已格式化记录。实现不许阻塞太久、不许在里面再打日志。
  virtual void write(const char* data, std::size_t size) noexcept = 0;

  /// 立刻落盘（宿主文件后端用；无缓冲后端空实现）。
  virtual void flush() noexcept = 0;
};

}  // namespace embark::hal

#endif /* EMBARK_HAL_LOG_SINK_H */
