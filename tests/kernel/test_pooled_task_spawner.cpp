/**
 * 内核测试：定容任务池（issue 15）
 *
 * 平台无关的那一半（platform/common/pooled_task_spawner.h）在没有调度器的普通进程里跑不
 * 起来 —— 所以这一份测试自带 FakeKernel：它不建真任务，只用一张静态登记表把"建 / 查 / 停"
 * 三个动作演出来，于是 spawn → 入口返回 → park → release → 复用这条状态机可以逐格验证。
 *
 * 契约（对应 issues/15-runtime-task-lifecycle.md 的验收）：
 *   - 参数非法 → invalid_argument；栈深超槽容量 / 槽满 → no_space；
 *   - 入口返回后槽位变 finished，release 之前必须"真的 park 了"（没停稳 → busy）；
 *   - release 之后槽位立即可复用，池的 free_block_count() 回到初始值；
 *   - 失败路径不泄漏：槽位回 free、outstanding_bytes 回 0、回滚不算池的失败；
 *   - 旧句柄（世代号不符 / 下标越界 / 已释放）稳定拿到 not_found。
 *
 * 注意：测试可执行文件【故意不链】embark_kernel_flags（见 tests/CMakeLists.txt），
 * 所以 FakeKernel::suspend_self() 可以用异常把控制权交回测试线程。
 */
#include <cstddef>
#include <cstdint>
#include <new>

#include <doctest/doctest.h>

#include <embark/error.h>
#include <embark/task_spawner.h>
#include <embark_limits.h>

#include "pooled_task_spawner.h"

namespace {

using embark::Error;
using embark::max_own_tasks;
using embark::PooledTaskSpawner;
using embark::PoolTask;
using embark::SlotState;
using embark::TaskToken;
using embark::platform::StaticPool;

/// "任务停下了"：真机上是 vTaskSuspend 之后永不返回，测试里用异常把控制权交回测试线程。
struct ParkedSignal {};

/// 调用方任务入口被调到的次数（验证 trampoline 真的把入口调起来了）。
std::uint32_t g_entry_calls = 0;

void counting_entry(void* argument) noexcept {
  ++g_entry_calls;
  if (argument != nullptr) {
    ++(*static_cast<std::uint32_t*>(argument));
  }
}

/// 假 Kernel：把任务槽、登记表与 park 状态都摆在测试进程里，没有调度器。
class FakeKernel {
 public:
  /// 槽类型：header 必须是第一个成员（header_of 直接取它）。
  struct Task {
    PoolTask header{};
    bool parked = false;
    bool deleted = false;
    std::uint16_t stack_words = 0U;
    std::uint16_t priority = 0U;
  };

  using KernelEntry = void (*)(void*);

  static constexpr std::size_t slot_bytes() noexcept {
    return (sizeof(Task) + (StaticPool::granule_bytes - 1U)) & ~(StaticPool::granule_bytes - 1U);
  }

  static PoolTask& header_of(Task& task) noexcept { return task.header; }

  static Task* make_task(std::uint8_t* storage, const char* name, KernelEntry trampoline,
                         const PoolTask& record, std::uint16_t stack_words,
                         std::uint16_t priority) noexcept {
    (void)name;
    if (storage == nullptr || trampoline == nullptr ||
        static_cast<std::size_t>(record.slot) >= max_own_tasks) {
      return nullptr;
    }
    if (fail_next_make) {
      fail_next_make = false;
      ++make_failures;
      return nullptr;
    }
    auto* const task = new (storage) Task();
    task->header = record;
    task->stack_words = stack_words;
    task->priority = priority;
    entry_[record.slot] = trampoline;
    task_[record.slot] = task;
    deleted_[record.slot] = false;
    ++make_calls;
    return task;
  }

  /// 真的停下了才算 parked：lag_park 模拟"入口已返回但内核还没摘干净"的交接窗口。
  static bool is_parked(const Task& task) noexcept { return task.parked && !lag_park; }

  static void delete_task(Task& task) noexcept {
    task.deleted = true;
    ++delete_calls;
    const std::uint16_t slot = task.header.slot;
    if (slot < max_own_tasks) {
      deleted_[slot] = true;
      task_[slot] = nullptr;
      entry_[slot] = nullptr;
    }
  }

