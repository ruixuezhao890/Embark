/**
 * Embark · 启动器 App 实现（issue 16、ADR 0006；界面现由 AiDoGUI Designer 生成代码提供）
 *
 * 见 launcher_app.h 头注释。本文件与 clock_app.cpp / album_app.cpp 同形：不手写任何
 * 界面调用，只把界面生成代码里同名屏（"launcher"）挂上显示器，并把界面工具的切屏事件
 * 反向接到框架的 App 切换上（屏名约定 == App 名，解析在 app/common/ui_screen_names.h）。
 *
 * 唯一的例外是主屏磁贴：它画不进设计稿（画进去就等于把 App 数写死），改为屏每次装载后
 * 按 App 注册表运行时生成 —— 见 rebuild_tiles() 与 app/launcher/launcher_tiles.cpp。
 *
 * 除导航外，启动器还负责刷屏上那两个时间控件（L_Time / L_Clock0）：宿主上墙钟就是 PC
 * 时间（host/host_time.cpp 直接读 system_clock），所以这里看到的 HH:MM 与电脑
 * 一致。刷新挂在**屏装载后**（bind_repaint → repaint_screen()）：屏每次都是新对象、
 * label 已被打回设计稿初值，必须重推一遍；这也是"回主屏时间停在设计稿 11:31"那个 bug
 * 的根治办法 —— 以前靠每个 App 自己记得清缓存（ADR 0015）。
 *
 * 桥调用的开关在 app/common/ui_backend.h：定义 EMBARK_AIDOGUI_UI 时桥调用生效（宿主与
 * esp32 真机构建都定义）；没定义的目标把桥调用编译成 no-op —— 本 App 照常可跑。
 */
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <embark/error.h>
#include <embark/framework.h>
#include <embark/log.h>

#include "launcher_app.h"
#include "local_clock.h"
#include "weather/weather_data.h"  // 天气卡与天气屏同源（ADR 0012）

#if defined(EMBARK_AIDOGUI_UI)
#include "ui_backend.h"
#endif
#if defined(EMBARK_AIDOGUI_UI)
#include "aidogui_ui_bridge.h"  // 控件表：launcher 的时间/天气出口都在屏上的 label
#endif

