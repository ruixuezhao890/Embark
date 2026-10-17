/**
 * Embark ESP32-S3 · 时间后端实现（issues/11）
 */
#include "esp32_time.h"

#include <cstddef>
#include <cstdint>

#include <esp_timer.h>

#include <middleware/elog/elog.hpp>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp32_board.h"

// 固件编译时刻（UTC 秒），由 platform/esp32/project/components/embark/CMakeLists.txt 在
// cmake 配置阶段注入。没有它就不播种（宁可如实报 unsupported，也不拿 0 去覆盖芯片）。
#ifndef EMBARK_BUILD_EPOCH
#define EMBARK_BUILD_EPOCH 0
#endif

namespace embark::platform::esp32 {
namespace {

constexpr std::uint64_t kBuildEpochSeconds = static_cast<std::uint64_t>(EMBARK_BUILD_EPOCH);

}  // namespace

Error Esp32Time::init(hal::IBus* bus) noexcept {
  // esp_timer 的微秒计数器在 IDF 启动阶段就已经开始跑了，这里不用管它。
  if (bus == nullptr) {
    wall_note_ = "没有板载总线";
    return Error::none;
  }

  const Error rtc_error = rtc_.init(bus);
  if (rtc_error != Error::none) {
    // 芯片没应答：不是致命错误（契约见 include/embark/hal/time.h）。
    wall_note_ = "0x51 无应答";
    return Error::none;
  }

  const auto epoch_seconds = rtc_.read_epoch_seconds();
  if (!epoch_seconds.has_value()) {
    wall_note_ = (epoch_seconds.error() == Error::unsupported) ? "时钟停过（OS 位=1）"
                                                              : "时钟寄存器不可用";
    // 芯片在、但不知道自己几点：这时才有"播种"这回事（芯片不在就没有对象可写）。
    seed_from_build_time();
    if (!wall_ready_) {
      ELOG_WARN("墙钟：RTC 在线但时间不可用（{}）—— clock App 会显示占位文案", wall_note_);
    }
    return Error::none;
  }

  base_epoch_ms_ = epoch_seconds.value() * 1000ULL;
  base_now_ms_ = now_ms();
  wall_ready_ = true;
  wall_note_ = "已对时";
  // 打印的秒数就是 UTC epoch：出问题时能直接和 PC 的 date -u +%s 对。
  ELOG_INFO("墙钟：RTC 已对时，UTC epoch {} 秒（{} ms），之后用 tick 推算", epoch_seconds.value(),
            base_epoch_ms_);
  return Error::none;
}

std::uint32_t Esp32Time::now_ms() const noexcept {
  // 从"芯片启动"起算的单调毫秒。与宿主的语义差别（宿主从后端初始化起算）不影响任何
  // 上层判断：框架里所有比较都是"两次 now_ms() 的差"。
  return static_cast<std::uint32_t>(esp_timer_get_time() / 1000LL);
}

void Esp32Time::delay_ms(std::uint32_t ms) noexcept {
  // 100 Hz 配置下 pdMS_TO_TICKS(5) 会算成 0，而 vTaskDelay(0) 只是让出一次调度、
  // 不会真的等 —— 那会让 UI 循环变成忙等。这里兜底成 1 tick。
  // （工程侧同时用 CONFIG_FREERTOS_HZ=1000 把粒度做成 1 ms，见 sdkconfig.defaults。）
  TickType_t ticks = pdMS_TO_TICKS(ms);
  if (ticks == 0 && ms > 0) {
    ticks = 1;
  }
  vTaskDelay(ticks);
}

etl::expected<std::uint64_t, Error> Esp32Time::epoch_ms() const noexcept {
  if (!wall_ready_) {
    // 没有 RTC / 没对时：如实上报"量不了"，不编一个假时间出来。
    return embark::unexpected(Error::unsupported);
  }
  // 无符号减法的回绕是安全的：now_ms() 是 32 位，两个值的差在 49 天周期内始终正确。
  const std::uint64_t elapsed_ms = static_cast<std::uint64_t>(now_ms() - base_now_ms_);
  return base_epoch_ms_ + elapsed_ms;
}

void Esp32Time::seed_from_build_time() noexcept {
  if (!rtc_.present()) {
    return;  // 芯片都没应答，没有可写的对象；wall_note_ 保持 init 里那句"0x51 无应答"。
  }
  if (!rtc_seed_from_build_time) {
    wall_note_ = "时钟停过，播种被板级开关关掉了";
    return;
  }
  if (kBuildEpochSeconds == 0U) {
    wall_note_ = "时钟停过（OS 位=1），且没有可用的编译时间";
    return;
  }

  const Error write_error = rtc_.write_epoch_seconds(kBuildEpochSeconds);
  if (write_error != Error::none) {
    wall_note_ = "时钟停过，播种失败";
    return;
  }

  // 写完立刻按"正常路径"再读一次：读出来的才是芯片真认下的时间（也顺手证明 OS 位已清）。
  const auto seeded = rtc_.read_epoch_seconds();
  if (!seeded.has_value()) {
    wall_note_ = "播种后仍读不出时间";
    return;
  }

  base_epoch_ms_ = seeded.value() * 1000ULL;
  base_now_ms_ = now_ms();
  wall_ready_ = true;
  wall_note_ = "已用编译时间播种";

  char text[24] = {};
  format_utc_epoch(seeded.value(), text, sizeof(text));
  // WARN 而不是 INFO：这条日志是在说"这个时间不权威"，出问题时最容易怀疑它。
  ELOG_WARN(
      "墙钟：RTC 报振荡器停过，已用固件编译时刻播种 {}（epoch {} 秒）—— 近似值，误差 = 编译到"
      "上电那段；没有纽扣电池时每次开机都会重播一次",
      text, seeded.value());
}

}  // namespace embark::platform::esp32
