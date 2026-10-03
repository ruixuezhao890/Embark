# 06 · 内核：App 契约 + 唯一 UI 任务 + 前后台切换

Status: resolved
Type: task
Blocked by: 02, 04, 05

## 目标

实现 spec §5 + §6 的核心：App 契约、编译期注册表、唯一 UI 任务、前后台切换。

## 范围

- `embark::App` 抽象类：7 个钩子（`onCreate` / `onEnter` / `onPause` / `onResume` / `onBackgroundTick` / `onMessage` / `onExit`），签名按 spec §5，钩子集合不再增加。
- 编译期静态注册（零堆）：`embark::app_registry<Apps...>()` 生成编译期注册表，**注册顺序即默认前台 App**；`EMBARK_APP_TABLE(...)` 在每个可执行文件里声明一次全局注册表（`embark_apps()`）。注册表是静态数组，零堆。
- `embark::Framework`：`request_switch(id)` / `request_switch(name)`（snake_case，仅登记），前台切换（输入焦点 + 渲染权 + 事件循环权一起转）在 `step()` 循环边界生效，`onPause`/`onEnter`（首次）/`onResume`（再次）通知；**切换决策与时机只由框架执行**。
- UI 任务：FreeRTOS 任务（宿主走 issue 02 的 MSVC-MingW port，静态分配 TCB + 栈），5 ms 一跳，空闲 `vTaskDelay` 让出；**全工程唯一允许 `lv_timer_handler()`（在 `LvglUiPort::process()`）与操作 LVGL 的地方**。
- App 的 `OwnTask` 策略留出接口（`AppSettings::background == own_task`，实现在 issue 07）。
- `IuiPort` 抽象：UI 端口（LVGL/渲染/输入泵）与任务循环解耦，宿主 = `LvglUiPort`，测试 = `FakeUiPort`。

## 验收

- 宿主能建 UI 任务并稳定跑事件循环（不忙等）✓（`--frames 150` 跑 150 帧正常退出，`--quit-at 30` 关窗干净退出，exit 0）。
- 两个 App 能互相切前台，钩子按 `onPause` → `onEnter`/`onResume` 顺序正确触发 ✓（`--switch`：counter enter 1 / resume 1，switch enter 1 / resume 0，切换 2 次，exit 0；单测 8 个 TEST_CASE 全过）。
- 注册顺序决定默认前台 ✓（counter 排第 0 = 默认前台；`AppRegistry` 单测）。
- 分配 hook 统计：这条路径 0 次动态分配 ✓（FreeRTOS `configSUPPORT_DYNAMIC_ALLOCATION=0` + `heap_4.c` 不编译 —— 结构性保证；UI 任务 TCB/栈为 BSS 静态数组）。
- 零警告构建；ctest 1/1 通过。

## 实现摘要

### 新文件

