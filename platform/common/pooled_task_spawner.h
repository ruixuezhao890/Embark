/**
 * Embark · 定容任务池（issue 15）：运行期创建 / 结束 / 回收，平台无关的那一半
 *
 * spec §6 的 own_task 从 issue 15 起可以在运行期创建并回收，但"静态分配"这条纪律不变：
 * 槽位内存由池自己在 .bss 里开（alignas(16) 的数组），池只做切分、记账与状态机；
 * FreeRTOS 的差异全部收在一个 Kernel 里（见文件末的"怎么接一个平台"）。
 *
 * 本文件**不许** include 任何 FreeRTOS 头：内核、单测替身与两个平台共用它，include 了就把
 * "内核不 include FreeRTOS"这条结构保证破了。tests/kernel/test_pooled_task_spawner.cpp 里的
 * FakeKernel 正是靠这一点，在没有调度器的普通进程里验证整套状态机。
 *
 * 三步生命周期：
 *   1. spawn_task(...)          → Kernel 在池给的槽上建任务，返回 TaskToken；
 *   2. 任务入口返回             → 池的 trampoline 记 finished，然后 park 在槽里（永不返回）；
 *   3. 持有者 release_task(...)  → 确认已 park 后 Kernel 删任务，++generation，槽位还池。
 *
 * 为什么 park + 持有者他删（而不是任务自删）：
 *   - 自删（vTaskDelete(NULL)）之后 TCB 还挂在 FreeRTOS 的"待清理"链表上，要等空闲任务收尾；
 *     此时槽位若已还池、被下一个任务复用，就是静态存储被别名 —— 致命。
 *   - 他删一个**已经 park** 的任务是同步的：TCB 当场摘干净。两平台的内核路径都逐行验证过，
 *     结论与证据记在 .scratch/embark-v1/issues/15-runtime-task-lifecycle.md。
 *   - "入口返回"这件事只有平台侧看得见，所以必须停在池的 trampoline 里再记状态。
 *   - 代价：回收责任落在持有者（全工程 = 唯一 UI 任务）头上。忘了 release 的槽位会一直占着，
 *     持有者用 finished() 就能看见（池不在任务栈上打日志：own task 的栈只有几百字节，
 *     elog 一条日志就要在调用者栈上放 385 字节的记录缓冲，见 docs/reference/pitfalls.md）。
 *
 * 线程纪律：spawn_task / release_task 只有持有者任务能调，本类不是线程安全的。唯一的跨任务
 * 访问是任务入口返回时的那一次记账（mark_finished），它是单写者、单调、且只用来把槽位从
 * running 推向 finished：持有者读到"还没 finished"最多是晚一帧回收，读到 finished 时任务的
 * 入口一定已经返回（FreeRTOS 建任务与入口返回之间本来就有一条内核级的先后关系）。
 * 不引入原子操作 —— 本仓库的 src/ 与 platform/ 至今没有任何 std::atomic，不为这一次单向标志破例。
 */
#ifndef EMBARK_POOLED_TASK_SPAWNER_H
#define EMBARK_POOLED_TASK_SPAWNER_H

#include <cstddef>
#include <cstdint>

#include <embark/error.h>
#include <embark/task_spawner.h>
#include <embark_limits.h>

#include "static_pool.h"

