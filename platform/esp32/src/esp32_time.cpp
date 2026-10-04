/**
 * Embark ESP32-S3 · 时间后端实现（issues/11）
 */
#include "esp32_time.h"

#include <esp_timer.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace embark::platform::esp32 {

Error Esp32Time::init() noexcept {
  // esp_timer 的微秒计数器在 IDF 启动阶段就已经开始跑了，这里无事可做。
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
  // 板上 RTC（PCF85063）还没接，如实上报"量不了"，不编一个假时间出来。
  return embark::unexpected(Error::unsupported);
}

}  // namespace embark::platform::esp32
