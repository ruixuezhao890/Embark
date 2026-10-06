/**
 * 宿主 · UI 演示可执行文件（issues/06；issue 16 起点；issue 19 起界面全部由
 * EEZ Studio 生成代码接管 —— 手绘 UI 已退役，App 只剩薄壳逻辑）
 *
 * 从 issue 06 起，这个程序是"唯一 UI 任务"的第一个真实例子：
 *   进程入口 main 只做两件事 —— 起 UI 任务（FreeRTOS 静态任务）+ 进调度器；
 *   真正的启动与主循环全在 ui_main 里（= 那个唯一 UI 任务）：
 *     HAL（SDL 窗口在创建它的线程上泵事件）→ UI 端口（唯一允许调 lv_timer_handler()
*     的地方）→ Framework（App 注册表 + 前台切换）→ 事件循环。
 *
 * 默认前台是 LauncherApp，它的主屏与 clock 屏一样来自 EEZ Studio 生成代码
 * （app/eez_ui/src/ui/；屏名约定 == App 名，子页 = <app名>_<编号>_sub，见
 * eez_ui_bridge.h / eez_ui_nav.h）：EEZ 屏上控件的 Flow SetPage 被桥翻成"切到
 * 同名 App"，所以点 EEZ 屏上的按钮 = 启动那个 App。手绘的扇形启动器与 demo App
 * 手绘按钮已全部下线（demo_apps.cpp 用 bump_level() 接替 Level +1 的触发点）。
 *
 * 验收开关分五档（全部由合成 SDL 输入 + 帧号驱动，确定性）：
 *   --click [X,Y]   点 EEZ launcher 屏的按钮（Flow SetPage → clock 屏 → 切 ClockApp），
 *                   再在 clock 屏上点 (X,Y)（默认 = clock 屏按钮中心，回启动器），
 *                   验证点击落点随前台切换走。
 *   --drag X1,Y1,X2,Y2  合成一次拖动（默认在空白处 40,60 → 100,140）：EEZ 屏上
 *                   没有可拖动的槽位了，这里只作输入通路冒烟 —— press → 3 个插值
 *                   move → release 全程不误触按钮、不切 App。
 *   --launch        完整故事线（无人值守验收主路径）：程序化 request_switch("clock")
 *                   → 点 clock 屏按钮回启动器 → 点 launcher 屏按钮再进 clock →
*                   再点 clock 屏按钮回启动器 → settings->bump_level()（消息广播）；
 *                   断言 switches==4 与各 App 钩子序。
 *   --own-task      验证 own_task 后台 App：TickerApp 的消息要能被 UI 收到。
 *   --eez           EEZ 验收（issue 19 / ADR 0008，issue 21 起接屏名约定）：屏表自检
 *                   + 变量表自检（launcher_tap_count 构建期生成）+ 变量读写往返
 *                   → 点 EEZ 屏按钮驱动切 App（Flow SetPage → 屏名 → request_switch）
*                   → 程序化切到 EEZ 宿主 App eezdemo（验证它的前台 tick）→ 点
*                   clock 屏按钮回启动器；断言 switches==3、nav_requests==2、last_screen=="launcher"。
 *   --screenshot FILE 最后一帧把窗口内容存成 BMP（用 Python/Pillow 转 PNG 便于查看）
 *   --frames N / --scale S / --delay MS / --quit-at N / --help 同旧版。
 *
 * 界面文案：demo App 界面与 EEZ 生成屏都用 LVGL 默认字体（Montserrat）；导航壳
 * 已退役（2026-10-06），不再有框架级叠加层；日志照旧是中文。
 * 日志照旧是中文。
 *
 * 退出码：0 = 正常；1 = 后端起不来或用法错误；2 = 验收失败（日志写明哪一步）。
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL.h>
#include <lvgl.h>

#include <embark/error.h>
#include <embark/framework.h>
#include <embark/log.h>
#include <embark_limits.h>

#include "demo_apps.h"
#include "eez_demo_app.h"
#include "eez_ui_bridge.h"
#include "eez_ui_nav.h"
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

// --- 合成输入的帧号与几何 ----------------------------------------------------
// 每帧 = HAL 一跳（默认 5 ms）。按下后隔两帧抬起，让 LVGL 分两个读周期处理。
// 两个屏的按钮几何（app/eez_ui/src/ui/screens.c）：
//   launcher 屏按钮 obj0 pos(70,199) 102×50 → 中心 (121,224)：Flow SetPage → clock 屏；
//   clock 屏按钮  obj1 pos(70,135) 100×50 → 中心 (120,160)：Flow SetPage → launcher 屏。
// 手写 demo App 的按钮几何已随 UI 退役（demo_apps.h 不再有 demo_click_center_*）。
constexpr int launcher_button_x = 121;  // EEZ launcher 屏按钮（Flow SetPage → clock 屏）
constexpr int launcher_button_y = 224;
constexpr int clock_button_x = 120;     // EEZ clock 屏按钮（Flow SetPage → launcher 屏）
constexpr int clock_button_y = 160;

// 合成点击：press_frame 按下，隔 click_release_delta 帧抬起（LVGL 分两个读周期）。
constexpr int press_frame = 20;
constexpr int click_release_delta = 2;

// --click 故事线：点 launcher 屏按钮切 clock（帧 20），再点 clock 屏按钮回启动器（帧 60）。
constexpr int click_second_frame = 60;
constexpr int click_frames = 80;

// --launch 故事线（程序化启动 + 真点击混合，间隔足够避开一次切换的收敛期）：
//   帧 20 request_switch("clock")；帧 40 点 clock 屏按钮 → 回启动器（屏名约定驱动）；
//   帧 60 点 launcher 屏按钮 → 再进 clock；帧 80 settings->bump_level()（手绘
//   Level +1 的逻辑入口，消息广播到 clock）；帧 90 再点 clock 屏按钮 → 回启动器
//   （EEZ 屏内按钮承担返回，导航壳已退役）。
constexpr int launch_switch_frame = 20;
constexpr int launch_clock_back_frame = 40;
constexpr int launch_launcher_btn_frame = 60;
constexpr int launch_bump_frame = 80;
constexpr int launch_home_frame = 90;
constexpr int launch_quit_frame = 102;
constexpr int launch_frames = 110;

// --eez 故事线（issue 19 / ADR 0008）：点 launcher 屏按钮（帧 20）→ Flow SetPage 到
// clock 屏 → 桥的屏观察者 request_switch("clock") → ClockApp 前台；帧 60 程序化切到
// eezdemo（EEZ 宿主 App，验证它的 onForegroundTick 在跑）。帧 90 再点 clock 屏按钮
// （eezdemo 没有同名屏、保持的当前屏）→ Flow SetPage 回 launcher 屏 → 观察者
// request_switch 回启动器 —— 返回由 EEZ 屏内按钮承担（导航壳已退役）。
constexpr int eez_switch_frame = press_frame;
constexpr int eez_demo_frame = 60;
constexpr int eez_home_frame = 90;
constexpr int eez_quit_frame = 114;
constexpr int eez_frames = 122;

// --drag 冒烟：press 15 → 三个插值 move（17/19/21）→ release 23。
constexpr int drag_press_frame = 15;
constexpr int drag_move_frame = 17;
constexpr int drag_release_frame = 23;
constexpr int drag_frames = 80;

struct Options {
  int frames = 0;
  int scale = 1;
  int delay_ms = 5;
  const char* screenshot = nullptr;
  int shot_frame = 0;      // --shot-frame N FILE：第 N 帧截图（任意模式）
  const char* shot_path = nullptr;
  bool click = false;
  int click_x = clock_button_x;   // 默认 = clock 屏按钮中心（回启动器）
  int click_y = clock_button_y;
  bool drag = false;
  int drag_x1 = 40;   int drag_y1 = 60;   // 默认落在 EEZ 屏的空白处（不碰按钮）
  int drag_x2 = 100;  int drag_y2 = 140;
  bool launch = false;
  int quit_at = 0;
  bool own_task = false;
  bool eez = false;
};

void print_usage() {
  std::printf(
      "用法：embark_host_ui [选项]\n"
"  --frames N         跑 N 帧后退出（默认按模式：launch 110 / click 80 / drag 80 / own-task 80 / eez 122 / 否则 0 = 一直跑到关窗）\n"
      "  --click [X,Y]      点 launcher 屏按钮切 clock（帧 20）→ 帧 60 在 clock 屏点 (X,Y)（默认按钮中心 %d,%d = 回启动器）\n"
      "  --drag X1,Y1,X2,Y2 合成拖动冒烟（帧 15 按下 → 帧 23 抬起；默认 40,60 → 100,140，落在空白处不误触按钮）\n"
"  --launch            完整验收故事（request_switch('clock')→点屏按钮往返→bump_level 消息）\n"
      "  --own-task          验证 own_task 后台 App（TickerApp 消息回流）\n"
      "  --eez               EEZ 验收（issue 19）：屏表/变量表自检 + 变量读写往返 + EEZ 屏点按钮切 App\n"
      "  --screenshot FILE  最后一帧存 BMP\n"
      "  --shot-frame N FILE 第 N 帧存 BMP（配 --eez 等故事线抓中途屏）\n"
      "  --scale S           窗口放大倍数（默认 1 = 240×320 1:1）\n"
      "  --delay MS          每帧间隔（默认 5）\n"
      "  --quit-at N         第 N 帧推关窗事件\n"
      "  --help              打印用法\n",
      clock_button_x, clock_button_y);
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

/// 拖动中间帧只推 motion（不按不抬）：LVGL 读到的还是"按住移动"。
void push_motion(int scale, int panel_x, int panel_y) {
  SDL_Event motion{};
  motion.type = SDL_MOUSEMOTION;
  motion.motion.x = panel_x * scale;
  motion.motion.y = panel_y * scale;
  SDL_PushEvent(&motion);
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

}  // namespace

namespace embark::platform::host {

// 整个可执行文件只出现一次的 App 注册表：LauncherApp（启动器）首位 = 默认前台，
// 之后是 ClockApp/SettingsApp/TickerApp/HelloApp/EezDemoApp。启动器与 clock 屏的
// 界面来自 EEZ Studio（app/eez_ui/），启动器仍必须第一位（默认前台）。
EMBARK_APP_TABLE(embark::demo::LauncherApp, embark::demo::ClockApp, embark::demo::SettingsApp,
                 embark::demo::TickerApp, embark::demo::HelloApp, embark::demo::EezDemoApp)

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
    auto next_is_flag = [&]() { return i + 1 >= argc || argv[i + 1][0] == '-'; };
    if (std::strcmp(arg, "--frames") == 0 && !next_is_flag()) { options.frames = std::atoi(argv[++i]); }
    else if (std::strcmp(arg, "--scale") == 0 && !next_is_flag()) { options.scale = std::atoi(argv[++i]); }
    else if (std::strcmp(arg, "--delay") == 0 && !next_is_flag()) { options.delay_ms = std::atoi(argv[++i]); }
    else if (std::strcmp(arg, "--screenshot") == 0 && !next_is_flag()) { options.screenshot = argv[++i]; }
    else if (std::strcmp(arg, "--shot-frame") == 0 && i + 2 < argc) {
      options.shot_frame = std::atoi(argv[++i]);
      options.shot_path = argv[++i];
    }
    else if (std::strcmp(arg, "--quit-at") == 0 && !next_is_flag()) { options.quit_at = std::atoi(argv[++i]); }
    else if (std::strcmp(arg, "--click") == 0) {
      options.click = true;
      if (!next_is_flag()) {
        int x = 0, y = 0;
        if (std::sscanf(argv[++i], "%d,%d", &x, &y) == 2) { options.click_x = x; options.click_y = y; }
      }
    }
    else if (std::strcmp(arg, "--drag") == 0) {
      options.drag = true;
      if (!next_is_flag()) {
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        if (std::sscanf(argv[++i], "%d,%d,%d,%d", &x1, &y1, &x2, &y2) == 4) {
          options.drag_x1 = x1; options.drag_y1 = y1; options.drag_x2 = x2; options.drag_y2 = y2;
        }
      }
    }
    else if (std::strcmp(arg, "--launch") == 0) { options.launch = true; }
    else if (std::strcmp(arg, "--own-task") == 0) { options.own_task = true; }
    else if (std::strcmp(arg, "--eez") == 0) { options.eez = true; }
    else if (std::strcmp(arg, "--help") == 0) { print_usage(); std::exit(0); }
    else {
      // 严格解析：不认识的选项/缺参数直接报错退出（旧版静默忽略会让 --drag
      // 少了坐标时退化成"跑到底不退出"，非常难查）。
      std::fprintf(stderr, "未知选项或缺参数：%s\n", arg);
      print_usage();
      std::exit(1);
    }
  }
  return options;
}

}  // namespace

/// 唯一 UI 任务（spec §6：唯一能碰 LVGL 的线程，窗口事件也在它上面泵）。
void ui_main(void* argument) noexcept {
  const Options* options = static_cast<const Options*>(argument);

  hp::HostHal& hal = hp::HostHal::instance();
  hp::HostDisplay display(static_cast<std::uint8_t>(options->scale), "Embark 宿主 UI");
  hp::HostInput input(hal.time(), display);
  hal.attach_display(display);
  hal.attach_input(input);

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

  ELOG_INFO("框架就绪：{} 个 App，默认前台 {}（上限 {}）", framework.apps().size(),
            framework.apps().at(0)->name(), embark::max_apps);

  // --- 按模式定帧数上限（0 = 一直跑到关窗）-----------------------------------
  int frames_limit = options->frames;
  if (frames_limit <= 0) {
    if (options->launch) { frames_limit = launch_frames; }
    else if (options->click) { frames_limit = click_frames; }
    else if (options->drag) { frames_limit = drag_frames; }
    else if (options->own_task) { frames_limit = 80; }
    else if (options->eez) { frames_limit = eez_frames; }
  }

  int frames_run = 0;
  for (;;) {
    framework.step();
    ++frames_run;
    if (framework.exit_requested()) { break; }

    // --- 合成输入（帧号驱动，确定性）-----------------------------------------
    if (options->launch) {
      // 启动器已无手绘界面：先程序化请求切到 clock；随后的往返全是真点击 ——
      // 点 EEZ 屏按钮（屏名约定 → 切同名 App），回程同由 EEZ 屏按钮承担。
      if (frames_run == launch_switch_frame) { (void)framework.request_switch("clock"); }
      if (frames_run == launch_clock_back_frame) { push_motion_and_press(options->scale, clock_button_x, clock_button_y); }
      if (frames_run == launch_clock_back_frame + click_release_delta) { push_release(options->scale, clock_button_x, clock_button_y); }
      if (frames_run == launch_launcher_btn_frame) { push_motion_and_press(options->scale, launcher_button_x, launcher_button_y); }
      if (frames_run == launch_launcher_btn_frame + click_release_delta) { push_release(options->scale, launcher_button_x, launcher_button_y); }
      if (frames_run == launch_home_frame) { push_motion_and_press(options->scale, clock_button_x, clock_button_y); }
      if (frames_run == launch_home_frame + click_release_delta) { push_release(options->scale, clock_button_x, clock_button_y); }
      if (frames_run == launch_bump_frame) { static_cast<embark::demo::SettingsApp*>(framework.app(2))->bump_level(); }
      if (frames_run == launch_quit_frame) { SDL_Event quit{}; quit.type = SDL_QUIT; SDL_PushEvent(&quit); }
    } else if (options->eez) {
      // 点 launcher 屏按钮：Flow SetPage 到 clock 屏 → 桥的屏观察者把这次切屏翻成
      // request_switch("clock")（帧 20 按下 → 帧 21/22 抬起 → 下次 tick 生效）。
      if (frames_run == eez_switch_frame) { push_motion_and_press(options->scale, launcher_button_x, launcher_button_y); }
      if (frames_run == eez_switch_frame + click_release_delta) { push_release(options->scale, launcher_button_x, launcher_button_y); }
      // 程序化切到 EEZ 宿主 App（eezdemo）：验证它的 onForegroundTick 一直在跑。
      if (frames_run == eez_demo_frame) { (void)framework.request_switch("eezdemo"); }
      // 回程：点 clock 屏按钮（eezdemo 保持的当前屏）→ Flow SetPage 回 launcher 屏 →
      // 观察者 request_switch 回启动器（EEZ 屏内按钮承担返回，无导航壳）。
      if (frames_run == eez_home_frame) { push_motion_and_press(options->scale, clock_button_x, clock_button_y); }
      if (frames_run == eez_home_frame + click_release_delta) { push_release(options->scale, clock_button_x, clock_button_y); }
      if (frames_run == eez_quit_frame) { SDL_Event quit{}; quit.type = SDL_QUIT; SDL_PushEvent(&quit); }
    } else if (options->click) {
      // 帧 20：点 launcher 屏按钮（切到 clock）；帧 60：在 clock 屏点 (X,Y)
      // （默认 = clock 屏按钮中心 → Flow SetPage 回 launcher 屏 → 回启动器）。
      if (frames_run == press_frame) { push_motion_and_press(options->scale, launcher_button_x, launcher_button_y); }
      if (frames_run == press_frame + click_release_delta) { push_release(options->scale, launcher_button_x, launcher_button_y); }
      if (frames_run == click_second_frame) { push_motion_and_press(options->scale, options->click_x, options->click_y); }
      if (frames_run == click_second_frame + click_release_delta) { push_release(options->scale, options->click_x, options->click_y); }
    } else if (options->drag) {
      if (frames_run == drag_press_frame) { push_motion_and_press(options->scale, options->drag_x1, options->drag_y1); }
      if (frames_run == drag_move_frame) { push_motion(options->scale, options->drag_x1 + (options->drag_x2 - options->drag_x1) / 3, options->drag_y1 + (options->drag_y2 - options->drag_y1) / 3); }
      if (frames_run == drag_move_frame + 2) { push_motion(options->scale, options->drag_x1 + (options->drag_x2 - options->drag_x1) * 2 / 3, options->drag_y1 + (options->drag_y2 - options->drag_y1) * 2 / 3); }
      if (frames_run == drag_move_frame + 4) { push_motion(options->scale, options->drag_x2, options->drag_y2); }
      if (frames_run == drag_release_frame) { push_release(options->scale, options->drag_x2, options->drag_y2); }
    }
    if (options->shot_path != nullptr && frames_run == options->shot_frame) {
      if (!save_screenshot(display, options->shot_path)) { hp::exit_process(1); }
    }
    if (options->quit_at > 0 && frames_run == options->quit_at) {
      SDL_Event quit{}; quit.type = SDL_QUIT; SDL_PushEvent(&quit);
    }

    if (frames_limit > 0 && frames_run >= frames_limit) {
      if (options->screenshot != nullptr && !save_screenshot(display, options->screenshot)) {
        hp::exit_process(1);
      }
      break;
    }
    hp::ui_loop_delay(static_cast<std::uint32_t>(options->delay_ms));
  }

  // --- 观测与断言 -------------------------------------------------------------
  const auto* launcher = static_cast<const embark::demo::LauncherApp*>(framework.app(0));
  const auto* clock = static_cast<const embark::demo::ClockApp*>(framework.app(1));
  auto* settings = static_cast<embark::demo::SettingsApp*>(framework.app(2));
  const auto* ticker = static_cast<const embark::demo::TickerApp*>(framework.app(3));
  const auto* eez_app = static_cast<const embark::demo::EezDemoApp*>(framework.app(5));

  ELOG_INFO("显示与前台：{} 帧；前台 {}；switches={}",
            frames_run, framework.apps().at(framework.foreground())->name(), framework.switches());
  ELOG_INFO("启动器（界面来自 EEZ 屏 \"launcher\"）：enters={} resumes={} 前台帧={}",
            launcher->enters(), launcher->resumes(), launcher->foreground_ticks());
  ELOG_INFO("后台/消息：clock ticks={} brightness={}；settings level={}；ticker sent={} received={}；LVGL 未回收 {} 字节",
            clock->ticks(), clock->brightness(), settings->level(), ticker->sent(), ticker->received(),
            embark_lvgl_outstanding_bytes());

  int exit_code = 0;
  if (options->launch) {
    const char* last_screen = embark::demo::eez_ui_nav_last_screen();
    const bool launched_ok = framework.switches() == 4 && launcher->enters() == 1 &&
                             launcher->resumes() == 2 && launcher->foreground_ticks() > 0 &&
                             clock->enters() == 1 && clock->resumes() == 1 && clock->ticks() > 0 &&
                             clock->brightness() == 1 &&
                             embark::demo::eez_ui_nav_switch_requests() == 3 &&
                             last_screen != nullptr && std::strcmp(last_screen, "launcher") == 0 &&
                             embark::demo::eez_ui_bridge_current_screen() == 1 &&
                             framework.foreground() == 0;
    if (launched_ok) { ELOG_INFO("〔launch 验收通过〕"); } else { ELOG_INFO("〔launch 验收失败〕"); }
    if (!launched_ok) { exit_code = 2; }
  }
  if (options->click) {
    // 点 launcher 屏按钮 → 切 clock（EEZ 驱动）→ 在 clock 屏点 (X,Y)（默认回启动器）。
    // 点击落点随前台切换走：同一套坐标只对当前屏的按钮有效，这正是薄壳切换的验收点。
    const char* last_screen = embark::demo::eez_ui_nav_last_screen();
    const bool clicked_ok = framework.switches() == 2 && launcher->enters() == 1 &&
                            launcher->resumes() == 1 && clock->enters() == 1 && clock->resumes() == 0 &&
                            settings->enters() == 0 &&
                            embark::demo::eez_ui_nav_switch_requests() == 2 &&
                            last_screen != nullptr && std::strcmp(last_screen, "launcher") == 0 &&
                            embark::demo::eez_ui_bridge_current_screen() == 1 &&
                            framework.foreground() == 0;
    if (clicked_ok) { ELOG_INFO("〔click 验收通过〕"); } else { ELOG_INFO("〔click 验收失败〕"); }
    if (!clicked_ok) { exit_code = 2; }
  }
  if (options->drag) {
    // EEZ 屏上没有可拖动的槽位了：拖动只走输入通路，不误触按钮、不切 App。
    const bool dragged_ok = framework.switches() == 0 && launcher->enters() == 1 &&
                            framework.foreground() == 0;
    ELOG_INFO("〔drag 验收{}〕：拖动 {}px，前台 {}，switches={}", dragged_ok ? "通过" : "失败",
              options->drag_y2 - options->drag_y1, framework.apps().at(framework.foreground())->name(),
              framework.switches());
    if (!dragged_ok) { exit_code = 2; }
  }
  if (options->own_task) {
    const bool ticked_ok = ticker->sent() > 0 && ticker->received() > 0;
    if (ticked_ok) { ELOG_INFO("〔own-task 验收通过〕"); } else { ELOG_INFO("〔own-task 验收失败〕"); }
    if (!ticked_ok) { exit_code = 2; }
  }
  if (options->eez) {
    // 故事线：点 launcher 屏按钮 → Flow SetPage 到 clock 屏 → 桥的屏观察者 →
    // request_switch("clock") → ClockApp 前台（switches 1）→ 程序化切 eezdemo
// （switches 2，EEZ 宿主 App 的前台 tick 在跑）→ 点 clock 屏按钮回启动器（switches 3）。
    // 这就是"EEZ 里切屏 = 切前台 App"（屏名约定 == App 名，见 eez_ui_nav.h）。
    // 屏表（构建期从 EEZ 生成代码解析）自检：表非空，且每项都能名字→id 反查回来
    // （未来按约定加屏，这里随之增长，不需要改本文件）。
    const int screen_count = embark::demo::eez_ui_bridge_screen_count();
    bool screen_table_ok = screen_count > 0;
    for (int i = 0; i < screen_count; ++i) {
      const char* screen_name = embark::demo::eez_ui_bridge_screen_name(i);
      ELOG_INFO("eez 屏表[{}]：{}", i, screen_name != nullptr ? screen_name : "<null>");
      if (screen_name == nullptr || embark::demo::eez_ui_bridge_screen_id(screen_name) < 0) {
        screen_table_ok = false;
      }
    }
    // 变量表（构建期从 eez-flow.h 的 FlowGlobalVariables 解析）自检：表非空、按名可查、
    // 未命中返回 kEezVarNone；再对 launcher_tap_count 做 int/float/bool 三型读写往返
    // （变量桥按名读写 == UI 显示数据的接口，见 eez_ui_bridge.h「四」）。
    const int var_count = embark::demo::eez_ui_bridge_var_count();
    bool var_table_ok = var_count > 0;
    for (int i = 0; i < var_count; ++i) {
      const char* var_name = embark::demo::eez_ui_bridge_var_name(i);
      ELOG_INFO("eez 变量表[{}]：{}", i, var_name != nullptr ? var_name : "<null>");
      if (var_name == nullptr || embark::demo::eez_ui_bridge_var_index(var_name) < 0) {
        var_table_ok = false;
      }
    }
    bool var_roundtrip_ok = true;
    if (embark::demo::eez_ui_bridge_var_index("launcher_tap_count") == 0) {
      std::int32_t got_int = 0;
      float got_float = 0.0f;
      bool got_bool = false;
      const bool set_int = embark::demo::eez_ui_bridge_set_var_int("launcher_tap_count", 42);
      const bool get_int = embark::demo::eez_ui_bridge_get_var_int("launcher_tap_count", got_int);
      const bool set_float = embark::demo::eez_ui_bridge_set_var_float("launcher_tap_count", 3.5f);
      const bool get_float = embark::demo::eez_ui_bridge_get_var_float("launcher_tap_count", got_float);
      const bool set_bool = embark::demo::eez_ui_bridge_set_var_bool("launcher_tap_count", true);
      const bool get_bool = embark::demo::eez_ui_bridge_get_var_bool("launcher_tap_count", got_bool);
      var_roundtrip_ok = set_int && get_int && got_int == 42 && set_float && get_float &&
                         got_float == 3.5f && set_bool && get_bool && got_bool;
      ELOG_INFO("变量读写往返：int {} -> {}（{}），float {} -> {}（{}），bool true -> {}（{}）",
                set_int ? "set" : "fail", got_int, get_int ? "get" : "fail",
                set_float ? "set" : "fail", got_float, get_float ? "get" : "fail",
                set_bool ? "set" : "fail", got_bool, get_bool ? "get" : "fail");
    } else {
      var_roundtrip_ok = false;
    }
    const bool no_such_var_ok = embark::demo::eez_ui_bridge_var_index("no_such_var") ==
                                embark::demo::kEezVarNone;
    if (!no_such_var_ok) { var_roundtrip_ok = false; }
    const char* last_screen = embark::demo::eez_ui_nav_last_screen();
    const bool eez_ok = framework.switches() == 3 && launcher->enters() == 1 &&
                        launcher->resumes() == 1 && launcher->foreground_ticks() > 0 &&
                        clock->enters() == 1 && clock->resumes() == 0 &&
                        eez_app->enters() == 1 && eez_app->resumes() == 0 &&
                        eez_app->foreground_ticks() > 0 &&
                        embark::demo::eez_ui_nav_switch_requests() == 2 &&
                        last_screen != nullptr && std::strcmp(last_screen, "launcher") == 0 &&
                        embark::demo::eez_ui_bridge_current_screen() == 1 &&
                        framework.foreground() == 0 && screen_table_ok && var_table_ok &&
                        var_roundtrip_ok && no_such_var_ok;
    ELOG_INFO(
        "eez: switches={} enters={} resumes={} ticks={} clock_enters={} eez_enters={} eez_ticks={} current_screen={} screens={} vars={} nav_requests={} last_screen={}",
        framework.switches(), launcher->enters(), launcher->resumes(), launcher->foreground_ticks(),
        clock->enters(), eez_app->enters(), eez_app->foreground_ticks(),
        embark::demo::eez_ui_bridge_current_screen(), screen_count, var_count,
        embark::demo::eez_ui_nav_switch_requests(),
        last_screen != nullptr ? last_screen : "<none>");
    if (eez_ok) { ELOG_INFO("〔eez 验收通过〕"); } else { ELOG_INFO("〔eez 验收失败〕"); }
    if (!eez_ok) { exit_code = 2; }
  }

  framework.shutdown();
  hp::exit_process(exit_code);
}

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  Options options = parse_options(argc, argv);

  ELOG_INFO("UI 任务启动：栈 {} 字（{} KB），优先级 {}，周期 {} ms", embark::ui_task_stack_words,
            embark::ui_task_stack_words * static_cast<int>(sizeof(StackType_t)) / 1024,
            static_cast<int>(embark::ui_task_priority), embark::ui_loop_period_ms);

  const Error task_error = hp::start_ui_task(&ui_main, &options);
  if (task_error != Error::none) {
    char text[24];
    std::fprintf(stderr, "启动 UI 任务失败：%s\n", embark::error_text(text, task_error));
    return 1;
  }

  hp::start_scheduler();
  return 0;
}
