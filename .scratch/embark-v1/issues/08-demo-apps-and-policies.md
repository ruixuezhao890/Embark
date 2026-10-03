# 08 · Demo App 与三种后台策略

Status: pending
Type: task
Blocked by: 07

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
