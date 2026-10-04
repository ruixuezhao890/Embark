# 07 · 消息设施与后台节拍

Status: resolved
Type: task
Blocked by: 06

## 目标

把 spec §7 落成代码：总线、消息队列、后台节拍（**A 方案**：框架定时器 + `onBackgroundTick`）、日志串行化、`OwnTask` 逃生舱。

## 范围

- `embark::Bus`：基于 `etl::message_bus<MAX_ROUTERS>`，自己实现 `publish()`（ETL 只给了 `subscribe`，见 spec §16.5），遍历订阅者调 `receive()`；覆写 `on_receive_unknown` → WARN + 计数（ETL 默认静默丢弃）。
- `embark::MessageQueue<T, SIZE>`：存储用 `etl::circular_buffer`（**原生满时覆盖最旧**）；`push` 前查 `full()` → 溢出计数 + 一条 WARN；跨任务加 `etl::mutex` + `etl::lock_guard`（单生产者单消费者）；**任何路径都不阻塞 UI 任务**。
- 后台节拍：`etl::callback_timer<MAX_TIMERS>`（宏按 spec §16.2 给），按 per-App 周期在 UI 任务里调 `onBackgroundTick(now_ms)`；`Suspend` / `Tick(period_ms)` / `OwnTask` 三种策略生效。
- `OwnTask`：框架创建任务（栈深/优先级由 App 配置）、任务名唯一、启动时机在 `onCreate` 之后、消息经队列进出。
- 日志：elog sink 外挂串行化（真机 `etl::mutex`、宿主按配置不加锁）；中断上下文不打日志（v1 不给中断日志缓冲）。

## 验收

- 溢出用例：队列满时丢**最旧**、计数 +1、有 WARN 日志（验证 ETL 的覆盖语义真的生效）。
- 跨任务用例：单生产者单消费者下不丢、不乱序。
- 后台 tick 周期可观测（日志/计数），`period_ms == 0` 等于完全不跑。
- `OwnTask` App 发来的消息能回到 UI 任务派发，且派发期间不阻塞。

## 备注

`etl::mutex` / `etl::atomic` 的宏开关与后端选择属"待实测"项（spec §7），本 issue 里边做边确认，结论回写 spec。

## Answer

**结论：按 spec §7 + 落地实况全部落地，71 个测试用例（含本 issue 新增 3 文件 19 用例）全绿，宿主演示 EXIT=0。** 与本 issue 目标逐项对照：

