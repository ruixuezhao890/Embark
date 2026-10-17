# 04 退役 AboutApp 并清理全局接线

Status: done
Stage: 清理
Blocked by: 03

## 背景

「关于」不再是一个 App，而是设置页最末一张卡。所以 `AboutApp` 连同它的目录、它的屏、它在 App 表里的位置一起删掉；与此同时，仓库里有一批注释把 `settings_app.cpp` 当「最小样板」引用 —— 它现在已经不是了，得换靶子。换靶 `app/album/album_app.cpp`（59 行，零绑定零消息，是真正最薄的那个）。

## 做什么

### 删

- `app/about/` 整个目录。
- `cmake/embark_sources.cmake:67` 与 `:89` 两行（`EMBARK_APP_SOURCES` 与 `_DEVICE` 各一处）。
- `platform/esp32/project/main/main.cpp:62-64` 与 `platform/host/ui_demo.cpp:410-412` 的 `EMBARK_APP_TABLE` 里的 `embark::demo::AboutApp`。
  （`platform/host/ui_tour.cpp:218` 与 `platform/host/user_main.cpp:96` 只有 Launcher/Clock/Settings 三个 App，不用动。）
- `tools/scaffold_app.py` 里的锚点行（若它按 App 表长度做假设）。
- `app/aidogui_ui/export/screens/ui_about.{c,h}`：**02 已经删过**，这里只做复查，别手删生成物。

### 改引用（「最小样板」换靶）

以下位置的 `settings_app.cpp` 一律换成 `album_app.cpp`：

- `app/album/album_app.cpp:4`、`app/music/music_app.cpp:4`、`app/clock/clock_app.cpp:4`（原文是「薄壳纪律与 settings_app.cpp 完全一致」）
- `app/clock/clock_app.h:94`、`app/launcher/launcher_app.h:70`（原文是「范式同 settings_app.cpp」）
- `app/CMakeLists.txt:66` 的同一条列举
- `docs/guides/eez-ui-manual.md:75`

## 验收

- 全仓 grep `AboutApp|about_app|ui_about` 无命中。
- 全仓 grep `settings_app.cpp` 只剩文件自己、cmake 源列表与 `docs/adr/` 里的历史记录。
- `cmake --build build` 通过；`--ui --frames 40` / `--ui-nav --delay 40` 通过。
## Comments

- **落地**：`app/about/` 删除；`platform/host/ui_demo.cpp` 与 `platform/esp32/project/main/main.cpp` 的 `EMBARK_APP_TABLE`
  都去掉 `embark::demo::AboutApp`（注册表 7 → 6，主屏磁贴 6 → 5，分页仍 2 页）；`cmake/embark_sources.cmake` 两行删除；
  `app/aidogui_ui/export/screens/ui_about.{c,h}` 已由 02 删掉，这里复查通过（`export/ui.c` 只剩 6 组 init/destroy）。
- **换靶**：`album_app.cpp:4` → `music_app.cpp`、`music_app.cpp:4` → `album_app.cpp`、`clock_app.cpp:4` → `launcher_app.cpp / album_app.cpp`、
  `launcher_app.cpp:4` → `clock_app.cpp / album_app.cpp`；`clock_app.h:94` 与 `launcher_app.h:70` 的「范式同 settings_app.cpp」
  改成「见 ui_backend.h 的声明」（bind_repaint 的说明本来就不需要一个 300 行的样板当靶子）；`docs/guides/eez-ui-manual.md:75` 已注明
  它不再是「最小样板」。`tools/scaffold_app.py` 没有 App 表长度假设，未改。
- 与本文的偏差：`app/CMakeLists.txt:66` 的注释列举保留 `settings_app.cpp` 作为「编译成 no-op」的举例（那句话说的是 EEZ 桥
  在 AiDoGUI 构建里为空，举例仍成立），只把 `album/music` 的薄壳靶子换了。
- 验收：全仓 grep `AboutApp|about_app|ui_about` 无命中（只剩一句历史注释「这七行从退役的「关于」App 搬进了设置屏」）；
  `cmake --build build` 通过，`--ui` / `--ui-nav` / `--ui-weather` / `--ui-launcher-pages` / `--launch` 全绿；真机烧录后 boot 无 panic。
