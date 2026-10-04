// 系统用例（tour）：把"系统从启动到任务切换"跑成一条能从头读到尾的用例。
//
// 与 tests/kernel/test_framework*.cpp 里那些细粒度契约用例不同，这条用例只做一件事：
// 按真实装配顺序把整套系统装起来跑一遍，并在控制台打印每一步"发生了什么"：
//
//   ① 假 HAL 就绪           ② 框架 boot（UI init → onCreate → 默认前台 onEnter）
//   ③ 后台节拍（tick 策略）  ④ App 间消息（publish 广播 / post 跨任务投递）
//   ⑤ 前台切换              ⑥ 再切回来（第二次进前台走 onResume）
//   ⑦ shutdown（前台 onPause → 全 App onExit → UI 端口 shutdown）
//
// 在 CLion 里单独跑这一条 case，从上往下读日志即可看清系统行为；末尾的 CHECK 才是
// 把关条件 —— 日志是给人看的，断言是给判定用的。
//
// 真机/宿主上的同名演示是可运行程序 `embark_host_tour`（SDL 窗口 + 真 UI、真输入、
// 真定时器、真 FreeRTOS 任务）：build/platform/host/embark_host_tour.exe。
#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>

#include <embark/app.h>
#include <embark/app_registry.h>
#include <embark/error.h>
#include <embark/framework.h>
#include <embark/message.h>

#include "fakes/fake_ui_port.h"
#include "fakes/fakes.h"

namespace {

using embark::AppSettings;
using embark::BackgroundPolicy;
using embark::Framework;

/// 注册表下标 = App 编号。
constexpr embark::AppId clock_id = 0U;
constexpr embark::AppId settings_id = 1U;

/// 本用例自己发的通知消息（id 自选，避开框架保留的 0xFE）。
inline constexpr embark::MessageId tour_notice_id = 0x7AU;

struct TourNotice final : public embark::MessageT<tour_notice_id> {
  std::uint32_t payload = 0;
};

/// 后台节拍周期：40 ms ÷ ui_loop_period_ms(5 ms) = 每 8 帧一拍。
constexpr std::uint32_t background_period_ms = 40U;
constexpr std::uint32_t frames_per_period = 8U;

/// 步骤小标题 / 说明行，让日志自己会说话。
void step_title(const char* text) {
  std::printf("\n── %s\n", text);
}

/// 参与本用例的 App：7 个钩子全部记账并打印，于是日志本身就是钩子顺序的证据。
class TourApp : public embark::App {
 public:
  explicit TourApp(const char* name) noexcept : name_(name) {}

  [[nodiscard]] const char* name() const override { return name_; }

  void onCreate(Framework&) override {
    ++create_calls;
    note("onCreate");
  }
  void onEnter() override {
    ++enter_calls;
    note("onEnter");
  }
  void onPause() override {
    ++pause_calls;
    note("onPause");
  }
  void onResume() override {
    ++resume_calls;
    note("onResume");
  }
  void onExit() override {
    ++exit_calls;
    note("onExit");
  }
  void onBackgroundTick(std::uint32_t now_ms) override {
    ++tick_calls;
    std::printf("  · %-8s onBackgroundTick(now_ms = %u)  ← 后台节拍\n", name_,
                static_cast<unsigned>(now_ms));
  }
  void onMessage(const embark::Message& msg) override {
    if (msg.get_message_id() == tour_notice_id) {
      const auto& notice = static_cast<const TourNotice&>(msg);
      ++notice_calls;
      last_payload = notice.payload;
      std::printf("  · %-8s onMessage(payload = %u)  ← 广播通知\n", name_,
                  static_cast<unsigned>(notice.payload));
    } else if (msg.get_message_id() == embark::cross_task_message_id) {
      const auto& envelope = static_cast<const embark::CrossTaskMessage&>(msg);
      ++cross_task_calls;
      std::printf("  · %-8s onMessage(from_app = %u, seq = %u)  ← 跨任务信封\n", name_,
                  static_cast<unsigned>(envelope.from_app), static_cast<unsigned>(envelope.seq));
    }
  }

