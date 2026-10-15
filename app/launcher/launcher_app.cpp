/**
 * Embark · 启动器 App 实现（issue 16、ADR 0006；界面现由 AiDoGUI Designer 生成代码提供）
 *
 * 见 launcher_app.h 头注释。本文件与 clock_app.cpp / settings_app.cpp 同形：不手写任何
 * 界面调用，只把界面生成代码里同名屏（"launcher"）挂上显示器，并把界面工具的切屏事件
 * 反向接到框架的 App 切换上（屏名约定 == App 名，解析在 eez_ui_screen_names.h）。
 *
 * 除导航外，启动器还负责刷屏上那两个时间控件（L_Time / L_Clock0）：宿主上墙钟就是 PC
 * 时间（platform/host/host_time.cpp 直接读 system_clock），所以这里看到的 HH:MM 与电脑
 * 一致。刷在 onBackgroundTick 里而不是 onForegroundTick：时间一分钟只变一次，没必要每帧算。
 *
 * 桥调用的开关在 app/common/ui_backend.h：宿主构建定义 EMBARK_AIDOGUI_UI（优先）与
 * EMBARK_EEZ_UI_BRIDGE（EEZ 老链路，迁移期并存），三句桥调用生效；两个宏都没定义的目标
 * （esp32 真机，尚未接界面文件系统）把桥调用编译成 no-op —— 本 App 照常可跑，真机接入
 * 后打开开关即可（esp32 的 EMBARK_APP_TABLE 已含 LauncherApp）。
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <embark/error.h>
#include <embark/framework.h>
#include <embark/log.h>

#include "launcher_app.h"
#include "local_clock.h"

#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
#include "ui_backend.h"
#endif
#if defined(EMBARK_AIDOGUI_UI)
#include "aidogui_ui_bridge.h"  // 控件表：launcher 的时间出口是屏上的两个 label
#endif

namespace embark::demo {

void LauncherApp::onCreate(Framework& fw) {
  ELOG_INFO("App {} onCreate", name());
  fw_ = &fw;  // 刷新时间要 hal().time.epoch_ms()（host_time.cpp = PC 墙上时间）
#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
  // 谁当 UI 的宿主谁接线：确保后端已就位（幂等）+ 装上屏切换观察者，于是界面工具里
  // 切到某屏 = 框架切到那个屏对应的 App（屏名约定 == App 名，见 ui_backend.h）。
  ui_backend_attach_nav(fw);

  // 磁贴 → App 的导航不在这里声明，也不在设计器里挂 CHANGE_SCREEN 动作（后者不经过桥，
  // 会绕过"切走即 destroy / 回来即 init"的纪律）：Tile1/Tile2 挂的是设计器的
  // 「调用函数」动作（embark_ui_open_clock / embark_ui_open_settings），生成代码里的裸调用
  // 落到 app/common/aidogui_ui_hooks.h 的 C 入口 → aidogui_ui_nav_request_app()。
  // Tile3（音乐）/ Tile4（相册）还没有对应 App：先不挂事件，点了没反应，好过切到空白屏。
#if defined(EMBARK_AIDOGUI_UI)
  // 时钟卡（Card_Clock）→ clock 走 C++ 备用路径 bind_click：这张卡不挂设计器事件，
  // 而是把点击绑定声明在桥的绑定表里，屏每次重建后由观察者自动重挂回调 —— 设计器里
  // autosave 不会碰它，事件丢了也不影响（与手写进 screen.aid 的事件形成双保险）。
  ui_backend_bind_click("launcher", "Card_Clock", "clock");
#endif
#endif
}

void LauncherApp::onEnter() {
  ++enters_;
  ELOG_INFO("App {} 进入前台（enter {}, resume {}）", name(), enters_, resumes_);
#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
  ui_backend_load_for_app(name());
#endif
  // 屏刚装载：控件是设计器里的初值（"11:31" 写死的文案），立刻刷一次真时间。
  // 后台策略是 tick（见本文件末尾 settings()），下一次刷新由 onBackgroundTick 接手。
  refresh_clock();
}

void LauncherApp::onPause() {
  ELOG_INFO("App {} 退到后台（enter {}, resume {}）", name(), enters_, resumes_);
}

void LauncherApp::onResume() {
  ++resumes_;
  ELOG_INFO("App {} 回到前台（enter {}, resume {}）", name(), enters_, resumes_);
#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
  ui_backend_load_for_app(name());
#endif
  refresh_clock();  // 同 onEnter：屏重建过，控件回到初值
}

void LauncherApp::onForegroundTick(std::uint32_t now_ms) {
  (void)now_ms;
  ++foreground_ticks_;
#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)
  ui_backend_tick();
#endif
}

void LauncherApp::onBackgroundTick(std::uint32_t now_ms) {
  (void)now_ms;
  ++ticks_;
  refresh_clock();
}

void LauncherApp::onExit() {
  ELOG_INFO("App {} onExit", name());
}

void LauncherApp::refresh_clock() {
#if defined(EMBARK_AIDOGUI_UI)
  if (fw_ == nullptr) return;
  const auto epoch = fw_->hal().time.epoch_ms();
  if (!epoch.has_value()) return;  // 没有 RTC / 没对时：留着设计器里的初值，不写

  // 本地时间（固定东八区，口径见 local_clock.h）。分钟粒度：同一分钟内不重写控件。
  const std::int64_t local_s = static_cast<std::int64_t>(epoch.value() / 1000U) + kLocalOffsetSeconds;
  const std::int64_t days = local_s / kSecondsPerDay;
  const int sod = static_cast<int>(local_s - days * kSecondsPerDay);
  const std::uint32_t minute = static_cast<std::uint32_t>(sod / 60);
  if (minute == last_minute_) return;
  last_minute_ = minute;

  char buf[8];
  std::snprintf(buf, sizeof buf, "%02d:%02d", sod / 3600, (sod / 60) % 60);
  for (const char* widget : {"L_Time", "L_Clock0"}) {
    if (lv_obj_t* label = aidogui_ui_bridge_widget("launcher", widget); label != nullptr) {
      lv_label_set_text(label, buf);  // 这两个 label 不染色，不需要 recolor 开关
    }
  }
#endif
}

AppSettings LauncherApp::settings() const {
  // 一分钟一次的刷新（周期取 1 s：墙钟本身按分钟变，多刷只是空转，省下来的 CPU
  // 给前台 App）。后台策略 tick 意味着这个 App 在前台和后台都会跑 —— 启动器是 App 0，
  // 常驻后台正是我们要的（切到 clock 再回来，时间已经是最新的）。
  AppSettings s{};
  s.background = BackgroundPolicy::tick;
  s.period_ms = 1000;
  return s;
}

}  // namespace embark::demo
