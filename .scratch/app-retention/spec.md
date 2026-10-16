# App 留存与刷新策略（运行策略）—— 规格

Status: done（2026-10-16 落地，验收见 [acceptance.md](acceptance.md)；决策记在 [ADR 0015](../../docs/adr/0015-app-retention-and-refresh.md)）

## 背景与提问

用户 2026-10-16 问「现在的运行策略是否需要优化」，并给出自己的模型：大部分 App「退出就结束」，
少数特别申请后台留存（music、将来想上的红白机 / NES）；要的 UX 是「退出后再点回来还留在原页面」；
「天气 / 时间这类更新怎么分配」是未决项。原话里还想「用 FreeRTOS 任务来做 App 主体」。

## 事实校正（先摆正三条）

- **今天没有「退出即结束」**：App 对象住在静态注册表，boot → 关机常驻；`onExit` 只在 `shutdown` 路径调。
  离开一个 App 时结束的只有**它的屏**（桥纪律「切走即 destroy / 回来即 init」，`app/common/aidogui_ui_bridge.h:26`）
  与**它的后台跑不跑**（`armed_` RAM 位）。
- 三件事被搅成一件：**留存申请**（要不要继续推进）+ **视图留存**（回来要不要原样）+ **数据新鲜度**（值何时重算）。
- 判据两句话：① 离开后还需要往前推进吗？② 回来时需要原样吗？时钟秒 / 主屏分钟 / 天气卡三问皆「否」→ 属前台渲染型。

## 裁决表（Q1–Q5，用户 2026-10-16 全部采纳「按推荐」）

| Q | 问题 | 裁决 | 理由 |
| --- | --- | --- | --- |
| Q1 | 屏常驻，还是屏照旧销毁 + App 自持 view state | **(b) 屏照旧销毁重建，App 自持** | 内存账不涨；屏常驻（a）留门——它能消灭「重建导致缓存失配」整类 bug，但要先实测 `lvgl_alloc_budget_bytes` |
| Q2 | 红白机这类要不要申请后台 | **(a) 冻结型不申请**：离开即冻结，回来原样继续 | 让「申请」保持稀疏；冻结不需要在离开后推进 |
| Q3 | 「谁能吃后台」怎么约束 | **(a) 编译期自报（`AppSettings`）+ (c) 宿主验收断言白名单** | 作者即用户；把「谁吃后台」变成可回归的不变量 |
| Q4 | clock / launcher 的 `BackgroundPolicy::tick` | **(a) 退役，改前台节拍驱动** | 它们「离开后不需要推进」；后台每秒 10 拍 + 每秒一条日志纯浪费 |
| Q5 | 控件值重推：进机制还是留纪律 | **(a) 进机制**：桥的观察者加第三张表 `ui_backend_bind_repaint` | 位置是架构自己指定的（已规定「每次屏装载都要做的事」只挂在观察者一处），留纪律就会出 `11:31` 那类 bug |

## 落地清单（①–⑤）

1. **文档**：新建 `docs/adr/0015-app-retention-and-refresh.md`；ADR 0004 / 0009 / 0010 加后续注；GLOSSARY 加
   Retention / View state / Frozen / Foreground-rendered refresh / Value repaint 五条并改写 Background 三条；
   app-lifecycle 加 §4.3；index / new-app-guide / hooks-and-api / aidogui_ui README / quickstart 同步。
2. **刷新归位**：clock 与 launcher 删 `settings()` 覆盖（回到默认 `suspend`），刷新改走 `onForegroundTick`。
3. **视图留存样板**：launcher 磁贴页码（`onPause` 记 `launcher_tiles_current_page()`、重推时跳回）、clock 月历视图月
   （`cal_view_y_` / `cal_view_m_` 在 `repaint_screen()` 里不清）。
4. **值重推机制**：`ui_backend_bind_repaint(屏名, fn)`（`app/common/ui_backend.h:61`）→ `aidogui_ui_nav_bind_repaint`
   （`app/common/aidogui_ui_nav.cpp:216-239`；观察者 `:109` 调、容量 8 条 `:79`；观测 `_bindings()` / `_calls()`）。
5. **留存白名单**：宿主验收断言「没有任何 App 申请后台体」「声明重推的屏数 == 3 且被调过」。

## 非目标（本轮明确不做）

- music 播放引擎、红白机 / NES、闹钟的 `at_boot`（继续无人用）、消息总线（天气**不**加新消息类型：回主屏 `onResume` 重读 KV 就够）。
- 「要不要进程级隔离 / 一个 App 卡死不拖垮界面」是另一个议题，单独再议。

## 为什么不用「每 App 一个 FreeRTOS 任务」

① LVGL 非线程安全，[ADR 0001](../../docs/adr/0001-single-ui-task-logic-apps.md) 定的是唯一 UI 任务 + 逻辑模块；
② `max_own_tasks = 2`、每槽 512 字栈（`config/embark_limits.h:67/:75`）；③ `ITaskSpawner` 只有
`spawn_task` / `release_task`，**没有 suspend/resume**（`include/embark/task_spawner.h:56-66`，ADR 0009 弃选 D 就是被这条挡住）；
④ 用户要的 UX 不需要它。
