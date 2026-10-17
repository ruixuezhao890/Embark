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

#include <esp_timer.h>  // 每帧耗时（微秒）：心跳里报"卡不卡"要靠它

#include <embark/app_registry.h>
#include <embark/diagnostics.h>
#include <embark/framework.h>
#include <embark/version.h>
#include <middleware/elog/elog.hpp>

#include "album_app.h"
#include "clock_app.h"
#include "launcher_app.h"
#include "music_app.h"
#include "settings_app.h"
#include "weather_app.h"

#include "aidogui_ui_bridge.h"   // AiDoGUI 桥：真机 UI 后端（ADR 0011），只用来登记字库作用域
#include "autodebug.h"  // esp32-autodebug 后门（UART 自报）：调试分支，见 platform/esp32/README.md §5
#include "esp32_board.h"
#include "esp32_bringup.h"
#include "esp32_hal.h"
#include "esp32_lvgl_mem.h"  // 静态池观测：容量 / 未回收 / 峰值（心跳日志要用）
#include "esp32_task_spawner.h"
#include "esp32_ui_task.h"
#include "font_alloc_scope.h"  // 字库分配作用域：把 lv_font_load 的申请路由到 PSRAM 字库区
#include "lvgl_ui_port.h"

using embark::Error;
using embark::Framework;

namespace ep = embark::platform::esp32;

namespace embark::platform::esp32 {

// 整个固件只出现一次的 App 注册表 —— 2026-10-17 起与宿主演示**完全一致**（六个 App）：
// LauncherApp 首位 = 默认前台（界面来自 AiDoGUI 生成代码，见 ADR 0011 / aidogui_ui_bridge）；
// ClockApp 后台 tick 100ms（节拍推进计数 + 消息驱动亮度档）；SettingsApp suspend（bump_level
// 逻辑入口）；WeatherApp 读本地桩数据重推天气卡；MusicApp/AlbumApp 是占位 App（屏上写着
// "界面还没做"）。
//
// 为什么之前只有三个：weather/music/album 三个 .cpp 没进真机源清单，屏能画出来但点磁贴
// 切前台时框架找不到 App 会安静忽略 —— 于是真机主屏只有 2 个磁贴 1 页，模拟器是 4 磁贴 2 页，
// 「真机画面 ≠ 模拟器画面」。三份源码只依赖 framework/log/ui_backend 与本地桩数据，没有
// 平台后端，2026-10-17 一并放进 cmake/embark_sources.cmake 的 EMBARK_APP_SOURCES_DEVICE。
// 注册顺序 = 主屏磁贴顺序（launcher 自己不算磁贴），要与 platform/host/ui_demo.cpp:406 同序。
EMBARK_APP_TABLE(embark::demo::LauncherApp, embark::demo::ClockApp, embark::demo::SettingsApp,
                 embark::demo::WeatherApp, embark::demo::MusicApp, embark::demo::AlbumApp)

}  // namespace embark::platform::esp32

namespace {

/// 心跳周期：2000 帧 × 5 ms ≈ 10 s。真机没有宿主那样的验收钩子，串口日志就是观测手段 ——
/// 堆、UI 栈水位、LVGL 静态池、刷新次数、触摸次数这几项周期性打出来（bring-up 主要依据）。
constexpr std::uint32_t heartbeat_frames = 2000U;

/// 字库分配作用域（issue 25 的 PSRAM 字库区）转发给 AiDoGUI 桥：
/// 生成代码里 7 个 BIN 字体合计 57 KB，加载期的分配必须落字库区，不能推动 48 KB 的 LVGL 静态池。
/// 桥是平台中立层（app/common），不认识 platform/common 的字库头，所以只收两个函数指针。
void aidogui_font_scope_begin() {
  embark::platform::font_alloc_scope_begin();
}

void aidogui_font_scope_end() {
  embark::platform::font_alloc_scope_end();
}

}  // namespace

