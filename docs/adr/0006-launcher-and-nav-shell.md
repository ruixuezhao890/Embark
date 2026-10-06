# 启动器：扇形半环 + 框架导航壳（不侵入 App）
用户要一个"管理别的 App"的启动器（/grill-with-docs 首问），并指定了一份 HTML 参考骨架
（`06-扇形半环.html`，折扇式：支点底部、8 个圆图标沿上半圆弧展开、选中项停正上方、虚线弧轨 +
实线选中弧段、拖动/滚轮换位 + 弹簧缓动），同时明确"风格得换"。多轮设计问答后敲定：
1. **管理边界**：启动器只负责"列出 App → 点击切前台 → 回到自己"，其余 App 按各自后台策略
   自动挂起/后台运行；不做安装/卸载/杀进程（spec v1 非目标）。
2. **骨架与交互照搬参考 HTML**：240×320 竖屏，支点 `(120,270)`，弧半径 R=82，槽距 STEP=26°，
   最多 8 槽（超过 `max_apps` 编译期报错），选中项停正上方；拖动 1px≈0.1 槽、滚轮/按键 ±1 槽，
   弹簧缓动 `pos += (tgt-pos)*0.15` 在 5 ms UI 节拍里推进；点击选中图标 = `request_switch` 启动，
   点击非选中槽只把该槽转正。
3. **风格换成深色科技风**（用户指定换风格；水墨参考仅保留骨架）：近黑蓝底 + 青色强调 +
   中文标题；图标 = 圆形细描边 + LV_SYMBOL 码点铺底（无图标时用中文首字兜底，字也进静态子集）。
   视觉常量全部进设计令牌（issue 16），手写 LVGL 的唯一取色来源。
4. **导航壳是框架的，不是 App 的**：`request_home()`（切回注册表首位）+ 统一返回键用
   `lv_layer_top` 叠加，跨 `lv_scr_load` 持久，App 契约一行不改（仍自建全屏 screen、自己
   `lv_scr_load`）；SettingsApp 里硬编码的 "Back to clock" 按钮移除，由壳统一接管。
5. **状态行只报状态不报计数**：左侧时间（宿主墙钟；真机 RTC 未接 → 占位），右侧当前 App 的
   后台策略名（`悬` / `tick:100ms` / `own:50ms`）；用户明确去掉 tick 计数。
**否决过的方案**：网格卡片启动器（第一轮初选，被用户指定的扇形参考取代——参照物优先）；
每 App 自绘返回按钮（重复、易偏离，历史耦合正是 SettingsApp 的硬编码）；框架改持全局
screen 容器、App 挂进去（破坏既有 `lv_scr_load` 契约，`lv_layer_top` 零侵入达成同样效果）。
**代价与影响**：启动器是纯手写 LVGL 定制渲染（不依赖主题对象），动画在 UI 节拍里推进、
脏区小；中文标题必须进静态子集字库并过缺字审计（ADR 0007）；切换路径收敛为
`request_switch` 与 `request_home` 两个入口；宿主验收新增壳与扇形互动的自动化开关。
工作项与验收草案见 `.scratch/embark-v1/issues/16-launcher-fan-and-nav.md`。
## 后续（EEZ 接管主屏后）：扇形自绘下线
启动器界面改由 EEZ Studio 工程提供：屏名 == App 名，`launcher` 屏上的按钮经 Flow SetPage
切到 `clock` 屏，桥的屏观察者把这次切屏翻成 `request_switch("clock")`（见 eez_ui_nav.h）。
于是本文档第 2/3 条的扇形几何、拖动/滚轮/弹簧与手绘视觉全部删除
（`include/embark/launcher_geometry.h` 与 `tests/kernel/test_launcher_geometry.cpp` 一并移除），
LauncherApp 退化成与 EezDemoApp 同形的"EEZ 宿主"薄壳。第 4/5 条（导航壳属于框架、
状态行只报状态）不变。宿主验收随之改写：`--launch` 改为程序化 `request_switch` 启动，
`--click`/`--eez` 改为点 EEZ 屏上的按钮驱动切 App。

## 后记（2026-10-06）：导航壳退役

框架导航壳（`platform/common/nav_shell.{h,cpp}`：lv_layer_top 状态行 + 统一返回键）已整体退役：
源文件删除，`framework` 不再持壳、`notify_foreground()` 移除。返回键与状态行的职责移交 EEZ 屏：
回主屏由屏内按钮（Flow SetPage 回 launcher 屏 → 屏观察者 `request_switch`）承担，状态信息由屏上
控件呈现。App 侧 `request_home()` 公共 API 保留（App 或 EEZ action 显式调用 = 回注册表首位）。

第 1-5 条与「后续」节正文保留为历史设计记录；宿主验收（`--launch` / `--click` / `--eez` / tour）
已改为由 EEZ 屏按钮驱动往返（前台切换次数随之更新，见根 README 的开关表）。
