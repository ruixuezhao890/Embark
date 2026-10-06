// 零分配审计（spec §14.4 / issue 09）。
//
// 内核 + App 的稳态路径在宿主构建下断言 0 次堆分配：
//   构造 → boot → 帧循环（含消息派发）→ 前台切换 → shutdown，
//   以及 own_task 装配路径（spawn 参数检查与失败兜底）。
//
// 计数来自 tests/detail/zero_alloc_hooks.{h,cpp} 的全局 new/delete 钩子。
// 取数前不经过任何 doctest 断言；取数后若非 0 直接 abort —— ctest 以
// 非零退出码失败，stderr 里写明分配次数，一眼看出契约被破坏。
// 白名单：空。内核契约就是"稳态路径无条件零分配"，没有启动期一次性
// 分配需要豁免（需要时在用例里补 reset 分段并注明原因，但 v1 不需要）。
#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <embark/app.h>
#include <embark/app_registry.h>
#include <embark/error.h>
#include <embark/framework.h>
#include <embark/task_spawner.h>

#include "fakes/fake_ui_port.h"
#include "fakes/fakes.h"

#include "detail/zero_alloc_hooks.h"

namespace {

using embark::Framework;

// 审计用最小 App：挂起策略，后台 tick 只计数、不触碰任何可分配资源。
// 默认构造 + 固定名：app_registry 的静态实例需要默认构造。
class AuditApp final : public embark::App {
 public:
  AuditApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "audit"; }

  void onCreate(Framework&) override {}
  void onEnter() override {}
  void onPause() override {}
  void onResume() override {}
  void onExit() override {}
  void onBackgroundTick(std::uint32_t) override { ++bg_ticks_; }
  void onMessage(const embark::Message&) override { ++messages_; }

  std::uint32_t bg_ticks_ = 0;
  std::uint32_t messages_ = 0;
};

// 审计用消息：栈上构造、publish 时拷进总线 —— 消息路径的全部对象都在栈上。
// 0x61 落在 0x60~0x7F 的测试私有段（spec §7 的消息 id 规划）。
class AuditMsg final : public embark::MessageT<0x61> {
 public:
  std::uint32_t value = 0;
};

// 记录型 spawner：走完 Framework 的 own_task 装配路径（参数检查、失败兜底），
// 但记录而不真正创建任务 —— 真任务创建（xTaskCreateStatic 静态内存）由
// 宿主验收 --own-task 覆盖。实现与 kernel messaging 测试里的 FakeSpawner
// 同构，按需精简。
class RecordSpawner final : public embark::ITaskSpawner {
 public:
  [[nodiscard]] etl::expected<embark::TaskToken, embark::Error> spawn_task(
      const char*, void (*)(void*) noexcept, void*, std::uint16_t,
      std::uint16_t) noexcept override {
    ++spawn_calls_;
    if (result_ != embark::Error::none) {
      return embark::unexpected(result_);
    }
    return embark::TaskToken{0U, 0U};
  }

  /// 记录型替身不真建任务，入口永远不会返回，所以回收一律报 busy。
  [[nodiscard]] embark::Error release_task(embark::TaskToken) noexcept override {
    return embark::Error::busy;
  }

  std::uint32_t spawn_calls_ = 0;
  embark::Error result_ = embark::Error::none;
};

// own_task 后台 App：让 boot 走真装配（周期 50 ms、栈 128 字、优先级 4；
// 与宿主 TickerApp 同参数档）。声明 ArmPolicy::at_boot（issue 24 / ADR 0010）：
// 它不在默认前台位，后台也该在 boot 的 at_boot 轮就武装起来。
class OwnApp final : public embark::App {
 public:
  [[nodiscard]] const char* name() const override { return "own"; }
  [[nodiscard]] embark::AppSettings settings() const override {
    return embark::AppSettings{embark::BackgroundPolicy::own_task, 50U, 128U, 4U,
                               embark::ArmPolicy::at_boot};
  }
  void onCreate(Framework&) override {}
  void onEnter() override {}
  void onPause() override {}
  void onResume() override {}
  void onExit() override {}
  void onBackgroundTick(std::uint32_t) override {}
  void onMessage(const embark::Message&) override {}
};

}  // namespace

