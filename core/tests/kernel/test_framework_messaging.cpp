// Framework（issues/07）：总线、收件箱回派、后台节拍与 own task 装配。
//
// 在 test_framework.cpp 的 06 契约之上验证 07 的增量：
//   boot 后总线广播（AppAdapter 订阅）→ onMessage；
//   post() 入收件箱，下一帧 step 的派发段广播（信封内容不丢）；
//   收件箱溢出计满（覆盖最旧，UI 不阻塞）；
//   tick 策略在"第一次进过前台"（武装）之后按 ui_loop_period_ms 粒度周期触发
//   onBackgroundTick（period 0 = 没有后台体）；own_task 策略在武装时经 ITaskSpawner
//   创建（名/栈/优先级），失败路径透传（武装时机：issue 23 / ADR 0009）；
//   ArmPolicy::at_boot 的 App 不等前台，boot 的 at_boot 轮就武装（issue 24 / ADR 0010）。
#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include <embark/app_registry.h>
#include <embark/error.h>
#include <embark/framework.h>
#include <embark/message.h>
#include <embark/task_spawner.h>
#include <embark_limits.h>

#include "fakes/fakes.h"

namespace {

using embark::AppSettings;
using embark::ArmPolicy;
using embark::BackgroundPolicy;
using embark::Framework;
using embark::Message;

// ---- 复用 test_framework.cpp 的钩子记录 App（各自独立静态实例）-----------
enum class Hook { create, enter, pause, resume, exit, bg_tick, message };
using HookList = std::vector<Hook>;

class RecorderApp : public embark::App {
 public:
  explicit RecorderApp(const char* name) noexcept : name_(name) {}
  [[nodiscard]] const char* name() const override { return name_; }

  void onCreate(Framework&) override { push(Hook::create); }
  void onEnter() override { push(Hook::enter); }
  void onPause() override { push(Hook::pause); }
  void onResume() override { push(Hook::resume); }
  void onExit() override { push(Hook::exit); }
  void onBackgroundTick(std::uint32_t) override { push(Hook::bg_tick); }
  void onMessage(const Message&) override { push(Hook::message); }

  void reset(HookList* record) noexcept {
    record_ = record;
    if (record_ != nullptr) {
      record_->clear();
    }
  }

 private:
  void push(Hook hook) noexcept {
    if (record_ != nullptr) {
      record_->push_back(hook);
    }
  }

  const char* name_;
  HookList* record_ = nullptr;
};

/// 后台节拍策略的 App：周期可配。
class TickApp final : public RecorderApp {
 public:
  TickApp() : RecorderApp("tick") {}  // 默认 period 0（= suspend，测试默认构造路径）
  explicit TickApp(std::uint32_t period_ms) : RecorderApp("tick"), period_ms_(period_ms) {}

  void set_period_ms(std::uint32_t period_ms) noexcept { period_ms_ = period_ms; }

  [[nodiscard]] AppSettings settings() const override {
    return AppSettings{BackgroundPolicy::tick, period_ms_, 0U, 0U};
  }

 private:
  std::uint32_t period_ms_ = 0;
};

/// 信封探针：记录收到的跨任务消息内容（其余钩子照旧记录）。
class ProbeApp final : public RecorderApp {
 public:
  ProbeApp() : RecorderApp("probe") {}

  void onMessage(const Message& msg) override {
    RecorderApp::onMessage(msg);
    if (msg.get_message_id() == embark::cross_task_message_id) {
      const auto& envelope = static_cast<const embark::CrossTaskMessage&>(msg);
      last_from = envelope.from_app;
      last_seq = envelope.seq;
      ++envelope_count;
    }
  }

  void reset_probe() noexcept {
    last_from = 0xFFU;
    last_seq = 0U;
    envelope_count = 0U;
  }

  std::uint32_t last_from = 0xFFU;
  std::uint32_t last_seq = 0U;
  std::uint32_t envelope_count = 0U;
};

/// own task 策略的 App（栈 128 字、优先级 4、周期 10 ms —— 只验装配参数）。
class OwnApp final : public RecorderApp {
 public:
  OwnApp() : RecorderApp("own") {}

  [[nodiscard]] AppSettings settings() const override {
    return AppSettings{BackgroundPolicy::own_task, 10U, 128U, 4U};
  }
};

/// at_boot 的 tick App（issue 24 / ADR 0010）：不等前台，boot 就武装。
class EagerTickApp final : public RecorderApp {
 public:
  EagerTickApp() : RecorderApp("eager_tick") {}

