# 01 · 主屏磁贴分页 + 上滑返回

Type: task
Status: resolved

## 要什么

用户原话（m00001）："我后续可能要增加新的app，这不能滑动根本选不到后面的app，帮我修改一下，
让它可以滑动选择app"。主屏（launcher）的 App 入口要满足：

1. 加 App 不需要改设计稿 —— 磁贴按 App 注册表运行时生成；
2. 磁贴超过一页时**左右滑动翻页**能选到后面的 App（吸附式，不做循环、不做点页码跳页）；
3. 任意**非主屏**界面上滑 = 返回主屏，且以后新增的 App 自动适配；
4. 容量从 8 提到 12 个 App。

裁决与设计见 [../spec.md](../spec.md)，决策记录见
[docs/adr/0013-launcher-paged-tiles.md](../../../docs/adr/0013-launcher-paged-tiles.md)。

## 做了什么

- `include/embark/design_tokens.h`：新增 `tile_bg` / `tile_text` / `tile_radius`；
- `config/embark_limits.h` + `docs/reference/limits.md`：`max_apps` 8 → 12；
- `app/launcher/launcher_pages.h`（新，纯函数）+`tests/kernel/test_launcher_pages.cpp`（新）+
  `tests/CMakeLists.txt` 登记；
- `app/launcher/launcher_tiles.{h,cpp}`（新）+ `launcher_app.{h,cpp}` 装载屏后重建磁贴；
- `app/common/aidogui_ui_nav.{h,cpp}`：`attach_swipe_back()`（非主屏屏根挂手势 → `request_home()`）；
- `app/aidogui_ui/screen.aid` + `export/` 重导出：launcher 屏删掉手摆的 Tile1-4/Dot1-3，
  改成空容器 `Band_Apps`/`Dots_Row`，提示行改「左右滑动翻页，上滑返回」；新增 music/album 两屏；
- `app/music/`、`app/album/`（新占位 App）+ `app/CMakeLists.txt` 源列表 +
  `platform/host/ui_demo.cpp` 的注册表；
  （`ui_tour.cpp`、`user_main.cpp`、真机 `EMBARK_APP_SOURCES` **刻意保持三个 App**：tour 是精简
  系统用例、真机在第三步之前还没有界面代码，占位 App 没有理由进这两处。）；
- `platform/host/ui_demo.cpp`：注册表加 music/album，新增 `--ui-launcher-pages` 验收开关
  （真拖动轨迹 + 吸附断言 + 字形守卫 + 主屏不响应上滑）；
- 文档：ADR 0013、本目录 spec/acceptance、新指南 `docs/guides/new-app-guide.md`（AiDoGUI 口径）、
  `app/aidogui_ui/README.md`、EEZ 时代文档加废弃横幅、`docs/index.md`。

## 证据

`ctest -R embark_tests` 通过；宿主（2026-10-15，`C:/eb/ac94612f`，MinGW g++ 15.1.0）：
`--ui-launcher-pages --delay 40 --frames 132` → 0〔主屏分页验收通过〕、`--ui-nav --delay 40` → 0
〔aidogui 导航验收通过〕、`--ui-weather --delay 40` → 0〔aidogui 天气验收通过〕、`--ui` → 0。
逐条断言与结论行见 [../acceptance.md](../acceptance.md)。
