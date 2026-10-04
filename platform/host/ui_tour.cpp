/**
 * 宿主 · 完整系统用例：从启动到前台切换（一条命令看全流程）
 *
 * 和 ui_demo.cpp 的区别只有一点：ui_demo 是"带开关的验收工具"，这个程序是
 * "一次走完的系统讲解"—— 不加任何参数就会把整条生命周期跑一遍，并逐步解说：
 *
 *   ① 进程入口：起唯一 UI 任务（FreeRTOS 静态任务）→ 进调度器，自己不干活
 *   ② HAL 初始化：时间 / 持久化 / 日志 / 系统 / 总线 / 显示 / 输入，逐个报状态
 *   ③ 框架 boot：4 个 App 注册 → 打印每个 App 的后台策略 → 默认前台 clock
 *   ④ 后台节拍：clock 的 onBackgroundTick 由框架定时器驱动（没占 UI 任务）
 *   ⑤ 前台切换 #1：合成点击 "Settings" → clock 退场、settings 进场（onEnter）
 *   ⑥ App 间消息：settings 屏点 "Level +1" → clock 收到亮度消息
 *   ⑦ 前台切换 #2：点 "Back to clock" → clock 走 onResume（不是 onEnter）
 *   ⑧ 真正的任务切换：own_task 的 ticker 任务发消息 → UI 任务收（SPSC 队列）
 *   ⑨ 关窗退出：SDL_QUIT → framework.exit_requested() → 收尾统计 + LVGL 堆账
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
 * 界面文案是英文（LVGL 内置字体只有 Montserrat），日志是中文。
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
#include <embark/version.h>
#include <embark_limits.h>

#include "demo_apps.h"
#include "hello_app.h"
#include "host_context.h"
#include "host_display.h"
#include "host_input.h"
#include "host_lvgl_mem.h"
#include "lvgl_port.h"
#include "lvgl_ui_port.h"
#include "own_task_spawner.h"
#include "ui_task.h"

namespace {

// --- 用例时间线（帧号）-------------------------------------------------------
// 每帧 = HAL 的一跳（默认 5 ms），帧号是确定性的，所以这条用例每次跑法完全一样。
constexpr int tick_checkpoint_first = 24;  ///< ④ 第一次看后台节拍
constexpr int click_settings_frame = 30;   ///< ⑤ 合成点击 clock 屏的 "Settings"
constexpr int click_release_delta = 2;  ///< 按下后隔两帧抬起（让 LVGL 分两个读周期看）
constexpr int click_level_frame = 40;       ///< ⑥ 合成点击 settings 屏的 "Level +1"
constexpr int click_back_frame = 50;        ///< ⑦ 合成点击 "Back to clock"
constexpr int own_task_checkpoint = 60;     ///< ⑧ own_task 的消息回流
constexpr int tick_checkpoint_second = 70;  ///< ④' 第二次看后台节拍（对比增长）
constexpr int quit_frame = 79;      ///< ⑨ 推 SDL_QUIT（等价于点窗口关闭按钮）
constexpr int tour_end_frame = 80;  ///< 最多跑到这里

struct Options {
  int scale = 1;     ///< 默认 1:1：窗口就是 240×320
  int delay_ms = 5;  ///< 每帧让出的毫秒数
  int frames = tour_end_frame;
  const char* screenshot = nullptr;
};

void print_usage() {
  std::printf(
      "用法：embark_host_tour [选项]\n"
      "  不带参数就会完整跑一遍：启动 → HAL 就绪 → 框架 boot → 后台节拍 →\n"
      "  前台切换（clock→settings→clock）→ App 间消息 → own_task 消息回流 → 关窗收尾\n"
      "  --scale S          窗口放大倍数（默认 1 = 240×320 与面板像素 1:1）\n"
      "  --delay MS         每帧间隔毫秒（默认 5；调大可放慢观察）\n"
      "  --frames N         最多跑 N 帧（默认 %d = 用例结束）\n"
      "  --screenshot FILE  收尾时存一张 BMP 截图\n"
      "  --help             显示本帮助\n",
      tour_end_frame);
}

Options parse_options(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const char* arg = argv[index];
    if (std::strcmp(arg, "--scale") == 0 && index + 1 < argc) {
      options.scale = std::atoi(argv[++index]);
    } else if (std::strcmp(arg, "--delay") == 0 && index + 1 < argc) {
      options.delay_ms = std::atoi(argv[++index]);
    } else if (std::strcmp(arg, "--frames") == 0 && index + 1 < argc) {
      options.frames = std::atoi(argv[++index]);
    } else if (std::strcmp(arg, "--screenshot") == 0 && index + 1 < argc) {
      options.screenshot = argv[++index];
    } else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
      print_usage();
      std::exit(0);
    }
  }
  if (options.scale < 1) {
    options.scale = 1;
  }
  if (options.delay_ms < 0) {
    options.delay_ms = 0;
  }
  if (options.frames <= 0) {
    options.frames = tour_end_frame;
  }
  return options;
}

/// 后台策略的汉语说法（打印 App 清单用）。
const char* policy_text(embark::BackgroundPolicy policy) noexcept {
  switch (policy) {
    case embark::BackgroundPolicy::suspend:
      return "suspend（退场即挂起，不再被叫）";
    case embark::BackgroundPolicy::tick:
      return "tick（框架定时器驱动 onBackgroundTick）";
    case embark::BackgroundPolicy::own_task:
      return "own_task（独占一个 FreeRTOS 任务）";
  }
  return "未知策略";
}

// --- 合成输入：进 SDL 的事件队列，和"人在窗口上点一下"走同一条链路 ------------

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

}  // namespace

namespace embark::platform::host {

// 进程级的 App 注册表：顺序 = ClockApp（默认前台）、SettingsApp、TickerApp（纯后台）、
// HelloApp（最简模板，挂在末位）。
EMBARK_APP_TABLE(embark::demo::ClockApp, embark::demo::SettingsApp, embark::demo::TickerApp,
                 embark::demo::HelloApp)

}  // namespace embark::platform::host

using embark::Error;
using embark::Framework;

namespace hp = embark::platform::host;

namespace {

/// 宿主的退出来源：SDL 的关窗标志（hal::IInput 里没有"退出"这个概念，端口收一个回调）。
bool host_exit_query(void* context) noexcept {
  return static_cast<hp::HostInput*>(context)->quit_requested();
}

/// 自检清单：一条一行，全过才算这轮用例成功。
struct CheckList {
  bool ok = true;

  void check(const char* title, bool passed, unsigned long measured, unsigned long expected) {
    if (passed) {
      ELOG_INFO(" [通过] {}：实测 {}（期望 {}）", title, measured, expected);
    } else {
      ELOG_ERROR(" [失败] {}：实测 {}（期望 {}）", title, measured, expected);
      ok = false;
    }
  }
};

}  // namespace

/// 唯一 UI 任务：HAL → UI 端口 → Framework → 帧循环，全在这一条线上（spec §6）。
void ui_main(void* argument) noexcept {
  const Options* options = static_cast<const Options*>(argument);

  // ---- ② HAL 初始化 --------------------------------------------------------
  hp::HostHal& hal = hp::HostHal::instance();
  hp::HostDisplay display(static_cast<std::uint8_t>(options->scale), "Embark 系统用例");
  hp::HostInput input(hal.time(), display);
  hal.attach_display(display);
  hal.attach_input(input);

  ELOG_INFO("===== 第 2 步 / HAL 初始化（时间·持久化·日志·系统·总线·显示·输入）=====");
  if (const Error hal_error = hal.init(); hal_error != Error::none) {
    char text[32];
    std::fprintf(stderr, "HAL 初始化失败：%s\n", embark::error_text(text, hal_error));
    hp::exit_process(1);
  }
  const embark::hal::DisplayInfo display_info = display.info();
  // 整对象直接打日志：DisplayInfo 的字段名与取值名来自 hal/types.h 的 E_FMT_DERIVE 声明。
  ELOG_INFO(" 显示 {}，输入 {}", display_info, input.is_ready() ? "就绪" : "未就绪");
  ELOG_INFO(" 持久化位置 {}", hal.storage_path());

  // ---- ③ UI 端口 + 框架 boot ----------------------------------------------
  ELOG_INFO("===== 第 3 步 / 框架 boot（唯一 UI 任务 + App 注册表）=====");
  embark::platform::LvglUiPort ui_port(hal.context(), &host_exit_query, &input);
  static hp::HostTaskSpawner spawner;  // own_task 的槽位（BSS，不占 UI 任务栈）
  Framework framework(hal.context(), hp::embark_apps(), &ui_port, &spawner);
  if (const Error boot_error = framework.boot(); boot_error != Error::none) {
    char text[32];
    std::fprintf(stderr, "框架启动失败：%s\n", embark::error_text(text, boot_error));
    hp::exit_process(1);
  }

  for (std::size_t index = 0; index < framework.apps().size(); ++index) {
    const embark::App& app = *framework.apps().at(index);
    const embark::AppSettings settings = app.settings();
    ELOG_INFO(" App[{}] {}：{}，{}{}", index, app.name(), policy_text(settings.background),
              index == framework.foreground() ? "前台（拿到事件循环）" : "待命",
              settings.background == embark::BackgroundPolicy::own_task ? "，自有任务栈" : "");
    if (settings.background == embark::BackgroundPolicy::own_task) {
      ELOG_INFO("        └ 自有任务：周期 {} ms、栈 {} 字、优先级 {}", settings.period_ms,
                settings.task_stack_words, static_cast<int>(settings.task_priority));
    } else if (settings.background == embark::BackgroundPolicy::tick) {
      ELOG_INFO("        └ 后台节拍 {} ms（框架定时器驱动，不占 UI 任务）", settings.period_ms);
    } else {
      ELOG_INFO("        └ 退场后不再被调度（v1 里 App 常驻，只是让出前台）");
    }
  }
  ELOG_INFO(" 系统里只有一条 UI 任务，App 是逻辑模块；当前 FreeRTOS 任务数 {}",
            static_cast<unsigned long>(uxTaskGetNumberOfTasks()));

  // 收尾自检要用的观测点（App 本体在 app/，用具体类型读计数器）。
  const embark::demo::ClockApp& clock_app =
      *static_cast<const embark::demo::ClockApp*>(framework.app(0));
  const embark::demo::SettingsApp& settings_app =
      *static_cast<const embark::demo::SettingsApp*>(framework.app(1));
  const embark::demo::TickerApp& ticker_app =
      *static_cast<const embark::demo::TickerApp*>(framework.app(2));

  // ---- ④ 帧循环：从后台节拍到前台切换 --------------------------------------
  ELOG_INFO("===== 第 4 步 / 进入 UI 循环（每帧：喂时间 → 喂输入 → 执行待办切换 → 渲染）=====");
  ELOG_INFO(" 检查点：后台节拍（clock 的 onBackgroundTick 由定时器驱动）");

  int frames_run = 0;
  unsigned ticks_at_first_checkpoint = 0;
  std::size_t last_switches = 0;
  unsigned last_brightness = 0;
  bool screenshot_done = (options->screenshot == nullptr);

  for (;;) {
    framework.step();
    ++frames_run;

    if (framework.exit_requested()) {
      ELOG_INFO(" 第 {} 帧：framework.exit_requested() 为真 → 退出帧循环，进入收尾", frames_run);
      break;
    }

    // 解说只在"状态真的变了"时才说：点击的帧号只是触发器，效果要等框架执行完才作数。
    const std::size_t switches = static_cast<std::size_t>(framework.switches());
    if (switches != last_switches) {
      last_switches = switches;
      ELOG_INFO("◆ 前台切换完成（第 {} 帧，累计 {} 次）：现在前台是 {}", frames_run, switches,
                framework.apps().at(framework.foreground())->name());
      ELOG_INFO("   钩子序：clock enter/resume = {}/{}，settings enter/resume = {}/{}",
                clock_app.enters(), clock_app.resumes(), settings_app.enters(),
                settings_app.resumes());
    }
    if (clock_app.brightness() != last_brightness) {
      last_brightness = clock_app.brightness();
      ELOG_INFO("◆ App 间消息到达（第 {} 帧）：clock（此刻在后台）的 brightness = {}", frames_run,
                last_brightness);
    }

    // ④ 后台节拍：两次采样，证明它一直在跑。
    if (frames_run == tick_checkpoint_first) {
      ticks_at_first_checkpoint = clock_app.ticks();
      ELOG_INFO(" 第 1 次采样（第 {} 帧）：clock ticks = {}，前台仍是 {}", frames_run,
                ticks_at_first_checkpoint, framework.apps().at(framework.foreground())->name());
    } else if (frames_run == tick_checkpoint_second) {
      ELOG_INFO(" 第 2 次采样（第 {} 帧）：clock ticks = {}（比第 1 次多 {}，后台一直在跳）",
                frames_run, clock_app.ticks(), clock_app.ticks() - ticks_at_first_checkpoint);
    }

    // ⑤ 前台切换 #1：点 clock 屏的 "Settings"。
    if (frames_run == click_settings_frame) {
      ELOG_INFO("===== 第 5 步 / 前台切换 #1：合成点击 \"Settings\" ({},{}) =====",
                embark::demo::demo_click_center_x, embark::demo::demo_click_center_y);
      ELOG_INFO(" 预期：clock 让出前台（onPause）→ settings 首次进场（onEnter），切换计数 +1");
      push_motion_and_press(options->scale, embark::demo::demo_click_center_x,
                            embark::demo::demo_click_center_y);
    } else if (frames_run == click_settings_frame + click_release_delta) {
      push_release(options->scale, embark::demo::demo_click_center_x,
                   embark::demo::demo_click_center_y);
    }

    // ⑥ App 间消息：settings 屏的 "Level +1"（和上一步同一个中心坐标，焦点已经换了）。
    if (frames_run == click_level_frame) {
      ELOG_INFO("===== 第 6 步 / App 间消息：点 settings 屏的 \"Level +1\" ({},{}) =====",
                embark::demo::demo_click_center_x, embark::demo::demo_click_center_y);
      ELOG_INFO(" 预期：settings 广播亮度消息 → 后台的 clock 收到，brightness 变 1");
      push_motion_and_press(options->scale, embark::demo::demo_click_center_x,
                            embark::demo::demo_click_center_y);
    } else if (frames_run == click_level_frame + click_release_delta) {
      push_release(options->scale, embark::demo::demo_click_center_x,
                   embark::demo::demo_click_center_y);
    }

    // ⑦ 前台切换 #2：settings 屏的 "Back to clock"（下排按钮）。
    if (frames_run == click_back_frame) {
      ELOG_INFO("===== 第 7 步 / 前台切换 #2：点 \"Back to clock\" ({},{}) =====",
                embark::demo::demo_switch_center_x, embark::demo::demo_switch_center_y);
      ELOG_INFO(" 预期：settings 让出前台（onPause）→ clock 走 onResume（它没退场），切换计数 +1");
      push_motion_and_press(options->scale, embark::demo::demo_switch_center_x,
                            embark::demo::demo_switch_center_y);
    } else if (frames_run == click_back_frame + click_release_delta) {
      push_release(options->scale, embark::demo::demo_switch_center_x,
                   embark::demo::demo_switch_center_y);
    }

    // ⑧ 真正的任务切换：own_task 的后台任务 ↔ UI 任务（跨任务消息走 SPSC 队列）。
    if (frames_run == own_task_checkpoint) {
      ELOG_INFO("===== 第 8 步 / 跨任务的真实切换：ticker 后台任务 → UI 任务 =====");
      ELOG_INFO(" ticker 发送 {} 条，UI 收到 {} 条（跨任务消息经 SPSC 队列进 UI 收件箱）",
                ticker_app.sent(), ticker_app.received());
    }

    // ⑨ 关窗退出（等价于用户点窗口右上角 ×，走的是真事件队列）。
    if (frames_run == quit_frame) {
      ELOG_INFO("===== 第 9 步 / 模拟用户关窗（SDL_QUIT）=====");
      SDL_Event quit{};
      quit.type = SDL_QUIT;
      SDL_PushEvent(&quit);
    }

    if (frames_run >= options->frames) {
      if (!screenshot_done) {
        screenshot_done = true;
        if (save_screenshot(display, options->screenshot)) {
          ELOG_INFO(" 截图已保存：{}", options->screenshot);
        }
      }
      ELOG_INFO(" 跑到 --frames 上限（{} 帧），正常收尾", options->frames);
      break;
    }

    hp::ui_loop_delay(static_cast<std::uint32_t>(options->delay_ms));
  }

  // ---- 收尾：统计 + 自检清单 + LVGL 堆账 -----------------------------------
  ELOG_INFO("===== 收尾 / 统计与自检 =====");
  ELOG_INFO(" 显示：刷新 {} 次（{} 字节），Present {} 次；窗口 {}×{}", ui_port.port().refreshes(),
            ui_port.port().flush_bytes(), display.presents(), display_info.width,
            display_info.height);
  ELOG_INFO(" 前台：{}；切换 {} 次；clock enter {}/resume {}/ticks {}/brightness {}",
            framework.apps().at(framework.foreground())->name(),
            static_cast<unsigned long>(framework.switches()), clock_app.enters(),
            clock_app.resumes(), clock_app.ticks(), clock_app.brightness());
  ELOG_INFO(" settings enter {}/resume {}；ticker 发送 {} / UI 收到 {}；收件箱溢出 {} 次",
            settings_app.enters(), settings_app.resumes(), ticker_app.sent(), ticker_app.received(),
            static_cast<unsigned long>(framework.inbox_overflows()));
  ELOG_INFO(" 总线：发布 {} 条，无人接收 {} 条；输入丢弃 {}，忽略按键 {}",
            static_cast<unsigned long>(framework.bus().published()),
            static_cast<unsigned long>(framework.bus().unknown()),
            static_cast<unsigned long>(ui_port.port().dropped_input_events()),
            static_cast<unsigned long>(ui_port.port().ignored_key_events()));
  ELOG_INFO(
      " LVGL 堆：分配 {} 次，峰值 {} 字节，退出前未回收 {} 字节（预算 {}）；UI 任务栈余量 {} 字",
      static_cast<unsigned long>(hp::lvgl_allocations()), hp::lvgl_peak_bytes(),
      hp::lvgl_outstanding_bytes(), embark::lvgl_alloc_budget_bytes,
      static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)));

  CheckList checks;
  ELOG_INFO(" 自检清单（全部通过 = 这轮系统用例成功）：");
  checks.check("唯一 UI 任务活着并跑完帧循环", frames_run >= tour_end_frame - 1, frames_run,
               tour_end_frame - 1);
  checks.check("后台节拍持续（clock ticks）", clock_app.ticks() > 1,
               static_cast<unsigned long>(clock_app.ticks()), 1);
  checks.check("前台切换次数（clock→settings→clock）", framework.switches() == 2U,
               static_cast<unsigned long>(framework.switches()), 2);
  checks.check("clock 进场/复场（onEnter 1 次 / onResume 1 次）",
               clock_app.enters() == 1U && clock_app.resumes() == 1U, clock_app.enters(), 1);
  checks.check("settings 进场（onEnter 1 次 / onResume 0 次）",
               settings_app.enters() == 1U && settings_app.resumes() == 0U, settings_app.enters(),
               1);
  checks.check("App 间消息到达（clock brightness）", clock_app.brightness() == 1U,
               static_cast<unsigned long>(clock_app.brightness()), 1);
  checks.check("own_task 消息回流（发送/收到都 > 0）",
               ticker_app.sent() > 0 && ticker_app.received() > 0, ticker_app.received(), 1);
  checks.check("收件箱无溢出", framework.inbox_overflows() == 0U,
               static_cast<unsigned long>(framework.inbox_overflows()), 0);

  if (checks.ok) {
    ELOG_INFO("===== 全流程通过：启动 → 后台节拍 → 前台切换 → 消息 → 关窗收尾 =====");
  } else {
    std::fprintf(stderr, "系统用例失败：上面标 [失败] 的步骤没达到预期\n");
  }

  framework.shutdown();
  hp::exit_process(checks.ok ? 0 : 2);
}

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // 重定向到文件时也能看到进度

  Options options = parse_options(argc, argv);

  // ---- ① 进程入口 ----------------------------------------------------------
  ELOG_INFO("===== 第 1 步 / 进程入口：Embark {}（{} 后端）=====", embark::version_string(),
            embark::platform_name());
  ELOG_INFO(" 起唯一 UI 任务：栈 {} 字（{} KB），优先级 {}，循环周期 {} ms；之后进程入口就进调度器",
            embark::ui_task_stack_words, embark::ui_task_stack_words * 4 / 1024,
            static_cast<int>(embark::ui_task_priority), embark::ui_loop_period_ms);
  ELOG_INFO(" 提示：窗口默认 1:1（{}×{}），--scale N 可整数倍放大看细节", embark::display_width,
            embark::display_height);

  const Error task_error = hp::start_ui_task(&ui_main, &options);
  if (task_error != Error::none) {
    char text[32];
    std::fprintf(stderr, "启动 UI 任务失败：%s\n", embark::error_text(text, task_error));
    return 1;
  }

  hp::start_scheduler();  // 永不返回（收尾走 exit_process）
  return 0;               // 编译器路径：永远到不了
}