  [[nodiscard]] AppSettings settings() const override {
    return AppSettings{BackgroundPolicy::tick, 40U, 0U, 0U, ArmPolicy::at_boot};
  }
};

/// at_boot 的 own_task App（issue 24）：boot 时就经 spawner 创建。
class EagerOwnApp final : public RecorderApp {
 public:
  EagerOwnApp() : RecorderApp("eager_own") {}

  [[nodiscard]] AppSettings settings() const override {
    return AppSettings{BackgroundPolicy::own_task, 10U, 128U, 4U, ArmPolicy::at_boot};
  }
};

/// 记录 spawn 调用的任务创建器替身。
class FakeSpawner final : public embark::ITaskSpawner {
 public:
  struct Call {
    const char* name;
    void (*entry)(void*) noexcept;
    void* argument;
    std::uint16_t stack_words;
    std::uint16_t priority;
  };

  [[nodiscard]] etl::expected<embark::TaskToken, embark::Error> spawn_task(
      const char* name, void (*entry)(void*) noexcept, void* argument, std::uint16_t stack_words,
      std::uint16_t priority) noexcept override {
    if (calls.size() < 16) {
      calls.push_back({name, entry, argument, stack_words, priority});
    }
    if (result != embark::Error::none) {
      return embark::unexpected(result);
    }
    return embark::TaskToken{static_cast<std::uint16_t>(calls.size() - 1U), 0U};
  }

  /// 替身不真建任务：入口永不返回，回收一律 busy（持有者侧记录保持 active）。
  [[nodiscard]] embark::Error release_task(embark::TaskToken) noexcept override {
    return embark::Error::busy;
  }

  std::vector<Call> calls;
  embark::Error result = embark::Error::none;
};

class FrontApp : public RecorderApp {
 public:
  FrontApp() : RecorderApp("front") {}
};

class BackApp : public RecorderApp {
 public:
  BackApp() : RecorderApp("back") {}
};

struct Records {
  HookList front;
  HookList back;
};

// 绑定钩子记录到调用者持有的 Records（out 参数：按值返回会让 App 的 record_
// 指向已析构的局部对象 —— 悬垂，所有断言都会读到被栈复用污染的脏数据）。
void bind_records(const embark::AppRegistry& registry, Records& records) {
  static_cast<RecorderApp*>(registry.at(0))->reset(&records.front);
  static_cast<RecorderApp*>(registry.at(1))->reset(&records.back);
}

// 摘掉注册表里所有 App 的钩子记录。FrontApp 这些是**进程级单实例**
//（detail::app_instance<T>() 每个类型只有一个），上一个用例的 Records 已经析构 ——
// boot 会立刻调 onCreate，不先断开就会写进悬垂的 vector（栈被踩 → 段错误）。
void unbind_records(embark::AppRegistry registry) {
  for (std::size_t index = 0; index < registry.size(); ++index) {
    static_cast<RecorderApp*>(registry.at(index))->reset(nullptr);
  }
}

std::uint32_t count_hooks(const HookList& list, Hook hook) {
  std::uint32_t count = 0;
  for (const Hook h : list) {
    if (h == hook) {
      ++count;
    }
  }
  return count;
}

using TestNotice = embark::MessageT<0x33>;

}  // namespace

TEST_CASE("Framework：boot 后总线装配，publish 广播给所有 App（onMessage）") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  Framework fw(ctx, embark::app_registry<FrontApp, BackApp>());
  Records records;
  bind_records(fw.apps(), records);
  REQUIRE(fw.boot() == embark::Error::none);
  records.front.clear();
  records.back.clear();

  fw.publish(TestNotice{});

  CHECK(fw.bus().published() == 1U);
  CHECK(fw.bus().unknown() == 0U);
  CHECK(count_hooks(records.front, Hook::message) == 1U);
  CHECK(count_hooks(records.back, Hook::message) == 1U);  // v1 广播：adapter 全收
}

TEST_CASE("Framework：post 入收件箱，下一帧 step 派发（信封内容保留）") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  Framework fw(ctx, embark::app_registry<ProbeApp>());
  // 单 App：probe 是注册表里的静态实例（app_registry 用进程级 static），
  // 必须经 fw.app(0) 拿；reset 防上一用例的绑定悬垂。
  ProbeApp& probe = *static_cast<ProbeApp*>(fw.app(0));
  probe.reset(nullptr);
  probe.reset_probe();
  REQUIRE(fw.boot() == embark::Error::none);

  fw.post(embark::CrossTaskMessage(0U, 42U));
  CHECK(probe.envelope_count == 0U);  // 未派发

  fw.step();
  CHECK(probe.envelope_count == 1U);
  CHECK(probe.last_from == 0U);
  CHECK(probe.last_seq == 42U);
}

