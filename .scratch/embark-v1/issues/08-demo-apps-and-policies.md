# 08 · Demo App 与三种后台策略

Status: resolved
Type: task
Blocked by: 07

Resolved: commit 7d7a71f（本地，未推送）

## Answer

- 三个 demo App 全部落在 `app/demo_apps.{h,cpp}`（命名空间 `embark::demo`），随 `embark_demo_apps` 静态库构建；`app/CMakeLists.txt` 只链 `embark_apps` + `embark::core` + `embark_lvgl`，**不含任何平台后端**；宿主 UI 可执行文件链 `platform_host_ui` + `demo_apps`。
- **三种后台策略各一个**：clock（`Tick`，100 ms，内部用 `etl::state_chart` 表达亮/灭状态机，chart_.start() 在 onCreate、onBackgroundTick 里 process_event(tick)）· settings（`Suspend`，无后台节拍）· ticker（`OwnTask`，50 ms 周期、256 字栈、优先级 4，后台任务经 `fw.post(CrossTaskMessage)` 上报计数，UI 任务在 step 末尾抽干收件箱并 publish 到总线）。
- **`etl::state_chart<ClockApp>` 实际用法**（ETL 20.49.0 运行时表版）：`static const transition kTransitions[2]` + `static const state kStates[2]` 数组（成员函数指针作 action/on_entry），构造传 4 指针 + 初始态 id；`start()` 触发初始态 on_entry（幂等）；`process_event(event_id)` 扫表转移。示范了两点：①「tick 事件驱动状态转移」——不是所有输入都要过状态机；② 亮度是**值**不是行为，走消息不进状态机（onMessage 里直接更新标签）。
- **App 间通信只走消息**：settings 的「Level +1」按钮 publish `BrightnessMessage(id 0x21)` → 总线广播 → clock 在 onMessage 收到后刷新界面（`clock 收到 BrightnessMessage：亮度档 -> 1`）。两个 App 互不 include 对方头文件。
- **交互与状态保持**：宿主合成点击 clock 屏「Settings」按钮 (160,170) 切到 settings；settings 屏内「Level +1」(160,170) 发消息、「Back to clock」(160,215) 切回；`--click` 与 `--switch` 参数分别走这两条路径。切回后 clock 的 `enters()==1`、`resumes()==1`，ticks 计数继续累加（状态保持）。
- **宿主验收结果**（`--frames 150 --click --switch --own-task --screenshot`，EXIT=0）：切换 2 次；clock ticks 7（后台节拍可观测）；brightness 消息 1 发 1 收；settings enter 1 / resume 0（onEnter 只在首次）；ticker 24 发 24 收（own task 不丢不乱序）；总线发布 25 / 无人接收 0；收件箱溢出 0；UI 任务栈余量 2045 字；LVGL 堆峰值 12846 字节。截图 `.scratch/embark-v1/evidence/08-demo-apps/ui-switch-own-task.png`（1228922 字节）。`--quit-at 30` 单独跑 EXIT=0（第 31 帧干净收尾）。
- **测试**：71 个内核单测 + ctest 100% 全绿（app 迁移后回归确认）。

正是 issue 12 的 demo 章节与 README 演示段依此整理。

## 目标

凑齐 spec §14.1 的验收：宿主里 ≥2 个 App、可切前台、后台 tick 可观测；顺带示范 `etl::state_chart`。

## 范围

- 两个 demo App（放在 `app/`）：例如「计数器/闪烁灯模拟」+「设置页」。
- 其中一个用 `etl::state_chart` 表达内部状态 —— **示范，不是强制**（§16.4 第 2 条：基类不绑范式）。
- 三种后台策略各演示一种：`Suspend` / `Tick(period_ms)` / `OwnTask`（后台干重活的那个）。
- 交互：按键或触摸切换前台；后台 App 的 tick 计数显示在日志或前台界面上。

## 验收

- 宿主窗口里能完整走一遍：启动 → 切前台 → 后台 tick 计数在涨 → 再切回来 → 状态保持。
- `app/` 与 `include/embark/` 里**没有任何平台后端头文件**（换后端不动 App 的验收前提）。
- App 之间不互相 include，只走消息。

## 备注

这些 demo 同时充当文档里的最短路径素材（issue 12 会引用）。
