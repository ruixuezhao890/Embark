# 15 · 运行时任务生命周期：任务池化（创建 / 回收）

Status: resolved
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

## Answer

### 交付物

| 层 | 文件 | 内容 |
| --- | --- | --- |
| 接口 | `include/embark/task_spawner.h` | `TaskToken{slot, generation}`；`spawn_task` 返回 `expected<TaskToken, Error>`；新增 `release_task(TaskToken)`（只给持有者用） |
| 平台无关实现 | `platform/common/pooled_task_spawner.h` | `PooledTaskSpawner<Kernel>`：`alignas(16)` arena + `StaticPool` + `max_own_tasks` 个槽（`free/running/finished` + 世代号），trampoline `finish_and_park`（入口返回 → 标 finished → 永久 park） |
| 宿主 Kernel | `platform/host/own_task_spawner.h` | `HostTask{header, StaticTask_t, StackType_t stack[own_task_stack_words]}` + `HostTaskKernel`；`using HostTaskSpawner = PooledTaskSpawner<HostTaskKernel>` |
| 真机 Kernel | `platform/esp32/src/esp32_task_spawner.h` | `Esp32Task` + `Esp32TaskKernel`（`xTaskCreateStaticPinnedToCore`，栈深按字节 × `sizeof(StackType_t)`）；`Esp32TaskSpawner` |
| 框架 | `src/embark/framework.cpp` / `include/embark/framework.h` | `spawn_own_task(AppId)`、`reap_finished_own_tasks()`、`own_tasks_spawned()/own_tasks_released()`；`step()` 每帧回收；任务入口返回 = 合法结束（`onBackgroundTick` 跑完即 return） |
| 常量 | `config/embark_limits.h` | `max_own_tasks = 2`（槽数）、`own_task_stack_words = 512`（`StackType_t` 字口径，注释写明两种平台差 8 倍） |
| 单测 | `tests/kernel/test_pooled_task_spawner.cpp` | FakeKernel 版池契约：6 用例 / 95 断言 |
| 系统用例 | `tests/kernel/test_own_task_lifecycle.cpp` | 假后端系统用例（创建 → 跑完 → 回收 → 再创建）+ 两条边界：3 用例 / 74 断言 |
| 系统用例（窗口版） | `platform/host/ui_tour.cpp` + `app/demo_apps.{h,cpp}` | 第 9 步改为「一次性 own_task：跑完 → 回收 → 再创建」，新增 `JobApp`（`period_ms = 0`），自检清单 8 → 10 项 |
| 文档 | `spec.md` §6/§10/§13/§15、`docs/adr/0005-*.md`、`docs/messages-and-background.md`、`docs/hal-backend-guide.md`、`docs/common-pitfalls.md`、`README.md`、`docs/README.md` | 生命周期语义、Kernel 契约、栈深口径、数字更新 |

### 实测（本机 2026-10-04）

- 构建：`cmake --build build` 零警告（`BUILD_EXIT=0`）。
- 单元测试：**95 用例 / 835 断言全绿**（原 92 / 761；本轮 +3 用例 +74 断言）。
- `ctest --test-dir build --output-on-failure`：`100% tests passed, 0 tests failed out of 1`。
- 系统用例程序 `embark_host_tour.exe`：退出码 0，**10/10 项 `[通过]`**，含新增两项 ——
  `[通过] 一次性任务跑完并回收（job 轮数 / 创建 / 回收）：实测 2（期望 2）`、
  `[通过] 常驻任务之外的槽位全部空闲（finished 残留 = 0）：实测 1（期望 1）`。
  收尾统计：`任务生命周期：job 跑完 2 轮；框架累计创建 3 / 回收 2；池 running 1 / finished 0 / 空闲槽 1`。
- 回归：`embark_host_ui.exe --frames 150 --click --switch --own-task` 退出码 0（ticker 发送 25 / UI 收到 25，收件箱溢出 0，UI 任务栈余量 2045 字）。
- 格式：`clang-format --dry-run --Werror` 对全部改动/新增 C++ 文件零不符。
- 真机目标：`cmake --build build-esp32` 退出码 0（见 `evidence/15-task-lifecycle/`）。
- 证据：`.scratch/embark-v1/evidence/15-task-lifecycle/`（`tour-host-run.log`、`host-ui-regression.log`、`own-task-lifecycle-case.txt`、`unit-tests.txt`、`esp32-build.txt`）。

### 关键设计点

1. **park 表达"任务真的已结束"**：入口返回后 trampoline 标 `finished`，然后 `vTaskSuspend(nullptr)` 永久
   挂起。挂起的任务永不被选中，所以 `is_parked` 一旦为真就稳定为真 —— 不用 `eTaskGetState() == eDeleted`
   判断（它既覆盖"在终止链表上"也覆盖"已摘链"，无法区分存储能不能复用）。
2. **回收只能由持有者做**：持有者 = 唯一 UI 任务。任务自删会走 FreeRTOS 的"删除中"分支
   （`xTasksWaitingTermination` + `xDeleteTCBInIdleTask`，静态 TCB 要等空闲任务才真正释放），此时复用
   存储会踩 `xStateListItem` 的致命别名；持有者删一个 park 着的任务走同步路径，删完立刻可复用。
3. **真机跨核交接窗口**：IDF 的 `eTaskGetState` 多核下没有 self→`eRunning` 捷径，
   `prvSelectHighestPriorityTask` 先清旧 `xTaskRunState` 后发布新当前任务 —— 于是 `is_parked` 补第二条判据
   `xTaskGetCurrentTaskHandleForCore(own_task_core) != handle`，命中窗口时 `release_task` 返回 `busy`，下一帧重试。
4. **失败点仍然唯一且确定**：参数非法 → `invalid_argument`；栈深超槽 / 无空闲槽 / 池满 → `no_space`；
   该 App 已有任务或内核没停稳 → `busy`。建任务失败时槽位与池记账原样回滚（单测钉住）。
5. **三条不变量全部保持**：全静态（`xTaskCreateStatic*`）、失败点唯一（`no_space`）、容量是可查常量
   （`config/embark_limits.h`）。`configSUPPORT_DYNAMIC_ALLOCATION` 仍为 0、`heap_4.c` 仍不参与编译，
   零分配审计用例（`tests/kernel/test_zero_alloc.cpp`）全绿。

### 备注

- 接口与最初的范围草案有一处收窄：**没有**把容量口径改成"池容量 + 块尺寸档位"。当前实现是
  "每槽一个 `Task`（登记项 + TCB + 定长栈）+ `StaticPool` 管槽位"，`max_own_tasks` 与
  `own_task_stack_words` 两个常量原样保留 —— 槽尺寸仍是一刀切，但回收语义已经落地，
  后续要按档位分槽时只需改 Kernel 的 `slot_bytes()`/`make_task()`。
- 没有做 `ITaskSpawner::reap_finished()` 这类"让池自己扫"的接口："入口返回"只有平台 trampoline 看得见，
  所以由框架的 `step()` 每帧对已知 token 逐个 `release_task`，语义更窄也更好测。
- WiFi 能力面本身仍不在 v1 范围内（见 spec §15）：本 issue 只补机制，第一个真实用例（WiFi 非阻塞连接）
  所需的"运行期创建 + 完成即回收"已具备。
- 与 issue 08 的关系：policy `own_task` 从"常驻任务"变成"有生命周期的任务"，
  `TickerApp` 仍是常驻写法（`period_ms = 50`），新加的 `JobApp` 演示一次性写法（`period_ms = 0`）。
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
