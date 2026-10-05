/**
 * 宿主 · UI 演示可执行文件（issues/06，issue 16 改造成启动器验收载具）
 *
 * 从 issue 06 起，这个程序是"唯一 UI 任务"的第一个真实例子：
 *   进程入口 main 只做两件事 —— 起 UI 任务（FreeRTOS 静态任务）+ 进调度器；
 *   真正的启动与主循环全在 ui_main 里（= 那个唯一 UI 任务）：
 *     HAL（SDL 窗口在创建它的线程上泵事件）→ UI 端口（唯一允许调 lv_timer_handler()
 *     的地方）→ Framework（App 注册表 + 前台切换 + 框架导航壳）→ 事件循环。
 *
 * issue 16 起默认前台是 LauncherApp（扇形主屏）。验收开关分四档：
 *   --click [X,Y]   演示启动器两段式点按：先点非选中槽（只转正，不启动），
 *                    弹簧转正后再点选中槽（= 启动目标 App，切到 clock）；
 *                    最后在 clock 屏上点 (X,Y)（默认 = "Settings" 按钮中心）
 *                    验证点击落点随前台切换走。
 *   --drag          合成一次竖直拖动（press → 3 个插值 move → release），
 *                    验证"1px ≈ 0.1 槽"：拖 10px = 转 1 槽，弹簧收敛后停住；
 *                    拖动不触发启动（switches()==0）。
 *   --launch        完整故事线（无人值守验收主路径）：拖动选槽 → 点选中槽切到 clock
 *                    → Settings → Level+1（消息广播）→ 导航壳返回键回 LauncherApp，
 *                    全程断言 switches==3 与各 App 钩子序。
 *   --own-task      验证 own_task 后台 App：TickerApp 的消息要能被 UI 收到。
 *   --screenshot FILE 最后一帧把窗口内容存成 BMP（用 Python/Pillow 转 PNG 便于查看）
 *   --frames N / --scale S / --delay MS / --quit-at N / --help 同旧版。
 *
 * 界面文案：demo App 自己的界面沿用英文（LVGL 默认 Montserrat）；启动器与导航壳
 * （返回键/状态行）用静态子集字库 embark_zh_14（tools/font/gen_font.mjs 生成，
 * 中文标题与 LV_SYMBOL 码点齐备）。日志照旧是中文。
 *
 * 退出码：0 = 正常；1 = 后端起不来；2 = 验收失败（stderr 写明哪一步）。
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL.h>
#include <lvgl.h>

#include <embark/error.h>
#include <embark/framework.h>
#include <embark/launcher_geometry.h>
#include <embark/log.h>
#include <embark_limits.h>

#include "demo_apps.h"
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
// 启动器几何（launcher_geometry.h）：支点 (120,270)、R=82、STEP=26°。
// pos=0 时槽 0 在正上方 (120,188)、槽 1 在 26° 处 (156,196)；选中槽时槽 1 在正上方。
constexpr int launcher_top_x = 120;             // 选中槽中心（正上方）
constexpr int launcher_top_y = 188;
constexpr int slot1_unselected_x = 156;         // 未选中时槽 1 的中心（26° 顺时针）
constexpr int slot1_unselected_y = 196;
constexpr int nav_back_x = 15;                  // NavShell 返回键中心（lv_layer_top）
constexpr int nav_back_y = 14;

// --launch 故事线帧号（确定性；间隔足够避开弹簧收敛期的半稳定状态）
constexpr int drag_press_frame = 15;
constexpr int drag_move_frame = 17;  // 三个插值 move（每两帧一个）
constexpr int drag_release_frame = 23;
constexpr int click_selected_frame = 60;  // 弹簧早已收敛（~帧 52），点选中槽 → clock
constexpr int click_release_delta = 2;
constexpr int click_settings_frame = 72;  // clock 屏的 "Settings"
constexpr int click_level_frame = 84;     // settings 屏的 "Level +1"
constexpr int click_back_frame = 96;      // 导航壳返回键 → LauncherApp
constexpr int launch_quit_frame = 110;
constexpr int launch_frames = 115;

// --click 两段式点按：先"点非选中槽只转正"，弹簧转正后再点选中槽启动
constexpr int click_rotate_frame = 20;
constexpr int click_launch_frame = 64;
constexpr int click_probe_frame = 90;  // 落到 clock 屏（默认点 "Settings" 按钮）

struct Options {
  int frames = 0;
  int scale = 1;
  int delay_ms = 5;
  const char* screenshot = nullptr;
  bool click = false;
  int click_x = embark::demo::demo_click_center_x;
  int click_y = embark::demo::demo_click_center_y;
  bool drag = false;
  int drag_x1 = 120; int drag_y1 = 220;
  int drag_x2 = 120; int drag_y2 = 210;
  bool launch = false;
  int quit_at = 0;
  bool own_task = false;
};

void print_usage() {
  std::printf(
      "用法：embark_host_ui [选项]\n"
      "  --frames N         跑 N 帧后退出（默认按模式：launch 115 / click 110 / drag 80 / own-task 80 / 否则 0 = 一直跑到关窗）\n"
      "  --click [X,Y]      两段式点按演示（默认点按钮中心 %d,%d；帧 20 点非选中槽转正 → 帧 64 点选中槽切到 clock → 帧 90 在 clock 屏点 (X,Y)）\n"
      "  --drag X1,Y1,X2,Y2 合成长拖动（帧 15 按下 → 帧 23 抬起；默认 120,220 → 120,210 = 10px = 1 槽）\n"
      "  --launch            完整验收故事（拖动→启动 clock→Settings→Level+1→返回键回启动器）\n"
      "  --own-task          验证 own_task 后台 App（TickerApp 消息回流）\n"
      "  --screenshot FILE  最后一帧存 BMP\n"
      "  --scale S           窗口放大倍数（默认 1 = 240×320 1:1）\n"
      "  --delay MS          每帧间隔（默认 5）\n"
      "  --quit-at N         第 N 帧推关窗事件\n"
      "  --help              打印用法\n",
      embark::demo::demo_click_center_x, embark::demo::demo_click_center_y);
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
// 之后是 ClockApp/SettingsApp/TickerApp/HelloApp（注册顺序 = 启动器槽位顺序，
// spec/issue 16：启动器必须第一位）。demo App 本体在 app/。
EMBARK_APP_TABLE(embark::demo::LauncherApp, embark::demo::ClockApp, embark::demo::SettingsApp,
                 embark::demo::TickerApp, embark::demo::HelloApp)

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
    else if (std::strcmp(arg, "--quit-at") == 0 && !next_is_flag()) { options.quit_at = std::atoi(argv[++i]); }
    else if (std::strcmp(arg, "--click") == 0) {
      options.click = true;
      if (!next_is_flag()) {
        int x = 0, y = 0;
        if (std::sscanf(argv[++i], "%d,%d", &x, &y) == 2) { options.click_x = x; options.click_y = y; }
      }
    }
    else if (std::strcmp(arg, "--drag") == 0 && !next_is_flag()) {
      options.drag = true;
      int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
      if (std::sscanf(argv[++i], "%d,%d,%d,%d", &x1, &y1, &x2, &y2) == 4) {
        options.drag_x1 = x1; options.drag_y1 = y1; options.drag_x2 = x2; options.drag_y2 = y2;
      }
    }
    else if (std::strcmp(arg, "--launch") == 0) { options.launch = true; }
    else if (std::strcmp(arg, "--own-task") == 0) { options.own_task = true; }
    else if (std::strcmp(arg, "--help") == 0) { print_usage(); std::exit(0); }
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

  ELOG_INFO("框架就绪：{} 个 App，默认前台 {}（剩余 {} 槽位）", framework.apps().size(),
            framework.apps().at(0)->name(),
            embark::launcher::max_slots - framework.apps().size());

  // --- 按模式定帧数上限（0 = 一直跑到关窗）-----------------------------------
  int frames_limit = options->frames;
  if (frames_limit <= 0) {
    if (options->launch) { frames_limit = launch_frames; }
    else if (options->click) { frames_limit = 110; }
    else if (options->drag) { frames_limit = 80; }
    else if (options->own_task) { frames_limit = 80; }
  }

  int frames_run = 0;
  for (;;) {
    framework.step();
    ++frames_run;
    if (framework.exit_requested()) { break; }

    // --- 合成输入（帧号驱动，确定性）-----------------------------------------
    if (options->launch) {
      if (frames_run == drag_press_frame) { push_motion_and_press(options->scale, 120, 220); }
      if (frames_run == drag_move_frame) { push_motion(options->scale, 120, 216); }
      if (frames_run == drag_move_frame + 2) { push_motion(options->scale, 120, 213); }
      if (frames_run == drag_move_frame + 4) { push_motion(options->scale, 120, 210); }
      if (frames_run == drag_release_frame) { push_release(options->scale, 120, 210); }
      if (frames_run == click_selected_frame) { push_motion_and_press(options->scale, launcher_top_x, launcher_top_y); }
      if (frames_run == click_selected_frame + click_release_delta) { push_release(options->scale, launcher_top_x, launcher_top_y); }
      if (frames_run == click_settings_frame) { push_motion_and_press(options->scale, embark::demo::demo_click_center_x, embark::demo::demo_click_center_y); }
      if (frames_run == click_settings_frame + click_release_delta) { push_release(options->scale, embark::demo::demo_click_center_x, embark::demo::demo_click_center_y); }
      if (frames_run == click_level_frame) { push_motion_and_press(options->scale, embark::demo::demo_click_center_x, embark::demo::demo_click_center_y); }
      if (frames_run == click_level_frame + click_release_delta) { push_release(options->scale, embark::demo::demo_click_center_x, embark::demo::demo_click_center_y); }
      if (frames_run == click_back_frame) { push_motion_and_press(options->scale, nav_back_x, nav_back_y); }
      if (frames_run == click_back_frame + click_release_delta) { push_release(options->scale, nav_back_x, nav_back_y); }
      if (frames_run == launch_quit_frame) { SDL_Event quit{}; quit.type = SDL_QUIT; SDL_PushEvent(&quit); }
    } else if (options->click) {
      if (frames_run == click_rotate_frame) { push_motion_and_press(options->scale, slot1_unselected_x, slot1_unselected_y); }
      if (frames_run == click_rotate_frame + click_release_delta) { push_release(options->scale, slot1_unselected_x, slot1_unselected_y); }
      if (frames_run == click_launch_frame) { push_motion_and_press(options->scale, launcher_top_x, launcher_top_y); }
      if (frames_run == click_launch_frame + click_release_delta) { push_release(options->scale, launcher_top_x, launcher_top_y); }
      if (frames_run == click_probe_frame) { push_motion_and_press(options->scale, options->click_x, options->click_y); }
      if (frames_run == click_probe_frame + click_release_delta) { push_release(options->scale, options->click_x, options->click_y); }
    } else if (options->drag) {
      if (frames_run == drag_press_frame) { push_motion_and_press(options->scale, options->drag_x1, options->drag_y1); }
      if (frames_run == drag_move_frame) { push_motion(options->scale, options->drag_x1 + (options->drag_x2 - options->drag_x1) / 3, options->drag_y1 + (options->drag_y2 - options->drag_y1) / 3); }
      if (frames_run == drag_move_frame + 2) { push_motion(options->scale, options->drag_x1 + (options->drag_x2 - options->drag_x1) * 2 / 3, options->drag_y1 + (options->drag_y2 - options->drag_y1) * 2 / 3); }
      if (frames_run == drag_move_frame + 4) { push_motion(options->scale, options->drag_x2, options->drag_y2); }
      if (frames_run == drag_release_frame) { push_release(options->scale, options->drag_x2, options->drag_y2); }
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
  const auto* settings = static_cast<const embark::demo::SettingsApp*>(framework.app(2));
  const auto* ticker = static_cast<const embark::demo::TickerApp*>(framework.app(3));

  ELOG_INFO("显示与前台：{} 帧；前台 {}；switches={}；导航壳返回键 {}（home_requests={}）",
            frames_run, framework.apps().at(framework.foreground())->name(), framework.switches(),
            ui_port.nav_shell().back_visible() ? "可见" : "隐藏",
            ui_port.nav_shell().home_requests());
  ELOG_INFO("启动器：pos={} target={}（×100），selected={} {}（enters={} resumes={} requests={}）",
            static_cast<int>(launcher->position() * 100.0F),
            static_cast<int>(launcher->target() * 100.0F), launcher->selected(),
            launcher->settled() ? "已稳定" : "弹簧中", launcher->enters(), launcher->resumes(),
            launcher->requests());
  ELOG_INFO("后台/消息：clock ticks={} brightness={}；settings level={}；ticker sent={} received={}；LVGL 未回收 {} 字节",
            clock->ticks(), clock->brightness(), settings->level(), ticker->sent(), ticker->received(),
            embark_lvgl_outstanding_bytes());

  int exit_code = 0;
  if (options->launch) {
    const bool launched_ok = framework.switches() == 3 && launcher->enters() == 1 && launcher->resumes() == 1 &&
                             launcher->settled() && launcher->selected() == 1 &&
                             clock->enters() == 1 && clock->resumes() == 0 && clock->ticks() > 0 &&
                             settings->enters() == 1 && settings->resumes() == 0 && clock->brightness() == 1 &&
                             ui_port.nav_shell().home_requests() == 1 &&
                             !ui_port.nav_shell().back_visible() && ui_port.nav_shell().foreground() == 0;
    ELOG_INFO(launched_ok ? "〔launch 验收通过〕" : "〔launch 验收失败〕");
    if (!launched_ok) { exit_code = 2; }
  }
  if (options->click) {
    // 非选中槽转正（不启动）→ 选中槽启动 → probe 点击落在 clock 屏的按钮上
    const bool clicked_ok = framework.switches() == 2 && settings->enters() == 1 &&
                            settings->resumes() == 0 && clock->enters() == 1 && clock->resumes() == 0 &&
                            launcher->enters() == 1 && launcher->resumes() == 0;
    ELOG_INFO(clicked_ok ? "〔click 验收通过〕" : "〔click 验收失败〕");
    if (!clicked_ok) { exit_code = 2; }
  }
  if (options->drag) {
    // 预期 = 松手瞬间的槽号（drag_target 夹取 + settle 时 lround）
    const int expected = embark::launcher::selected_index(
        embark::launcher::drag_target(0.0F, static_cast<float>(options->drag_y2 - options->drag_y1),
                                      static_cast<int>(launcher->slot_count())),
        static_cast<int>(launcher->slot_count()));
    const bool dragged_ok = launcher->settled() && launcher->selected() == expected &&
                            framework.switches() == 0;  // 拖动只转正，不启动
    ELOG_INFO("〔drag 验收{}〕：预期槽 {}，实际 {}（dist={}px）", dragged_ok ? "通过" : "失败",
              expected, launcher->selected(), options->drag_y2 - options->drag_y1);
    if (!dragged_ok) { exit_code = 2; }
  }
  if (options->own_task) {
    const bool ticked_ok = ticker->sent() > 0 && ticker->received() > 0;
    ELOG_INFO(ticked_ok ? "〔own-task 验收通过〕" : "〔own-task 验收失败〕");
    if (!ticked_ok) { exit_code = 2; }
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