  /// 平台上的 suspend_self() 永不返回；测试里用异常跳出 finish_and_park 的死循环。
  static void suspend_self() { throw ParkedSignal{}; }

  // --- 测试驱动与观测 -------------------------------------------------------

  static void reset() noexcept {
    g_entry_calls = 0U;
    make_calls = 0U;
    make_failures = 0U;
    delete_calls = 0U;
    fail_next_make = false;
    lag_park = false;
    for (std::size_t index = 0U; index < max_own_tasks; ++index) {
      task_[index] = nullptr;
      entry_[index] = nullptr;
      deleted_[index] = false;
    }
  }

  /// 让槽里的"任务"在测试线程上跑一次：入口返回后它会 park，抛异常回到这里。
  /// 返回 true = 它确实跑到了 park（release 的前提）。
  static bool run(std::uint16_t slot) {
    if (slot >= max_own_tasks || entry_[slot] == nullptr) {
      return false;
    }
    const KernelEntry entry = entry_[slot];
    void* const storage = task_[slot];
    try {
      entry(storage);
    } catch (const ParkedSignal&) {
      task_[slot]->parked = true;
      return true;
    }
    return false;  // 不该发生：finish_and_park 的循环不返回
  }

  static bool deleted(std::uint16_t slot) noexcept {
    return slot < max_own_tasks ? deleted_[slot] : false;
  }

  static std::uint16_t stack_words_of(std::uint16_t slot) noexcept {
    return task_[slot] != nullptr ? task_[slot]->stack_words : 0U;
  }

  static std::uint16_t priority_of(std::uint16_t slot) noexcept {
    return task_[slot] != nullptr ? task_[slot]->priority : 0U;
  }

  static inline bool fail_next_make = false;
  static inline bool lag_park = false;
  static inline std::size_t make_calls = 0U;
  static inline std::size_t make_failures = 0U;
  static inline std::size_t delete_calls = 0U;

 private:
  static inline Task* task_[max_own_tasks] = {};
  static inline KernelEntry entry_[max_own_tasks] = {};
  static inline bool deleted_[max_own_tasks] = {};
};

using Pool = PooledTaskSpawner<FakeKernel>;

/// 池的 arena 至少要装下 max_own_tasks 个槽（容量在接口上是可查的常量）。
static_assert(Pool::arena_bytes() >= max_own_tasks * FakeKernel::slot_bytes(),
              "arena_bytes() 装不下 max_own_tasks 个槽");

}  // namespace

TEST_CASE("定容任务池：参数非法与容量上限的失败口径") {
  FakeKernel::reset();
  Pool spawner;

  const auto no_name = spawner.spawn_task(nullptr, &counting_entry, nullptr, 128U, 4U);
  REQUIRE_FALSE(no_name.has_value());
  CHECK(no_name.error() == Error::invalid_argument);

  const auto no_entry = spawner.spawn_task("worker", nullptr, nullptr, 128U, 4U);
  REQUIRE_FALSE(no_entry.has_value());
  CHECK(no_entry.error() == Error::invalid_argument);

  const auto no_stack = spawner.spawn_task("worker", &counting_entry, nullptr, 0U, 4U);
  REQUIRE_FALSE(no_stack.has_value());
  CHECK(no_stack.error() == Error::invalid_argument);

  const auto too_deep =
      spawner.spawn_task("worker", &counting_entry, nullptr,
                         static_cast<std::uint16_t>(embark::own_task_stack_words + 1U), 4U);
  REQUIRE_FALSE(too_deep.has_value());
  CHECK(too_deep.error() == Error::no_space);

  // 失败路径不该碰池：容量、记账、槽位全没动。
  CHECK(spawner.pool().allocations() == 0U);
  CHECK(spawner.pool().failures() == 0U);
  CHECK(spawner.pool().outstanding_bytes() == 0U);
  CHECK(spawner.free_slots() == max_own_tasks);
  CHECK(spawner.free_blocks() == 1U);
  CHECK(FakeKernel::make_calls == 0U);
}