  /// 静态实例（app_registry 的实例是进程级 static）跨用例存活，每次进来先清零。
  void reset_counters() noexcept {
    create_calls = 0;
    enter_calls = 0;
    pause_calls = 0;
    resume_calls = 0;
    exit_calls = 0;
    tick_calls = 0;
    notice_calls = 0;
    cross_task_calls = 0;
    last_payload = 0;
  }

  unsigned create_calls = 0;
  unsigned enter_calls = 0;
  unsigned pause_calls = 0;
  unsigned resume_calls = 0;
  unsigned exit_calls = 0;
  unsigned tick_calls = 0;
  unsigned notice_calls = 0;
  unsigned cross_task_calls = 0;
  std::uint32_t last_payload = 0;

 private:
  void note(const char* hook) const noexcept { std::printf("  · %-8s %s\n", name_, hook); }

  const char* name_;
};

/// 前台 App + 后台按节拍跑的 App，与真机 demo 的 ClockApp/SettingsApp 同形。
class TourClockApp final : public TourApp {
 public:
  TourClockApp() : TourApp("clock") {}

  [[nodiscard]] AppSettings settings() const override {
    return AppSettings{BackgroundPolicy::tick, background_period_ms, 0U, 0U};
  }
};

class TourSettingsApp final : public TourApp {
 public:
  TourSettingsApp() : TourApp("settings") {}
};

}  // namespace

