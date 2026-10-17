/**
 * Embark · 关于 App 实现：见 about_app.h 头注释。
 *
 * 七行数据来自四个只读来源，各自都可能"量不了"，所以每行都有兜底文案 —— 兜底本身也是信息：
 *   平台 / 版本  → embark::platform_name() / version_string()（函数取，不靠编译期宏）
 *   可用堆 / 栈水位 → hal().system.free_heap_bytes() / stack_high_water_bytes()（宿主返回 0）
 *   字库记账      → aidogui_ui_bridge_font_report()（设计器 BIN 在池里装了几份 / 共几份）
 *   存盘          → hal().filesystem 是否为空（空 = 没有 SD 后端）
 *   对时          → hal().time.epoch_ms()（没有 RTC 或没对时 → Error::unsupported）
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <embark/framework.h>
#include <embark/log.h>
#include <embark/version.h>

#include "about_app.h"
#include "local_clock.h"

#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
#include "ui_backend.h"
#endif
#if defined(EMBARK_AIDOGUI_UI)
#include "aidogui_ui_bridge.h"  // 字库记账 + 控件查表
#endif

namespace embark::demo {

AboutApp* AboutApp::g_for_ui = nullptr;

namespace {

constexpr const char* kScreen = "about";

/// 字节 → KB（四舍五入）。堆/栈的数字是给"够不够用"看的，字节精度没有意义；
/// 而且 7 位数字塞进 188px 的行会把「KB」两个字挤出去。
[[nodiscard]] std::size_t to_kb(std::size_t bytes) noexcept { return (bytes + 512U) / 1024U; }

}  // namespace

void AboutApp::onCreate(Framework& fw) {
  fw_ = &fw;
  ELOG_INFO("App {} onCreate（后台策略 = suspend；界面 = 屏 {}，七行只读信息）", name(), kScreen);
#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
  // 屏切走即 destroy、回来即 init（ADR 0011 决策②），新控件上是设计器初值；
  // refresh_display() 负责把七行换成真值（ADR 0015 的 repaint 通路）。
  g_for_ui = this;
  ui_backend_bind_repaint(kScreen, &AboutApp::on_screen_reloaded);
#endif
}

void AboutApp::onEnter() {
  ++enters_;
  ELOG_INFO("App {} 进入前台（enter {}, resume {}，累计 {} 次）", name(), enters_, resumes_,
            enters_ + resumes_);
#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
  ui_backend_load_for_app(name());  // 屏装载后由桥回调 on_screen_reloaded()
#endif
}

void AboutApp::onPause() {
  ELOG_INFO("App {} 离开前台（enter {}, resume {}）", name(), enters_, resumes_);
}

void AboutApp::onResume() {
  ++resumes_;
  ELOG_INFO("App {} 回到前台（enter {}, resume {}）", name(), enters_, resumes_);
#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
  ui_backend_load_for_app(name());  // 同 onEnter：屏装载后由桥回调 on_screen_reloaded()
#endif
}

void AboutApp::onExit() {
  ELOG_INFO("App {} onExit", name());
}

void AboutApp::onForegroundTick(std::uint32_t now_ms) {
  (void)now_ms;
#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
  ui_backend_tick();
#endif
}

void AboutApp::on_screen_reloaded() {
  if (g_for_ui != nullptr) {
    g_for_ui->refresh_display();
  }
}

void AboutApp::set_label(const char* widget_name, const char* text) {
#if defined(EMBARK_AIDOGUI_UI)
  if (lv_obj_t* obj = aidogui_ui_bridge_widget(kScreen, widget_name); obj != nullptr) {
    lv_label_set_text(obj, text);
  }
#else
  (void)widget_name;
  (void)text;
#endif
}

void AboutApp::set_row(const char* widget_name, const char* key, const char* value) {
  char text[64];
  std::snprintf(text, sizeof text, "%s：%s", key, value);
  set_label(widget_name, text);
}

void AboutApp::refresh_display() {
#if defined(EMBARK_AIDOGUI_UI)
  if (!aidogui_ui_bridge_screen_created(kScreen)) {
    // 设计器里还没有 about 屏（或缺屏）：保持当前屏 + 告警，别把界面写成半截。
    ELOG_INFO("关于屏 {} 未装载，跳过刷新", kScreen);
    return;
  }
  if (fw_ == nullptr) {
    return;
  }

  ++refreshes_;
  const hal::Context& h = fw_->hal();
  char value[48];

  // 1) 平台 / 版本：函数取，同一份 C++ 在宿主报 host、在真机报 esp32。
  set_row("L_Platform", "平台", platform_name());
  set_row("L_Version", "版本", version_string());

  // 2) 可用堆：宿主后端不量（返回 0），真机报 KB。
  const std::size_t free_heap = h.system.free_heap_bytes();
  if (free_heap != 0U) {
    std::snprintf(value, sizeof value, "%u KB", static_cast<unsigned>(to_kb(free_heap)));
    set_row("L_Heap", "可用堆", value);
  } else {
    set_row("L_Heap", "可用堆", "量不了");
  }

  // 3) 字库记账：设计器导出 7 份 BIN，池里装上了几份（真机 SD 缺字库时 loaded < total）。
  const AidoguiFontReport font = aidogui_ui_bridge_font_report();
  std::snprintf(value, sizeof value, "%d/%d 份", font.loaded, font.total);
  set_row("L_Font", "字库", value);

  // 4) 栈水位：宿主不量（返回 0）——单位用 KB，真机的栈水位是几千字节量级。
  const std::size_t stack = h.system.stack_high_water_bytes();
  if (stack != 0U) {
    std::snprintf(value, sizeof value, "%u KB", static_cast<unsigned>(to_kb(stack)));
    set_row("L_Stack", "栈水位", value);
  } else {
    set_row("L_Stack", "栈水位", "量不了");
  }

  // 5) 存盘：没有文件系统后端（宿主默认、真机 EMBARK_SD_ENABLED=0）就是未挂载。
  set_row("L_Disk", "存盘", h.filesystem != nullptr ? "已挂载" : "未挂载");

  // 6) 对时：epoch_ms() 没有 RTC/没对时就是 Error::unsupported —— 界面写"未对时"，不猜。
  const auto epoch = h.time.epoch_ms();
  if (epoch.has_value()) {
    const std::int64_t local_s = static_cast<std::int64_t>(epoch.value() / 1000U) + kLocalOffsetSeconds;
    const std::int64_t days = floor_div(local_s, kSecondsPerDay);
    int y = 0;
    int m = 0;
    int d = 0;
    civil_from_days(days, y, m, d);
    const std::int64_t sod = local_s - days * kSecondsPerDay;
    std::snprintf(value, sizeof value, "%d-%02d-%02d %02d:%02d", y, m, d,
                  static_cast<int>(sod / 3600), static_cast<int>((sod / 60) % 60));
    set_row("L_Clock", "对时", value);
  } else {
    set_row("L_Clock", "对时", "未对时");
  }

  // 一行汇总日志：真机串口上一眼能看出七行推成了什么（宿主没有堆/栈，会是"量不了"）。
  ELOG_INFO("关于页刷新（第 {} 次）：{} / 堆 {} / 字库 {}/{} / 存盘 {} / 对时 {}",
            refreshes_, platform_name(),
            free_heap != 0U ? "有" : "量不了", font.loaded, font.total,
            h.filesystem != nullptr ? "已挂载" : "未挂载",
            epoch.has_value() ? "有" : "未对时");
#endif
}

}  // namespace embark::demo