- `include/embark/message.h` —— `Message = etl::imessage` / `MessageId` / `MessageT<Id>` 别名。
- `include/embark/app.h` —— `AppId` / `invalid_app_id` / `BackgroundPolicy` / `AppSettings` / `App`（七个钩子 + `settings()` 默认实现）。
- `include/embark/app_registry.h` —— `AppRegistry`（编译期表，`at/find/valid/id_of`）+ `detail::app_instance<T>()`（进程级静态实例）+ `app_registry<Apps...>()` + `EMBARK_APP_TABLE(...)` 宏。
- `include/embark/ui_port.h` —— `IUiPort`：`init/tick/pump_input/process/exit_requested/shutdown`，全部方法只被唯一 UI 任务调用。
- `include/embark/framework.h` / `src/embark/framework.cpp` —— `Framework`：`boot/step/request_switch/shutdown` + 查询族。`step` 序 = UI tick → pump_input → 循环边界 `apply_pending_switch` → process。
- `config/embark_limits.h` 新增 UI 任务小节：`ui_task_stack_words=2048` / `ui_task_priority=5` / `ui_loop_period_ms=5`。
- `platform/host/ui_task.{h,cpp}` —— `start_ui_task` / `start_scheduler` / `ui_loop_delay` / `exit_process`（`std::_Exit`，避免 `exit()` 在宿主模拟中断循环里 join 挂死）。
- `platform/host/lvgl_ui_port.{h,cpp}` —— `LvglUiPort`：init 校验 HAL 就绪 → `LvglPort::init`；`process` = `lv_timer_handler`（唯一调用点）；shutdown 报 LVGL 未回收字节。
- `platform/host/demo_apps.{h,cpp}` —— `CounterApp` / `SwitchApp` 演示：按钮回调里 `fw_->request_switch("switch"/"counter")`，两个按钮同在 (160,170) 中心 → 截图证明输入焦点真的随前台切换。
- `platform/host/ui_demo.cpp` —— 重写为任务入口：`EMBARK_APP_TABLE(CounterApp, SwitchApp)` + `ui_main`（HAL → `LvglUiPort` → `Framework::boot` → 循环 `step` + 合成点击脚本 + 截屏 + 统计），验收退出码 0/1/2（2 = 点击或切换断言失败）。
- `platform/host/freertos/FreeRTOSConfig.h` —— 宿主正式配置：1000 Hz、`configSUPPORT_STATIC_ALLOCATION=1` / `DYNAMIC=0`、`configUSE_TIMERS=0`、`configASSERT` → `embark_freertos_assert_failed`、栈溢出钩子 → `embark_freertos_stack_overflow`。
- `platform/host/freertos/freertos_hooks.c` —— `vApplicationGetIdleTaskMemory`/`GetTimerTaskMemory`（静态 TCB+栈）+ 栈溢出/alloc 失败钩子。
- `platform/host/freertos/freertos_bridge.cpp` —— 把 FreeRTOS 的 C 诊断转成 `embark::assert_failed` / `embark::fatal`。
- `cmake/embark_freertos.cmake` —— `embark_freertos` 静态库：`tasks/queue/list/event_groups/stream_buffer.c` + `MSVC-MingW/port.c` + 钩子 + 桥；**不编** `heap_4.c`（动态分配 = 0 的结构保证）/`timers.c`/`croutine.c`；链 `winmm`；PROJECT 顶层 `EMBARK_PLATFORM_HAS_FREERTOS` 默认翻 ON。
- `tests/fakes/fake_ui_port.h`、`tests/kernel/test_app_registry.cpp`、`tests/kernel/test_framework.cpp` —— 见验收。

### 关键决策（含踩坑）

- **桥进内核归档**：静态归档链接按成员抽取，`embark_freertos`（tasks.o 引用桥符号）放平台归档之前会导致循环 → 桥编译进 `embark_freertos.a`，归档内部成员互相满足引用。
- **embark_freertos 不链 build_options**：`-include config/embark_config.h` 是 C++ 强制头（ETL static_assert / namespace），灌给内核 C 文件直接炸 → 桥的 C++ 侧只需 ETL include 根 + 工程 include，编译选项按语言单列（`$<$<COMPILE_LANGUAGE:CXX>:...>`）。
- **`request_switch(0)` 字面量歧义**：`0` 同时匹配 `AppId` 与 `const char*`（空指针常量）→ 测试统一走 `switch_to_ok(fw, AppId)` 绕一层。
- **静态实例跨测试悬垂**：`app_registry` 的 App 实例是进程级 static，`record_` 指针跨 doctest 用例存活 → 每个用例必须 `bind_records`，漏绑 = 上一用例已析构的 vector 被写入 → 堆破坏段错误（case 7 踩过）。
- **UI 任务收尾必须 `std::_Exit`**：主线程卡 `vTaskStartScheduler` 的模拟中断循环，`exit()` join 会挂死（issue 02 实测）；FreeRTOS 的 TCB/栈都是 BSS 静态数组，无需析构。
- **LVGL 关屏语义**：每个 App `onCreate` 建游离 screen 对象（`lv_obj_create(nullptr)`），`onEnter/onResume` 里 `lv_scr_load` —— 框架不碰 App 的 LVGL 对象（spec §5「UI 生命周期自决」）；`LV_MEM_CUSTOM=1` 下 8.3.11 无 `lv_deinit`，收尾只报未回收字节。

## 备注

后台 tick 的驱动（A 方案：`etl::callback_timer`）在 issue 07，本 issue 先只定义钩子被框架调用（`AppSettings::background` 目前默认 suspend，`onBackgroundTick` 还没有调用点 —— issue 07 在 `Framework::step` 里插入调度）。
