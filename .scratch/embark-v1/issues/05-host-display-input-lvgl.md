# 05 · 宿主显示/输入后端 + LVGL 集成

Status: pending
Type: task
Blocked by: 03

## 目标

宿主跑出真实窗口：SDL2 显示/输入后端 + LVGL 8.3.x 集成（`lv_conf.h` 自持）。

## 范围

- SDL2 窗口/纹理 → 显示 HAL（区域刷新、背光映射为窗口亮度/空操作）；SDL2 事件 → 输入 HAL（按键 + 触摸坐标）。
- LVGL 8.3.x：disp/input driver 接线；`config/lv_conf.h` 以 `lvgl_template_laste` 的宿主配置为起点改写（关掉不需要的 widget 与字体，内存相关项显式给定）。
- LVGL allocator 按 spec §10 显式配置（宿主走堆 + 对象总量上限）。
- 先跑通一个最小界面（空白屏 + 一个可点按钮 + 一行文字）证明通路。

## 验收

- 宿主窗口可见、点击有响应；关窗能干净退出。
- LVGL 在这一阶段只被 main 循环调用；issue 06 接手后改由唯一 UI 任务调用。
- 记录 LVGL 补丁版号（spec §15 待定项）到 `config/`。

## 备注

LVGL 用 submodule 引入并固定一个小补丁版；`lv_conf.h` 自持、不依赖上游示例。