- **Bus**：`include/embark/bus.h` + `src/embark/bus.cpp`。`embark::Bus : etl::message_bus<max_bus_subscribers>`（ETL 只提供 subscribe/unsubscribe/receive）。自实现 `publish()`：镜像订阅表（基类 `router_list` 是私有）按 `accepts(id)` 计数，0 个订阅者 → 丢弃 + `unknown_` 计数 + 一条 ELOG_WARN（覆盖 ETL「静默丢弃」）；≥1 个 → 基类 `receive()` 同步广播。ETL 的 `subscribe` 按 router id 排序插入且**不查重**（重复订阅重复插）→ Bus 自持 `etl::vector` 镜像表先查重（重复 → false）。观测：`published()/unknown()/subscriber_count()/reset_counters()`。对应「覆写 on_receive_unknown → WARN + 计数」验收：无人订阅时 unknown=1 且一条 WARN。
- **MessageQueue**：`include/embark/message_queue.h`（header-only）。`etl::circular_buffer<T, MaxSize>` 原生「满时覆盖最旧」（容量 = MaxSize，+1 空槽是内部实现）；`push` 前 `full()` → `overflows_` 计数 + 一条 WARN → 覆盖最旧；跨任务默认 `etl::mutex` + `etl::lock_guard`（单生产者单消费者）；`pop` 空返回 false。**任何路径不阻塞 UI 任务**。验收（覆盖语义）：3 槽 push 1..5 → 幸存 3,4,5（1、2 被覆盖），overflows=2，FIFO 顺序保持。
- **后台节拍**：`etl::callback_timer<max_background_timers>`（宏 `ETL_CALLBACK_TIMER_USE_ATOMIC_LOCK` 已定义，config 无需再改）。boot 时对 `BackgroundPolicy::tick` 且 `period_ms > 0` 的 App 注册定时器：`period_ticks = max(1, (period_ms + ui_loop_period_ms - 1) / ui_loop_period_ms)`（向上取整，周期精度 = UI 循环周期，spec 注明）；**`timers_.enable(true)` 必须先调**（构造默认 enabled=false，否则 tick() 直接返回）；UI 任务每帧喂 `timers_.tick(1)` → 到期回调 trampoline（`etl::ifunction<void>`，operator() **const**）→ `fire_background_tick(app_id)` → `App::onBackgroundTick(now_ms)`。`period_ms == 0` 不注册（等价 suspend）。tick 策略 App 与 own_task 策略 App 的 `onBackgroundTick` 都在此路径（own_task 在自己任务里）。
- **OwnTask 逃生舱**：框架不 include FreeRTOS（内核零 OS 依赖）→ 注入接口 `ITaskSpawner::spawn_task(name, entry, argument, stack_words, priority)`；宿主实现 `HostTaskSpawner`（BSS 静态槽 `max_own_tasks=2` × TCB + `own_task_stack_words=512` 字栈；App 配置栈深 > 槽 → `no_space`；任务名唯一由 App name 保证；boot 时机 = 全部 onCreate → 前台 onEnter → spawn）。任务体 `while(true){ delay_ms(period); onBackgroundTick(now_ms); }` 永不返回；v1 不做优雅停止（宿主 `_Exit` 兜底，真机策略留给 issue 11）。
- **跨任务消息回派**：own task 用 `Framework::post(CrossTaskMessage)`（入 `MessageQueue<CrossTaskMessage, message_queue_depth> inbox_`，带锁）；UI 任务在 step 的消息段 `while (inbox_.pop(e)) bus_.publish(e)` 抽干回派 → `App::onMessage`。信封 = `cross_task_message_id=0xFE` + `from_app` + `seq`（定长可平凡拷贝；v1 跨任务只此一型）。
- **演示验收**（`embark_host_ui --frames 150 --click --switch --own-task --screenshot`，EXIT=0）：ticker（own_task，周期 50 ms）后台发送 **23 条 / UI 收到 23 条**，seq 1..23 递增、不丢不乱序（now 538→1916 ms）；点击 1 次；切换 2 次（counter→switch→counter，counter enter 1/resume 1、switch enter 1/resume 0 —— onEnter 只在第一次）；总线发布 23 / 无人接收 0；收件箱溢出 0；UI 任务栈余量 2045 字；LVGL 堆峰值 12846 字节（预算 262144）。`--quit-at 30` 关窗收尾 EXIT=0；`--help` 11 行选项完整。
- **测试**：`tests/kernel/test_bus.cpp`（publish→订阅者收到 / 无人接收 unknown+WARN / unsubscribe 后不再收 / 重复订阅返回 false / 不同 accepts 过滤）、`test_message_queue.cpp`（FIFO 顺序 / 满时覆盖最旧+溢出计数 / 移动入队 / etl::mutex 默认实例化）、`test_framework_messaging.cpp`（publish 广播 / post 下帧派发 / 收件箱满溢出计数 / tick 周期 8 帧 1 次 16 帧 2 次 / period 0 = suspend / own_task spawn 参数 / 无 spawner → unsupported、spawn 失败 → no_space）。tests 链接 `embark_freertos`（fakes.cpp 提供 assert_failed/fatal 满足桥符号；winmm 由 PUBLIC 传递）。
- **ETL 事实补充**（回写 spec §7 落地实况）：circular_buffer 容量 = MaxSize（raw 缓冲 +1 槽是 in==out 区分空满的内部实现）；callback_timer 构造 enabled=false；message_bus 同 id 多订阅者按 router id 升序派发（订阅即排序）；mutex（FreeRTOS 分支）静态信号量、单线程快路径不挂。

**改动文件**：`include/embark/{bus.h,message_queue.h,task_spawner.h}`（新）、`message.h`（AppId/invalid_app_id 移入 + CrossTaskMessage）、`app.h`（删重复 AppId）、`framework.h/.cpp`（v2：4 参构造 + AppAdapter + Bus + 后台节拍 + own task + publish/post/bus()/inbox_overflows()）、`config/embark_limits.h`（max_bus_subscribers=8/max_own_tasks=2/own_task_stack_words=512）、`platform/host/{own_task_spawner.h 新, demo_apps.h/.cpp TickerApp, ui_demo.cpp --own-task}`、`src/CMakeLists.txt`（+bus.cpp）、`tests/CMakeLists.txt`（+3 源 + 链 embark_freertos）、`tests/kernel/test_{bus,message_queue,framework_messaging}.cpp`（新）、spec.md §7 落地实况。

**已知限制（记入后续）**：v1 跨任务只走 CrossTaskMessage 一个信封；App 收全部消息、自分发（声明式订阅后续版本）；own task 无优雅停止（issue 09/11）；tick 周期精度 = UI 循环周期。
