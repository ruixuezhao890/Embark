// Framework（issues/06）：boot/step/shutdown 的生命周期与钩子顺序契约。
//
// 用 FakeUiPort + 记录型 App 验证 spec §5/§6 定死的顺序：
//   boot   = UI init → 全 App onCreate（注册顺序）→ 默认前台 onEnter；
//   step   = tick → pump_input → 循环边界切换生效 → process；每帧一次；
//   切换   = 旧前台 onPause → 新前台 onEnter（首次）/ onResume（再次）；
//   shutdown = 前台 onPause → 全 App onExit（注册顺序）→ UI 端口 shutdown。
#include <doctest/doctest.h>

#include <cstdint>
#include <initializer_list>
#include <vector>

#include <embark/app_registry.h>
#include <embark/error.h>
#include <embark/framework.h>

#include "fakes/fake_ui_port.h"
#include "fakes/fakes.h"

namespace {

using embark::Framework;
using embark::invalid_app_id;

// 钩子事件：按调用顺序记下来，断言"谁先谁后"。
enum class Hook { create, enter, pause, resume, exit, bg_tick, message };

using HookList = std::vector<Hook>;

void check_hooks(const HookList& got, std::initializer_list<Hook> expected) {
  REQUIRE(got.size() == expected.size());
  std::size_t index = 0;
  for (const Hook want : expected) {
    CHECK(got[index] == want);
    ++index;
  }
}

// 记录钩子序列的通用 App。进程级静态实例跨测试存活，所以每个测试开头要 reset。
// 两个测试 App 用不同类型的静态实例，互不串扰。
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
  void onMessage(const embark::Message&) override { push(Hook::message); }

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

class FrontApp final : public RecorderApp {
 public:
  FrontApp() : RecorderApp("front") {}
};

class BackApp final : public RecorderApp {
 public:
  BackApp() : RecorderApp("back") {}
};

struct Records {
  HookList front;
  HookList back;
};

/// 把注册表里两个 App 绑定到两个记录（静态实例，必须每测试重置）。
Records bind_records(const embark::AppRegistry& registry) {
  Records records;
  static_cast<RecorderApp*>(registry.at(0))->reset(&records.front);
  static_cast<RecorderApp*>(registry.at(1))->reset(&records.back);
  return records;
}

/// 请求切换并断言成功。参数是 AppId：字面量 0 直接传会同时匹配
/// AppId 与 const char*（空指针常量），绕一层就不歧义了；也顺带消费 [[nodiscard]]。
void switch_to_ok(embark::Framework& fw, embark::AppId target) {
  CHECK(fw.request_switch(target) == embark::Error::none);
}

}  // namespace

TEST_CASE("Framework：boot 顺序 = UI init → 全 App onCreate → 默认前台 onEnter") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  embark::fakes::FakeUiPort ui;
  Framework fw(ctx, embark::app_registry<FrontApp, BackApp>(), &ui);

  CHECK_FALSE(fw.booted());
  CHECK(fw.foreground() == invalid_app_id);
  CHECK(fw.frames() == 0U);
  CHECK_FALSE(fw.exit_requested());

  Records records = bind_records(fw.apps());
  REQUIRE(fw.boot() == embark::Error::none);

  CHECK(fw.booted());
  CHECK(ui.init_count == 1);
  CHECK(fw.foreground() == 0U);
  CHECK(fw.pending_foreground() == 0U);
  CHECK_FALSE(fw.switch_pending());
  check_hooks(records.front, {Hook::create, Hook::enter});
  check_hooks(records.back, {Hook::create});
}

TEST_CASE("Framework：boot 幂等；UI init 失败 → boot 失败且不碰任何 App") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  embark::fakes::FakeUiPort ui;
  ui.init_result = embark::Error::no_space;
  Framework fw(ctx, embark::app_registry<FrontApp, BackApp>(), &ui);

  Records records = bind_records(fw.apps());
  REQUIRE(fw.boot() == embark::Error::no_space);
  CHECK_FALSE(fw.booted());
  CHECK(records.front.empty());  // onCreate 一个都没跑（失败不进循环）
  CHECK(records.back.empty());
  CHECK(ui.init_count == 1);

  // 纠正后重调 boot 即可（没有"回滚"概念）
  ui.init_result = embark::Error::none;
  REQUIRE(fw.boot() == embark::Error::none);
  check_hooks(records.front, {Hook::create, Hook::enter});
}

TEST_CASE("Framework：空注册表 = 装配错误（没有默认前台）") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  Framework fw(ctx, embark::app_registry<>(), nullptr);

  CHECK(fw.boot() == embark::Error::invalid_argument);
  CHECK_FALSE(fw.booted());

  // 未 boot 时的所有入口都安全（no-op 或不损坏状态）
  fw.step();
  fw.shutdown();
  CHECK(fw.frames() == 0U);
}

