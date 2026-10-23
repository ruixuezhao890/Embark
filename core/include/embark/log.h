/**
 * 日志装配（spec §7）
 *
 * 上游 elog 的 sink 就是"一对函数指针 + user_data"（elog.hpp:85 的 write_fn），
 * 不带任何锁；而 spec §7 要求框架给出串行化层。这里就是那一层：
 *
 *   elog（格式化 + 级别判断）
 *     └── LogSinkBinder::write_thunk（攒成整行 + 可选加锁）
 *           └── hal::ILogSink（平台后端：UART / stderr / 文件）
 *
 * 为什么要"攒成整行"：elog 的一条记录会分几次调用 sink —— 带颜色时是
 * [颜色转义] [前缀+正文] [复位转义] 三次（elog.hpp:222 write_with_style），之后
 * 再单独写一个 '\n'（elog.hpp:224）。如果每次调用各自加锁/各自下发，两个任务同时打日志
 * 就会把两行切成四段交错。所以串行化必须按"整行"为单位：这里先按 '\n' 攒行，
 * 整行齐了才加锁并一次性交给后端（每个后端因此也只被打扰一次）。
 *
 * 中断上下文不许打日志（spec §7），所以这把锁永远不会在中断里被取 —— 它只是挡住两个
 * 任务同时往同一个 UART 里塞字节。EMBARK_LOG_SERIALIZE = 1 时用 etl::mutex（需要平台
 * 已经提供 FreeRTOS 头文件），宿主默认 0（单线程产出日志，省一次原子操作）。
 */
#ifndef EMBARK_LOG_H
#define EMBARK_LOG_H

#include <cstddef>

#include <embark/hal/log_sink.h>
#include <middleware/etl/array.h>
#include <middleware/elog/elog.hpp>

#if EMBARK_LOG_SERIALIZE
#include <middleware/etl/mutex.h>
#endif

namespace embark {

/// sink 与 elog 之间的桥：把 hal::ILogSink 交给 elog，按整行串行化写入。
class LogSinkBinder {
 public:
  explicit LogSinkBinder(hal::ILogSink& sink) noexcept : sink_(&sink) {}

  LogSinkBinder(const LogSinkBinder&) = delete;
  LogSinkBinder& operator=(const LogSinkBinder&) = delete;

  /// 造一个指向本对象的 elog sink。binder 必须活得比 logger 久。
  [[nodiscard]] e_log::sink make_sink() noexcept {
    return e_log::sink::from_callback(&LogSinkBinder::write_thunk, this);
  }

  /// 已经整行交付出去的记录数（含被后端丢掉的）。
  [[nodiscard]] std::size_t lines_written() const noexcept { return lines_; }

  /// 已经交付给后端的字节数。
  [[nodiscard]] std::size_t bytes_written() const noexcept { return bytes_; }

  /// 行缓冲里还有几个字节没凑成整行（正常运行时只会在"最后一条日志没换行"时非 0）。
  [[nodiscard]] std::size_t pending_bytes() const noexcept { return pending_length_; }

 private:
  static bool write_thunk(const char* data, std::size_t size, void* user_data) noexcept;

  void append(const char* data, std::size_t size) noexcept;
  void emit(const char* data, std::size_t size) noexcept;

  hal::ILogSink* sink_;
  // 一整行的上限 = elog 的单条记录上限 + 颜色转义与收尾的余量。
  etl::array<char, e_log::config::max_record_size + 16U> pending_{};
  std::size_t pending_length_ = 0;
  std::size_t lines_ = 0;
  std::size_t bytes_ = 0;
#if EMBARK_LOG_SERIALIZE
  mutable etl::mutex mutex_;
#endif
};

/// 注册一个 logger 并把它的输出接到 binder 的 sink 上。
/// 失败返回 nullptr（名字非法 / sink 无效 / 重名 / 注册表满 —— elog 的四种情况），
/// 不是致命错误：没有日志框架照样能跑。
[[nodiscard]] e_log::logger* install_logger(const char* name, LogSinkBinder& binder,
                                            e_log::level level = e_log::level::debug) noexcept;

}  // namespace embark

#endif /* EMBARK_LOG_H */