TEST_CASE("系统用例：从启动到任务切换走一遍（跟着日志读）") {
  std::printf("\n===== 系统用例：启动 → 后台节拍 → 消息 → 前台切换 → 收尾 =====\n");

  step_title("① 假 HAL：七个能力端点就绪（display/input/time/storage/log/system/bus）");
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  const embark::Error hal_error = hal.init();
  std::printf("  hal.init() → %s\n", hal_error == embark::Error::none ? "none" : "失败");
  CHECK(hal_error == embark::Error::none);

  step_title("② 框架装配：UI 端口 + 两个 App，然后 boot()");
  embark::fakes::FakeUiPort ui;
  Framework fw(ctx, embark::app_registry<TourClockApp, TourSettingsApp>(), &ui);
  std::printf("  boot 前：booted = %d，foreground = %u（invalid_app_id），frames = %u\n",
              fw.booted() ? 1 : 0, static_cast<unsigned>(fw.foreground()),
              static_cast<unsigned>(fw.frames()));
  CHECK_FALSE(fw.booted());
  CHECK(fw.foreground() == embark::invalid_app_id);
  CHECK(fw.frames() == 0U);

  auto* clock = static_cast<TourClockApp*>(fw.apps().at(clock_id));
  auto* settings = static_cast<TourSettingsApp*>(fw.apps().at(settings_id));
  clock->reset_counters();
  settings->reset_counters();

  REQUIRE(fw.boot() == embark::Error::none);
  CHECK(fw.booted());
  CHECK(ui.init_count == 1);
  CHECK(fw.foreground() == clock_id);  // 默认前台 = 注册表第 0 个
  CHECK(clock->create_calls == 1U);    // 两个 App 都 onCreate
  CHECK(settings->create_calls == 1U);
  CHECK(clock->enter_calls == 1U);     // 但只有前台 onEnter
  CHECK(settings->enter_calls == 0U);  // 后台 App 在 boot 阶段不 enter
  std::printf("  boot 后：前台 = %s（默认取注册表第 0 个），两个 App 都已 onCreate\n",
              fw.app(fw.foreground())->name());

  step_title("③ 后台节拍：clock 是 tick 策略，period_ms = 40（每 8 帧一拍）");
  std::printf("  clock.settings()  = { background = tick, period_ms = %u }\n",
              background_period_ms);
  std::printf("  settings 用默认策略（suspend）：退场后不跑节拍，只等消息\n");

  /// 走 n 帧：每帧先把假时钟推到"帧号 × ui_loop_period_ms"，再 step()。
  const auto run_frames = [&fw, &hal](std::uint32_t count) {
    for (std::uint32_t i = 0; i < count; ++i) {
      hal.time.set_now_ms(fw.frames() * embark::ui_loop_period_ms);
      fw.step();
    }
  };

  run_frames(frames_per_period - 1U);
  std::printf("  走了 %u 帧：clock 节拍 %u 次（还差一拍）\n", frames_per_period - 1U,
              clock->tick_calls);
  CHECK(clock->tick_calls == 0U);
  CHECK(settings->tick_calls == 0U);

  run_frames(1U);
  std::printf("  第 %u 帧：clock 节拍 %u 次（周期到期触发），settings 仍是 %u 次\n",
              frames_per_period, clock->tick_calls, settings->tick_calls);
  CHECK(clock->tick_calls == 1U);

  step_title("④ App 间消息：publish 广播给所有 App，post 走收件箱下一帧派发");
  TourNotice notice;
  notice.payload = 7U;
  fw.publish(notice);
  CHECK(fw.bus().published() == 1U);
  CHECK(clock->notice_calls == 1U);
  CHECK(settings->notice_calls == 1U);
  CHECK(clock->last_payload == 7U);
  std::printf("  publish 后：clock 收到 %u 条、settings 收到 %u 条（广播，前台后台都算）\n",
              clock->notice_calls, settings->notice_calls);

  fw.post(embark::CrossTaskMessage(settings_id, 42U));
  std::printf("  post 后（尚未 step）：只进收件箱，帧数还是 %u\n",
              static_cast<unsigned>(fw.frames()));
  CHECK(clock->cross_task_calls == 0U);
  run_frames(1U);
  CHECK(clock->cross_task_calls == 1U);
  CHECK(settings->cross_task_calls == 1U);
  CHECK(fw.inbox_overflows() == 0U);
  std::printf("  step 后：两个 App 都收到信封（跨任务消息在下一帧派发，内容不丢）\n");

  step_title("⑤ 前台切换：request_switch(settings) 在下一帧的循环边界生效");
  CHECK(fw.request_switch(settings_id) == embark::Error::none);
  std::printf("  请求已受理：此刻前台仍是 %s（切换不在请求里发生）\n",
              fw.app(fw.foreground())->name());
  CHECK(fw.foreground() == clock_id);
  run_frames(1U);
  CHECK(fw.foreground() == settings_id);
  CHECK(fw.switches() == 1U);
  CHECK(clock->pause_calls == 1U);     // 旧前台让出
  CHECK(settings->enter_calls == 1U);  // 新前台首次进入
  std::printf("  step 后：前台 = %s，累计切换 %u 次\n", fw.app(fw.foreground())->name(),
              static_cast<unsigned>(fw.switches()));

  step_title("⑥ 再切回 clock：第二次进前台走 onResume，不再走 onEnter");
  CHECK(fw.request_switch(clock_id) == embark::Error::none);
  run_frames(1U);
  CHECK(fw.foreground() == clock_id);
  CHECK(clock->enter_calls == 1U);   // 没有第二次 onEnter
  CHECK(clock->resume_calls == 1U);  // 而是 onResume
  CHECK(settings->pause_calls == 1U);
  std::printf("  step 后：前台 = %s（clock.onEnter 仍是 %u 次、clock.onResume %u 次）\n",
              fw.app(fw.foreground())->name(), clock->enter_calls, clock->resume_calls);

  step_title("⑦ shutdown()：前台 onPause → 全 App onExit → UI 端口 shutdown");
  fw.shutdown();
  CHECK(clock->exit_calls == 1U);
  CHECK(settings->exit_calls == 1U);
  CHECK(ui.shutdown_count == 1);
  std::printf("  收尾：帧数 %u，UI pump %d 次 / process %d 次，切换 %u 次，收件箱溢出 %u\n",
              static_cast<unsigned>(fw.frames()), ui.pump_count, ui.process_count,
              static_cast<unsigned>(fw.switches()), static_cast<unsigned>(fw.inbox_overflows()));
  std::printf("===== 全流程走完：注册 → boot → 节拍 → 消息 → 切换 → 收尾 =====\n\n");
}