TEST_CASE("Framework：收件箱满时覆盖最旧并计数溢出") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  Framework fw(ctx, embark::app_registry<FrontApp, BackApp>());
  Records records;
  bind_records(fw.apps(), records);
  REQUIRE(fw.boot() == embark::Error::none);

  constexpr std::uint32_t depth = embark::message_queue_depth;
  for (std::uint32_t i = 0; i < depth; ++i) {
    fw.post(embark::CrossTaskMessage(0U, i));
  }
  CHECK(fw.inbox_overflows() == 0U);

  fw.post(embark::CrossTaskMessage(0U, depth));  // 第 depth+1 条：溢出 1
  CHECK(fw.inbox_overflows() == 1U);
  fw.post(embark::CrossTaskMessage(0U, depth + 1U));
  CHECK(fw.inbox_overflows() == 2U);
}

TEST_CASE("Framework：tick 策略在武装（第一次进前台）之后按 UI 循环粒度触发") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  const std::uint32_t period_ticks = 8U;  // 40 ms / 5 ms（ui_loop_period_ms）
  Framework fw(ctx, embark::app_registry<FrontApp, TickApp>());
  // app_registry 的实例是进程级 static，构造参数传不进去（默认 period 0 =
  // suspend）；boot 前经 setter 把周期配上。
  static_cast<TickApp*>(fw.app(1))->set_period_ms(40U);
  Records records;
  bind_records(fw.apps(), records);
  REQUIRE(fw.boot() == embark::Error::none);
  records.front.clear();
  records.back.clear();

  // tick App 不是默认前台（注册表第 1 个）→ boot 不武装它（issue 23 / ADR 0009）：
  // 还没被打开过，跑满两个周期也不该有节拍。
  CHECK_FALSE(fw.background_armed(1U));
  for (std::uint32_t frame = 0; frame < period_ticks * 2U; ++frame) {
    fw.step();
  }
  CHECK(count_hooks(records.back, Hook::bg_tick) == 0U);  // 没进过前台 = 后台不跑

  // 第一次切到前台 = 武装时机。切换在这一帧的循环边界生效，同帧的后台节拍段已经
  // 算武装之后的第 1 拍，所以"到期"落在武装后第 period_ticks 拍。
  REQUIRE(fw.request_switch(1U) == embark::Error::none);
  fw.step();
  CHECK(fw.background_armed(1U));
  records.front.clear();
  records.back.clear();

  for (std::uint32_t frame = 0; frame < period_ticks - 2U; ++frame) {
    fw.step();
  }
  CHECK(count_hooks(records.back, Hook::bg_tick) == 0U);   // 还差一拍
  CHECK(count_hooks(records.front, Hook::bg_tick) == 0U);  // front 是 suspend

  // 注意 0U 本身是空指针常量，会和 request_switch(const char*) 撞重载 → 显式写类型
  REQUIRE(fw.request_switch(embark::AppId{0U}) == embark::Error::none);  // 退回 front：后台照跑
  fw.step();  // 武装后第 period_ticks 拍：到期触发
  CHECK(count_hooks(records.back, Hook::bg_tick) == 1U);
  CHECK(count_hooks(records.front, Hook::bg_tick) == 0U);  // front 仍然没有后台

  for (std::uint32_t frame = 0; frame < period_ticks; ++frame) {
    fw.step();
  }
  CHECK(count_hooks(records.back, Hook::bg_tick) == 2U);  // repeating，周期重排
}

TEST_CASE("Framework：period_ms == 0 的 tick 策略 = 没有后台体（武装后也不跑）") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  Framework fw(ctx, embark::app_registry<FrontApp, TickApp>());
  // 静态实例的 period 会被上一用例的 setter 污染（同为 app_registry<FrontApp,
  // TickApp>），显式归零后 boot —— 才是本用例要验证的"period 0 = 没有后台体"。
  static_cast<TickApp*>(fw.app(1))->set_period_ms(0U);
  Records records;
  bind_records(fw.apps(), records);
  REQUIRE(fw.boot() == embark::Error::none);

  // 先切进去武装它：这样"不跑"就只能解释成 period 0，而不是"还没武装"。
  REQUIRE(fw.request_switch(1U) == embark::Error::none);
  fw.step();
  CHECK(fw.background_armed(1U));
  records.front.clear();
  records.back.clear();

  for (std::uint32_t frame = 0; frame < 32; ++frame) {
    fw.step();
  }
  CHECK(count_hooks(records.back, Hook::bg_tick) == 0U);
  CHECK(fw.arm_failures() == 0U);  // 没有后台体不是失败
}

