// 服务层装配（ADR 0019 / issue 07）：Context 加三个可空服务槽之后，
// 各槽的类型、默认值，以及"没接服务时框架照常启动"。
//
// 这里刻意只做静态与空值检查：IImu / IAudio / INetwork 在第 07 票只有前置声明，
// 没有完整定义，造不出实例 —— 这正是不让 Context include 服务头的代价，也是收益。
#include <doctest/doctest.h>

#include <type_traits>
#include <utility>

#include <embark/app_registry.h>
#include <embark/framework.h>

#include "fakes/fake_ui_port.h"
#include "fakes/fakes.h"

using embark::fakes::FakeHal;

namespace {

// 成员访问表达式的 decltype 就是声明类型：不完整类型也能这么比（不需要完整定义）。
// 这条把"槽的名字按能力取、不按芯片取"钉在编译期（AGENTS.md 本仓硬规则）。
static_assert(std::is_same_v<decltype(std::declval<embark::hal::Context&>().imu), embark::hal::IImu*>);
static_assert(std::is_same_v<decltype(std::declval<embark::hal::Context&>().audio), embark::hal::IAudio*>);
static_assert(std::is_same_v<decltype(std::declval<embark::hal::Context&>().network), embark::hal::INetwork*>);

/// 只读服务槽的 App：今天读到的必须是 nullptr，且读它不该让启动失败。
class ProbeServiceApp final : public embark::App {
 public:
  [[nodiscard]] const char* name() const override { return "probe-service"; }

  void onCreate(embark::Framework& fw) override {
    imu_present = fw.hal().imu != nullptr;
    audio_present = fw.hal().audio != nullptr;
    network_present = fw.hal().network != nullptr;
  }
  void onEnter() override {}
  void onPause() override {}
  void onResume() override {}
  void onExit() override {}

  bool imu_present = true;
  bool audio_present = true;
  bool network_present = true;
};

}  // namespace

TEST_CASE("Context：三个服务槽缺省是 nullptr（没有这块硬件不是错误）") {
  FakeHal hal;
  const embark::hal::Context ctx = hal.context();

  CHECK(ctx.imu == nullptr);
  CHECK(ctx.audio == nullptr);
  CHECK(ctx.network == nullptr);
}

TEST_CASE("Context：聚合初始化项少于成员数时，服务槽按值初始化成 nullptr") {
  // 防的是"有人只写 8 项、以为编译器会拦"—— C++17 的聚合初始化不会拦，加成员是静默的。
  // 所以三个装配点（esp32_hal / host_context / fakes）只能靠清单 + grep 保证。
  FakeHal hal;
  // 只写 8 项（filesystem 也不接，FakeHal 没有盘后端）。
  const embark::hal::Context partial{hal.time, hal.storage, hal.log, hal.system, hal.bus,
                                     &hal.display, &hal.input, nullptr};

  CHECK(partial.display == &hal.display);
  CHECK(partial.input == &hal.input);
  CHECK(partial.filesystem == nullptr);
  CHECK(partial.imu == nullptr);
  CHECK(partial.audio == nullptr);
  CHECK(partial.network == nullptr);
}

TEST_CASE("Context：八个既有槽各自指向自己的后端（防写反）") {
  // display / input / filesystem 都是裸指针，写反了编译器一句话不说 —— 这条逐个点名。
  // 它同时是"服务槽只能加在尾部"的回归网：装配点用聚合初始化，重排成员这里会立刻红。
  FakeHal hal;
  embark::hal::Context ctx = hal.context();

  CHECK(&ctx.time == &hal.time);
  CHECK(&ctx.storage == &hal.storage);
  CHECK(&ctx.log == &hal.log);
  CHECK(&ctx.system == &hal.system);
  CHECK(&ctx.bus == &hal.bus);
  CHECK(ctx.display == &hal.display);
  CHECK(ctx.input == &hal.input);
  CHECK(ctx.filesystem == nullptr);
}

TEST_CASE("Framework：服务槽为空时 App 照常 boot（08/09/10 接入前的基线）") {
  FakeHal hal;
  auto ctx = hal.context();
  embark::fakes::FakeUiPort ui;
  embark::Framework fw(ctx, embark::app_registry<ProbeServiceApp>(), &ui);

  REQUIRE(fw.boot() == embark::Error::none);

  const auto* app = static_cast<const ProbeServiceApp*>(fw.apps().at(0));
  CHECK_FALSE(app->imu_present);
  CHECK_FALSE(app->audio_present);
  CHECK_FALSE(app->network_present);
  CHECK(fw.foreground() == 0U);

  fw.shutdown();
}
