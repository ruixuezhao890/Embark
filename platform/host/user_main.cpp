/**
 * Embark 宿主 · 用户示例入口 —— "写你自己的 main" 的干净模板
 *
 * 这是给用户照抄的模板（构建目标：embark_host_user）：没有任何自动验收开关、
 * 没有合成输入、没有帧数故事线。运行方式 = 模拟单片机：
 *   main 起唯一 UI 任务 + 进调度器（永不返回 = "上电后一直跑"），
 *   ui_main 里框架初始化完进入 for(;;) 主循环（一拍 = 输入泵 → 前台切换 →
 *   消息/后台节拍 → lv_timer_handler()），默认跑到关窗才停。
 *
 * 日志（elog）怎么来的：不用你初始化 —— hal.init()（见 ui_main 第 1 步）里
 * 已经装好：全文只有一个默认 logger（名 "embark"，级别 info），ELOG_* 宏
 * 直接可用（include <embark/log.h> 即带上前缀，本文件已包含）。宿主日志链：
 *   elog（格式化 + 级别过滤）→ LogSinkBinder（按整行攒线，一行齐了一次交给
 *   后端）→ HostLogSink → stderr（Windows 无缓冲，日志即时可见）。
 * logger 装不上（重名/注册表满）不致命：日志静默，框架照常跑。
 *
 * 看懂模板的三个概念：
 *   HAL        平台后端抽象：显示/输入/存储/时钟/日志……（见 HostHal）；
 *   Framework  App 壳：App 注册表、前台切换、消息总线、后台策略（见 app.h；
*              后台策略在 App 第一次进前台时才武装 —— issue 23 / ADR 0009）；
 *   LvglUiPort LVGL 接入 UI 端口的桥：屏幕刷新、输入分发、退出查询。
 *
 * 改成你自己的程序，只动三处：
 *   1) EMBARK_APP_TABLE(...)  —— 换成你的 App 类（第一位 = 上电默认前台）；
 *   2) HostDisplay 的窗口标题（可选）；
 *   3) 主循环里 framework.step() 前后想做的每拍业务。
 *
 * 退出方式（默认）：关掉窗口（SDL_QUIT / 点 X / Alt+F4）= 停止模拟；
 *   想"永不退出"（真机 v1 的样子）就把 LvglUiPort 的退出查询传 nullptr，
 *   见 ui_main 里的注释。--frames N 只是给脚本/CI 用的收尾便利，不是常态。
 *
 * 验收开关（--click / --launch / --eez …）都在 ui_demo.cpp（embark_host_ui），
 * 那是"demo + 自动验收"二合一；本文件是干干净净的"用户程序"。
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <embark/error.h>
#include <embark/framework.h>
#include <embark/log.h>
#include <embark_limits.h>

#include "clock/clock_app.h"
#include "host_context.h"
#include "host_display.h"
#include "host_input.h"
#include "host_lvgl_mem.h"
#include "launcher/launcher_app.h"
#include "lvgl_ui_port.h"
#include "own_task_spawner.h"
#include "settings/settings_app.h"
#include "ui_task.h"

namespace {

// 退出查询：窗口被关（SDL_QUIT / 点 X / Alt+F4）= 停止模拟（像按下电源）。
// 想让程序"永不退出"（模拟真机上电后一直跑、直到断电），就把 ui_main 里
// LvglUiPort 的构造参数换成 nullptr（第二个参数），本函数就不再被调用。
bool host_exit_query(void* context) noexcept {
  return static_cast<embark::platform::host::HostInput*>(context)->quit_requested();
}

struct Options {
  int frames = 0;  ///< 0 = 一直跑（模拟单片机常驻）；N > 0 = 跑 N 拍后收尾（脚本/CI 用）
};

// 极简命令行：只留 --frames（脚本/CI 自动收尾）与 --help。
// 真机没有命令行，这里是宿主模拟的便利，不是程序的一部分。
Options parse_options(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (std::strcmp(arg, "--frames") == 0 && i + 1 < argc) {
      options.frames = std::atoi(argv[++i]);
    } else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
      std::printf(
          "用法：embark_host_user [选项]\n"
          "  --frames N   跑 N 拍后正常收尾（默认 0 = 一直跑到关窗，模拟单片机上电常驻）\n"
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

// App 注册表：整个可执行文件只出现一次。第一位 = 上电默认前台（启动器）。
// 换成你自己的 App：EMBARK_APP_TABLE(embark::demo::MyApp1, embark::demo::MyApp2, ...)
EMBARK_APP_TABLE(embark::demo::LauncherApp, embark::demo::ClockApp, embark::demo::SettingsApp)

}  // namespace embark::platform::host

using embark::Error;
using embark::Framework;

namespace hp = embark::platform::host;

/// 唯一 UI 任务（spec §6：全工程只有这一个任务能碰 LVGL，真机同构）。
/// 它不许返回：收尾走 hp::exit_process()（主线程还卡在调度器里）。
void ui_main(void* argument) noexcept {
  const Options* options = static_cast<const Options*>(argument);

  // --- 1) HAL 装配（显示/输入后端挂到唯一一份 HAL 上）------------
  // HAL = 平台后端抽象：真机上换成 esp32 的同一套接口（esp32_hal.cpp），
  // 程序主体（App 们）不用改。装配点只有这一处。
  hp::HostHal& hal = hp::HostHal::instance();           // 全进程唯一实例（函数内静态）
  hp::HostDisplay display(1, "Embark 用户程序");        // 1 = 240×320 1:1，不放大不糊
  hp::HostInput input(hal.time(), display);             // 输入要借显示换算坐标 → 在显示之后挂
  hal.attach_display(display);
  hal.attach_input(input);
  // init() 的初始化顺序有讲究：log → storage → display → input（日志最先：
  // 后面每一步失败都要有出口；elog 的默认 logger 就是在这里装的，见文件头注释）。
  if (const Error hal_error = hal.init(); hal_error != Error::none) {
    char text[32];
    std::fprintf(stderr, "HAL 初始化失败：%s\n", embark::error_text(text, hal_error));
    hp::exit_process(1);
  }

  // --- 2) 框架装配与启动（boot 里会做 LVGL 接入 + App onCreate）---
  // LvglUiPort = LVGL 与 UI 端口的桥：它持有 HAL 上下文、输入与"退出查询"。
  // 退出查询传 &host_exit_query = 关窗停止；传 nullptr = 永不退出。
  embark::platform::LvglUiPort ui_port(hal.context(), &host_exit_query, &input);
  static hp::HostTaskSpawner spawner;  // 静态存储：own_task 后台任务的槽位池
  // Framework 是 App 壳：boot() 会遍历 EMBARK_APP_TABLE 注册的 App、
  // 依次 onCreate()，再把默认前台（第一位）拉上前台（onEnter → 加载它的 EEZ 屏）。
  Framework framework(hal.context(), hp::embark_apps(), &ui_port, &spawner);
  if (const Error boot_error = framework.boot(); boot_error != Error::none) {
    char text[32];
    std::fprintf(stderr, "框架启动失败：%s\n", embark::error_text(text, boot_error));
    hp::exit_process(1);
  }

  ELOG_INFO("框架就绪：{} 个 App，默认前台 {} —— 像真机一样跑起来了（关窗停止模拟）",
            framework.apps().size(), framework.apps().at(0)->name());

  // --- 3) 主循环：一拍一拍跑下去，永不 return（模拟单片机常驻）-----
  // framework.step() 一拍到底做了什么（spec §6 的时序）：
  //   输入泵（LVGL 收事件）→ 循环边界的前台切换（request_switch 生效）→
  //   消息/后台节拍（bus 投递 + 各 App 的 onForegroundTick/onBackgroundTick）→
  //   lv_timer_handler()（LVGL 动画/控件刷新）。
  // 为什么 UI 只能这一个任务碰：LVGL 不是线程安全的，真机同构 —— 全工程
  // 只有一个任务调用它；别的任务想刷界面只能发消息给前台 App。真机 v1 同构。
  int frames_run = 0;
  for (;;) {
    framework.step();  // 这一拍：输入泵 → 前台切换 → 消息/后台节拍 → lv_timer_handler()
    ++frames_run;
    if (framework.exit_requested()) {
      break;  // 关窗（用户要求收尾）
    }

    // 你自己的每拍业务写在这里：推 UI 变量、读传感器、发消息……
    // 注意：这里跑在 UI 任务里，别做耗时/阻塞的事（会卡住 LVGL 动画）；
    // 重活应放进 own_task 后台 App（见 docs 的 BackgroundPolicy）。

    if (options->frames > 0 && frames_run >= options->frames) {
      break;  // 脚本/CI 用：跑够 N 拍自己收尾
    }
    hp::ui_loop_delay();  // 让出 5 ms（spec §6，见 embark_limits.h 的 ui_loop_period_ms），绝不忙等
  }

  framework.shutdown();  // 按注册表逆序收尾：前台 onPause/onExit → 各 App onDestroy → LVGL 卸载
  // 退出前打一行内存账（LVGL 记账来自 host_lvgl_mem 的钩子）：
  // 也让链接器把 host_lvgl_mem 的编译单元拉进来（它同时定义 LVGL 断言的钩子，
  // UI 库缺了它链不上 —— 不引用任何符号时归档成员不会被抽取）。
  ELOG_INFO("停止模拟：LVGL 未回收 {} 字节（预算 {} 字节）",
            hp::lvgl_outstanding_bytes(), embark::lvgl_alloc_budget_bytes);
  hp::exit_process(0);
}

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // 日志立即落屏，不因缓冲延迟
  // 注意：ELOG_* 走 stderr（宿主日志后端），stdout 没被日志占用 ——
  // 你的程序如果往 stdout 打数据，不会被日志行污染（HostLogSink 的取舍）。

  Options options = parse_options(argc, argv);

  // UI 任务参数来自 config/embark_limits.h（真机同源）：栈大小/优先级/周期。
  // 这三个值在嵌入式端的 FreeRTOS 配置里也是同一处定义，模拟与真机行为一致。
  ELOG_INFO("UI 任务启动：栈 {} 字（{} KB），优先级 {}，周期 {} ms",
            embark::ui_task_stack_words,
            embark::ui_task_stack_words * static_cast<int>(sizeof(StackType_t)) / 1024,
            static_cast<int>(embark::ui_task_priority), embark::ui_loop_period_ms);

  // main 只做两件事：起 UI 任务 + 进调度器。
  const Error task_error = hp::start_ui_task(&ui_main, &options);
  if (task_error != Error::none) {
    char text[24];
    std::fprintf(stderr, "启动 UI 任务失败：%s\n", embark::error_text(text, task_error));
    return 1;
  }

  hp::start_scheduler();  // 永不返回：调度器接管后 UI 任务开始跑 = "上电"
  return 0;               // 不可达
}