namespace embark {

/// 每个槽额外留给 StaticPool 的字节：块头（64 位宿主 sizeof(Block)=40 → 对齐后 48，
/// 32 位目标 20 → 32）+ 一次切分的粒度损耗 + 余量。池的 arena 大小按它算（见 arena_bytes()）。
inline constexpr std::size_t pool_overhead_per_slot_bytes = 96U;

/// 槽位状态机。名字是给日志、测试与解说程序看的，池内部只做状态迁移。
enum class SlotState : std::uint8_t {
  free = 0,      ///< 空槽：可以再分配
  running = 1,   ///< 任务已创建、入口还没返回
  finished = 2,  ///< 入口已返回、park 着等持有者 release_task
};

/// 槽里的任务登记项：Kernel 的槽类型必须**第一个成员**就是它（Kernel::header_of 负责取回）。
/// 调用方的入口与参数存在这里，于是 trampoline 拿到槽指针就能自己调入口，Kernel 不必掺和。
struct PoolTask {
  void* spawner = nullptr;  ///< 拥有这块槽的 PooledTaskSpawner（trampoline 用它回调）
  void (*entry)(void*) noexcept = nullptr;  ///< 调用方给的任务入口（可以返回）
  void* argument = nullptr;                 ///< 传给入口的参数
  std::uint16_t slot = 0xFFFFU;             ///< 槽位下标
  std::uint16_t reserved_ = 0U;             ///< 显式补齐：槽的布局不要随编译器变
};

/// 定容任务池：ITaskSpawner 的平台无关实现，Kernel 只出"建 / 查 / 停"几个动作。
///
/// Kernel 需要提供（宿主见 platform/host/own_task_spawner.h，真机见
/// platform/esp32/src/esp32_task_spawner.h）：
///   using Task = ...;   // 槽类型：{ PoolTask header; StaticTask_t tcb; alignas(16) StackType_t stack[N]; }
///   static constexpr std::size_t slot_bytes() noexcept;                  // sizeof(Task) 按 16 上取整
///   static PoolTask& header_of(Task& task) noexcept;                     // 取回登记项（就是第一个成员）
///   static Task* make_task(std::uint8_t* storage, const char* name, KernelEntry trampoline,
///                          const PoolTask& record, std::uint16_t stack_words,
///                          std::uint16_t priority) noexcept;             // 先写登记项、再建任务；失败返回 nullptr
///   static bool is_parked(const Task& task) noexcept;                    // 真的停下了（不是"看起来停了"）
///   static void delete_task(Task& task) noexcept;                        // 删掉已 park 的任务（同步回收）
///   static void suspend_self() noexcept;                                 // 把当前任务永久停下（不许返回）
/// KernelEntry = void (*)(void*)：平台的任务函数类型（注意不是 noexcept 版本）。
/// Kernel 无状态，池按值不持有它 —— 所有方法都是 static，池只按类型调用。
template <typename Kernel>
class PooledTaskSpawner final : public ITaskSpawner {
 public:
  using Task = typename Kernel::Task;
  using KernelEntry = void (*)(void*);

  PooledTaskSpawner() noexcept = default;

  /// 池的 arena 大小：max_own_tasks 个槽 + 每槽一份 StaticPool 记账开销。
  /// 推导出来的数字是编译期常量，容量因此在接口上是可查询的（spec §6）。
  static constexpr std::size_t arena_bytes() noexcept {
    return max_own_tasks * Kernel::slot_bytes() + max_own_tasks * pool_overhead_per_slot_bytes;
  }

  [[nodiscard]] etl::expected<TaskToken, Error> spawn_task(
      const char* name, void (*entry)(void*) noexcept, void* argument, std::uint16_t stack_words,
      std::uint16_t priority) noexcept override {
    if (name == nullptr || entry == nullptr || stack_words == 0U) {
      return unexpected(Error::invalid_argument);
    }
    if (stack_words > own_task_stack_words) {
      return unexpected(Error::no_space);  // 槽的栈容量不够（容量是编译期常量，不是运行时资源）
    }
    const std::uint16_t slot = find_free_slot();
    if (slot == invalid_slot) {
      return unexpected(Error::no_space);  // 池满：并发上限 = max_own_tasks
    }
    std::uint8_t* const storage = static_cast<std::uint8_t*>(pool_.allocate(Kernel::slot_bytes()));
    if (storage == nullptr) {
      return unexpected(Error::no_space);  // 记账池满（正常不该发生：槽数已按容量算过）
    }

    PoolTask record;
    record.spawner = this;
    record.entry = entry;
    record.argument = argument;
    record.slot = slot;

    Slot& book = slots_[slot];
    // 先立状态再建任务：真机上任务落在另一个核，建完就可能开跑，先写状态才不会丢记账。
    book.state = SlotState::running;
    book.task = Kernel::make_task(storage, name, &PooledTaskSpawner::finish_and_park, record,
                                  stack_words, priority);
    if (book.task == nullptr) {
      book.state = SlotState::free;
      pool_.deallocate(storage);
      return unexpected(Error::busy);  // 静态创建理论不失败；真失败只可能是平台暂时给不出
    }
    return TaskToken{slot, book.generation};
  }

  [[nodiscard]] Error release_task(TaskToken token) noexcept override {
    if (token.slot >= max_own_tasks) {
      return Error::not_found;  // 槽位下标越界：不是本池发出的句柄
    }
    Slot& book = slots_[token.slot];
    if (book.state == SlotState::free || book.generation != token.generation) {
      return Error::not_found;  // 已经释放过 / 槽位换人了（旧句柄绝不误杀新任务）
    }
    if (book.state != SlotState::finished) {
      return Error::busy;  // 入口还没返回：v1 不做强制终止，持有者稍后重试
    }
    if (!Kernel::is_parked(*book.task)) {
      return Error::busy;  // 还没真的停下（交接窗口），下一帧再来
    }

    Kernel::delete_task(*book.task);
    pool_.deallocate(book.task);
    book.task = nullptr;
    book.state = SlotState::free;
    ++book.generation;  // 旧句柄从此稳定拿到 not_found
    return Error::none;
  }

