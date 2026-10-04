/**
 * 宿主 · UI 演示可执行文件（issues/06）
 *
 * 从 issue 06 起，这个程序是"唯一 UI 任务"的第一个真实例子：
 *   进程入口 main 只做两件事 —— 起 UI 任务（FreeRTOS 静态任务）+ 进调度器；
 *   真正的启动与主循环全在 ui_main 里（= 那个唯一 UI 任务）：
 *     HAL（SDL 窗口在创建它的线程上泵事件）→ UI 端口（唯一允许调 lv_timer_handler()
 *     的地方）→ Framework（App 注册表 + 前台切换）→ 事件循环。
 *
 * 无人值守验收开关（会自动退出，不需要人去点窗口）：
 *   --frames N        跑 N 帧后退出（默认 0 = 一直跑到关窗）
 *   --click [X,Y]     在第 20 帧合成一次鼠标点击（走 SDL_PushEvent → 真事件队列 → 输入后端；
 *                     点 clock 屏的 "Settings" = 触发一次前台切换）
 *   --switch          合成两次点击验证前台切换：帧 30/32 点 settings 的 "Level +1"（亮度消息
 *                     广播给 clock），帧 50/52 点 "Back to clock"（同一中心，两个前台 App 各中一个按钮）
 *   --screenshot FILE 最后一帧把窗口内容存成 BMP（用 Python/Pillow 转 PNG 便于查看）
 *   --scale S         窗口放大倍数（默认 2）
 *   --delay MS        每帧间隔（默认 5）
 *   --quit-at N       第 N 帧推一个关窗事件（验收「关窗干净退出」用）
 *   --help            打印用法
 *
 * 界面文案是英文：LVGL 内置字体只有 Montserrat（无中文字形），中文文案要配自定义字体，
 * 那是 issue 12 的事。日志照旧是中文。
 *
 * 退出码：0 = 正常；1 = 后端起不来；2 = 验收失败（点了但按钮没响应 / 切换钩子序不对）。
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

// 合成点击的帧号：先移动+按下，隔两帧再抬起 ——
// 让 LVGL 分两个读周期处理，点一下就是完整的一次"按下 → 抬起"。
constexpr int click_move_frame = 20;  // 点 clock 屏的 "Settings"（中心 160,170）
constexpr int click_release_frame = click_move_frame + 2;
constexpr int switch_move_frame = 30;  // 点 settings 屏的 "Level +1"（同一坐标，焦点已换）
constexpr int switch_release_frame = switch_move_frame + 2;
constexpr int back_move_frame = 50;  // "Back to clock"（settings 屏，下排按钮 160,215）
constexpr int back_release_frame = back_move_frame + 2;

struct Options {
  int frames = 0;
  int scale = 2;
  int delay_ms = 5;
  const char* screenshot = nullptr;
  bool click = false;
  int click_x = embark::demo::demo_click_center_x;
  int click_y = embark::demo::demo_click_center_y;
  int quit_at = 0;  ///< >0 时在第 N 帧推一个 SDL_QUIT（等价于用户点窗口的关闭按钮）
  bool switch_mode = false;
  bool own_task = false;  ///< 验证 own_task 后台 App：TickerApp 的消息要能被 UI 收到
};

void print_usage() {
  std::printf(
      "用法：embark_host_ui [选项]\n"
      "  --frames N         跑 N 帧后退出（默认 0：一直跑到关窗）\n"
      "  --click [X,Y]      合成一次鼠标点击（默认点按钮中心 %d,%d；触发 clock 的 Settings 按钮）\n"
      "  --switch           合成两次点击验证前台切换（clock→settings→clock）\n"
      "  --own-task         验证 own_task 后台 App（TickerApp 的消息要被 UI 收到）\n"
      "  --quit-at N        第 N 帧推一个关窗事件（验收「关窗干净退出」用）\n"
      "  --screenshot FILE  最后一帧存 BMP 截图\n"
      "  --scale S          窗口放大倍数（默认 2）\n"
      "  --delay MS         每帧间隔毫秒（默认 5）\n"
      "  --help             显示本帮助\n",
      embark::demo::demo_click_center_x, embark::demo::demo_click_center_y);
}

/// 解析 "X,Y"（失败就用默认值）。
void parse_point(const char* text, int& x, int& y) {
  int parsed_x = 0;
  int parsed_y = 0;
  if (std::sscanf(text, "%d,%d", &parsed_x, &parsed_y) == 2) {
    x = parsed_x;
    y = parsed_y;
  }
}

Options parse_options(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const char* arg = argv[index];
    if (std::strcmp(arg, "--frames") == 0 && index + 1 < argc) {
      options.frames = std::atoi(argv[++index]);
    } else if (std::strcmp(arg, "--scale") == 0 && index + 1 < argc) {
      options.scale = std::atoi(argv[++index]);
    } else if (std::strcmp(arg, "--delay") == 0 && index + 1 < argc) {
      options.delay_ms = std::atoi(argv[++index]);
    } else if (std::strcmp(arg, "--screenshot") == 0 && index + 1 < argc) {
      options.screenshot = argv[++index];
    } else if (std::strcmp(arg, "--quit-at") == 0 && index + 1 < argc) {
      options.quit_at = std::atoi(argv[++index]);
    } else if (std::strcmp(arg, "--click") == 0) {
      options.click = true;
      if (index + 1 < argc && argv[index + 1][0] != '-') {
        parse_point(argv[++index], options.click_x, options.click_y);
      }
    } else if (std::strcmp(arg, "--switch") == 0) {
      options.switch_mode = true;
    } else if (std::strcmp(arg, "--own-task") == 0) {
      options.own_task = true;
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
  return options;
}

// --- 合成输入：走 SDL 自己的事件队列，等于把"人在窗口上点了一下"喂给整条链路 ---

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

/// 把窗口内容存成 BMP（SDL_SaveBMP 只认 BMP；要看图就转 PNG）。
/// 取像素这段交给显示后端（HostDisplay::capture_surface）—— 它知道正确的读回顺序。
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

// 整个可执行文件只出现一次的 App 注册表：ClockApp 是默认前台，SettingsApp 待命，
// TickerApp 是纯后台（own_task 策略，issue 07 的消息回 UI 演示），HelloApp 是
// 最简模板（issue 12 的新手最短路径，挂在末位不参与验收断言）。
// demo App 本体在 app/（issues/08 起宿主侧不再自带）。
EMBARK_APP_TABLE(embark::demo::ClockApp, embark::demo::SettingsApp, embark::demo::TickerApp,
                 embark::demo::HelloApp)

}  // namespace embark::platform::host

using embark::Error;
using embark::Framework;

namespace hp = embark::platform::host;

/// 唯一 UI 任务。SDL 的窗口与事件泵要求同线程，LVGL 只允许一条线上跑，
/// 所以 HAL 初始化、UI 端口、App 前台、事件循环全都在这一个任务里（spec §6）。
void ui_main(void* argument) noexcept {
  const Options* options = static_cast<const Options*>(argument);

  hp::HostHal& hal = hp::HostHal::instance();
  hp::HostDisplay display(static_cast<std::uint8_t>(options->scale), "Embark 宿主 UI");
  hp::HostInput input(hal.time(), display);
  hal.attach_display(display);
  hal.attach_input(input);

  if (const Error hal_error = hal.init(); hal_error != Error::none) {
    std::fprintf(stderr, "HAL 初始化失败：%s\n", embark::to_string(hal_error));
    hp::exit_process(1);
  }
  ELOG_INFO("HAL 就绪：显示 {}×{}，输入 {}，持久化 {}", static_cast<int>(display.info().width),
            static_cast<int>(display.info().height), input.is_ready() ? "就绪" : "未就绪",
            hal.storage_path());

  hp::LvglUiPort ui_port(hal.context(), display, input);
  // own task 的后台任务槽位（BSS，不占 UI 任务栈）：进程级一个实例。
  static hp::HostTaskSpawner spawner;
  Framework framework(hal.context(), hp::embark_apps(), &ui_port, &spawner);
  if (const Error boot_error = framework.boot(); boot_error != Error::none) {
    std::fprintf(stderr, "框架启动失败：%s\n", embark::to_string(boot_error));
    hp::exit_process(1);
  }
  // LVGL_VERSION_* 是整数宏（third_party/lvgl/lvgl.h:16-18），拼字符串要逐个占位。
  ELOG_INFO("框架就绪：{} 个 App，默认前台 {}；LVGL {}.{}.{}，绘制缓冲 {} 行，LVGL 堆预算 {} 字节",
            framework.apps().size(), framework.apps().at(0)->name(), LVGL_VERSION_MAJOR,
            LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, static_cast<int>(embark::lvgl_draw_buf_lines),
            embark::lvgl_alloc_budget_bytes);

  int frames_run = 0;
  bool screenshot_done = (options->screenshot == nullptr);

  for (;;) {
    // 一帧 = UI 端口喂时间 → 喂输入 → （框架在边界执行前台切换）→ LVGL 渲染
    framework.step();
    ++frames_run;

    if (framework.exit_requested()) {
      ELOG_INFO("收到关窗请求：第 {} 帧，开始收尾", frames_run);
      break;
    }

    if (options->click) {
      if (frames_run == click_move_frame) {
        ELOG_INFO("合成点击：移动 + 按下 ({},{})", options->click_x, options->click_y);
        push_motion_and_press(options->scale, options->click_x, options->click_y);
      } else if (frames_run == click_release_frame) {
        push_release(options->scale, options->click_x, options->click_y);
      }
    }

    if (options->switch_mode) {
      if (frames_run == switch_move_frame) {
        // settings 屏的 "Level +1"（与 clock 屏 "Settings" 同一中心 (160,170)，证明焦点已换）。
        ELOG_INFO("合成点击：切换按钮 ({},{})", embark::demo::demo_click_center_x,
                  embark::demo::demo_click_center_y);
        push_motion_and_press(options->scale, embark::demo::demo_click_center_x,
                              embark::demo::demo_click_center_y);
      } else if (frames_run == switch_release_frame) {
        push_release(options->scale, embark::demo::demo_click_center_x,
                     embark::demo::demo_click_center_y);
      } else if (frames_run == back_move_frame) {
        // settings 屏的 "Back to clock"（下排按钮 (160,215)）。
        ELOG_INFO("合成点击：返回按钮 ({},{})", embark::demo::demo_switch_center_x,
                  embark::demo::demo_switch_center_y);
        push_motion_and_press(options->scale, embark::demo::demo_switch_center_x,
                              embark::demo::demo_switch_center_y);
      } else if (frames_run == back_release_frame) {
        push_release(options->scale, embark::demo::demo_switch_center_x,
                     embark::demo::demo_switch_center_y);
      }
    }

    if (options->quit_at > 0 && frames_run == options->quit_at) {
      // 等价于用户点窗口右上角的 ×：SDL_QUIT 进事件队列，由输入后端认出来再置 quit_requested_
      // （下一帧的 pump_input 会读走它）—— 验收「关窗干净退出」走的就是这条路径。
      SDL_Event quit{};
      quit.type = SDL_QUIT;
      SDL_PushEvent(&quit);
    }

    if (options->frames > 0 && frames_run >= options->frames) {
      if (!screenshot_done) {
        screenshot_done = true;
        if (save_screenshot(display, options->screenshot)) {
          ELOG_INFO("截图已保存：{}", options->screenshot);
        }
      }
      ELOG_INFO("跑满 {} 帧，正常退出", options->frames);
      break;
    }

    hp::ui_loop_delay(static_cast<std::uint32_t>(options->delay_ms));
  }

  const embark::demo::ClockApp& clock_app =
      *static_cast<const embark::demo::ClockApp*>(framework.app(0));
  const embark::demo::SettingsApp& settings =
      *static_cast<const embark::demo::SettingsApp*>(framework.app(1));
  const embark::demo::TickerApp& ticker =
      *static_cast<const embark::demo::TickerApp*>(framework.app(2));

  // 拆两条统计（efmt 的 format 参数上限 16，观测项多）
  ELOG_INFO(
      "统计（显示/前台）：帧 {}，刷新 {} 次（{} 字节），Present {} 次，前台 {}（切换 {} 次），"
      "clock ticks {}/brightness {}，settings enter {}/resume {}",
      frames_run, ui_port.port().refreshes(), ui_port.port().flush_bytes(), display.presents(),
      framework.apps().at(framework.foreground())->name(), framework.switches(), clock_app.ticks(),
      clock_app.brightness(), settings.enters(), settings.resumes());
  ELOG_INFO(
      "统计（后台/消息）：丢输入 {}，忽略按键 {}，ticker 发送 {} 条 / UI 收到 {} 条，"
      "总线发布 {} 条 / 无人接收 {} 条，收件箱溢出 {} 次，UI 任务栈余量 {} 字",
      ui_port.port().dropped_input_events(), ui_port.port().ignored_key_events(), ticker.sent(),
      ticker.received(), framework.bus().published(), framework.bus().unknown(),
      framework.inbox_overflows(),
      static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)));
  ELOG_INFO("LVGL 堆：分配 {} 次，峰值 {} 字节，退出前未回收 {} 字节（预算 {}）",
            hp::lvgl_allocations(), hp::lvgl_peak_bytes(), hp::lvgl_outstanding_bytes(),
            embark::lvgl_alloc_budget_bytes);

  int exit_code = 0;
  if (options->click && settings.enters() != 1U) {
    // --click 点的是 clock 屏的 "Settings" 按钮：settings 必须恰好首次进入前台一次。
    // （不校验 switches() 总数：--click 与 --switch 叠加时帧 50 的"返回"也算一次切换。）
    std::fprintf(stderr, "合成点击没有触发前台切换（验收失败）：settings 进入 %u 次（期望 1）\n",
                 settings.enters());
    exit_code = 2;
  }
  if (exit_code == 0 && options->switch_mode) {
    // 切换验收：clock→settings→clock 恰好 2 次；onEnter 只在第一次，切回走 onResume；
    // 帧 30/32 的合成点击落在 (160,170)：与 --click 叠加时焦点已在 settings，点中的是
    // "Level +1"（App 间消息要真被 clock 收到）；单独 --switch 时焦点还在 clock，点中的是
    // "Settings" 按钮本身——此时不应有任何亮度消息（brightness 必须 0）；
    // clock 后台节拍必须要跑过（spec §14.1：后台 tick 可观测）。
    const bool brightness_ok =
        options->click ? (clock_app.brightness() == 1U) : (clock_app.brightness() == 0U);
    const bool hooks_ok = framework.switches() == 2U && clock_app.enters() == 1U &&
                          clock_app.resumes() == 1U && settings.enters() == 1U &&
                          settings.resumes() == 0U && brightness_ok && clock_app.ticks() > 0U;
    if (!hooks_ok) {
      std::fprintf(stderr,
                   "前台切换验收失败：切换 %u 次（期望 2），clock enter %u/resume %u（期望 1/1），"
                   "settings enter %u/resume %u（期望 1/0），clock brightness %u（期望 %u），"
                   "clock ticks %u（期望 > 0）\n",
                   framework.switches(), clock_app.enters(), clock_app.resumes(), settings.enters(),
                   settings.resumes(), clock_app.brightness(), options->click ? 1U : 0U,
                   clock_app.ticks());
      exit_code = 2;
    }
  }

  if (exit_code == 0 && options->own_task) {
    // own_task 验收：TickerApp 的后台任务至少发出一条，且 UI 任务真的收到了。
    // （sent() 是跨线程观测读取：own task 单写者，v1 接受；received() 在 UI 线程读）
    if (ticker.sent() == 0 || ticker.received() == 0) {
      std::fprintf(stderr, "own_task 验收失败：后台发送 %u 条 / UI 收到 %u 条（期望都 > 0）\n",
                   ticker.sent(), ticker.received());
      exit_code = 2;
    }
  }

  framework.shutdown();
  hp::exit_process(exit_code);
}

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // 重定向到文件时也能看到进度

  Options options = parse_options(argc, argv);

  ELOG_INFO("UI 任务启动：栈 {} 字（{} KB），优先级 {}，周期 {} ms", embark::ui_task_stack_words,
            embark::ui_task_stack_words * 4 / 1024, static_cast<int>(embark::ui_task_priority),
            embark::ui_loop_period_ms);

  const Error task_error = hp::start_ui_task(&ui_main, &options);
  if (task_error != Error::none) {
    std::fprintf(stderr, "启动 UI 任务失败：%s\n", embark::to_string(task_error));
    return 1;
  }

  hp::start_scheduler();  // 永不返回（调度器把控制权交给 UI 任务；收尾走 exit_process）
  return 0;               // 编译器路径：永远到不了
}