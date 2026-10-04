/**
 * 内核测试：own task 的运行时生命周期（issue 15）
 *
 * 平台无关的那一半（定容任务池）由 tests/kernel/test_pooled_task_spawner.cpp 逐格验证；
 * 这一份验证**框架侧**的接线：boot 按策略创建、step 每帧回收、运行期再创建一轮。
 *
 * 普通进程里没有调度器，所以"任务入口返回"这件事由 ScriptedSpawner::run() 代劳 —— 它调
 * 的正是框架真正的入口（Framework::own_task_entry），于是整条链路上只有"平台 trampoline"
 * 一段是替身（真机上那一段是 platform/common/pooled_task_spawner.h 的 finish_and_park）。
 *
 * 契约（issues/15-runtime-task-lifecycle.md 验收②：可观测"创建 → 跑完 → 回收 → 再创建"）：
 *   - boot 按 own_task 策略创建任务，名字/栈深/优先级原样交给 ITaskSpawner（0 = 取默认栈）；
 *   - 入口没返回时回收段什么也不做（release 报 busy 不是错误，下一帧再看）；
 *   - 入口返回后 step 自动回收；回收后槽位可再用，同一个 App 也能再来一轮；
 *   - 回收数也能显式查询（reap_finished_own_tasks() 的返回值）；
 *   - 失败口径：编号无效 → not_found；boot 前 / shutdown 后 → not_ready；同一 App 已有
 *     任务 → busy；池满或平台建不出来 → 透传 no_space 且不留下半条记录。
 */
#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <embark/app_registry.h>
#include <embark/error.h>
#include <embark/framework.h>
#include <embark/task_spawner.h>
#include <embark_limits.h>

#include "fakes/fake_ui_port.h"
#include "fakes/fakes.h"

namespace {

using embark::AppSettings;
using embark::BackgroundPolicy;
using embark::Error;
using embark::Framework;
using embark::max_own_tasks;
using embark::TaskToken;

/// 注册表下标 = App 编号（注册顺序即默认前台顺序）。
constexpr embark::AppId job_id = 0U;
constexpr embark::AppId default_stack_id = 1U;
constexpr embark::AppId loop_id = 0U;

/// 任务创建器替身：记下每次创建，并按"停稳没停稳"给 release 定调。
/// 它模仿真机池（PooledTaskSpawner）在框架眼里的样子：
///   入口没返回 → busy；返回了但没停稳 → busy；停稳了 → none，世代号 +1、槽位还池。
class ScriptedSpawner final : public embark::ITaskSpawner {
 public:
  struct Task {
    const char* name = nullptr;
    void (*entry)(void*) noexcept = nullptr;
    void* argument = nullptr;
    std::uint16_t stack_words = 0U;
    std::uint16_t priority = 0U;
    bool finished = false;  ///< 入口已返回（真机：池的 trampoline 记的）
    bool parked = false;    ///< 内核已停稳（真机：eTaskGetState() == eSuspended）
    bool deleted = false;   ///< 槽已被回收（真机：vTaskDelete 已同步删干净）
  };

  [[nodiscard]] etl::expected<TaskToken, Error> spawn_task(
      const char* name, void (*entry)(void*) noexcept, void* argument, std::uint16_t stack_words,
      std::uint16_t priority) noexcept override {
    if (fail_next) {
      fail_next = false;
      ++failures;
      return embark::unexpected(Error::no_space);  // 平台建不出来（真机上是 no_space）
    }
    const std::uint16_t slot = find_free_slot();
    if (slot == invalid_slot) {
      return embark::unexpected(Error::no_space);  // 池满：并发上限 = max_own_tasks
    }
    Task& task = tasks_[slot];
    task = Task{};
    task.name = name;
    task.entry = entry;
    task.argument = argument;
    task.stack_words = stack_words;
    task.priority = priority;
    live_[slot] = true;
    return TaskToken{slot, generations_[slot]};
  }

  [[nodiscard]] Error release_task(TaskToken token) noexcept override {
    if (token.slot >= max_own_tasks) {
      return Error::not_found;
    }
    Task& task = tasks_[token.slot];
    if (!live_[token.slot] || token.generation != generations_[token.slot]) {
      return Error::not_found;  // 已经释放过 / 槽位换人了
    }
    if (!task.finished || !task.parked) {
      return Error::busy;  // 还在跑 / 入口返回了但内核没停稳
    }
    task.deleted = true;
    live_[token.slot] = false;
    ++generations_[token.slot];
    return Error::none;
  }

  // --- 测试驱动与观测 -------------------------------------------------------

