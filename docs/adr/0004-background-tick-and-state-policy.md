# 后台节拍实现定 A、状态不绑范式、ETL 版本先锁后升

后台节拍（`BackgroundPolicy::tick`）的实现定为 **A：框架在唯一 UI 任务里用 `etl::callback_timer` 按 per-App 周期调用 `onBackgroundTick(now_ms)`**；周期以 UI 循环周期（宿主 5 ms）为粒度向上取整，`period_ms == 0` 等价挂起。状态表达**不绑任何范式**：`etl::state_chart` 是 spec §16.4 推荐工具、demo 里示范过，但框架不强制、不内置，App 内部想用什么用什么。

这样定的理由：A 方案把后台节拍放进既有的单任务循环，零并发、零新分配、时序可推理，代价是周期粒度受 UI 循环限制、回调必须轻量——正好匹配"后台只跑轻量逻辑"的定位；重的、真并行的活由 `own_task` 策略承接（异构职责不塞进同一机制）。状态不与范式绑定，是因为"状态如何表达"是 App 内部实现细节，框架强行规定只会制造与 LVGL 对象树、资源生命周期打架的样板代码；给推荐、给例子、不给约束。

ETL 依赖版本是"先锁后升"：立项定稿时锁 **20.40.0**（spec §16.3 记过 A/B 版本差异），2026-10-03 用户拍板升级到 **20.49.0** 并已入库（submodule pin `7d604f2e4f7fa79ff49bf675c089656943f9171b`），期间核对过 `message_bus` 无 publish/`callback_timer` 锁宏二选一、`ifunction` 的 `operator()` 为 const 等差异点（见 spec §16.3/§16.6 与 issue 07）。

## Considered Options

- **A. UI 任务内 tick（选定）**：框架在 step 边界 `timers_.tick(1)`，`etl::callback_timer` 到期回调 trampoline → `onBackgroundTick`。零新线程、零新分配，周期粒度 = UI 循环周期（向上取整：`(period_ms + ui_loop_period_ms - 1) / ui_loop_period_ms` 拍）。
- **B. 每个 tick App 一个定时器任务**：独立任务睡到周期再调回调。周期精确，但每 App 一份任务栈换一个"每拍 5 ms 的活"，明显浪费；且回调仍在碰 UI 对象，跨任务访问反而引入并发问题，收益为负。
- **own_task 是 A 的逃生舱而不是 B 的变体**：需要真并行的 App 显式声明 `BackgroundPolicy::own_task`（自带栈深/优先级），经 `ITaskSpawner` 创建任务、周期跑同一钩子；回 UI 只走 `post(CrossTaskMessage)` 信封由 UI 任务统一派发（issue 07 落地，见 `docs/messages-and-background.md`）。

## 落地与验证

- issue 07：节拍机制 + 总线 + 收件箱 + own_task 装配（`src/embark/framework.cpp`、`tests/kernel/test_framework_messaging.cpp`）。
- issue 08：三种策略各一个 demo App（clock=tick + state_chart、settings=suspend、ticker=own_task），宿主验收 EXIT=0。
- 交叉阅读：spec §6（执行模型）、§16.3/§16.4（ETL 决策）、`docs/messages-and-background.md`（用法）、`GLOSSARY.md`（Background tick / Own task 词条）。
- 后续（2026-10-06，ADR 0009）："后台**从何时开始跑**"收紧为 App 第一次进过前台才武装（issue 23）；本 ADR 定下的实现方式与"武装之后后台与前后台无关"不变。