TEST_CASE("定容任务池：槽满 no_space、release 后可复用、池记账回到原点") {
  FakeKernel::reset();
  Pool spawner;

  const std::size_t initial_blocks = spawner.free_blocks();
  CHECK(initial_blocks == 1U);
  CHECK(spawner.pool().capacity_bytes() >= max_own_tasks * FakeKernel::slot_bytes());
  CHECK(spawner.pool().outstanding_bytes() == 0U);

  std::uint32_t argument = 0U;
  TaskToken tokens[max_own_tasks] = {};
  for (std::size_t index = 0U; index < max_own_tasks; ++index) {
    const auto spawned = spawner.spawn_task("worker", &counting_entry, &argument, 128U, 4U);
    REQUIRE(spawned.has_value());
    tokens[index] = *spawned;
    CHECK(tokens[index].slot == static_cast<std::uint16_t>(index));
    CHECK(tokens[index].generation == 0U);
  }
  CHECK(spawner.running() == max_own_tasks);
  CHECK(spawner.finished() == 0U);
  CHECK(spawner.free_slots() == 0U);
  CHECK(spawner.pool().outstanding_bytes() == max_own_tasks * FakeKernel::slot_bytes());

  // 槽满：第三个任务拿不到槽位（确定性失败点），且不该记成池的分配失败。
  const std::size_t failures_before = spawner.pool().failures();
  const auto overflow = spawner.spawn_task("worker", &counting_entry, &argument, 128U, 4U);
  REQUIRE_FALSE(overflow.has_value());
  CHECK(overflow.error() == Error::no_space);
  CHECK(spawner.pool().failures() == failures_before);

  // 入口还没返回：release 报 busy（v1 不做强制终止），槽位与池都不许动。
  CHECK(spawner.release_task(tokens[0]) == Error::busy);
  CHECK(FakeKernel::deleted(tokens[0].slot) == false);
  CHECK(spawner.state_of(tokens[0].slot) == SlotState::running);

  // 让第一个任务跑完：入口返回 → 池的 trampoline 记 finished 并 park 在槽里。
  CHECK(FakeKernel::run(tokens[0].slot));
  CHECK(g_entry_calls == 1U);
  CHECK(argument == 1U);
  CHECK(spawner.state_of(tokens[0].slot) == SlotState::finished);
  CHECK(spawner.running() == max_own_tasks - 1U);
  CHECK(spawner.finished() == 1U);
  CHECK(FakeKernel::deleted(tokens[0].slot) == false);

  // 回收：删掉已 park 的任务、槽位还池、世代号 +1。
  CHECK(spawner.release_task(tokens[0]) == Error::none);
  CHECK(FakeKernel::deleted(tokens[0].slot));
  CHECK(spawner.state_of(tokens[0].slot) == SlotState::free);
  CHECK(spawner.generation_of(tokens[0].slot) == 1U);
  CHECK(spawner.free_slots() == 1U);
  CHECK(spawner.pool().outstanding_bytes() == (max_own_tasks - 1U) * FakeKernel::slot_bytes());

  // 旧句柄不再指向任何任务：重复释放是安全的，稳定 not_found。
  CHECK(spawner.release_task(tokens[0]) == Error::not_found);

  // 槽位立即可复用，而且能再次跑完、再次回收。
  const auto reused = spawner.spawn_task("worker", &counting_entry, &argument, 128U, 4U);
  REQUIRE(reused.has_value());
  CHECK(reused->slot == tokens[0].slot);
  CHECK(reused->generation == 1U);
  CHECK(FakeKernel::run(reused->slot));
  CHECK(spawner.release_task(*reused) == Error::none);

  CHECK(FakeKernel::run(tokens[1].slot));
  CHECK(spawner.release_task(tokens[1]) == Error::none);

  // 全部还清：记账回原点（free_block_count 恢复 = 相邻空闲块已合并成一块）。
  CHECK(spawner.running() == 0U);
  CHECK(spawner.finished() == 0U);
  CHECK(spawner.free_slots() == max_own_tasks);
  CHECK(spawner.pool().outstanding_bytes() == 0U);
  CHECK(spawner.free_blocks() == initial_blocks);
  CHECK(spawner.pool().allocations() == 3U);
  CHECK(spawner.pool().deallocations() == 3U);
  CHECK(FakeKernel::delete_calls == 3U);
}

