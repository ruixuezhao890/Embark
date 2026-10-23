# App 留存与刷新：三条轴、五类，控件值重推挂到屏装载后

「App 退出就结束」今天**并不存在**：App 对象住在静态注册表里，boot 到关机一直活着（`onExit` 只在关机路径调）。离开一个 App 时真正结束的只有两件事——**它的屏**（桥的纪律「切走即 destroy / 回来即 init」）和**它的后台跑不跑**。用户要的「退出后再点回来还留在原页面」缺的不是进程模型，是**视图状态（view state）契约**；而「天气/时间怎么更新」既不是留存也不是后台，是**数据新鲜度**。所以把搅在一起的三件事拆成三条正交的轴：

| 轴 | 问题 | 今天谁负责 |
| --- | --- | --- |
| 留存申请（retention） | 离开之后还需要**往前推进**吗？ | 显式声明 `BackgroundPolicy::tick` / `own_task`（默认没有） |
| 视图留存（view state） | 回来时需要**原样**吗？ | App 自己持有（屏会被销毁重建） |
| 数据新鲜度（freshness） | 值什么时候**重算**？ | 前台节拍 + 屏装载后重推 + 事件通知 |

## 决定

1. **屏照旧切走即 destroy、回来即 init**；视图状态由 App 自持（成员变量，不进 KV）。否决「十二张屏常驻」：内存账不涨，代价只是 App 多两个成员。
2. **后台是例外，不是默认。** `BackgroundPolicy` 三种策略都还在（[ADR 0004](0004-background-tick-and-state-policy.md)），但默认 `suspend`，要用后台体的 App 必须**显式申请**；今天全仓没有一个 App 声明后台体（clock 与 launcher 已按本 ADR 退役 `tick`）。红白机这类「离开即停、回来继续」的**冻结型**不需要申请——它的 CPU 不跑、状态留 RAM。
3. **前台渲染型退出后台节拍**：clock（秒）与 launcher（主屏分钟、天气卡）改由 `onForegroundTick`（只在前台跑）+ 进屏 / 回屏重推驱动。这就是本 ADR 对 [ADR 0009](0009-background-arm-on-first-enter.md) 的修订：**机制（武装时机、`at_boot`）保留，其中「后台 = 随时间跑」的默认假设撤回**。
4. **控件值重推进机制**：桥的屏观察者加第三张表——`ui_backend_bind_repaint(屏名, fn)`（实现 `aidogui_ui_nav_bind_repaint`，与点击绑定 / 逻辑动作绑定同规矩：容量 8 条、同一屏后写的生效）。App 在 `onCreate` 声明一次，屏每次装载完后由桥回调，由它写控件值。理由是**位置**：桥已经规定「每次屏装载都要做的事」挂在观察者这一处（点击绑定就是这么做的），控件值重推是唯一还没挂上去的那件事，于是退化成每个 App 手工维护的纪律——launcher 的时间停在设计稿 `11:31` 就是这么漏出来的。
5. **留存白名单 = 编译期自报 + 宿主验收断言**：谁吃后台由 `AppSettings` 自报（作者即用户），验收再把「今天没有任何 App 申请后台体」「写了控件值的屏都声明了重推」当**不变量**断言。

## 五类 App

| 类 | 离开时 | 回来时 | 需要什么 | 例 |
| --- | --- | --- | --- | --- |
| ① 交互型 | 屏 destroy、后台停 | 重建屏 + 恢复上次位置 | view state（App 自持） | clock / weather / album / music 界面、settings |
| ② 前台渲染型 | 什么都不必跑 | 进屏 / 回屏立刻重算 | `onForegroundTick` + 重推 | 主屏分钟、天气卡、clock 秒 |
| ③ 后台留存型 | 任务继续推进 | 界面接管正在跑的它 | **显式申请** `tick` / `own_task` | 放歌、下载（都是将来） |
| ④ 冻结型 | 冻结（CPU 停、状态留 RAM） | 原样继续 | view state（可能很大，看内存预算） | 红白机 / 编辑器（都是将来） |
| ⑤ 事件驱动型 | 与前台无关地跑，没有界面 | — | `ArmPolicy::at_boot`（今天无人用） | 闹钟、定时任务 |

