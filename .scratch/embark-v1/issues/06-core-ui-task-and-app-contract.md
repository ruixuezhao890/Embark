# 06 · 内核：App 契约 + 唯一 UI 任务 + 前后台切换

Status: pending
Type: task
Blocked by: 02, 04, 05

## 目标

实现 spec §5 + §6 的核心：App 契约、编译期注册表、唯一 UI 任务、前后台切换。

## 范围

- `embark::App` 抽象类：7 个钩子（`onCreate` / `onEnter` / `onPause` / `onResume` / `onBackgroundTick` / `onMessage` / `onExit`），签名按 spec §5，钩子集合不再增加。
- 编译期静态注册（零堆）：`EMBARK_REGISTER_APP(...)`，**注册顺序即默认前台 App**；注册表本身是 `etl::array`/静态数组。
- `embark::Framework`：`requestSwitch(id)`、前台切换（输入焦点 + 渲染权 + 事件循环权一起转）、`onPause`/`onResume` 通知；**切换决策与时机只由框架执行**。
- UI 任务：FreeRTOS 任务（宿主走 issue 02 的 port），5 ms 一跳，空闲 `vTaskDelay` 让出；**全工程唯一允许 `lv_timer_handler()` 与操作 LVGL 的地方**。
- App 的 `OwnTask` 策略留出接口（实现在 issue 07）。

## 验收

- 宿主能建 UI 任务并稳定跑事件循环（不忙等）。
- 两个 App 能互相切前台，钩子按 `onPause` → `onEnter`/`onResume` 顺序正确触发。
- 注册顺序决定默认前台。
- 分配 hook 统计：这条路径 0 次动态分配。

## 备注

后台 tick 的驱动（A 方案：`etl::callback_timer`）在 issue 07，本 issue 先只定义钩子被框架调用。
