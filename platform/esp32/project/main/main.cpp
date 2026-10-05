/**
 * @file main.cpp
 * @brief 真机固件入口（ESP32-S3-Touch-LCD-2.8，issue 11）
 *
 * 与宿主入口（platform/host/ui_demo.cpp）同构：HAL 初始化、UI 端口、App 注册表、
 * 框架 boot、事件循环全在唯一 UI 任务里（spec §6）。差别只有三处：
 *   - 宿主有命令行参数与"关窗退出"，真机没有（没有退出请求，循环永不返回）；
 *   - 宿主自己起 FreeRTOS 调度器（start_scheduler），真机的调度器已经在跑；
 *   - app_main 返回后 IDF 会删掉 main 任务，此后唯一 UI 任务接管一切。
 */
#include <cstdint>
#include <cstdio>

#include <embark/app_registry.h>
#include <embark/diagnostics.h>
#include <embark/framework.h>
#include <embark/version.h>
#include <middleware/elog/elog.hpp>

#include "demo_apps.h"
#include "hello_app.h"

#include "esp32_hal.h"
#include "esp32_lvgl_mem.h"  // 静态池观测：容量 / 未回收 / 峰值（心跳日志要用）
#include "esp32_task_spawner.h"
#include "esp32_ui_task.h"
#include "lvgl_ui_port.h"

using embark::Error;
using embark::Framework;

namespace ep = embark::platform::esp32;

namespace embark::platform::esp32 {

// 整个固件只出现一次的 App 注册表（与宿主演示同一套 App）：ClockApp 默认前台、
// SettingsApp 待命、TickerApp 纯后台（own_task，issue 07 的消息回 UI 演示）、
// HelloApp 是最简模板（issue 12 的新手最短路径）。
EMBARK_APP_TABLE(embark::demo::ClockApp, embark::demo::SettingsApp, embark::demo::TickerApp,
                 embark::demo::HelloApp)

}  // namespace embark::platform::esp32

namespace {

/// 心跳周期：2000 帧 × 5 ms ≈ 10 s。真机没有宿主那样的验收钩子，串口日志就是观测手段 ——
/// 堆、UI 栈水位、LVGL 静态池、刷新次数、触摸次数这几项周期性打出来（bring-up 主要依据）。
constexpr std::uint32_t heartbeat_frames = 2000U;

}  // namespace

/// 唯一 UI 任务：HAL 已在 app_main 里初始化完，这里只装配 UI 端口与框架。
void ui_main(void*) noexcept {
  ep::Esp32Hal& hal = ep::Esp32Hal::instance();

  // 真机没有"退出请求"这个概念（宿主是关窗），所以不给 LvglUiPort 传退出来源回调。
  embark::platform::LvglUiPort ui_port(hal.context());
  // own task 的后台任务槽位（BSS，不占 UI 任务栈）：固件级一个实例。
  static ep::Esp32TaskSpawner spawner;

  Framework framework(hal.context(), ep::embark_apps(), &ui_port, &spawner);
  if (const Error boot_error = framework.boot(); boot_error != Error::none) {
    char text[24];
    embark::fatal(embark::error_text(text, boot_error));
  }

  // LVGL_VERSION_* 是整数宏（third_party/lvgl/lvgl.h），拼日志要逐个占位。
  ELOG_INFO("框架就绪：{} 个 App，默认前台 {}；LVGL {}.{}.{}，绘制缓冲 {} 行，LVGL 静态池 {} 字节",
            framework.apps().size(), framework.apps().at(0)->name(), LVGL_VERSION_MAJOR,
            LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, static_cast<int>(embark::lvgl_draw_buf_lines),
            static_cast<unsigned>(ep::lvgl_pool_capacity_bytes()));

  std::uint32_t frames = 0;
  for (;;) {
    // 一帧 = 喂时间 → 喂输入 →（框架在边界处执行前台切换）→ LVGL 渲染
    framework.step();
    ++frames;

    if ((frames % heartbeat_frames) == 0U) {
      ELOG_INFO(
          "心跳：{} 帧；堆空余 {} 字节，UI 栈余 {} 字节；LVGL 池未回收 {} / 峰值 {} 字节；"
          "刷新 {} 次，触摸按下 {} 次",
          frames, static_cast<unsigned>(hal.system().free_heap_bytes()),
          static_cast<unsigned>(ep::ui_task_stack_high_water_bytes()),
          static_cast<unsigned>(ep::lvgl_outstanding_bytes()),
          static_cast<unsigned>(ep::lvgl_peak_bytes()),
          static_cast<unsigned>(hal.display().refreshes()),
          static_cast<unsigned>(hal.input().presses()));
    }

    ep::ui_loop_delay();  // 5 ms 让出（vTaskDelay；100 Hz 下也兜底成 1 tick，绝不忙等）
  }
}

extern "C" void app_main(void) {
  // 只做两件事：把 HAL 点起来（失败就没有任何出口），再把唯一 UI 任务交出去。
  ep::Esp32Hal& hal = ep::Esp32Hal::instance();
  const Error hal_error = hal.init();
  if (hal_error != Error::none) {
    char text[24];
    std::fprintf(stderr, "HAL 初始化失败：%s\n", embark::error_text(text, hal_error));
    embark::fatal("HAL 初始化失败");
  }

  const embark::hal::DisplayInfo display_info = hal.display().info();
  ELOG_INFO("Embark {} 启动（平台 {}）：显示 {}，NVS 容量 {} 字节；UI 任务栈 {} 字节，周期 {} ms",
            embark::version_string(), embark::platform_name(), display_info,
            hal.storage().capacity_bytes(), ep::ui_task_stack_bytes, embark::ui_loop_period_ms);

  const Error task_error = ep::start_ui_task(&ui_main, nullptr);
  if (task_error != Error::none) {
    char text[24];
    std::fprintf(stderr, "启动 UI 任务失败：%s\n", embark::error_text(text, task_error));
    embark::fatal("UI 任务启动失败");
  }

  // 返回即交棒：IDF 删掉 main 任务，只留唯一 UI 任务在跑（ui_main 永不返回）。
}
