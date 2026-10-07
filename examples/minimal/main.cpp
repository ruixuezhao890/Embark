/**
 * Embark · 最小示例入口（examples/minimal）
 *
 * 一个可执行文件最少要有三样东西：
 *   1. EMBARK_APP_TABLE(...)  —— App 注册表，整个可执行文件只出现一次；
 *                                第一个 App 就是上电默认前台。
 *   2. ui_main()              —— 唯一 UI 任务的函数体：装配 HAL → 装框架 → 跑主循环。
 *   3. main()                 —— 起 UI 任务，然后把主线程交给调度器（永不返回）。
 *
 * 与 platform/host/user_main.cpp 的区别：那份是"演示入口"（App 表里是 demo 三件套），
 * 这份是"从零写一个 App 的最小样本"（一个 App、一块手绘屏、一个主循环）。
 *
 * 对照阅读：docs/index.md → guides/quickstart.md（跑起来）→ concepts/app-lifecycle.md
 * （为什么主循环长这样）。
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <embark/error.h>
#include <embark/framework.h>
#include <embark/log.h>
#include <embark_limits.h>

#include "host_context.h"
#include "host_display.h"
#include "host_input.h"
#include "host_lvgl_mem.h"
#include "lvgl_ui_port.h"
#include "minimal_app.h"
#include "own_task_spawner.h"
#include "ui_task.h"

namespace {

/// 退出查询：窗口被关（SDL_QUIT / 点 X / Alt+F4）= 停止模拟，像按下电源。
/// 真机没有"关窗"，所以 LvglUiPort 的这个参数传 nullptr 就是"永不退出"。
bool window_closed(void* context) noexcept {
  return static_cast<embark::platform::host::HostInput*>(context)->quit_requested();
}

struct Options {
  int frames = 0;  ///< 0 = 一直跑；N > 0 = 跑 N 拍后收尾（脚本 / CI 用）
};

Options parse_options(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (std::strcmp(arg, "--frames") == 0 && i + 1 < argc) {
      options.frames = std::atoi(argv[++i]);
    } else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
      std::printf(
          "用法：embark_example_minimal [选项]\n"
          "  --frames N   跑 N 拍后正常收尾（默认 0 = 一直跑到关窗）\n"
          "  --help       打印本用法\n");
      std::exit(0);
    } else {
      std::fprintf(stderr, "未知选项：%s（--help 看用法）\n", arg);
      std::exit(1);
    }
  }
  return options;
}

}  // namespace

namespace embark::platform::host {

// App 注册表：整个可执行文件只出现一次，第一位 = 上电默认前台。
EMBARK_APP_TABLE(embark::example::MinimalApp)

}  // namespace embark::platform::host

using embark::Error;
using embark::Framework;

namespace hp = embark::platform::host;

/// 唯一 UI 任务（全工程只有这一个任务能碰 LVGL，真机同构）。
/// 它不许返回：收尾走 hp::exit_process()（主线程还卡在调度器里）。
void ui_main(void* argument) noexcept {
  const Options* options = static_cast<const Options*>(argument);

  // --- 1) HAL 装配：显示 / 输入后端挂到唯一一份 HAL 上 ----------------------
  hp::HostHal& hal = hp::HostHal::instance();
  hp::HostDisplay display(1, "Embark 最小示例");  // 1 = 240×320 1:1，不放大不糊
  hp::HostInput input(hal.time(), display);       // 输入借显示换算坐标 → 必须后建
  hal.attach_display(display);
  hal.attach_input(input);
  // init() 顺序有讲究：log → storage → display → input（日志最先，后面每步失败都要有出口）。
  if (const Error hal_error = hal.init(); hal_error != Error::none) {
    char text[32];
    std::fprintf(stderr, "HAL 初始化失败：%s\n", embark::error_text(text, hal_error));
    hp::exit_process(1);
  }

  // --- 2) 框架装配与启动 ---------------------------------------------------
  // LvglUiPort = LVGL 与 UI 端口的桥（持有 HAL 上下文、输入与退出查询）。
  embark::platform::LvglUiPort ui_port(hal.context(), &window_closed, &input);
  // spawner 是 own_task 后台任务的槽位池；本例没有 own_task，传了也白传 ——
  // 但先留着，将来覆写 settings() 要后台任务时不用改装配代码。
  static hp::HostTaskSpawner spawner;
  // Framework::boot() 会：遍历注册表 → 依次 onCreate → 把第 0 个 App 拉上前台（onEnter）。
  Framework framework(hal.context(), hp::embark_apps(), &ui_port, &spawner);
  if (const Error boot_error = framework.boot(); boot_error != Error::none) {
    char text[32];
    std::fprintf(stderr, "框架启动失败：%s\n", embark::error_text(text, boot_error));
    hp::exit_process(1);
  }

  ELOG_INFO("框架就绪：{} 个 App，默认前台 {}", framework.apps().size(),
            framework.foreground());

  // --- 3) 主循环：一拍 = framework.step() ----------------------------------
  // 你写代码的日常就在这里：step() 之前或之后插自己的每拍业务。
  // step() 内部那七段见 docs/concepts/app-lifecycle.md §1。
  int frames_run = 0;
  for (;;) {
    framework.step();
    ++frames_run;
    if (framework.exit_requested()) {
      break;
    }
    if (options->frames > 0 && frames_run >= options->frames) {
      break;
    }
    hp::ui_loop_delay();
  }

  // --- 4) 收尾 -------------------------------------------------------------
  framework.shutdown();

  // 从框架取回自己的 App 打观测点：框架不认识具体类型，所以这里自己 static_cast。
  if (const auto* app = static_cast<const embark::example::MinimalApp*>(framework.app(0))) {
    ELOG_INFO("示例 App 观测：前台节拍 {} 帧，回到前台 {} 次", app->foreground_ticks(),
              app->resumes());
  }
  ELOG_INFO("停止模拟：共 {} 拍；LVGL 未回收 {} 字节（预算 {} 字节）", frames_run,
            hp::lvgl_outstanding_bytes(), embark::lvgl_alloc_budget_bytes);

  hp::exit_process(0);
}

int main(int argc, char** argv) {
  // stdout 无缓冲：程序被强杀时块缓冲会丢掉最后几行（调试时最需要的那几行）。
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  const Options options = parse_options(argc, argv);

  ELOG_INFO("UI 任务启动：栈 {} 字（{} KB），优先级 {}，周期 {} ms", embark::ui_task_stack_words,
            static_cast<unsigned>(embark::ui_task_stack_words * sizeof(void*) / 1024U),
            static_cast<unsigned>(embark::ui_task_priority), embark::ui_loop_period_ms);

  if (const Error start_error = hp::start_ui_task(&ui_main, const_cast<Options*>(&options));
      start_error != Error::none) {
    char text[32];
    std::fprintf(stderr, "UI 任务启动失败：%s\n", embark::error_text(text, start_error));
    return 1;
  }

  // 永不返回：UI 任务跑完会自己 hp::exit_process()，主线程就停在这里。
  hp::start_scheduler();
}