TEST_CASE("定容任务池：入口已返回但内核还没停稳时 release 只报 busy") {
  FakeKernel::reset();
  Pool spawner;
  FakeKernel::lag_park = true;  // 模拟真机跨核的交接窗口

  const auto spawned = spawner.spawn_task("worker", &counting_entry, nullptr, 128U, 4U);
  REQUIRE(spawned.has_value());
  CHECK(FakeKernel::run(spawned->slot));
  CHECK(spawner.state_of(spawned->slot) == SlotState::finished);

  // finished 但还没 park：绝不删（删一个还在跑的/正在交接的任务会踩内核的异步回收）。
  CHECK(spawner.release_task(*spawned) == Error::busy);
  CHECK(FakeKernel::deleted(spawned->slot) == false);
  CHECK(spawner.pool().outstanding_bytes() == FakeKernel::slot_bytes());

  FakeKernel::lag_park = false;  // 下一帧再看：停稳了
  CHECK(spawner.release_task(*spawned) == Error::none);
  CHECK(FakeKernel::deleted(spawned->slot));
  CHECK(spawner.pool().outstanding_bytes() == 0U);
}

TEST_CASE("定容任务池：平台建任务失败时槽位与池记账原样回滚") {
  FakeKernel::reset();
  Pool spawner;
  const std::size_t blocks_before = spawner.free_blocks();

  FakeKernel::fail_next_make = true;
  const auto failed = spawner.spawn_task("worker", &counting_entry, nullptr, 128U, 4U);
  REQUIRE_FALSE(failed.has_value());
  CHECK(failed.error() == Error::busy);
  CHECK(FakeKernel::make_failures == 1U);

  CHECK(spawner.state_of(0U) == SlotState::free);
  CHECK(spawner.free_slots() == max_own_tasks);
  CHECK(spawner.pool().outstanding_bytes() == 0U);
  CHECK(spawner.free_blocks() == blocks_before);
  CHECK(spawner.pool().failures() == 0U);  // 回滚不算池的失败

  // 失败之后池仍然可用。
  const auto ok = spawner.spawn_task("worker", &counting_entry, nullptr, 128U, 4U);
  REQUIRE(ok.has_value());
  CHECK(ok->slot == 0U);
  CHECK(spawner.pool().outstanding_bytes() == FakeKernel::slot_bytes());
}

TEST_CASE("定容任务池：越界、陌生与过期句柄稳定拿到 not_found") {
  FakeKernel::reset();
  Pool spawner;

  CHECK(spawner.release_task(TaskToken{static_cast<std::uint16_t>(max_own_tasks), 0U}) ==
        Error::not_found);
  CHECK(spawner.release_task(TaskToken{Pool::invalid_slot, 0U}) == Error::not_found);
  CHECK(spawner.release_task(TaskToken{0U, 7U}) == Error::not_found);  // 空槽 + 陌生世代号

  const auto spawned = spawner.spawn_task("worker", &counting_entry, nullptr, 128U, 4U);
  REQUIRE(spawned.has_value());
  CHECK(spawner.release_task(TaskToken{spawned->slot, spawned->generation + 1U}) ==
        Error::not_found);  // 世代号不符：旧句柄绝不误杀新任务
  CHECK(spawner.release_task(*spawned) == Error::busy);  // 正主，但还在跑
  CHECK(FakeKernel::deleted(spawned->slot) == false);
}

TEST_CASE("定容任务池：栈深、优先级与参数原样交给平台 Kernel") {
  FakeKernel::reset();
  Pool spawner;

  std::uint32_t argument = 0U;
  const auto spawned = spawner.spawn_task("named", &counting_entry, &argument, 333U, 7U);
  REQUIRE(spawned.has_value());
  CHECK(FakeKernel::stack_words_of(spawned->slot) == 333U);
  CHECK(FakeKernel::priority_of(spawned->slot) == 7U);

  CHECK(FakeKernel::run(spawned->slot));
  CHECK(argument == 1U);  // trampoline 把调用方的 argument 原样转给了入口
  CHECK(g_entry_calls == 1U);
}
