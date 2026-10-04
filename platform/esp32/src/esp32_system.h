/**
 * Embark ESP32-S3 · 系统控制后端（issues/11 + spec §9/§8）
 *
 * 真机这一组是"能拿到真实数字"的一侧：空闲堆走 heap_caps、栈高水位走 FreeRTOS、
 * 喂狗走任务看门狗（TWDT）、致命处理走 abort() —— 在 IDF 上 abort() 会进 panic 处理，
 * 打印回溯与寄存器现场，比直接 esp_restart() 有用得多（想看堆栈就别急着重启）。
 */
#ifndef EMBARK_PLATFORM_ESP32_SYSTEM_H
#define EMBARK_PLATFORM_ESP32_SYSTEM_H

#include <cstddef>

// Error 必须自己带进来：hal/system.h 只声明能力，不传递 error.h（宿主那边是靠别的
// 头文件间接进来的，真机的编译单元少，露出来了）。
#include <embark/error.h>
#include <embark/hal/system.h>

namespace embark::platform::esp32 {

class Esp32System final : public hal::ISystem {
 public:
  Esp32System() noexcept = default;
  Esp32System(const Esp32System&) = delete;
  Esp32System& operator=(const Esp32System&) = delete;
  ~Esp32System() override = default;

  /// 幂等。看门狗由 IDF 自己启动（CONFIG_ESP_TASK_WDT_INIT），这里无事可做。
  [[nodiscard]] Error init() noexcept;

  /// 立即重启（esp_restart，不返回）。
  void restart() noexcept override;

  /// 内部 SRAM 的空闲量（不含 PSRAM：PSRAM 是 8 MB，报它没有意义，
  /// 会掩盖真正的 SRAM 压力）。
  [[nodiscard]] std::size_t free_heap_bytes() const noexcept override;
  [[nodiscard]] std::size_t min_free_heap_bytes() const noexcept override;

  /// 当前任务的栈高水位（字节 = 剩余最小字深 × sizeof(StackType_t)）。
  [[nodiscard]] std::size_t stack_high_water_bytes() const noexcept override;

  /// 喂任务看门狗。没被 TWDT 订阅的任务调用它是无副作用的（返回 NOT_FOUND），
  /// 所以这里忽略返回码。
  void feed_watchdog() noexcept override;

  /// 平台级致命处理：写 stderr → abort（进 panic，打印回溯）。
  void fatal(const char* reason) noexcept override;
};

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_SYSTEM_H */
