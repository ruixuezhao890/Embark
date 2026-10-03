/**
 * HAL · 系统控制（spec §8）
 *
 * 这一组里"能失败"的操作很少 —— 重启和进安全态根本没有返回值可言，所以它们的失败路径
 * 走的是 fatal 上报（诊断），而不是 Error 码。这是 issue 04 里明确记下的一条：
 * 【系统类能力没有 Error 语义的操作，其失败路径用 fatal 上报覆盖】。
 */
#ifndef EMBARK_HAL_SYSTEM_H
#define EMBARK_HAL_SYSTEM_H

#include <cstddef>
#include <cstdint>

namespace embark::hal {

class ISystem {
 public:
  virtual ~ISystem() = default;

  /// 立即重启。
  /// 故意不加 [[noreturn]]：真机后端不返回，但测试替身要"记录一次重启然后返回"，
  /// 否则一条断言都写不了（自由函数 embark::fatal 才是 [[noreturn]]）。
  virtual void restart() noexcept = 0;

  /// 当前空闲堆。宿主没有 FreeRTOS 堆就返回 0（0 表示"这个后端量不了"）。
  [[nodiscard]] virtual std::size_t free_heap_bytes() const noexcept = 0;

  /// 历史最小空闲堆（判断有没有泄漏用）。
  [[nodiscard]] virtual std::size_t min_free_heap_bytes() const noexcept = 0;

  /// 当前任务的栈高水位（字节）。量不了就返回 0。
  [[nodiscard]] virtual std::size_t stack_high_water_bytes() const noexcept = 0;

  /// 喂看门狗。不需要看门狗的后端空实现。
  virtual void feed_watchdog() noexcept = 0;

  /// 平台级的致命处理：写日志 → 重启或进安全态。
  /// 同 restart()，虚拟函数不加 [[noreturn]]；自由函数 embark::fatal 是 [[noreturn]]。
  virtual void fatal(const char* reason) noexcept = 0;
};

}  // namespace embark::hal

#endif /* EMBARK_HAL_SYSTEM_H */
