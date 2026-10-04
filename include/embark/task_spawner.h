/**
 * Embark · 后台任务创建能力（issues/07）
 *
 * spec §6 的 own_task 逃生舱：内核不 include FreeRTOS，任务创建是平台能力。
 * 平台把"静态创建任务"包装成 ITaskSpawner 交给 Framework（宿主实现
 * HostTaskSpawner 用 xTaskCreateStatic + BSS 上的 TCB/栈，零堆）。
 *
 * 纪律：spawn_task 只负责"创建"，不负责"停止"。v1 的 own task 是常驻
 * 任务（for(;;)），与 App 一样没有退役路径；优雅停止留给后续 issue。
 */
#ifndef EMBARK_TASK_SPAWNER_H
#define EMBARK_TASK_SPAWNER_H

#include <cstdint>

#include <embark/error.h>

namespace embark {

/// 平台的任务创建能力（注入 Framework 的第 4 参；可空 = 不支持 own_task）。
class ITaskSpawner {
 public:
  ITaskSpawner() noexcept = default;
  virtual ~ITaskSpawner() = default;

  ITaskSpawner(const ITaskSpawner&) = delete;
  ITaskSpawner& operator=(const ITaskSpawner&) = delete;

  /// 创建一个后台任务。
  ///   name        任务名（≤ configMAX_TASK_NAME_LEN-1，平台自行截断/校验）
  ///   entry       任务函数，合同与 UI 任务相同：不许返回
  ///   argument    传给 entry 的指针（生命周期归调用方）
  ///   stack_words 栈深（平台单位字；0 非法，框架会先填默认值）
  ///   priority    优先级（平台语义，下界留给平台；uint16_t 见 message.h 的说明）
  /// 失败返回 busy（槽位耗尽）/ invalid_argument / no_space（静态存储不足）。
  [[nodiscard]] virtual Error spawn_task(const char* name, void (*entry)(void*) noexcept,
                                         void* argument, std::uint16_t stack_words,
                                         std::uint16_t priority) noexcept = 0;
};

}  // namespace embark

#endif /* EMBARK_TASK_SPAWNER_H */