namespace embark::demo {

// repaint 回调是裸函数指针（不能捕获 this）：进程内只有这一个启动器实例，文件级记下来。
LauncherApp* LauncherApp::g_for_ui = nullptr;

void LauncherApp::onCreate(Framework& fw) {
  ELOG_INFO("App {} onCreate", name());
  fw_ = &fw;  // 刷新时间要 hal().time.epoch_ms()（host_time.cpp = PC 墙上时间）
#if defined(EMBARK_AIDOGUI_UI)
  // 谁当 UI 的宿主谁接线：确保后端已就位（幂等）+ 装上屏切换观察者，于是界面工具里
  // 切到某屏 = 框架切到那个屏对应的 App（屏名约定 == App 名，见 ui_backend.h）。
  ui_backend_attach_nav(fw);

  // 磁贴 → App 的导航不在设计器里声明：磁贴是**运行时**按注册表建的（rebuild_tiles →
  // launcher_tiles.cpp），点击直接调 aidogui_ui_nav_request_app()。设计器里那个
  // 「调用函数」动作（embark_ui_open_<App 名>）留给屏上画死的按钮（如各子屏的"返回"）。
  // 磁贴也不挂 CHANGE_SCREEN 动作：那个不经过桥，会绕过"切走即 destroy / 回来即 init"的纪律。
#if defined(EMBARK_AIDOGUI_UI)
  // 时钟卡（Card_Clock）→ clock 走 C++ 备用路径 bind_click：这张卡不挂设计器事件，
  // 而是把点击绑定声明在桥的绑定表里，屏每次重建后由观察者自动重挂回调 —— 设计器里
  // autosave 不会碰它，事件丢了也不影响（与手写进 screen.aid 的事件形成双保险）。
  ui_backend_bind_click("launcher", "Card_Clock", "clock");

  // 主屏装载后重推控件值（ADR 0015）：桥在装载完新屏、销毁完旧屏之后回调本 App 的
  // repaint_screen()。磁贴、时间、天气卡、"上次停在第几页"都归它，onEnter/onResume 里
  // 不再需要各自记着清缓存/重建。
  g_for_ui = this;
  ui_backend_bind_repaint("launcher", &LauncherApp::on_screen_reloaded);
#endif
#endif
}

void LauncherApp::onEnter() {
  ++enters_;
  ELOG_INFO("App {} 进入前台（enter {}, resume {}）", name(), enters_, resumes_);
#if defined(EMBARK_AIDOGUI_UI)
  // 装载自己的屏；桥装载完新屏会回调 repaint_screen()（磁贴/时间/天气卡/页号）。
  ui_backend_load_for_app(name());
#endif
  // 兜底：上一个 App 没有同名屏时桥不会重载屏、也就不会回调 repaint（既定降级路径），
  // 这里补一次值刷新。正常路径下 repaint 刚推过，两个缓存都命中，这两行是空转。
  refresh_clock();
  refresh_weather();
}

void LauncherApp::onPause() {
  // 视图留存：离开时屏还在，记下磁贴带停在第几页，回来时 repaint_screen() 跳回去。
  // 磁贴带是 AiDoGUI 屏上的控件：没有 UI 后端的构建（esp32 真机当前状态）里
  // launcher_tiles.cpp 根本不参与编译，读页号无从谈起，保持上次记住的页号。
#if defined(EMBARK_AIDOGUI_UI)
  page_ = launcher_tiles_current_page();
#endif
  ELOG_INFO("App {} 退到后台（enter {}, resume {}, 磁贴停在第 {} 页）", name(), enters_, resumes_,
            page_ + 1U);
}

void LauncherApp::onResume() {
  ++resumes_;
  ELOG_INFO("App {} 回到前台（enter {}, resume {}）", name(), enters_, resumes_);
#if defined(EMBARK_AIDOGUI_UI)
  ui_backend_load_for_app(name());  // 同 onEnter：屏装载后由桥回调 repaint_screen()
#endif
  refresh_clock();  // 兜底（同上）：屏没被重载时也能把时间推到最新
  refresh_weather();
}

void LauncherApp::onForegroundTick(std::uint32_t now_ms) {
  (void)now_ms;
  ++foreground_ticks_;
#if defined(EMBARK_AIDOGUI_UI)
  ui_backend_tick();
#endif
}

void LauncherApp::onExit() {
  g_for_ui = nullptr;
  ELOG_INFO("App {} onExit", name());
}

void LauncherApp::on_screen_reloaded() {
  if (g_for_ui != nullptr) {
    g_for_ui->repaint_screen();
  }
}

void LauncherApp::repaint_screen() {
  // 屏是新的：label 回设计稿初值（时间 "11:31"、天气卡是默认城市），缓存的"上次画过什么"
  // 全部作废，然后重建磁贴、最后跳回上次停留的页（磁贴带要先有新内容再跳）。
  last_minute_ = 0xFFFFFFFFU;
  last_day_ = INT64_MIN;  // 同上：日期行也回到设计稿那一天，必须重推
  shown_city_ = 0xFFU;
  refresh_clock();
  refresh_weather();  // 在天气屏里换了城市，回主屏时卡片跟着变（值没变就不写控件）
  rebuild_tiles();
  launcher_tiles_show_page(page_);
}

void LauncherApp::rebuild_tiles() {
#if defined(EMBARK_AIDOGUI_UI)
  if (fw_ == nullptr) {
    return;
  }
  // 磁贴 = 注册表里除第 0 项（主屏自己）以外的每个 App，顺序就是注册顺序。
  // 文案取 App::title()（中文名，如"时钟"）：与设计稿里的手写磁贴字一致。
  const AppRegistry& apps = fw_->apps();
  tile_count_ = 0;
  for (std::size_t i = 1; i < apps.size() && tile_count_ < launcher::max_tiles; ++i) {
    const App* app = apps.at(i);
    if (app == nullptr) {
      continue;
    }
    tiles_[tile_count_].app_name = app->name();
    tiles_[tile_count_].label = app->title() != nullptr ? app->title() : app->name();
    ++tile_count_;
  }

  const std::size_t built = launcher_tiles_rebuild(name(), tiles_, tile_count_);
  ELOG_INFO("主屏磁贴：注册表 {} 个 App → {} 个磁贴 → 建出 {} 个 / {} 页", apps.size(), tile_count_,
            built, launcher_tiles_page_count());
#endif
}

void LauncherApp::refresh_clock() {
#if defined(EMBARK_AIDOGUI_UI)
  if (fw_ == nullptr)
    return;
  const auto epoch = fw_->hal().time.epoch_ms();
  if (!epoch.has_value())
    return;  // 没有 RTC / 没对时：留着设计器里的初值，不写

  // 本地时间（固定东八区，口径见 local_clock.h）。分钟粒度：同一分钟内不重写控件。
  const std::int64_t local_s =
      static_cast<std::int64_t>(epoch.value() / 1000U) + kLocalOffsetSeconds;
  const std::int64_t days = local_s / kSecondsPerDay;
  // 当日秒数归一化到 [0, 86399]：C++ 整除向零截断，偏移为负（1970 之前 / 负时区）时
  // local_s - days*kSecondsPerDay 会是负数，直接拿去格式化成 "-1:59" 还过不了真机的
  // -Werror=all（-Wformat-truncation 认为 %02d 可能吐出负号）。
  const int sod =
      static_cast<int>(((local_s - days * kSecondsPerDay) + kSecondsPerDay) % kSecondsPerDay);
  const std::uint32_t minute = static_cast<std::uint32_t>(sod / 60);
  if (minute == last_minute_)
    return;
  last_minute_ = minute;

  // 16 字节而不是 8：真机按 -Werror=all 编（-Wformat-truncation），而两个 %02d 的理论上限
  // 在编译器眼里是 12 字节 —— 给足余量它才肯证明"不会截断"。
  char buf[16];
  std::snprintf(buf, sizeof buf, "%02d:%02d", (sod / 3600) % 24, (sod / 60) % 60);
  for (const char* widget : {"L_Time", "L_Clock0"}) {
    if (lv_obj_t* label = aidogui_ui_bridge_widget("launcher", widget); label != nullptr) {
      lv_label_set_text(label, buf);  // 这两个 label 不染色，不需要 recolor 开关
    }
  }

  // 日期行（L_Date）：设计稿初值是画死的「2026年10月12日 星期一」，谁也不会替它变，
  // 所以真机与宿主都一直显示那一天。按天刷新，格式与设计稿同款（星期X，不是 clock 屏的周X）。
  const std::int64_t day = floor_div(local_s, kSecondsPerDay);
  if (day == last_day_) {
    return;
  }
  last_day_ = day;
  char date_buf[48];
  format_local_date(local_s, date_buf, sizeof date_buf);
  if (lv_obj_t* label = aidogui_ui_bridge_widget("launcher", "L_Date"); label != nullptr) {
    lv_label_set_text(label, date_buf);
    // 一天一次，留一行串口证据：日期行错通常是墙钟/RTC 的问题，不是控件的问题。
    ELOG_INFO("主屏日期行：{}", date_buf);
  }
#endif
}

void LauncherApp::refresh_weather() {
#if defined(EMBARK_AIDOGUI_UI)
  if (fw_ == nullptr)
    return;
  // 城市下标与天气屏共用同一个持久化键：谁改都对另一个生效。
  const std::size_t city = weather_load_city(fw_->hal().storage);
  if (static_cast<std::uint8_t>(city) == shown_city_)
    return;  // 没换城市就不动控件
  shown_city_ = static_cast<std::uint8_t>(city);

  const WeatherCity& w = weather_city(city);
  struct {
    const char* widget;
    const char* text;
  } kCard[] = {
      {"L_WCity", w.name},
      {"L_WTemp", w.temp},
      {"L_WIcon", w.days[0].icon},
      {"L_WDesc", w.days[0].desc},
  };
  for (const auto& item : kCard) {
    if (lv_obj_t* label = aidogui_ui_bridge_widget("launcher", item.widget); label != nullptr) {
      lv_label_set_text(label, item.text);  // 与设计器初值同款纯文本，不染色
    }
  }
#endif
}

}  // namespace embark::demo
