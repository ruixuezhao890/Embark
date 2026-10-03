/**
 * 宿主 · 系统后端（issues/04）
 *
 * 这里同时定义两个全局符号：embark::assert_failed 与 embark::fatal（spec §9 把它们交给
 * 平台 target 实现，链接期唯一）。两条都直接写 stderr 而不是走日志框架 —— 它们是"最后一
 * 条日志"，此时日志注册表可能已经不可用了（甚至是坏在日志后端里）。写完 abort()。
 *
 * 宿主没有可重启的对象：进程就是它的全部，所以 restart() 也走 abort（退出码非零），
 * 由启动它的人决定要不要再来一次。
 */
#ifndef EMBARK_PLATFORM_HOST_SYSTEM_H
#define EMBARK_PLATFORM_HOST_SYSTEM_H

#include <cstddef>

#include <embark/hal/system.h>

namespace embark::platform::host {

class HostSystem final : public hal::ISystem {
 public:
  void restart() noexcept override;

  [[nodiscard]] std::size_t free_heap_bytes() const noexcept override;

  [[nodiscard]] std::size_t min_free_heap_bytes() const noexcept override;

  [[nodiscard]] std::size_t stack_high_water_bytes() const noexcept override;

  void feed_watchdog() noexcept override;

  void fatal(const char* reason) noexcept override;
};

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_SYSTEM_H */
