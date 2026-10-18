/**
 * 宿主 · 完整系统用例：从启动到前台切换（一条命令看全流程）
 *
 * 和 ui_demo.cpp 的区别只有一点：ui_demo 是"带开关的验收工具"，这个程序是
 * "一次走完的系统讲解"—— 不加任何参数就会把整条生命周期跑一遍，并逐步解说：
 *
 *   ① 进程入口：起唯一 UI 任务（FreeRTOS 静态任务）→ 进调度器，自己不干活
 *   ② HAL 初始化：时间 / 持久化 / 日志 / 系统 / 总线 / 显示 / 输入，逐个报状态
 *   ③ 框架 boot：3 个 App 注册（launcher/clock/settings）→ 打印每个 App 的后台策略 → 默认前台 LauncherApp
 *      （后台武装时机 = issue 23 / ADR 0009：boot 只武装默认前台；其余 App 等第一次被切进前台。
 *       ADR 0015 之后 demo 里没有一个 App 申请后台体：时间/天气是**前台渲染型**，只随前台帧推 ——
 *       ⑨ 的清单里有三项盯这件事）
 *   ④ 启动器主屏：AiDoGUI Designer 的 launcher 屏 —— 点屏上时钟卡 Card_Clock（设计器
 *      「调用函数」事件 → 切 clock）
 *   ⑤ 前台切换 #1：屏上控件驱动 → clock 进场（onEnter；同帧走一遍武装时机，无后台体）
 *   ⑥ 屏往返 + App 间消息：点 clock 屏「返回」按钮 Btn_Back 回启动器（屏名约定驱动切回
 *      归）；settings->bump_brightness(+10) 广播亮度消息 → clock 收到
 *       （亮度百分比，界面在 AiDoGUI 设置屏上，App 只留逻辑入口）
 *   ⑦ 回程交给屏上的返回键：无导航壳 —— 用例在 clock 收尾（回程由屏内按钮承担）
 *   ⑧ 关窗退出：SDL_QUIT → framework.exit_requested() → 收尾统计 + LVGL 堆账
 *
 * 每一步都打一行中文解说，末尾再打一张"自检清单"（✓/✗ + 实测值），
 * 所以它既是给人看的演示，也是能无人值守判定的系统用例：
 *   退出码 0 = 全流程通过；2 = 某一步没达到预期（stderr 会写清楚哪一步）。
 *
 * 参数（都可以不传）：
 *   --scale S          窗口放大倍数（默认 1 = 240×320 与面板像素 1:1）
 *   --delay MS         每帧间隔（默认 5；调小可加速，调大便于肉眼观察）
 *   --frames N         最多跑 N 帧（默认走到用例结束）
 *   --screenshot FILE  收尾时把最后一帧存成 BMP
 *   --help             显示用法
 *
 * 界面文案：demo App 的界面 = AiDoGUI 生成屏（字体走卡上的 ui_heiti.ttf，ADR 0017）；
 * 导航壳已退役（2026-10-06，返回与状态由屏内控件承担）。日志是中文。
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL.h>
#include <lvgl.h>

#include <embark/app.h>
#include <embark/error.h>
#include <embark/framework.h>
#include <embark/log.h>
#include <embark/task_spawner.h>
#include <embark/version.h>
#include <embark_limits.h>

#include "aidogui_ui_bridge.h"
#include "clock/clock_app.h"
#include "host_context.h"
#include "host_display.h"
#include "host_input.h"
#include "host_lvgl_mem.h"
#include "launcher/launcher_app.h"
#include "lvgl_port.h"
#include "lvgl_ui_port.h"
#include "own_task_spawner.h"
#include "settings/settings_app.h"
#include "settings/settings_prefs.h"
#include "ui_task.h"

namespace {

// --- 用例时间线（帧号）-------------------------------------------------------
// 每帧 = HAL 的一跳（默认 5 ms），帧号是确定性的，所以这条用例每次跑法完全一样。
// 启动器主屏来自 AiDoGUI Designer（app/aidogui_ui/export 的 launcher 屏）：屏上的时钟卡
// Card_Clock 挂了「调用函数」事件 → embark_ui_open_clock → 切前台 clock App；clock 屏的
// 「返回」按钮 Btn_Back 同样走设计器事件回 launcher。手绘 demo App 按钮已全部退役。
//
// 坐标不硬编码：widget_center() 每帧从控件表按名读真实几何（lv_obj_get_coords），
// 改屏后自动跟着走。下面两个常量只是屏还没建好时的兜底。
constexpr int launcher_button_fallback_x = 120;  ///< ④ launcher 屏时钟卡 Card_Clock 中心
constexpr int launcher_button_fallback_y = 78;
constexpr int clock_button_fallback_x = 40;  ///< ⑥ clock 屏返回键 Btn_Back 中心
constexpr int clock_button_fallback_y = 18;
constexpr int click_release_delta = 2;  ///< 按下后隔两帧抬起（LVGL 分两个读周期）

// AiDoGUI 的屏切换是「换根控件」，没有切页动画的输入屏蔽窗口：后续点击只要错开
// 「按下 2 帧 + 切换在下个 step 边界生效」就够了（本用例各步之间隔了几十帧）。

constexpr int arm_probe_frame = 12;  ///< ③b 还没点进 clock：确认它尚未武装、ticks 恒 0
constexpr int ui_click_frame = 14;  ///< ④ 点 launcher 屏时钟卡：按下（→ clock）
constexpr int tick_checkpoint_first = 28;  ///< 第一次看 clock 的后台节拍
constexpr int clock_back_frame = 70;  ///< ⑥ 点 clock 屏「返回」：回 launcher 屏（→ 启动器）
constexpr int tick_checkpoint_second = 82;  ///< 第二次看后台节拍（对比增长）
constexpr int bump_level_frame = 80;  ///< ⑥ settings->bump_brightness(+10)：广播亮度消息
constexpr int launcher_reenter_frame =
    120;  ///< ⑦ 再点 launcher 屏时钟卡：进 clock（回程交给屏内返回键）
constexpr int screen_life_check_frame = 115;  ///< ⑧ 屏生命周期检查：⑥ 回程之后、⑦ 点击（120）之前
constexpr int quit_frame = 170;      ///< ⑩ 推关窗事件
constexpr int tour_end_frame = 180;  ///< 默认帧数上限

struct Options {
  int scale = 1;
  int delay_ms = 5;
  int frames = tour_end_frame;
  const char* screenshot = nullptr;
};

void print_usage() {
  std::printf(
      "用法：embark_host_ui_tour [选项]\n"
      "  --scale S          窗口放大倍数（默认 1）\n"
      "  --delay MS         每帧间隔（默认 5）\n"
      "  --frames N         最多跑 N 帧（默认 %d）\n"
      "  --screenshot FILE  收尾时把最后一帧存成 BMP\n"
      "  --help             显示用法\n",
      tour_end_frame);
}

/// 后台策略的中文解说（framework.apps().at(i)->settings() 展示用）。
const char* policy_text(embark::BackgroundPolicy policy) {
  switch (policy) {
    case embark::BackgroundPolicy::suspend:
      return "suspend（退前台就完全不跑）";
    case embark::BackgroundPolicy::tick:
      return "tick（后台定时器，period_ms 如上）";
    case embark::BackgroundPolicy::own_task:
      return "own_task（独立任务，period_ms 如上）";
  }
  return "?";
}

// --- 真实控件几何（从 AiDoGUI 控件表按名读）-----------------------------------
// 合成点击必须落在控件真正的中心：屏幕随设计稿重画而移动，硬编码坐标每改一次屏
// 就失效一次。这里只读、不建控件不改样式。
struct PanelPoint {
  int x;
  int y;
  bool ok;
};

PanelPoint widget_center(const lv_obj_t* obj, int fallback_x, int fallback_y) {
  if (obj == nullptr) {
    return PanelPoint{fallback_x, fallback_y, false};
  }
  lv_area_t coords{};
  lv_obj_get_coords(obj, &coords);
  return PanelPoint{(coords.x1 + coords.x2) / 2, (coords.y1 + coords.y2) / 2, true};
}

/// 按名取控件的真实中心（控件名 = AiDoGUI 里的控件名；查不到才退回兜底坐标）。
PanelPoint aidogui_widget_center(const char* screen, const char* widget, int fallback_x,
                                 int fallback_y) {
  return widget_center(embark::demo::aidogui_ui_bridge_widget(screen, widget), fallback_x,
                       fallback_y);
}

// --- 合成输入（SDL_PushEvent → 真事件队列 → 输入后端）-------------------------
void push_motion_and_press(int scale, int panel_x, int panel_y) {
  SDL_Event motion{};
  motion.type = SDL_MOUSEMOTION;
  motion.motion.x = panel_x * scale;
  motion.motion.y = panel_y * scale;
  SDL_PushEvent(&motion);

  SDL_Event press{};
  press.type = SDL_MOUSEBUTTONDOWN;
  press.button.button = SDL_BUTTON_LEFT;
  press.button.state = SDL_PRESSED;
  press.button.clicks = 1;
  press.button.x = panel_x * scale;
  press.button.y = panel_y * scale;
  SDL_PushEvent(&press);
}

void push_release(int scale, int panel_x, int panel_y) {
  SDL_Event release{};
  release.type = SDL_MOUSEBUTTONUP;
  release.button.button = SDL_BUTTON_LEFT;
  release.button.state = SDL_RELEASED;
  release.button.clicks = 1;
  release.button.x = panel_x * scale;
  release.button.y = panel_y * scale;
  SDL_PushEvent(&release);
}

bool save_screenshot(embark::platform::host::HostDisplay& display, const char* path) {
  SDL_Surface* surface = display.capture_surface();
  if (surface == nullptr) {
    ELOG_ERROR("截图失败：{}", SDL_GetError());
    return false;
  }
  const bool ok = SDL_SaveBMP(surface, path) == 0;
  if (!ok) {
    ELOG_ERROR("截图失败：写文件失败（{}）", SDL_GetError());
  }
  SDL_FreeSurface(surface);
  return ok;
}

/// 自检清单：每项打一行 [通过]/[失败]，失败会置整体失败标志。
struct CheckList {
  bool ok = true;
  void check(const char* title, bool passed, unsigned long measured, unsigned long expected) {
    if (passed) {
      ELOG_INFO("  [通过] {}（实测 {}，期望 {}）", title, measured, expected);
    } else {
      ELOG_ERROR("  [失败] {}（实测 {}，期望 {}）", title, measured, expected);
      ok = false;
    }
  }
};

}  // namespace

namespace embark::platform::host {

// 整个可执行文件只出现一次的 App 注册表（启动器首位 = 默认前台）：
// LauncherApp（界面 = AiDoGUI 屏 "launcher"）；ClockApp suspend；SettingsApp suspend。
EMBARK_APP_TABLE(embark::demo::LauncherApp, embark::demo::ClockApp, embark::demo::SettingsApp)

}  // namespace embark::platform::host

using embark::Error;
using embark::Framework;

namespace hp = embark::platform::host;

namespace {

bool host_exit_query(void* context) noexcept {
  return static_cast<hp::HostInput*>(context)->quit_requested();
}

Options parse_options(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    // 45 = ASCII 撇号（避免本文件里出现引号字面量，保持可移植）
    auto next_is_flag = [&]() { return i + 1 >= argc || argv[i + 1][0] == 45; };
    if (std::strcmp(arg, "--scale") == 0 && !next_is_flag()) {
      options.scale = std::atoi(argv[++i]);
    } else if (std::strcmp(arg, "--delay") == 0 && !next_is_flag()) {
      options.delay_ms = std::atoi(argv[++i]);
    } else if (std::strcmp(arg, "--frames") == 0 && !next_is_flag()) {
      options.frames = std::atoi(argv[++i]);
    } else if (std::strcmp(arg, "--screenshot") == 0 && !next_is_flag()) {
      options.screenshot = argv[++i];
    } else if (std::strcmp(arg, "--help") == 0) {
      print_usage();
      std::exit(0);
    }
  }
  return options;
}

}  // namespace

/// 唯一 UI 任务（spec §6：唯一能碰 LVGL 的线程，窗口事件也在它上面泵）。
void ui_main(void* argument) noexcept {
  const Options* options = static_cast<const Options*>(argument);

  ELOG_INFO("① 进程入口：main 只起 UI 任务 + 进调度器，现在跑的是那唯一 UI 任务（{} 字栈）",
            embark::ui_task_stack_words);

  hp::HostHal& hal = hp::HostHal::instance();
  hp::HostDisplay display(static_cast<std::uint8_t>(options->scale), "Embark 系统用例");
  hp::HostInput input(hal.time(), display);
  hal.attach_display(display);
  hal.attach_input(input);

  ELOG_INFO("② HAL 初始化……");
  if (const Error hal_error = hal.init(); hal_error != Error::none) {
    char text[32];
    std::fprintf(stderr, "HAL 初始化失败：%s\n", embark::error_text(text, hal_error));
    hp::exit_process(1);
  }

  embark::platform::LvglUiPort ui_port(hal.context(), &host_exit_query, &input);
  static hp::HostTaskSpawner spawner;
  Framework framework(hal.context(), hp::embark_apps(), &ui_port, &spawner);
  if (const Error boot_error = framework.boot(); boot_error != Error::none) {
    char text[32];
    std::fprintf(stderr, "框架启动失败：%s\n", embark::error_text(text, boot_error));
    hp::exit_process(1);
  }

  ELOG_INFO("③ 框架 boot：{} 个 App；默认前台 {}（启动器，suspend）", framework.apps().size(),
            framework.apps().at(0)->name());
  for (embark::AppId i = 1; i < framework.apps().size(); ++i) {
    const auto* app = framework.apps().at(i);
    ELOG_INFO("    槽 {}：{}（{}，title={}）", i, app->name(),
              policy_text(app->settings().background), app->title());
  }
  ELOG_INFO("    LVGL {}.{}.{}", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
  ELOG_INFO("③b 后台武装：默认前台 {} 已武装={}；其余 App 等第一次被切进前台（clock 此刻未武装）",
            framework.apps().at(0)->name(), framework.background_armed(0U) ? 1 : 0);

  // 前台切换与亮度变化的解说跟踪
  std::uint32_t prev_switches = 0;
  std::uint32_t prev_brightness = 0;
  int bump_before_percent = -1;  // bump 前的亮度百分比（KV 跨进程留存，期望值不能写死 70）
  bool clock_released_after_leave = false;  // ⑧ 离开 clock 后其对象被回收、当前屏是 launcher
  auto* launcher = static_cast<embark::demo::LauncherApp*>(framework.app(0));
  auto* clock = static_cast<embark::demo::ClockApp*>(framework.app(1));
  auto* settings = static_cast<embark::demo::SettingsApp*>(framework.app(2));

  int frames_run = 0;
  std::uint32_t fg_ticks_before_first_enter =
      0;  ///< 进 clock 之前采到的前台帧数（应为 0：没打开过就不推）
  ELOG_INFO("④ UI 循环（5 ms/帧）：先点 launcher 屏的时钟卡 —— 设计器事件把前台切到 clock");
  for (;;) {
    framework.step();
    ++frames_run;
    if (framework.exit_requested()) {
      break;
    }

    // --- 合成输入（帧号驱动，确定性）-----------------------------------------
    // 每帧重新读真实几何：屏会随前台切换换掉，坐标必须跟着走。
    const PanelPoint card_pt = aidogui_widget_center(
        "launcher", "Card_Clock", launcher_button_fallback_x, launcher_button_fallback_y);
    const PanelPoint back_pt = aidogui_widget_center("clock", "Btn_Back", clock_button_fallback_x,
                                                     clock_button_fallback_y);

    if (frames_run == ui_click_frame || frames_run == launcher_reenter_frame) {
      push_motion_and_press(options->scale, card_pt.x, card_pt.y);
    }
    if (frames_run == ui_click_frame + click_release_delta ||
        frames_run == launcher_reenter_frame + click_release_delta) {
      push_release(options->scale, card_pt.x, card_pt.y);
    }
    if (frames_run == clock_back_frame) {
      push_motion_and_press(options->scale, back_pt.x, back_pt.y);
    }
    if (frames_run == clock_back_frame + click_release_delta) {
      push_release(options->scale, back_pt.x, back_pt.y);
    }
    if (frames_run == bump_level_frame) {
      // 亮度：逻辑入口是 bump_brightness(delta)，不再是「档位 +1」的 bump_level。
      bump_before_percent = static_cast<int>(settings->brightness_percent());
      settings->bump_brightness(embark::demo::kBrightStep);
    }
    if (frames_run == quit_frame) {
      SDL_Event quit{};
      quit.type = SDL_QUIT;
      SDL_PushEvent(&quit);
    }
    if (frames_run == arm_probe_frame) {
      fg_ticks_before_first_enter = clock->foreground_ticks();
      ELOG_INFO(
          "③b 武装时机（第 {} 帧，还没点进 clock）：clock 已武装={}，前台帧={}（没打开过的 App "
          "一帧都不跑）",
          frames_run, framework.background_armed(framework.id_of(*clock)) ? 1 : 0,
          fg_ticks_before_first_enter);
    }

    // --- 解说：前台切换 / 亮度 / 后台回流 -------------------------------------
    if (framework.switches() != prev_switches) {
      prev_switches = framework.switches();
      ELOG_INFO("◆ 前台切换完成（第 {} 帧，累计 {} 次）：现在前台是 {}", frames_run,
                framework.switches(), framework.apps().at(framework.foreground())->name());
    }
    if (clock->brightness() != prev_brightness) {
      prev_brightness = clock->brightness();
      ELOG_INFO("◆ clock 收到了新的亮度百分比：{}%（消息经总线广播，App 间不直接通信）",
                clock->brightness());
    }
    if (frames_run == tick_checkpoint_first) {
      ELOG_INFO(
          "前台帧采样（第 {} 帧）：clock 前台帧={}（它还没进过前台 → "
          "0）；启动器前台帧={}（它在跑）",
          frames_run, clock->foreground_ticks(), launcher->foreground_ticks());
    }
    if (frames_run == ui_click_frame + 6) {
      ELOG_INFO(
          "⑤ 屏上控件驱动切前台：切到 {}（onEnter；同一帧走武装时机，但它没有后台体 —— "
          "时间/日历从这一帧起随前台帧推）",
          framework.apps().at(framework.foreground())->name());
    }
    if (frames_run == tick_checkpoint_second) {
      ELOG_INFO(
          "前台帧第二次采样：clock 前台帧={}（进前台后逐帧推时间与日历 = 前台渲染型，ADR 0015）",
          clock->foreground_ticks());
    }
    if (frames_run == screen_life_check_frame) {
      const bool clock_gone = !embark::demo::aidogui_ui_bridge_screen_created("clock");
      const bool launcher_alive = embark::demo::aidogui_ui_bridge_screen_created("launcher");
      clock_released_after_leave = clock_gone && launcher_alive;
      ELOG_INFO(
          "⑧ 屏生命周期（第 {} 帧，离开 clock 后、回程点击前）：clock created={}（应为 0，"
          "切走即释放），launcher created={}（应为 1，此刻它是当前屏）",
          frames_run, clock_gone ? 0 : 1, launcher_alive ? 1 : 0);
    }

    if (options->frames > 0 && frames_run >= options->frames) {
      if (options->screenshot != nullptr && !save_screenshot(display, options->screenshot)) {
        hp::exit_process(1);
      }
      break;
    }
    hp::ui_loop_delay(static_cast<std::uint32_t>(options->delay_ms));
  }

  ELOG_INFO("⑨ 收尾统计（共 {} 帧）：", frames_run);
  CheckList checklist;
  checklist.check("总帧数跑到位", frames_run >= quit_frame + 1,
                  static_cast<unsigned long>(frames_run),
                  static_cast<unsigned long>(quit_frame) + 1UL);
  const auto clock_fg_ticks = static_cast<unsigned long>(clock->foreground_ticks());
  checklist.check("clock 前台帧 > 0（进过前台后逐帧推时间与日历）", clock_fg_ticks > 0UL,
                  clock_fg_ticks, 1UL);
  checklist.check("进 clock 之前它一帧都没跑（刷新只随前台）", fg_ticks_before_first_enter == 0UL,
                  static_cast<unsigned long>(fg_ticks_before_first_enter), 0UL);
  unsigned apps_with_background = 0;
  for (embark::AppId i = 0; i < framework.apps().size(); ++i) {
    if (framework.apps().at(i)->settings().background != embark::BackgroundPolicy::suspend) {
      ++apps_with_background;
    }
  }
  checklist.check("没有任何 App 申请后台体（时间/天气靠前台节拍，ADR 0015）",
                  apps_with_background == 0U, static_cast<unsigned long>(apps_with_background),
                  0UL);
  const auto clock_armed =
      static_cast<unsigned long>(framework.background_armed(framework.id_of(*clock)));
  checklist.check("clock 在第一次进前台时就已武装", clock_armed == 1UL, clock_armed, 1UL);
  const auto arm_failures = static_cast<unsigned long>(framework.arm_failures());
  checklist.check("武装失败计数 = 0", arm_failures == 0UL, arm_failures, 0UL);
  checklist.check("前台切换恰好 3 次（launcher→clock→launcher→clock）", framework.switches() == 3,
                  static_cast<unsigned long>(framework.switches()), 3UL);
  checklist.check("启动器 enter=1 / resume=1（一次回程：屏往返）",
                  launcher->enters() == 1 && launcher->resumes() == 1,
                  static_cast<unsigned long>(launcher->enters() * 10U + launcher->resumes()), 11UL);
  checklist.check("clock enter=1 / resume=1（往返再进）",
                  clock->enters() == 1 && clock->resumes() == 1,
                  static_cast<unsigned long>(clock->enters() * 10U + clock->resumes()), 11UL);
  checklist.check(
      "settings 本剧本从未进入（它的屏由 --ui-launcher-pages 覆盖：进屏 / 亮度 ＋ / 返回）",
      settings->enters() == 0 && settings->resumes() == 0,
      static_cast<unsigned long>(settings->enters() * 10U + settings->resumes()), 0UL);
  // 旧口径是 0..3 四档的 +1，已随 issue 03 退役；新期望 = 起点 +10 钳位到 0..100。
  const std::uint32_t bump_expect = embark::demo::clamp_percent(
      bump_before_percent + embark::demo::kBrightStep);
  checklist.check("亮度消息回流（clock.brightness == 起点+10）", clock->brightness() == bump_expect,
                  static_cast<unsigned long>(clock->brightness()),
                  static_cast<unsigned long>(bump_expect));
  checklist.check("收件箱无溢出", framework.inbox_overflows() == 0,
                  static_cast<unsigned long>(framework.inbox_overflows()), 0UL);
  checklist.check("结尾前台 = clock（回程交给屏内返回键，导航壳已退役）",
                  framework.foreground() == framework.id_of(*clock),
                  static_cast<unsigned long>(framework.foreground()), 1UL);
  checklist.check("启动器前台帧 > 0（界面来自 AiDoGUI 屏 launcher）",
                  launcher->foreground_ticks() > 0,
                  static_cast<unsigned long>(launcher->foreground_ticks()), 1UL);
  checklist.check("AiDoGUI 屏表非空（屏名约定：screen == App 名）",
                  embark::demo::aidogui_ui_bridge_screen_count() > 0,
                  static_cast<unsigned long>(embark::demo::aidogui_ui_bridge_screen_count()), 1UL);
  checklist.check("屏生命周期：离开 clock 后对象被回收（回来即重建，ADR 0015）",
                  clock_released_after_leave, clock_released_after_leave ? 1UL : 0UL, 1UL);
  // 屏是"一次性资源"：切走即 destroy、回来即 init（ADR 0015 第 1 条）。所以回到 clock 时
  // 只有 clock 的根控件存在，launcher 的已经被释放 —— 期望编码 10 = clock 1 / launcher 0。
  checklist.check(
      "屏生命周期：回到 clock 后 clock 重建、launcher 已释放（切走即释放）",
      embark::demo::aidogui_ui_bridge_screen_created("clock") &&
          !embark::demo::aidogui_ui_bridge_screen_created("launcher"),
      static_cast<unsigned long>(embark::demo::aidogui_ui_bridge_screen_created("clock")) * 10UL +
          static_cast<unsigned long>(embark::demo::aidogui_ui_bridge_screen_created("launcher")),
      10UL);

  ELOG_INFO("启动器最终：enters={} resumes={} 前台帧={}（界面 = AiDoGUI 屏 launcher）",
            launcher->enters(), launcher->resumes(), launcher->foreground_ticks());
  ELOG_INFO("LVGL 堆账：未回收 {} 字节（budget {}）", embark_lvgl_outstanding_bytes(),
            embark_lvgl_budget_bytes());

  framework.shutdown();
  hp::exit_process(checklist.ok ? 0 : 2);
}

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  Options options = parse_options(argc, argv);

  const Error task_error = hp::start_ui_task(&ui_main, &options);
  if (task_error != Error::none) {
    char text[24];
    std::fprintf(stderr, "启动 UI 任务失败：%s\n", embark::error_text(text, task_error));
    return 1;
  }

  hp::start_scheduler();
  return 0;
}
