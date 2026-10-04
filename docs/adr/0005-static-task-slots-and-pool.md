# 任务创建：静态槽位为默认，运行期增删走固定块池

本仓库所有任务都用 `xTaskCreateStatic*` 创建，栈与 TCB 来自编译期定死的存储：内置的 UI 任务用
`platform/host/ui_task.cpp` / `platform/esp32/src/esp32_ui_task.cpp` 里的静态数组，App 申请的后台任务
走注入接口 `ITaskSpawner`，实现（宿主 `platform/host/own_task_spawner.h`、真机
`platform/esp32/src/esp32_task_spawner.h`）用 `max_own_tasks × own_task_stack_words` 的静态槽。
宿主 FreeRTOS 配 `configSUPPORT_STATIC_ALLOCATION 1` + `configSUPPORT_DYNAMIC_ALLOCATION 0`，并且
`heap_4.c` 刻意不参与编译（`cmake/embark_freertos.cmake`）—— 于是 `pvPortMalloc/vPortFree` 在**符号层面
就不存在**，"内核与 App 零动态分配"是结构保证而不是纪律。

这样定的理由：静态把"内存够不够"从运行期问题挪成链接期问题 —— 账目能在 map 文件 / 真机
`check_sizes.py` 里核对，没有碎片，没有堆锁，唯一的失败点是 boot 时的容量不足（返回
`Error::no_space`，容量是 `config/embark_limits.h` 里可调的常量）。要注意 FreeRTOS 的"动态"
（heap_4/heap_5）本身也是编译期定死的静态数组 `ucHeap[configTOTAL_HEAP_SIZE]`，动态并不省掉预留，
只是把切分推到运行期，并引入碎片与分配期间的 `vTaskSuspendAll()`。代价是预留即占用
（`max_own_tasks = 2 × own_task_stack_words = 512` 字 = 4 KB）与尺寸一刀切（App 声明的栈深超过全局槽深
直接 `no_space`），这些代价在"任务集合编译期已知"的前提下成立。

**升级路径已定**：将来需要"运行期创建、完成即回收"的任务时（第一个真实用例：WiFi 非阻塞连接 ——
发起连接时创建任务，连接成功或超时后任务自行结束并归还资源，UI 线程全程不阻塞），把 `ITaskSpawner`
的实现从"二维静态槽"换成"固定块池 + 空闲链"：池直接复用 `platform/common/static_pool.h` 的
`StaticPool`（真机 LVGL 已在用它，`.bss` 里 48 KB arena），仍然调 `xTaskCreateStatic`，接口增加
`release_task`，并且**回收必须能确认"任务真的已结束"**（内核不会通知静态任务被删完；动态版本由空闲任务
回收 TCB/栈），所以要么配删除/空闲钩子，要么规定只在 boot 期分配。池化并不去掉失败路径，只是把它从
"不确定的碎片 + 不确定的失败点"变成"确定的容量上限 + 确定的失败点（池满 = `no_space`）"。
工作项与验收草案见 `.scratch/embark-v1/issues/15-runtime-task-lifecycle.md`。