  /// 让槽里的任务在测试线程上跑一次：入口（= 框架的 own_task_entry）返回即 finished。
  /// park_immediately = false 模拟"入口已返回、内核还没摘干净"的交接窗口。
  /// **只对 period_ms == 0 的一次性任务用**：常驻任务的入口是个死循环。
  bool run(std::uint16_t slot, bool park_immediately = true) {
    if (slot >= max_own_tasks || !live_[slot] || tasks_[slot].entry == nullptr) {
      return false;
    }
    Task& task = tasks_[slot];
    task.entry(task.argument);  // ← 真机上这一段跑在另一个核上
    task.finished = true;
    task.parked = park_immediately;
    return true;
  }

  /// 交接窗口走完：内核把任务摘干净了。
  void let_park(std::uint16_t slot) noexcept { tasks_[slot].parked = true; }

  [[nodiscard]] bool live(std::uint16_t slot) const noexcept {
    return slot < max_own_tasks ? live_[slot] : false;
  }
  [[nodiscard]] bool deleted(std::uint16_t slot) const noexcept {
    return slot < max_own_tasks ? tasks_[slot].deleted : false;
  }
  [[nodiscard]] const char* name_of(std::uint16_t slot) const noexcept { return tasks_[slot].name; }
  [[nodiscard]] std::uint16_t stack_words_of(std::uint16_t slot) const noexcept {
    return tasks_[slot].stack_words;
  }
  [[nodiscard]] std::uint16_t priority_of(std::uint16_t slot) const noexcept {
    return tasks_[slot].priority;
  }
  [[nodiscard]] std::size_t live_count() const noexcept {
    std::size_t total = 0U;
    for (bool used : live_) {
      total += used ? 1U : 0U;
    }
    return total;
  }

  bool fail_next = false;  ///< 下一次 spawn 让平台"建不出来"
  std::size_t failures = 0U;

 private:
  [[nodiscard]] std::uint16_t find_free_slot() const noexcept {
    for (std::uint16_t index = 0U; index < max_own_tasks; ++index) {
      if (!live_[index]) {
        return index;
      }
    }
    return invalid_slot;
  }

  static constexpr std::uint16_t invalid_slot = 0xFFFFU;

  Task tasks_[max_own_tasks] = {};
  bool live_[max_own_tasks] = {};
  std::uint32_t generations_[max_own_tasks] = {};  ///< release 时 +1（跨槽位复用保留）
};

/// 一次性 own task 的 App：period_ms = 0（框架解释成"跑一轮就结束"），栈 256 字、优先级 4。
class JobApp final : public embark::App {
 public:
  [[nodiscard]] const char* name() const override { return "job"; }

  [[nodiscard]] AppSettings settings() const override {
    return AppSettings{BackgroundPolicy::own_task, 0U, 256U, 4U};
  }

  void onCreate(Framework&) override {}
  void onEnter() override {}
  void onPause() override {}
  void onResume() override {}
  void onBackgroundTick(std::uint32_t) override { ++runs; }
  void onExit() override {}

  std::uint32_t runs = 0U;  // 静态实例跨用例存活：每次进来先清零
};

/// 栈深/优先级都不填的 App：框架必须补上 own_task_stack_words 的默认栈。
class DefaultStackApp final : public embark::App {
 public:
  [[nodiscard]] const char* name() const override { return "default"; }

  [[nodiscard]] AppSettings settings() const override {
    return AppSettings{BackgroundPolicy::own_task, 0U, 0U, 0U};
  }

  void onCreate(Framework&) override {}
  void onEnter() override {}
  void onPause() override {}
  void onResume() override {}
  void onBackgroundTick(std::uint32_t) override { ++runs; }
  void onExit() override {}

  std::uint32_t runs = 0U;
};

/// 常驻 own task 的 App（周期非 0）：入口是死循环，永远不会被回收。
class LoopApp final : public embark::App {
 public:
  [[nodiscard]] const char* name() const override { return "loop"; }

  [[nodiscard]] AppSettings settings() const override {
    return AppSettings{BackgroundPolicy::own_task, 50U, 128U, 3U};
  }

  void onCreate(Framework&) override {}
  void onEnter() override {}
  void onPause() override {}
  void onResume() override {}
  void onBackgroundTick(std::uint32_t) override {}
  void onExit() override {}
};

/// 走 n 帧（与 test_system_tour.cpp 同一套喂时钟的方式）。
void run_frames(Framework& fw, embark::fakes::FakeHal& hal, std::uint32_t count) {
  for (std::uint32_t index = 0U; index < count; ++index) {
    hal.time.set_now_ms(fw.frames() * embark::ui_loop_period_ms);
    fw.step();
  }
}

}  // namespace