## Considered Options

- **每个 App 一个 FreeRTOS 任务做 App 主体**：否决。① LVGL 非线程安全，[ADR 0001](0001-single-ui-task-logic-apps.md) 定的是唯一 UI 任务 + 逻辑模块，任务化会把「谁能碰控件」从架构保证降级成纪律；② `max_own_tasks = 2`（[core/config/embark_limits.h](../../core/config/embark_limits.h) `:67`）、每槽 512 字栈，装不下 App 数量；③ `ITaskSpawner` 只有 `spawn_task` / `release_task`，**没有 suspend/resume 原语**（[core/include/embark/task_spawner.h](../../core/include/embark/task_spawner.h) `:56-66`，ADR 0009 弃选 D 就是被这条挡住）；④ 用户要的 UX（回来还在原页面）不需要它。
- **屏常驻（十二张屏全留着）**：能消灭「重建导致缓存失配」整类 bug（`11:31` 就是这类），但与「屏 = 一次性资源」的既有纪律冲突、内存账要重算。留一道门：真做了 `lvgl_alloc_budget_bytes` 的实测再回头议。
- **为冻结型（红白机）也申请后台**：会让「申请」失去意义（人人都要），且冻结型不需要在离开后推进。
- **给天气加一条新消息类型**：不需要。回主屏 `onResume` 重读 KV 就够；今天 launcher 每秒轮询一次存储，只是把刷新放错了位置。

## 落地与验证

- 值重推：`app/common/ui_backend.h:61`（`ui_backend_bind_repaint`，没有导航桥的构建下退化为空实现）、[app/common/aidogui_ui_nav.h](../../app/common/aidogui_ui_nav.h) `:77`（`aidogui_ui_nav_bind_repaint`，观测 `..._bindings()` / `..._calls()`；观察者里在点击绑定之后调用）。声明方：launcher / clock / weather 三个 App 各一行。
- 退役后台体：`app/clock/clock_app.h` 与 `app/launcher/launcher_app.h` 的 `settings()` 覆盖已删（回到默认 suspend），刷新改走 `onForegroundTick`；weather 只在屏装载后与切城市时写控件。
- 视图留存样板：launcher 磁贴页码（[app/launcher/launcher_app.cpp](../../app/launcher/launcher_app.cpp) `onPause` 记 `launcher_tiles_current_page()`、重推时跳回）、clock 月历月（`cal_view_y_` / `cal_view_m_` 在 `repaint_screen()` 里**不**清）。
- 宿主验收（[host/ui_demo.cpp](../../host/ui_demo.cpp) `--ui-launcher-pages`）：新增两条全局不变量——「没有任何 App 申请后台体」「声明重推的屏数 == 3 且屏装载后被调过」，以及端到端断言「翻到第 2 页 → 进 clock → 回主屏 → 仍在第 2 页」。
- 内核用例不变（框架侧执行模型未动）。
- 交叉阅读：[ADR 0004](0004-background-tick-and-state-policy.md)（节拍实现）、[ADR 0009](0009-background-arm-on-first-enter.md)（武装时机，被本 ADR 修订默认假设）、[ADR 0010](0010-arm-at-boot.md)（`at_boot`）、[ADR 0013](0013-launcher-paged-tiles.md) / [ADR 0014](0014-in-screen-back.md)（主屏与回程）、`docs/concepts/app-lifecycle.md` §4.3、`docs/concepts/app-lifecycle.md`、[app/aidogui_ui/README.md](../../app/aidogui_ui/README.md)、`GLOSSARY.md`（Retention / View state / Frozen / Value repaint）。
