# 23 · 后台策略武装时机：App 第一次进过前台才开跑

Status: resolved
Type: task
Blocked by: —
来源: 用户观察宿主运行日志 —— `[clock_app.cpp:97 onBackgroundTick] clock 后台节拍：第 0 拍（约 0 s，now=606 ms）`：App 被创建后，声明了后台策略的它立刻就在跑后台，而用户还没点进去过。

## 现象（证据）

改动前 `Framework::boot()` 对每个 `tick` 策略的 App 直接 `register_timer(...)` + `timers_.start(id, false)`，对每个 `own_task` 策略的 App 直接 `spawn_own_task_internal(id)`：后台与"是否进过前台"（`entered_`）无关，开机即跑。宿主上 clock（当时全仓唯一非 `suspend` 的 App）的表在注册表第 1 位，boot 完成后 606 ms 就打出上面那条日志。

用户诉求（2026-10-06）：App 的后台策略要等"用户点进过它、跑过一次前台"之后才开始执行。

## 决策（ADR 0009）

选 **A：第一次进前台（`onEnter`）就武装**。boot 只注册 `tick` 定时器（不 `start`）、只校验 `own_task` 数量上限（超 `max_own_tasks` → `no_space`，不进循环）；默认前台在 boot 里已进过前台 → 当场武装；其余 App 在第一次被切进前台、`onEnter` 之后同一帧武装。`arm_background(id)` 幂等，`armed_[id]` 记账；`tick` 此刻才 `start`（周期从这一刻起算），`own_task` 此刻才创建任务，`suspend` / `tick + period_ms == 0` 只记账。

弃选见 ADR 0009 的 Considered Options（B 首次 `onPause` 武装、C 加 eager 字段、D 让后台"只在前台之外跑"）。

## 落地

- `src/embark/framework.cpp`：新增 `arm_background(AppId)`（幂等、失败 `ELOG_ERROR`）；boot 的 tick 段改为只注册、新增 own_task 数量预检、boot 末尾武装默认前台；`apply_pending_switch` 在 `onEnter`/`onResume` 后武装并累计 `arm_failures_`。
- `include/embark/framework.h`：武装时机契约写进文件头；新增 `background_armed(id)` / `arm_failures()`；新增成员 `armed_` / `bg_timer_ids_` / `arm_failures_`。
- 内核用例：`tests/kernel/test_framework_messaging.cpp`（武装前不跑 / 武装后按周期跑 / `period_ms == 0` 无后台体 / 武装失败路径）、`tests/kernel/test_own_task_lifecycle.cpp`（默认前台 boot 即武装；两个 App 各自在武装时创建）、`tests/kernel/test_system_tour.cpp`、`tests/kernel/test_zero_alloc.cpp`（武装路径零分配）。
- 宿主验收：`platform/host/ui_tour.cpp` 新增"进 clock 之前它的后台没跑""clock 在第一次进前台时就已武装""武装失败计数 = 0"三项自检（清单 15 → 17 项）；`platform/host/CMakeLists.txt`、`platform/host/user_main.cpp` 流程叙述同步。
- 文档：`docs/adr/0009-background-arm-on-first-enter.md`（新增）、`docs/app-lifecycle/README.md`（§2 装配顺序、新增 §4.1、§5、§7 索引）、`docs/messages-and-background.md`（新增「武装时机」小节）、`docs/new-app-guide.md`（钩子契约表）、`README.md` / `docs/README.md` / `docs/quickstart.md`（tour 叙述与自检项数）、`GLOSSARY.md`（新增 **Arm** 词条）。

## 验收

- `build/tests/embark_tests.exe`：100 用例 / 887 断言全绿（改动前 847 断言，新增 40 条武装相关断言）。
- 宿主 tour：新增三项自检按预期工作 —— 第 12 帧（还没点进 clock）`clock 已武装=0，ticks=0`；切进 clock 的那一帧打出 `App clock 后台武装：AppSettings { background = tick, period_ms = 100, ... }`（`framework.cpp:222`），且该帧的后台节拍段在武装之后；`arm_failures() == 0`。
- 内核语义：`suspend` / `tick + period_ms == 0` 的 App 武装后 `onBackgroundTick` 一次都不被调；未被打开过的 App 跑满多个周期仍是 0 拍。

## 备注

- 武装后的后台仍与前后台无关（前台期间照跑）：`own_task` 是 `for (;;) { delay(period_ms); onBackgroundTick(now); }` 的常驻平台任务，框架没有"暂停/恢复"原语；"后台"描述职责，不描述"只在前台之外运行"。
- `armed_` 是 RAM 位、每次开机清零 → 语义是"每次开机之后被打开过一次才跑"。真机闹钟类需要"上电即跑"的 App 将来靠 `AppSettings` 的 eager 字段表达，v1 不做（YAGNI）。
- **待用户拍板**：宿主 tour 第 `clock 后台节拍 > 0（进过前台后持续跑）` 一项在本次改动**之前**就是红的，根因不是本 issue —— 工作区里 `app/clock/clock_app.cpp:55` 的 `chart_.start();` 被注释掉了（HEAD 是未注释的），FSM 因而不派发 `Event::tick`，`ticks_` 恒为 0（`ticks_` 只在状态机 action `on_tick` 里自增）。候选修法：① 恢复该行；② 给 `ClockApp` 加一个独立的后台节拍计数（`onBackgroundTick` 开头自增）并让 tour 改读它；③ 保持现状、把该项检查降级/改口径。在拍板前不动 `app/clock/clock_app.cpp`，也不改该项判据。

## Comments

### 2026-10-06 · 用户提问与拍板

- 用户（m00001）先质疑"App 被创建后后台直接跑"是否合理，并要求先汇报方案再动手。
- 汇报后用户（m00042）明确期望："至少要我点击进去过 App 让 UI 加载，然后返回 launcher 之后，这个后台策略才开始执行"，并问 A/B 两案；用户（m00069）选 **A**（首次 `onEnter` 武装）。
- 讨论中确认的判据：`own_task` 无法"前台时暂停"，所以本次只解决**武装时机**，不改"后台与前后台无关"这条既有语义（ADR 0004）。