TEST_CASE("Framework：own_task 是默认前台 → boot 当场武装并经 spawner 创建") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  FakeSpawner spawner;
  Framework fw(ctx, embark::app_registry<OwnApp>(), nullptr, &spawner);
  // 防御：静态实例的 record_ 归零（本文件里 OwnApp 只在 own_task 用例出现）
  static_cast<RecorderApp*>(fw.app(0))->reset(nullptr);

  REQUIRE(fw.boot() == embark::Error::none);
  CHECK(fw.background_armed(0U));  // 默认前台：boot 里就算"进过前台"，当场武装
  REQUIRE(spawner.calls.size() == 1U);
  CHECK(std::string(spawner.calls[0].name) == "own");
  CHECK(spawner.calls[0].entry != nullptr);
  CHECK(spawner.calls[0].argument != nullptr);
  CHECK(spawner.calls[0].stack_words == 128U);  // 来自 AppSettings
  CHECK(spawner.calls[0].priority == 4U);
}

TEST_CASE("Framework：own_task 武装失败路径（无 spawner / spawner 报错）") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();

  // 平台没给任务创建能力：unsupported
  Framework no_spawner(ctx, embark::app_registry<OwnApp>());
  CHECK(no_spawner.boot() == embark::Error::unsupported);
  CHECK_FALSE(no_spawner.booted());
  CHECK(no_spawner.arm_failures() == 0U);  // boot 期失败走返回值，不计入运行期计数

  // spawner 拒绝：错误原样透传，booted 不置位
  FakeSpawner spawner;
  spawner.result = embark::Error::no_space;
  Framework rejected(ctx, embark::app_registry<OwnApp>(), nullptr, &spawner);
  CHECK(rejected.boot() == embark::Error::no_space);
  CHECK_FALSE(rejected.booted());
}

TEST_CASE("Framework：ArmPolicy::at_boot 的 tick App 开机即武装（不等前台）") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  const std::uint32_t period_ticks = 8U;  // 40 ms / 5 ms（ui_loop_period_ms）
  Framework fw(ctx, embark::app_registry<FrontApp, EagerTickApp>());
  Records records;
  bind_records(fw.apps(), records);
  REQUIRE(fw.boot() == embark::Error::none);
  records.front.clear();
  records.back.clear();

  // 一次前台都没切过：boot 的 at_boot 轮已经武装它（issue 24 / ADR 0010）。
  CHECK(fw.background_armed(1U));
  CHECK(fw.arm_failures() == 0U);
  for (std::uint32_t frame = 0; frame < period_ticks - 1U; ++frame) {
    fw.step();
  }
  CHECK(count_hooks(records.back, Hook::bg_tick) == 0U);  // 还差一拍
  fw.step();                                              // 武装后第 period_ticks 拍
  CHECK(count_hooks(records.back, Hook::bg_tick) == 1U);
  CHECK(count_hooks(records.front, Hook::bg_tick) == 0U);  // front 是 suspend
}

TEST_CASE("Framework：ArmPolicy::at_boot 的 own_task App 不必进前台，boot 就创建") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  FakeSpawner spawner;
  Framework fw(ctx, embark::app_registry<FrontApp, EagerOwnApp>(), nullptr, &spawner);
  unbind_records(fw.apps());  // 防御：上一个用例的 Records 已析构（见 unbind_records）

  REQUIRE(fw.boot() == embark::Error::none);
  CHECK(fw.background_armed(1U));
  REQUIRE(spawner.calls.size() == 1U);
  CHECK(std::string(spawner.calls[0].name) == "eager_own");
  CHECK(spawner.calls[0].stack_words == 128U);
}

TEST_CASE("Framework：没声明 at_boot 的 own_task App 仍然等第一次进前台") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  FakeSpawner spawner;
  Framework fw(ctx, embark::app_registry<FrontApp, OwnApp>(), nullptr, &spawner);
  unbind_records(fw.apps());  // 防御：同上

  REQUIRE(fw.boot() == embark::Error::none);
  CHECK_FALSE(fw.background_armed(1U));  // 默认口径：进过前台才武装
  CHECK(spawner.calls.empty());

  REQUIRE(fw.request_switch(1U) == embark::Error::none);
  fw.step();
  CHECK(fw.background_armed(1U));
  REQUIRE(spawner.calls.size() == 1U);
  CHECK(std::string(spawner.calls[0].name) == "own");
}