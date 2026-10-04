# 15 · 运行时任务生命周期：任务池化（创建 / 回收）

Status: open
Type: feature
Blocked by: 14
来源: 用户指示（"先记下吧，因为未来可能会出现运行时创建任务的情况，例如wifi非阻塞链接是吧，链接时创建任务链接wifi，完成后删除任务，你先记下吧。"）

## 目标

v1 的任务集合在编译期定死：`ITaskSpawner` 只有"创建"没有"停止"（纪律行写在
`include/embark/task_spawner.h` 头部），宿主与真机的实现都是
`max_own_tasks × own_task_stack_words` 的静态槽，只增不减 —— 第 3 个 own_task 起直接
`no_space`。

本 issue 把"运行期创建、完成即回收"补上。第一个真实用例是 **WiFi 非阻塞连接**：
发起连接时创建一个任务（扫描 / 配网 / 握手的顺序阻塞代码写在任务里），连接成功或超时后
任务自行结束并归还资源，UI 任务全程不被阻塞。

## 范围（待设计，先记录意图）

- **接口**：`ITaskSpawner` 增加回收语义（如 `release_task(handle)`），并把"任务已真正结束"
  的确认机制定下来 —— 内核不会通知静态任务被删完（动态版本由空闲任务回收 TCB/栈），
  所以要么配删除钩子 / 空闲钩子，要么规定运行期只创建"短命任务"。
- **存储**：`HostTaskSpawner` / `Esp32TaskSpawner` 从"固定二维槽"改为"固定块池 + 空闲链"，
  复用 `platform/common/static_pool.h` 的 `StaticPool`（真机 LVGL 已在用，`.bss` 里 48 KB
  arena），仍然调 `xTaskCreateStatic*`；池化不引入 malloc，`configSUPPORT_DYNAMIC_ALLOCATION`
  保持 0、`heap_4.c` 仍不参与编译。
- **容量口径**：从 `max_own_tasks` / `own_task_stack_words` 两个常量改为"池容量 + 块尺寸
  （或块尺寸档位）"，保留现有的启动期校验（声明栈深超过块尺寸 → `no_space`）。
- **WiFi 能力面**：先做接口设计 —— `embark::hal` 是否要新增一个 net 能力面
  （连接 / 断开 / 事件 / 状态），以及"任务里跑阻塞式 WiFi API"与 UI 任务的关系
  （谁等锁、谁负责超时、事件怎么回到 UI）。
- **不变量**：无论哪一步失败，都不留半张 TCB / 半块栈（要么全拿要么全还）。

## 验收（草案）

- 池化 spawner 的单测：池满返回 `no_space`；归还后可复用；归还后 `free_block_count()` 恢复；
  失败路径无泄漏（统计口径与 `tests/kernel/test_static_pool.cpp` 一致）。
- 扩展系统用例（issue 14 的两个入口）：可观测"创建 → 跑完 → 回收 → 再创建"一轮循环。
- 零分配审计用例仍全绿（`tests/kernel/test_zero_alloc.cpp`）。
- 宿主与真机同款行为（同一个 `ITaskSpawner` 实现语义）；spec §10 与 ADR 0005 同步更新。

## 备注

- 记录时点 2026-10-04，来源是用户在设计问答（"为什么不用动态任务创建，静态任务创建相比的
  孰优孰劣，如果用动态任务创建是不是可以考虑用内存池？"）之后的一句指示；当时明确"现在不改
  v1"，只把升级路径与判据写进 spec §10 与 `docs/adr/0005-static-task-slots-and-pool.md`。
  静态 vs 动态的逐项对比（内存来源、账目、碎片、失败点、并发成本、槽位复用、审查友好度……）
  也在那份 ADR 里。
- 与 issue 08 的关系：policy `own_task` 目前是"常驻任务"，issue 15 之后才有真正的
  "任务生命周期"一说。
- 现在就有、池化后要继续保持的三条：全静态（`xTaskCreateStatic*`）、失败点唯一且确定
  （`no_space`）、容量是可查常量（`config/embark_limits.h`）。
