# 任务创建：静态槽位为默认，运行期增删走固定块池

本仓库所有任务都用 `xTaskCreateStatic*` 创建，栈与 TCB 来自编译期定死的存储：内置的 UI 任务用
`platform/host/ui_task.cpp` / `platform/esp32/src/esp32_ui_task.cpp` 里的静态数组，App 申请的后台任务
走注入接口 `ITaskSpawner`，实现（宿主 `platform/host/own_task_spawner.h`、真机
`platform/esp32/src/esp32_task_spawner.h`）各自定义一个 Kernel，套在平台无关的
`platform/common/pooled_task_spawner.h` 上：`.bss` 里一块 `alignas(16)` 的 arena，按
`max_own_tasks` 个槽切给 Kernel（每槽 = 登记项 + `StaticTask_t` + `own_task_stack_words` 字栈）。
宿主 FreeRTOS 配 `configSUPPORT_STATIC_ALLOCATION 1` + `configSUPPORT_DYNAMIC_ALLOCATION 0`，并且
`heap_4.c` 刻意不参与编译（`cmake/embark_freertos.cmake`）—— 于是 `pvPortMalloc/vPortFree` 在**符号层面
就不存在**，"内核与 App 零动态分配"是结构保证而不是纪律。

这样定的理由：静态把"内存够不够"从运行期问题挪成链接期问题 —— 账目能在 map 文件 / 真机
`check_sizes.py` 里核对，没有碎片，没有堆锁，唯一的失败点是 boot 时的容量不足（返回
`Error::no_space`，容量是 `config/embark_limits.h` 里可调的常量）。要注意 FreeRTOS 的"动态"
（heap_4/heap_5）本身也是编译期定死的静态数组 `ucHeap[configTOTAL_HEAP_SIZE]`，动态并不省掉预留，
只是把切分推到运行期，并引入碎片与分配期间的 `vTaskSuspendAll()`。代价是预留即占用
（默认 `max_own_tasks = 2` 个槽：一槽 = 登记项 + `StaticTask_t` + `own_task_stack_words` 字栈，
宿主实测 ≈ 4.2 KB、真机 ≈ 4.4 KB —— `*_stack_words` 按宿主字长算、真机用 `stack_word_bytes = 8`
换成同样的字节数，两个平台预留的是同一份栈字节数，见 `docs/reference/pitfalls.md`）与尺寸
一刀切（App 声明的栈深超过全局槽深直接 `no_space`），这些代价在"任务集合编译期已知"的前提下成立。

**升级路径已落地（2026-10-04，issue 15）**："运行期创建、完成即回收"（第一个真实用例：WiFi 非阻塞连接 ——
发起连接时创建任务，连接成功或超时后任务自行结束并归还资源，UI 线程全程不阻塞）按当初定的路线实现：
`ITaskSpawner` 的实现从"二维静态槽"换成"固定块池 + 空闲链"，池复用 `platform/common/static_pool.h` 的
`StaticPool`（真机 LVGL 已在用它，`.bss` 里 48 KB arena），仍然调 `xTaskCreateStatic*`，接口增加
`release_task(TaskToken)`（槽位下标 + 世代号，世代号让"重复释放"变成可判定的 `not_found`）。

三个实现要点：

1. **"任务真的已结束"用 park 表达**：任务入口返回后由平台 trampoline 标记 `finished`，然后
   `vTaskSuspend(nullptr)` 永久挂起自己。挂起的任务永远不会被调度器选中，所以 `is_parked` 一旦为真就
   稳定为真 —— 这比"等 TCB 摘干净"可靠：`eTaskGetState() == eDeleted` 既覆盖"在终止链表上"也覆盖
   "已摘链"，无法区分存储能不能复用（宿主 `tasks.c:1433`、IDF `:2585` 同一处歧义）。
2. **回收只能由持有者做**：全工程持有者 = 唯一 UI 任务（它也是唯一调 `spawn_task` 的人）。任务自己删
   自己会走 FreeRTOS 的"删除中"分支（`xTasksWaitingTermination` + `xDeleteTCBInIdleTask = pdTRUE`，
   静态 TCB 要等空闲任务才真正释放），此时复用静态存储会踩 `xStateListItem` 的致命别名。持有者删
   一个 park 着的任务则走同步路径（IDF `tasks.c:2324`、宿主 `:1192-1195`），删完立刻可复用。
3. **真机跨核的交接窗口**：IDF 的 `eTaskGetState` 在多核下没有"`pxTCB == pxCurrentTCB` ⇒ `eRunning`"
   这条捷径（只有 `configNUMBER_OF_CORES == 1` 才有），而 `prvSelectHighestPriorityTask` 会先清旧任务的
   `xTaskRunState`、后发布新的当前任务 —— 于是存在"已在挂起链表上、但仍是某核的当前任务"的一瞬。
   判据补第二条：`xTaskGetCurrentTaskHandleForCore(own_task_core) != handle`。命中这个窗口时
   `release_task` 返回 `busy`（持有者下一帧重试），绝不删。

池化并没有去掉失败路径，只是把它从"不确定的碎片 + 不确定的失败点"变成"确定的容量上限 + 确定的失败点
（池满 = `no_space`）"：入口参数非法 → `invalid_argument`，栈深超槽 / 没空槽 / 池满 → `no_space`，
任务还在跑或内核没停稳 → `busy`。实现与验收见 `.scratch/embark-v1/issues/15-runtime-task-lifecycle.md`。
