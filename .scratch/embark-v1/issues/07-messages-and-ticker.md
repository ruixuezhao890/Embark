# 07 · 消息设施与后台节拍

Status: pending
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
