/**
 * Embark · 定容消息队列（issues/07）
 *
 * spec §7 的跨任务收件箱：单生产者单消费者的非阻塞队列（own task → UI）。
 * 满时覆盖最旧并计数 + WARN —— 与"宁可丢消息，不阻塞 UI 任务"同一条纪律
 * （LVGL 输入中转队列也用同样策略）。
 *
 * 锁：默认 etl::mutex（平台提供 FreeRTOS 时 = mutex_freertos.h 的互斥量）。
 * 单生产单消费下锁只防"两个任务同时碰同一个队列"（UI 任务 + 自己的 own task），
 * 不解决广播的串行化 —— 那是日志层的事（log.h）。
 *
 * 模板参数 TMutex 可换（测试用无锁替身时传空锁类型）。
 */
#ifndef EMBARK_MESSAGE_QUEUE_H
#define EMBARK_MESSAGE_QUEUE_H

#include <cstddef>
#include <cstdint>

#include <middleware/etl/circular_buffer.h>
#include <middleware/etl/mutex.h>
#include <middleware/elog/elog.hpp>

namespace embark {

template <typename T, std::size_t MaxSize, typename TMutex = etl::mutex>
class MessageQueue {
 public:
  /// 入队。满时覆盖最旧（circular_buffer 语义）并 WARN + 计数。绝不阻塞。
  void push(const T& item) noexcept {
    etl::lock_guard<TMutex> lock(mutex_);
    if (buffer_.full()) {
      ++overflows_;
      ELOG_WARN("消息队列满：丢弃最旧一条（累计溢出 {} 次）", overflows_);
    }
    buffer_.push(item);
  }

  /// 入队（右值版）。语义同上。
  void push(T&& item) noexcept {
    etl::lock_guard<TMutex> lock(mutex_);
    if (buffer_.full()) {
      ++overflows_;
      ELOG_WARN("消息队列满：丢弃最旧一条（累计溢出 {} 次）", overflows_);
    }
    buffer_.push(etl::move(item));
  }

  /// 出队到 out。空返回 false（不修改 out）。绝不阻塞。
  [[nodiscard]] bool pop(T& out) noexcept {
    etl::lock_guard<TMutex> lock(mutex_);
    if (buffer_.empty()) {
      return false;
    }
    out = buffer_.front();
    buffer_.pop();
    return true;
  }

  [[nodiscard]] bool empty() const noexcept {
    etl::lock_guard<TMutex> lock(mutex_);
    return buffer_.empty();
  }

  [[nodiscard]] bool full() const noexcept {
    etl::lock_guard<TMutex> lock(mutex_);
    return buffer_.full();
  }

  [[nodiscard]] std::size_t size() const noexcept {
    etl::lock_guard<TMutex> lock(mutex_);
    return buffer_.size();
  }

  /// 累计溢出次数（观测；不加锁读，仅当观测路径与 UI 任务同线程时可靠）。
  [[nodiscard]] std::uint32_t overflows() const noexcept { return overflows_; }

  /// 清零溢出计数（观测打点用）。
  void reset_overflows() noexcept { overflows_ = 0; }

 private:
  etl::circular_buffer<T, MaxSize> buffer_;
  mutable TMutex mutex_;
  std::uint32_t overflows_ = 0;
};

}  // namespace embark

#endif /* EMBARK_MESSAGE_QUEUE_H */