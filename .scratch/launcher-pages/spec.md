# 启动器分页磁贴（feature: launcher-pages）

## 目标

主屏（launcher）的 App 磁贴，从「设计器里手摆 4 个固定磁贴」改成「**按 App 注册表运行时生成 + 横向分页**」：

1. 后续新增 App 自动出现，不再受 4 格限制 —— 用户原话（m00001）："我后续可能要增加新的app，这不能滑动根本选不到后面的app"，要的是**能滑动选到后面的 App**；
2. 左右滑动翻页选 App（吸附式连续滚动，跟手拖、松手吸附最近页）；
3. 任意**非主屏**界面上滑 = 返回主屏（后续新增 App 自动适配，App 与设计器都不用声明）；
4. 目标容量：12 个 App（`max_apps` 8 → 12）。

## 非目标

- 不做点页码跳页、不做循环翻页（用户 Q2A/Q7A 明确）；
- 不做磁贴图标（保持纯文字，Q1A）；
- **不放大字体字符表**（Q10A）：只补本轮新增文字，字号行只放「现有全部 App 标题 ∪ 新提示行 ∪ 设计期文字」；
- 不做 EEZ 侧同等改造：EEZ 是待退役的老链路（ADR 0011），只加废弃横幅。

## 设计要点（逐条来自 /grill-with-docs 用户裁决）

| # | 决定 | 取值 / 落点 |
| --- | --- | --- |
| Q1 | 磁贴样式 | **新增令牌** `tile_bg=0x202B47` / `tile_radius=10` / `tile_text=0xC7D0E6`（照设计稿现值；现有令牌族取值不同，不复用） |
| Q2 | 磁贴来源 | 运行时按 `framework.apps()` 生成；**跳过注册表第 0 项**（主屏自己）→ 磁贴数 = App 数 − 1 |
| Q3 | 每页 | 4 个磁贴：y=202、48×48、x=12/68/124/180（与设计稿一致） |
| Q4 | 页码点 | 运行时生成：点数 = 页数，居中于 `Dots_Row`（行宽 = n×6 + (n−1)×2，起始 x = 120 − 行宽/2）；设计器里的 Dot1/Dot2/Dot3 删除 |
| Q5 | 翻页手感 | 吸附式连续滚动：设计器里 `Band_Apps` 横向可滚 + `LV_SCROLL_SNAP_START` + 关滚动条；运行时每页一个 240×56 页容器（页容器 = 吸附点，一格 = 一页） |
| Q6 | 上滑返回 | 挂在 `app/common/aidogui_ui_nav.cpp` 的屏观察者里：每次屏装载给**当前屏根**挂 `LV_EVENT_GESTURE`，`LV_DIR_TOP` → `framework.request_home()`；主屏（注册表第 0 个 App 的屏）不挂 |
| Q7 | 分页/几何 | 纯函数 `app/launcher/launcher_pages.h`（无 LVGL、无 App 依赖）→ doctest 单测（3/4/5/11 个 App 等） |
| Q8 | 新增 App | 「音乐」「相册」两个占位 App + 两张极简屏；注册顺序 = launcher, clock, settings, weather, music, album |
| Q9 | 容量 | `max_apps` 8 → 12（`config/embark_limits.h` + `docs/reference/limits.md`） |
| Q10 | 文档 | 新指南重写为 AiDoGUI 口径；EEZ 时代文档加「已废弃」横幅；ADR 0013 记录这次决策 |

## 关键文件

- `include/embark/design_tokens.h`：新增 `tile_bg` / `tile_radius` / `tile_text`
- `config/embark_limits.h`：`max_apps = 12`
- `app/launcher/launcher_pages.h`（新，纯函数）+ `tests/kernel/test_launcher_pages.cpp`（新）+ `tests/CMakeLists.txt` 登记
- `app/launcher/launcher_tiles.{h,cpp}`（新，LVGL 侧建页/磁贴/页码点）；`launcher_app.cpp` 装载屏后调 `launcher_tiles_rebuild(fw_->apps())`
- `app/common/aidogui_ui_nav.{h,cpp}`：`aidogui_ui_nav_attach_swipe_back()`（屏观察者里调用）
- `app/aidogui_ui/screen.aid` + `export/`：launcher 屏改造（删 Tile1-4/Dot1-3、加 `Band_Apps`/`Dots_Row`、提示行文案）+ 新增 `music`/`album` 屏
- `app/music/`、`app/album/`（新占位 App）+ `app/CMakeLists.txt` 源列表
- `platform/host/ui_demo.cpp`：注册表加音乐/相册，新增验收开关 `--ui-launcher-pages`
- `docs/adr/0013-launcher-paged-tiles.md`（新）

## 验收

见 [acceptance.md](acceptance.md)：新增 `embark_host_ui --ui-launcher-pages`；`--ui` / `--ui-nav` / `--ui-weather` 不回归。