  // --- 观测（测试、系统用例与验收）------------------------------------------

  /// 正在跑（入口还没返回）的任务数。
  [[nodiscard]] std::size_t running() const noexcept { return count(SlotState::running); }

  /// 已结束、等持有者回收的任务数。**不为 0 就说明有槽位被占着没还**。
  [[nodiscard]] std::size_t finished() const noexcept { return count(SlotState::finished); }

  /// 空闲槽数。
  [[nodiscard]] std::size_t free_slots() const noexcept {
    return max_own_tasks - running() - finished();
  }

  /// 池里还剩几个空闲块（相邻块已合并）。
  [[nodiscard]] std::size_t free_blocks() const noexcept { return pool_.free_block_count(); }

  /// 记账池本身（容量、峰值、失败次数……）：验收时对着容量常量看。
  [[nodiscard]] const platform::StaticPool& pool() const noexcept { return pool_; }

  /// 某个槽当前的状态（测试与解说用）。
  [[nodiscard]] SlotState state_of(std::uint16_t slot) const noexcept {
    return slot < max_own_tasks ? slots_[slot].state : SlotState::free;
  }

  /// 某个槽当前的世代号（测试用：release 之后应该 +1）。
  [[nodiscard]] std::uint32_t generation_of(std::uint16_t slot) const noexcept {
    return slot < max_own_tasks ? slots_[slot].generation : 0U;
  }

  /// 槽位下标的上界（日志与断言用）。
  static constexpr std::uint16_t invalid_slot = 0xFFFFU;

 private:
  struct Slot {
    std::uint32_t generation = 0U;  ///< 该槽第几代任务（release 时 +1）
    Task* task = nullptr;           ///< 槽里当前的 Task（未分配时 nullptr）
    SlotState state = SlotState::free;
  };

  static_assert(alignof(Task) <= platform::StaticPool::granule_bytes,
                "槽的对齐要求超过静态池的粒度（16 字节）");

  /// 平台的任务入口：先跑调用方的入口，再记 finished，然后永久停下。
  ///
  /// 故意不写 noexcept：KernelEntry 本身就是非 noexcept 的函数指针类型，而单测的 FakeKernel
  /// 用"suspend_self() 抛异常"把控制权交回测试进程（真机与宿主都不抛，异常也编不进来）。
  static void finish_and_park(void* storage) {
    Task& task = *static_cast<Task*>(storage);
    PoolTask& header = Kernel::header_of(task);
    auto* const self = static_cast<PooledTaskSpawner*>(header.spawner);
    if (self == nullptr) {
      for (;;) {
        Kernel::suspend_self();  // 防御：登记项没写好也绝不让任务入口返回
      }
    }
    if (header.entry != nullptr) {
      header.entry(header.argument);  // ← 调用方的任务体；**可以返回** = 任务结束
    }
    // 入口返回之后才记账：finished 的语义严格是"入口已经返回"（持有者据此判断能不能删）。
    self->mark_finished(header.slot);
    for (;;) {
      Kernel::suspend_self();  // 永不返回；万一返回了（不该）也继续停在这
    }
  }

  void mark_finished(std::uint16_t slot) noexcept {
    if (slot >= max_own_tasks) {
      return;
    }
    slots_[slot].state = SlotState::finished;
  }

  [[nodiscard]] std::uint16_t find_free_slot() const noexcept {
    for (std::uint16_t index = 0U; index < max_own_tasks; ++index) {
      if (slots_[index].state == SlotState::free) {
        return index;
      }
    }
    return invalid_slot;
  }

  [[nodiscard]] std::size_t count(SlotState wanted) const noexcept {
    std::size_t total = 0U;
    for (const Slot& book : slots_) {
      if (book.state == wanted) {
        ++total;
      }
    }
    return total;
  }

  alignas(16) std::uint8_t arena_[arena_bytes()];
  platform::StaticPool pool_{arena_, sizeof(arena_)};
  Slot slots_[max_own_tasks] = {};
};

}  // namespace embark

#endif /* EMBARK_POOLED_TASK_SPAWNER_H */