TEST_CASE("零分配审计：boot→帧循环→消息→切换→shutdown 全程 0 次堆分配") {
  embark::test::reset_global_allocation_counters();

  // 全部对象在栈上；注册表实例是进程级静态（首次调用才构造，构造零分配）。
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  embark::fakes::FakeUiPort ui;
  Framework fw(ctx, embark::app_registry<AuditApp, AuditApp>(), &ui);

  REQUIRE(fw.boot() == embark::Error::none);

  // 帧循环：tick → 输入泵 → 切换生效 → 消息抽干，8 帧覆盖
  // 前台 onBackgroundTick 挂起（AuditApp 是 suspend，tick 不触发）。
  for (std::uint32_t i = 0U; i < 8U; ++i) {
    fw.step();
  }

  // 消息路径：publish 广播给两个订阅的 adapter（Bus 镜像表扫描 + ETL 分发）。
  AuditMsg msg;
  msg.value = 7U;
  fw.publish(msg);

  // 切换路径：请求 + 两帧让切换在循环边界生效。
  REQUIRE(fw.request_switch(1U) == embark::Error::none);
  fw.step();
  fw.step();
  CHECK(fw.foreground() == 1U);

  // 收尾：前台 onPause → 全 App onExit → UI 端口 shutdown。
  fw.shutdown();

  // —— 取数（前面一个 doctest 断言都没有）——
  const std::uint32_t allocations = embark::test::global_allocation_count();
  if (allocations != 0U) {
    std::fprintf(stderr,
                 "零分配审计失败：内核稳态路径（构造→boot→8 帧→publish→切换→shutdown）"
                 "共 %u 次堆分配（期望 0）\n",
                 allocations);
    std::abort();
  }

  // 钩子自检：主动分配一次，计数必须 +1 —— 证明上面的"0 次"不是钩子失效的假象。
  {
    void* probe = ::operator new(sizeof(std::uint32_t));
    ::operator delete(probe);
  }
  const std::uint32_t after_probe = embark::test::global_allocation_count();
  if (after_probe != allocations + 1U) {
    std::fprintf(stderr, "零分配审计钩子失效：主动分配 1 次后计数 %u（期望 %u，钩子没在数分配）\n",
                 after_probe, allocations + 1U);
    std::abort();
  }
}

TEST_CASE("零分配审计：own_task 装配路径（成功与失败兜底）0 次堆分配") {
  embark::test::reset_global_allocation_counters();

  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  embark::fakes::FakeUiPort ui;
  RecordSpawner spawner;

  // 成功路径：OwnApp 在第 1 个且声明了 at_boot → boot 的 at_boot 轮就武装并 spawn
  //（issue 24 / ADR 0010：一趟前台都没进过也照样跑），boot 正常完成。
  Framework fw(ctx, embark::app_registry<AuditApp, OwnApp>(), &ui, &spawner);
  REQUIRE(fw.boot() == embark::Error::none);
  CHECK(fw.background_armed(1U));
  CHECK(spawner.spawn_calls_ == 1U);
  fw.shutdown();

  // 失败路径：spawner 返回 no_space → boot 把错误原样返回（不分配）。
  Framework fw2(ctx, embark::app_registry<AuditApp, OwnApp>(), &ui, &spawner);
  spawner.result_ = embark::Error::no_space;
  REQUIRE(fw2.boot() == embark::Error::no_space);
  CHECK(spawner.spawn_calls_ == 2U);
  fw2.shutdown();

  const std::uint32_t allocations = embark::test::global_allocation_count();
  if (allocations != 0U) {
    std::fprintf(stderr,
                 "零分配审计失败：own_task 装配路径（成功 + no_space 兜底）"
                 "共 %u 次堆分配（期望 0）\n",
                 allocations);
    std::abort();
  }
}