TEST_CASE("系统用例：own task 创建 → 跑完 → 回收 → 再创建（跟着日志读）") {
  std::printf("\n===== own task 生命周期：boot 创建 → 跑完 → step 回收 → 运行期再创建 =====\n");

  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  REQUIRE(hal.init() == Error::none);
  embark::fakes::FakeUiPort ui;
  ScriptedSpawner spawner;

  JobApp& job = embark::detail::app_instance<JobApp>();
  job.runs = 0U;

  Framework fw(ctx, embark::app_registry<JobApp>(), &ui, &spawner);

  std::printf("① boot 前：spawn_own_task() 只能拿到 not_ready（持有者生命周期之外）\n");
  CHECK(fw.spawn_own_task(job_id) == Error::not_ready);
  CHECK(fw.reap_finished_own_tasks() == 0U);
  CHECK(fw.own_tasks_spawned() == 0U);

  std::printf("② boot：own_task 策略的 App 在这里被创建（名字/栈/优先级原样交出去）\n");
  REQUIRE(fw.boot() == Error::none);
  CHECK(fw.own_tasks_spawned() == 1U);
  CHECK(fw.own_tasks_released() == 0U);
  CHECK(spawner.live_count() == 1U);
  CHECK(std::strcmp(spawner.name_of(0U), "job") == 0);
  CHECK(spawner.stack_words_of(0U) == 256U);
  CHECK(spawner.priority_of(0U) == 4U);
  std::printf("  已创建：job（栈 256 字 / 优先级 4）；此刻它还没跑，槽位是 running\n");

  std::printf("③ 入口还没返回：回收段什么也不做（busy 不是错误，下一帧再看）\n");
  run_frames(fw, hal, 3U);
  CHECK(job.runs == 0U);
  CHECK(fw.own_tasks_released() == 0U);
  CHECK(fw.reap_finished_own_tasks() == 0U);
  CHECK(spawner.live(0U));
  CHECK_FALSE(spawner.deleted(0U));

  std::printf("④ 任务跑完：入口返回 → 平台记 finished 并 park；step 的回收段把槽位还池\n");
  REQUIRE(spawner.run(0U));              // ← 真机上这一步由调度器完成
  CHECK(job.runs == 1U);                 // period_ms = 0：任务体只跑一轮 onBackgroundTick
  CHECK(fw.own_tasks_released() == 0U);  // 还没 step：回收责任在持有者
  run_frames(fw, hal, 1U);
  CHECK(fw.own_tasks_released() == 1U);
  CHECK_FALSE(spawner.live(0U));
  CHECK(spawner.deleted(0U));
  std::printf("  回收后：创建 1 / 回收 1，槽位空闲（世代号 +1，旧句柄从此 not_found）\n");

  std::printf("⑤ 运行期再创建一轮：spawn_own_task(job) 复用同一个槽\n");
  CHECK(fw.spawn_own_task(job_id) == Error::none);
  CHECK(fw.own_tasks_spawned() == 2U);
  CHECK(spawner.live(0U));                          // 池把空出来的槽又发了出去
  CHECK(fw.spawn_own_task(job_id) == Error::busy);  // 同一 App 同时只允许一个任务
  CHECK(fw.spawn_own_task(embark::invalid_app_id) == Error::not_found);
  CHECK(fw.spawn_own_task(1U) == Error::not_found);  // 注册表里只有一个 App
  REQUIRE(spawner.run(0U));
  run_frames(fw, hal, 1U);
  CHECK(fw.own_tasks_released() == 2U);
  CHECK(job.runs == 2U);
  CHECK(spawner.live_count() == 0U);
  std::printf("  第二轮同样跑完并回收：创建 2 / 回收 2，池回到全空\n");

  std::printf("⑥ 回收段也认「入口返回了但内核还没停稳」：那一帧只报 busy，绝不提前删\n");
  CHECK(fw.spawn_own_task(job_id) == Error::none);
  REQUIRE(spawner.run(0U, /*park_immediately=*/false));
  run_frames(fw, hal, 2U);
  CHECK(fw.own_tasks_released() == 2U);  // 交接窗口里一帧都不许回收
  CHECK_FALSE(spawner.deleted(0U));
  spawner.let_park(0U);                       // 内核停稳了
  CHECK(fw.reap_finished_own_tasks() == 1U);  // 显式回收：返回本次回收数
  CHECK(fw.own_tasks_released() == 3U);
  CHECK(spawner.deleted(0U));

  std::printf("⑦ shutdown 之后不再创建（槽位随进程一起结束）\n");
  fw.shutdown();
  CHECK(fw.spawn_own_task(job_id) == Error::not_ready);
  CHECK(fw.own_tasks_spawned() == 3U);
  std::printf("  收尾：创建 %u / 回收 %u，job 共跑完 %u 轮\n",
              static_cast<unsigned>(fw.own_tasks_spawned()),
              static_cast<unsigned>(fw.own_tasks_released()), static_cast<unsigned>(job.runs));
  std::printf("===== 一轮循环走完：创建 → 跑完 → 回收 → 再创建 =====\n\n");
}

