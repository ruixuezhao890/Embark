/**
 * Embark · 后台任务能力（issues/07、15）
 *
 * spec §6 的 own_task 逃生舱：内核不 include FreeRTOS，任务创建是平台能力。
 * 平台把"静态创建任务"包装成 ITaskSpawner 交给 Framework（宿主实现
 * HostTaskSpawner / 真机 Esp32TaskSpawner，都是 platform/common/pooled_task_spawner.h
 * 里那块（平台无关的）定容任务池 + 各自的 Kernel）。
 *
 * 纪律：
 *   - 任务入口**可以返回**（issue 15 起）。入口返回 = 任务结束，平台把它 park 在槽里
 *     等持有者回收 —— 不会再走 fatal，也不会自行删自己（见 TaskToken 与 release_task）。
 *   - 回收是**持有者的事**（唯一 UI 任务）：只有它知道自己的任务跑完没、什么时候该把
 *     槽位还给池。任务自己回收自己会踩 FreeRTOS 的"删除中"中间态（TCB 还在终止链表上，
 *     此时复用静态存储会写出致命的别名）。
 *   - 容量与失败点是确定的：池满 / 栈深超过槽容量 → Error::no_space，参数非法 →
 *     invalid_argument。没有 malloc，"够不够"在配置常量里查得到。
 */
#ifndef EMBARK_TASK_SPAWNER_H
#define EMBARK_TASK_SPAWNER_H

#include <cstdint>

#include <embark/error.h>
#include <middleware/etl/expected.h>
#include <middleware/efmt/core/format.hpp>

namespace embark {

/// 任务句柄：池内槽位下标 + 世代号。
///
/// 世代号让"释放一个已经被释放过的句柄"变成可判定的错误：槽位被回收时会 ++generation，
/// 于是旧句柄（slot 相同、generation 落后）在 release_task 里稳定地拿到 not_found，
/// 而不会误杀刚刚复用这个槽位的新任务。
E_FMT_DERIVE(struct TaskToken {
  std::uint16_t slot = 0xFFFFU;   ///< 槽位下标；0xFFFF = 无效
  std::uint32_t generation = 0U;  ///< 该槽位的第几代任务（从 0 开始）
});

/// 平台的任务创建/回收能力（注入 Framework 的第 4 参；可空 = 不支持 own_task）。
class ITaskSpawner {
 public:
  ITaskSpawner() noexcept = default;
  virtual ~ITaskSpawner() = default;

  ITaskSpawner(const ITaskSpawner&) = delete;
  ITaskSpawner& operator=(const ITaskSpawner&) = delete;

  /// 创建一个后台任务。
  ///   name        任务名（≤ configMAX_TASK_NAME_LEN-1，平台自行截断/校验）
  ///   entry       任务函数；**可以返回**（返回 = 结束，之后等 release_task 回收）
  ///   argument    传给 entry 的指针（生命周期归调用方 —— 到 release_task 为止）
  ///   stack_words 栈深（StackType_t 字；0 非法，框架会先填默认值）
  ///   priority    优先级（平台语义；uint16_t 见 message.h 的说明）
  /// 返回句柄（回收时原样交回），或失败：invalid_argument（参数）/ no_space（池满、
  /// 栈深超过槽容量）/ busy（平台暂时给不出，稍后重试）。
  [[nodiscard]] virtual etl::expected<TaskToken, Error> spawn_task(
      const char* name, void (*entry)(void*) noexcept, void* argument, std::uint16_t stack_words,
      std::uint16_t priority) noexcept = 0;

  /// 回收一个**已经结束**的任务（entry 已返回），并把槽位还给池。
  ///   entry 还没返回（任务在跑）      → busy（v1 不做强制终止；持有者稍后重试）
  ///   句柄无效 / 释放过 / 槽位已换人  → not_found
  ///   内核还没把 TCB 摘干净           → busy（真机跨核的罕见中间态，下一帧重试即好）
  /// 成功后该槽位立即可被下一次 spawn_task 复用；重复释放同一个句柄是安全的
  /// （第二次必然 not_found）。
  [[nodiscard]] virtual Error release_task(TaskToken token) noexcept = 0;
};

}  // namespace embark

#endif /* EMBARK_TASK_SPAWNER_H */
