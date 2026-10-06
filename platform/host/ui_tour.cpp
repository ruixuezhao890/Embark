/**
 * 宿主 · 完整系统用例：从启动到前台切换（一条命令看全流程）
 *
 * 和 ui_demo.cpp 的区别只有一点：ui_demo 是"带开关的验收工具"，这个程序是
 * "一次走完的系统讲解"—— 不加任何参数就会把整条生命周期跑一遍，并逐步解说：
 *
 *   ① 进程入口：起唯一 UI 任务（FreeRTOS 静态任务）→ 进调度器，自己不干活
 *   ② HAL 初始化：时间 / 持久化 / 日志 / 系统 / 总线 / 显示 / 输入，逐个报状态
 *   ③ 框架 boot：6 个 App 注册 → 打印每个 App 的后台策略 → 默认前台 LauncherApp
 *   ④ 启动器主屏：EEZ Studio 的 launcher 屏 —— 点屏上按钮（Flow SetPage → clock 屏）
 *   ⑤ 前台切换 #1：EEZ 屏按钮驱动 → clock 进场（onEnter，后台 tick 继续跑）
 *   ⑥ EEZ 屏往返 + App 间消息：点 clock 屏按钮回启动器（SetPage 回 launcher 屏，
 *       屏名约定驱动切回归）；settings->bump_level() 广播亮度消息 → clock 收到（手绘
 *       Level +1 按钮退役后的逻辑入口，界面提交给 EEZ 屏）
 *   ⑦ 回程交给 EEZ 屏按钮：无导航壳 —— 用例在 clock 收尾（SetPage 回程由 EEZ 屏承担）
 *   ⑧ 真正的任务切换：own_task 的 ticker 任务发消息 → UI 任务收（SPSC 队列）
 *   ⑨ 任务生命周期：一次性 own_task（job）跑完 → 框架回收槽位 → 运行期再创建一轮
 *   ⑩ 关窗退出：SDL_QUIT → framework.exit_requested() → 收尾统计 + LVGL 堆账
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
 * 界面文案：demo App 界面与 EEZ 生成屏都用 LVGL 默认字体（Montserrat）；导航壳已退役
 * （2026-10-06，返回与状态由 EEZ 屏承担）。日志是中文。
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

#include "demo_apps.h"
#include "eez_ui_bridge.h"
#include "hello_app.h"
#include "host_context.h"
#include "host_display.h"
#include "host_input.h"
#include "host_lvgl_mem.h"
#include "launcher_app.h"
#include "lvgl_port.h"
#include "lvgl_ui_port.h"
#include "own_task_spawner.h"
#include "ui_task.h"

namespace {

/// 注册表下标（与 EMBARK_APP_TABLE 的顺序一一对应）：第 6 个是 JobApp。
constexpr embark::AppId job_id = 5U;

// --- 用例时间线（帧号）-------------------------------------------------------
// 每帧 = HAL 的一跳（默认 5 ms），帧号是确定性的，所以这条用例每次跑法完全一样。
// 启动器主屏来自 EEZ Studio（app/eez_ui/src/ui/screens.c 的 launcher 屏）：屏上按钮
// 中心 (121,224)，Flow SetPage 动作切到 clock 屏，桥的屏观察者把切屏翻成
// request_switch("clock")（屏名约定 == App 名，见 eez_ui_nav.h）；clock 屏按钮
// 中心 (120,160)，Flow SetPage 回 launcher 屏。手绘 demo App 按钮已全部退役。
constexpr int launcher_button_x = 121;  ///< ④ EEZ launcher 屏的按钮中心（SetPage → clock 屏）
constexpr int launcher_button_y = 224;
constexpr int clock_button_x = 120;     ///< ⑥ EEZ clock 屏的按钮中心（SetPage → launcher 屏）
constexpr int clock_button_y = 160;
constexpr int click_release_delta = 2;  ///< 按下后隔两帧抬起（LVGL 分两个读周期）

constexpr int eez_click_frame = 14;         ///< ④ 点 launcher 屏按钮：按下（→ clock）
constexpr int tick_checkpoint_first = 28;   ///< 第一次看 clock 的后台节拍
constexpr int clock_back_frame = 36;        ///< ⑥ 点 clock 屏按钮：回 launcher 屏（→ 启动器）
constexpr int bump_level_frame = 44;        ///< ⑥ settings->bump_level()：广播亮度消息
constexpr int launcher_reenter_frame = 52;  ///< ⑦ 再点 launcher 屏按钮：进 clock（回程交给 EEZ 屏按钮）
constexpr int own_task_checkpoint = 78;     ///< ⑧ own_task 的消息回流
constexpr int tick_checkpoint_second = 82;  ///< 第二次看后台节拍（对比增长）
constexpr int own_task_cycle_frame = 86;    ///< ⑨ 运行期再创建一次 job
constexpr int quit_frame = 98;              ///< ⑩ 推关窗事件
constexpr int tour_end_frame = 100;         ///< 默认帧数上限

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
    case embark::BackgroundPolicy::suspend: return "suspend（退前台就完全不跑）";
    case embark::BackgroundPolicy::tick: return "tick（后台定时器，period_ms 如上）";
    case embark::BackgroundPolicy::own_task: return "own_task（独立任务，period_ms 如上）";
  }
  return "?";
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

// 整个可执行文件只出现一次的 App 注册表（启动器首位 = 默认前台，JobApp 挂末位）：
// LauncherApp（界面 = EEZ 屏 "launcher"）；ClockApp tick 100ms；SettingsApp suspend；
// TickerApp own_task 50ms（issue 07 消息回 UI）；HelloApp 最简模板；JobApp 一次性任务。
EMBARK_APP_TABLE(embark::demo::LauncherApp, embark::demo::ClockApp, embark::demo::SettingsApp,
                 embark::demo::TickerApp, embark::demo::HelloApp, embark::demo::JobApp)

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
    if (std::strcmp(arg, "--scale") == 0 && !next_is_flag()) { options.scale = std::atoi(argv[++i]); }
    else if (std::strcmp(arg, "--delay") == 0 && !next_is_flag()) { options.delay_ms = std::atoi(argv[++i]); }
    else if (std::strcmp(arg, "--frames") == 0 && !next_is_flag()) { options.frames = std::atoi(argv[++i]); }
    else if (std::strcmp(arg, "--screenshot") == 0 && !next_is_flag()) { options.screenshot = argv[++i]; }
    else if (std::strcmp(arg, "--help") == 0) { print_usage(); std::exit(0); }
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
    ELOG_INFO("    槽 {}：{}（{}，title={}）", i, app->name(), policy_text(app->settings().background),
              app->title());
  }
  ELOG_INFO("    LVGL {}.{}.{}", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);

  // 前台切换与亮度变化的解说跟踪
  std::uint32_t prev_switches = 0;
  std::uint32_t prev_brightness = 0;
  auto* launcher = static_cast<embark::demo::LauncherApp*>(framework.app(0));
  auto* clock = static_cast<embark::demo::ClockApp*>(framework.app(1));
  auto* settings = static_cast<embark::demo::SettingsApp*>(framework.app(2));
  auto* ticker = static_cast<embark::demo::TickerApp*>(framework.app(3));
  auto* job = static_cast<embark::demo::JobApp*>(framework.app(job_id));

  int frames_run = 0;
  ELOG_INFO("④ UI 循环（5 ms/帧）：先点 EEZ launcher 屏的按钮 —— Flow SetPage 到 clock 屏，桥把切屏翻成切 App");
  for (;;) {
    framework.step();
    ++frames_run;
    if (framework.exit_requested()) { break; }

    // --- 合成输入（帧号驱动，确定性）-----------------------------------------
    if (frames_run == eez_click_frame) { push_motion_and_press(options->scale, launcher_button_x, launcher_button_y); }
    if (frames_run == eez_click_frame + click_release_delta) { push_release(options->scale, launcher_button_x, launcher_button_y); }
    if (frames_run == clock_back_frame) { push_motion_and_press(options->scale, clock_button_x, clock_button_y); }
    if (frames_run == clock_back_frame + click_release_delta) { push_release(options->scale, clock_button_x, clock_button_y); }
    if (frames_run == bump_level_frame) { settings->bump_level(); }
    if (frames_run == launcher_reenter_frame) { push_motion_and_press(options->scale, launcher_button_x, launcher_button_y); }
    if (frames_run == launcher_reenter_frame + click_release_delta) { push_release(options->scale, launcher_button_x, launcher_button_y); }
    if (frames_run == quit_frame) { SDL_Event quit{}; quit.type = SDL_QUIT; SDL_PushEvent(&quit); }

    // --- 解说：前台切换 / 亮度 / 后台回流 -------------------------------------
    if (framework.switches() != prev_switches) {
      prev_switches = framework.switches();
      ELOG_INFO("◆ 前台切换完成（第 {} 帧，累计 {} 次）：现在前台是 {}",
                frames_run, framework.switches(), framework.apps().at(framework.foreground())->name());
    }
    if (clock->brightness() != prev_brightness) {
      prev_brightness = clock->brightness();
      ELOG_INFO("◆ clock 收到了新的亮度档：{}（消息经总线广播，App 间不直接通信）",
                clock->brightness());
    }
    if (frames_run == tick_checkpoint_first) {
      ELOG_INFO("后台节拍采样（第 {} 帧）：clock ticks={}；启动器前台帧={}",
                frames_run, clock->ticks(), launcher->foreground_ticks());
    }
    if (frames_run == eez_click_frame + 6) {
      ELOG_INFO("⑤ EEZ 屏按钮驱动切前台：切到 {}（onEnter；clock 的后台 tick 继续跑）",
                framework.apps().at(framework.foreground())->name());
    }
    if (frames_run == own_task_checkpoint) {
      ELOG_INFO("⑧ 断点（第 {} 帧）：ticker own_task 已发 {} 条 / UI 收 {} 条；inbox 溢出 {} 次",
                frames_run, ticker->sent(), ticker->received(), framework.inbox_overflows());
    }
    if (frames_run == tick_checkpoint_second) {
      ELOG_INFO("后台节拍第二次采样：clock ticks={}（继续增长 = 退后台也在跑）", clock->ticks());
    }
    if (frames_run == own_task_cycle_frame) {
      ELOG_INFO("⑨ 运行期再创建 job（第 {} 帧，spawned={} released={} running={} finished={} free={}）：",
                frames_run, framework.own_tasks_spawned(), framework.own_tasks_released(),
                spawner.running(), spawner.finished(), spawner.free_slots());
      const Error spawn_error = framework.spawn_own_task(job_id);
      ELOG_INFO("     spawn_own_task(job)={}（job 累计跑 {} 轮）", spawn_error, job->runs());
    }

    if (options->frames > 0 && frames_run >= options->frames) {
      if (options->screenshot != nullptr && !save_screenshot(display, options->screenshot)) {
        hp::exit_process(1);
      }
      break;
    }
    hp::ui_loop_delay(static_cast<std::uint32_t>(options->delay_ms));
  }

  ELOG_INFO("⑩ 收尾统计（共 {} 帧）：", frames_run);
  CheckList checklist;
  checklist.check("总帧数跑到位", frames_run >= quit_frame + 1, static_cast<unsigned long>(frames_run),
                  static_cast<unsigned long>(quit_frame) + 1UL);
  const auto first_ticks = static_cast<unsigned long>(clock->ticks());
  checklist.check("clock 后台节拍 > 0（退后台仍在跑）", first_ticks > 0UL, first_ticks, 1UL);
  checklist.check("前台切换恰好 3 次（launcher→clock→launcher→clock）",
                  framework.switches() == 3, static_cast<unsigned long>(framework.switches()), 3UL);
  checklist.check("启动器 enter=1 / resume=1（一次回程：EEZ 屏往返）",
                  launcher->enters() == 1 && launcher->resumes() == 1,
                  static_cast<unsigned long>(launcher->enters() * 10U + launcher->resumes()), 11UL);
  checklist.check("clock enter=1 / resume=1（EEZ 往返再进）",
                  clock->enters() == 1 && clock->resumes() == 1,
                  static_cast<unsigned long>(clock->enters() * 10U + clock->resumes()), 11UL);
  checklist.check("settings 从未进入（薄壳：逻辑入口 bump_level 代替屏交互）",
                  settings->enters() == 0 && settings->resumes() == 0,
                  static_cast<unsigned long>(settings->enters() * 10U + settings->resumes()), 0UL);
  checklist.check("亮度消息回流（clock.brightness==1）", clock->brightness() == 1,
                  static_cast<unsigned long>(clock->brightness()), 1UL);
  checklist.check("ticker own_task 消息回流（sent/received > 0）",
                  ticker->sent() > 0 && ticker->received() > 0,
                  static_cast<unsigned long>(ticker->received()), 1UL);
  checklist.check("收件箱无溢出", framework.inbox_overflows() == 0,
                  static_cast<unsigned long>(framework.inbox_overflows()), 0UL);
  checklist.check("job 生命周期（runs ≥ 2：boot 一轮 + 运行期一轮）",
                  job->runs() >= 2 && framework.own_tasks_spawned() >= 2 &&
                      framework.own_tasks_released() >= 2 && spawner.finished() == 0,
                  static_cast<unsigned long>(job->runs()), 2UL);
  checklist.check("own_task 槽位回收（free_slots == max-1）",
                  spawner.free_slots() == static_cast<int>(embark::max_own_tasks) - 1,
                  static_cast<unsigned long>(spawner.free_slots()),
                  static_cast<unsigned long>(embark::max_own_tasks) - 1UL);
  checklist.check("结尾前台 = clock（回程交给 EEZ 屏按钮，导航壳已退役）",
                  framework.foreground() == framework.id_of(*clock),
                  static_cast<unsigned long>(framework.foreground()), 1UL);
  checklist.check("启动器前台帧 > 0（界面来自 EEZ 屏 launcher）", launcher->foreground_ticks() > 0,
                  static_cast<unsigned long>(launcher->foreground_ticks()), 1UL);
  checklist.check("EEZ 屏表非空（屏名约定：screen == App 名）",
                  embark::demo::eez_ui_bridge_screen_count() > 0,
                  static_cast<unsigned long>(embark::demo::eez_ui_bridge_screen_count()), 1UL);
  checklist.check("EEZ 变量表非空（构建期生成：launcher_tap_count）",
                  embark::demo::eez_ui_bridge_var_count() > 0 &&
                      embark::demo::eez_ui_bridge_var_index("launcher_tap_count") == 0,
                  static_cast<unsigned long>(embark::demo::eez_ui_bridge_var_count()), 1UL);

  ELOG_INFO("启动器最终：enters={} resumes={} 前台帧={}（界面 = EEZ 屏 launcher）", launcher->enters(),
            launcher->resumes(), launcher->foreground_ticks());
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