TEST_CASE("Framework：request_switch 只登记不触发钩子，step 边界才生效（首次 = onEnter）") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  Framework fw(ctx, embark::app_registry<FrontApp, BackApp>(), nullptr);
  Records records = bind_records(fw.apps());
  REQUIRE(fw.boot() == embark::Error::none);
  records.front.clear();
  records.back.clear();

  switch_to_ok(fw, 1);
  CHECK(fw.switch_pending());
  CHECK(fw.pending_foreground() == 1U);
  CHECK(fw.foreground() == 0U);
  check_hooks(records.front, {});  // 登记不改任何东西
  check_hooks(records.back, {});

  fw.step();
  check_hooks(records.front, {Hook::pause});
  check_hooks(records.back, {Hook::enter});  // 第一次当前台 = onEnter
  CHECK(fw.foreground() == 1U);
  CHECK_FALSE(fw.switch_pending());
  CHECK(fw.switches() == 1U);
}

TEST_CASE("Framework：切回已进过前台的 App = onResume；switch 计数累计") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  Framework fw(ctx, embark::app_registry<FrontApp, BackApp>(), nullptr);
  Records records = bind_records(fw.apps());
  REQUIRE(fw.boot() == embark::Error::none);
  records.front.clear();
  records.back.clear();

  switch_to_ok(fw, 1);
  fw.step();

  // 切回前台 App 0
  REQUIRE(fw.request_switch("front") == embark::Error::none);
  fw.step();
  check_hooks(records.back, {Hook::enter, Hook::pause});
  check_hooks(records.front, {Hook::pause, Hook::resume});  // 不是 enter
  CHECK(fw.switches() == 2U);

  // 再去 back：仍然 resume
  switch_to_ok(fw, 1);
  fw.step();
  check_hooks(records.back, {Hook::enter, Hook::pause, Hook::resume});
  check_hooks(records.front, {Hook::pause, Hook::resume, Hook::pause});
  CHECK(fw.switches() == 3U);
}

TEST_CASE("Framework：request_switch 错误路径与切当前前台 no-op") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  Framework fw(ctx, embark::app_registry<FrontApp, BackApp>(), nullptr);
  Records records = bind_records(fw.apps());
  REQUIRE(fw.boot() == embark::Error::none);
  records.front.clear();
  records.back.clear();
  CHECK(fw.switches() == 0U);

  CHECK(fw.request_switch(99) == embark::Error::not_found);
  CHECK(fw.request_switch(invalid_app_id) == embark::Error::not_found);
  CHECK(fw.request_switch("nope") == embark::Error::not_found);
  CHECK(fw.request_switch(nullptr) == embark::Error::not_found);

  // 切当前前台：登记被接受但不生效（no-op），计数与钩子都不动
  switch_to_ok(fw, 0);
  CHECK_FALSE(fw.switch_pending());
  fw.step();
  CHECK(fw.switches() == 0U);
  check_hooks(records.front, {});
  check_hooks(records.back, {});
}

TEST_CASE("Framework：step 每帧 = tick → pump → process；exit_requested 透传；帧计数") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  embark::fakes::FakeUiPort ui;
  Framework fw(ctx, embark::app_registry<FrontApp, BackApp>(), &ui);
  // 必须 bind：boot 会触发 onCreate/onEnter，静态 App 实例的 record_ 此刻还指着
  // 上一个用例的已析构 records（悬垂）——不绑就写坏堆。
  Records records = bind_records(fw.apps());
  REQUIRE(fw.boot() == embark::Error::none);

  fw.step();
  fw.step();
  fw.step();

  CHECK(ui.tick_count == 3);
  CHECK(ui.pump_count == 3);
  CHECK(ui.process_count == 3);
  CHECK(fw.frames() == 3U);
  CHECK(ui.shutdown_count == 0);

  // exit_requested 透传；查询类接口
  CHECK_FALSE(fw.exit_requested());
  ui.exit_requested_value = true;
  CHECK(fw.exit_requested());
  const embark::AppRegistry& registry = fw.apps();
  CHECK(fw.app(0) == registry.at(0));
  CHECK(fw.app(9) == nullptr);
  CHECK(fw.id_of(*registry.at(1)) == 1U);
}

TEST_CASE(
    "Framework：shutdown = 前台 onPause → 全 App onExit（注册序）→ UI 端口 shutdown；之后 step "
    "无效") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  embark::fakes::FakeUiPort ui;
  Framework fw(ctx, embark::app_registry<FrontApp, BackApp>(), &ui);
  Records records = bind_records(fw.apps());
  REQUIRE(fw.boot() == embark::Error::none);

  // 切一次：让 back 当前台，验证收尾从"当前前台"开始
  switch_to_ok(fw, 1);
  fw.step();
  records.front.clear();
  records.back.clear();

  fw.shutdown();
  check_hooks(records.front, {Hook::exit});              // 没在前台 → 只收尾
  check_hooks(records.back, {Hook::pause, Hook::exit});  // 前台 → pause 再 exit
  CHECK(ui.shutdown_count == 1);
  CHECK(fw.frames() == 1U);  // shutdown 本身不计帧

  // 幂等：第二次 shutdown 什么都不做
  fw.shutdown();
  CHECK(ui.shutdown_count == 1);

  // shutdown 后 step 无效（不崩、不计数、不触发钩子）
  fw.step();
  CHECK(fw.frames() == 1U);
  check_hooks(records.front, {Hook::exit});
  check_hooks(records.back, {Hook::pause, Hook::exit});
}