TEST_CASE("own task：boot 一次装两个任务，池满后 no_space，失败路径不留记录") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  REQUIRE(hal.init() == Error::none);
  embark::fakes::FakeUiPort ui;
  ScriptedSpawner spawner;

  JobApp& job = embark::detail::app_instance<JobApp>();
  DefaultStackApp& other = embark::detail::app_instance<DefaultStackApp>();
  job.runs = 0U;
  other.runs = 0U;

  Framework fw(ctx, embark::app_registry<JobApp, DefaultStackApp>(), &ui, &spawner);
  REQUIRE(fw.boot() == Error::none);
  CHECK(fw.own_tasks_spawned() == 2U);  // max_own_tasks = 2：两个槽刚好装满
  CHECK(spawner.live_count() == 2U);

  // 栈深 0 = 没填：框架补默认栈（config/embark_limits.h 的 own_task_stack_words）。
  CHECK(spawner.stack_words_of(default_stack_id) == embark::own_task_stack_words);
  CHECK(spawner.priority_of(default_stack_id) == 0U);

  // 池满：第三个任务拿不到槽位，错误原样透传（确定性失败点 = no_space）。
  const std::size_t spawned_before = fw.own_tasks_spawned();
  CHECK(fw.spawn_own_task(job_id) == Error::busy);  // 它自己已经有一个
  CHECK(fw.own_tasks_spawned() == spawned_before);

  // 两个都跑完并回收，槽位立刻可用。
  REQUIRE(spawner.run(job_id));
  REQUIRE(spawner.run(default_stack_id));
  CHECK(fw.reap_finished_own_tasks() == 2U);
  CHECK(job.runs == 1U);
  CHECK(other.runs == 1U);
  CHECK(spawner.live_count() == 0U);
  CHECK(fw.own_tasks_released() == 2U);

  // 平台建不出来：错误透传，记录回滚（下次还能再创建）。
  spawner.fail_next = true;
  CHECK(fw.spawn_own_task(job_id) == Error::no_space);
  CHECK(spawner.failures == 1U);
  CHECK(fw.own_tasks_spawned() == spawned_before);  // 失败不算创建成功
  CHECK(spawner.live_count() == 0U);
  CHECK(fw.spawn_own_task(job_id) == Error::none);  // 回滚干净：同一个 App 还能再来
  CHECK(fw.own_tasks_spawned() == spawned_before + 1U);

  fw.shutdown();
}

TEST_CASE("own task：常驻任务永远不被回收（回收只认「入口已返回」）") {
  embark::fakes::FakeHal hal;
  auto ctx = hal.context();
  REQUIRE(hal.init() == Error::none);
  embark::fakes::FakeUiPort ui;
  ScriptedSpawner spawner;

  Framework fw(ctx, embark::app_registry<LoopApp>(), &ui, &spawner);
  REQUIRE(fw.boot() == Error::none);
  CHECK(fw.own_tasks_spawned() == 1U);
  CHECK(spawner.stack_words_of(loop_id) == 128U);
  CHECK(spawner.priority_of(loop_id) == 3U);

  // 周期非 0 = 常驻循环：走多少帧都不会 finished，回收段一律 busy。
  run_frames(fw, hal, 5U);
  CHECK(fw.reap_finished_own_tasks() == 0U);
  CHECK(fw.own_tasks_released() == 0U);
  CHECK(spawner.live(loop_id));
  CHECK_FALSE(spawner.deleted(loop_id));
  CHECK(fw.spawn_own_task(loop_id) == Error::busy);  // 它自己就占着唯一的槽

  fw.shutdown();
  // shutdown 不终止 own task（v1 口径）：槽位还记着，进程退出时随 .bss 一起没。
  CHECK(fw.own_tasks_released() == 0U);
  CHECK(spawner.live(loop_id));
}