/// 唯一 UI 任务：HAL 已在 app_main 里初始化完，这里只装配 UI 端口与框架。
void ui_main(void*) noexcept {
  ep::Esp32Hal& hal = ep::Esp32Hal::instance();

  // 真机没有"退出请求"这个概念（宿主是关窗），所以不给 LvglUiPort 传退出来源回调。
  embark::platform::LvglUiPort ui_port(hal.context());
  // own task 的后台任务槽位（BSS，不占 UI 任务栈）：固件级一个实例。
  static ep::Esp32TaskSpawner spawner;

  // 必须在 boot() 之前登记：boot 会跑第一个 App 的 onCreate，那条路会调 AiDoGUI 桥的 init
  //（挂 'S' 盘 + 加载 7 个 BIN 字体），字库分配就在那一刻发生。
  embark::demo::aidogui_ui_bridge_set_font_scope(&aidogui_font_scope_begin, &aidogui_font_scope_end);

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

  // B 阶段上板核对屏（四角色标 + R/G/B/W 色带 + 中央触摸坐标）：bring-up 专用。
  // 2026-10-17 起真机接上了 AiDoGUI UI 后端（界面来自 app/aidogui_ui/export 的生成代码），
  // 默认屏不再空白 ⇒ 这张自检屏默认关（esp32_board.h 的 bringup_self_test = false）。
  // 要重新核对方向/颜色/触摸时把它打开，看完再关：它占着默认屏，会挡住真实界面。
  if constexpr (ep::bringup_self_test) {
    ep::bringup::show();
  }

  // SD 卡 + 运行时字库自检（issue 25）：与自检屏相互独立（自检屏是 B 阶段的上板核对，
  // 这项是 issue 25 的验收证据）。返回值 = 过没过：卡没挂载、字库加载不出来、尺寸不在窗口里、
  // 账目没归零，都算没过。
  if (ep::bringup::check_sd_font()) {
    // 通过令牌：宿主工具（esp32-autodebug）看到这一行就判 TEST_PASSED（退出码 0）并停止观测。
    // 两条都发是有意的：autodebug 那条走 uart_write_bytes（要求 UART 驱动真装上了），ELOG 这条
    // 走控制台（驱动没装上时，令牌也一定出现在串口日志里）。
    autodebug_pass("SD_FONT_OK");
    ELOG_INFO("[AUTODEBUG_PASS] SD_FONT_OK");
  } else if constexpr (ep::sd_font_check_fatal) {
    // 没过就 abort()：IDF panic handler 打出回溯，宿主工具据此判 PANIC_DETECTED 并把地址
    // addr2line 回源码行。只在"专门验收这一项"时把 esp32_board.h 的开关临时置 true ——
    // 常开会让缺卡/缺文件的设备进 panic 重启循环（原因看不见，界面也进不去）。
    embark::fatal("SD 字库自检未通过（issue 25）");
  } else {
    // 默认策略：报错但**继续启动**。缺字在界面上是空心方框，但设备能用、串口能看到原因；
    // 宿主工具那边因为没收到通过令牌，最终判 TIMEOUT（退出码 4）而不是 PANIC_DETECTED。
    ELOG_ERROR("SD 字库自检未通过（sd_font_check_fatal = false）：继续启动，界面可能缺字 —— 原因见上面几行");
  }

  std::uint32_t frames = 0;
  // 每帧耗时（微秒）：心跳里报均值与峰值 —— "卡不卡"只能看这个，不能看 LVGL 自己的 FPS
  // 估计（2026-10-17 真机浮层上出现过 123 FPS，而刷新周期是 30 ms，上限本该 33）。
  std::uint64_t frame_us_sum = 0;
  std::uint64_t frame_us_max = 0;
  for (;;) {
    // 一帧 = 喂时间 → 喂输入 →（框架在边界处执行前台切换）→ LVGL 渲染
    const std::int64_t frame_begin_us = esp_timer_get_time();
    framework.step();
    const std::uint64_t frame_us =
        static_cast<std::uint64_t>(esp_timer_get_time() - frame_begin_us);
    frame_us_sum += frame_us;
    if (frame_us > frame_us_max) {
      frame_us_max = frame_us;
    }
    ++frames;

    if constexpr (ep::bringup_self_test) {
      ep::bringup::pump_touch();  // 自检屏上的坐标读数与按下沿日志
    }

    if ((frames % heartbeat_frames) == 0U) {
      // 触摸五个计数是一组诊断（2026-10-17 的那次"一次点击连切 4 个城市"）：
      //   按下/抬起 —— 正常应该几乎相等，且与"手点了几次"一致；
      //   滑动     —— 按住期间的位置变化数；
      //   吞掉空拍 —— 面板在手指静止时报"0 个触点"的拍数（去抖窗口吃掉的），远大于手点次数
      //              才说明这颗触摸屏"静止时确实报 0 点"；
      //   读失败   —— I2C 抖动/接触问题。
      const auto& touch = hal.input();
      ELOG_INFO(
          "心跳：{} 帧；堆空余 {} 字节，UI 栈余 {} 字节；LVGL 池未回收 {} / 峰值 {} 字节；"
          "刷新 {} 次；触摸 按下 {} / 抬起 {} / 滑动 {} / 吞掉空拍 {} / 读失败 {}；"
          "帧耗时 均值 {} us / 峰值 {} us；墙钟 {}",
          frames, static_cast<unsigned>(hal.system().free_heap_bytes()),
          static_cast<unsigned>(ep::ui_task_stack_high_water_bytes()),
          static_cast<unsigned>(ep::lvgl_outstanding_bytes()),
          static_cast<unsigned>(ep::lvgl_peak_bytes()),
          static_cast<unsigned>(hal.display().refreshes()),
          static_cast<unsigned>(touch.presses()), static_cast<unsigned>(touch.releases()),
          static_cast<unsigned>(touch.moves()), static_cast<unsigned>(touch.swallowed_gaps()),
          static_cast<unsigned>(touch.read_failures()),
          static_cast<unsigned>(frame_us_sum / heartbeat_frames),
          static_cast<unsigned>(frame_us_max), hal.time().wall_clock_note());
      frame_us_sum = 0;
      frame_us_max = 0;
    }

    ep::ui_loop_delay();  // 5 ms 让出（vTaskDelay；100 Hz 下也兜底成 1 tick，绝不忙等）
  }
}

extern "C" void app_main(void) {
  // esp32-autodebug 后门（调试分支）：装上 UART0 驱动、打一条启动横幅，之后的通过/失败令牌
  // 才有地方自报给宿主工具（esp32-autodebug 的 run_autodebug.py 靠 [AUTODEBUG_*] 令牌判退出码）。
  // 放最前面是为了从第一条日志起就能被工具采到。代价：uart_driver_install 约 2 KB 堆，这是本
  // 分支唯一的动态分配（零堆纪律的例外，见 platform/esp32/README.md §7），调试分支用完即删。
  autodebug_init(0);
  autodebug_boot_banner("embark-esp32s3");

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
