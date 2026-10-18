# 01 · EEZ 全套退役

Status: done（2026-10-18）
Blocked by: —

## 为什么

用户 Q4（m00075）："不算（债务），把 eez 相关的给我删了，**不要误导 ai**"。

EEZ 不是"历史参考"，是**错误规则的来源**：`docs/guides/quickstart.md`（64 行）整篇还在教 EEZ 流程（提 `eez_ui`、`--eez`、`SetPage`、EEZ Studio），而代码早已走 AiDoGUI。下一个 AI 读它就会写错。

## 要删的

- `app/eez_ui/` 整目录（`src/ui/` 下 16 个生成文件：screens.c / screens.h / actions.h / vars.h / images.c / images.h / fonts.h / ui_font_sim.c 等）
- `app/CMakeLists.txt:31` 的 `add_subdirectory(eez_ui)`
- `app/CMakeLists.txt:68` 的 `EMBARK_EEZ_UI_BRIDGE=1`（`:73` 的 `EMBARK_AIDOGUI_UI=1` 保留；选后端只在 `app/common/ui_backend.h` 一处）
- `platform/host/CMakeLists.txt` 里 "两个验收程序额外链 embark::eez_ui" 的注释与链接
- `docs/guides/eez-ui-manual.md`、`docs/guides/eez-studio-guide.md`（docs/ 里 15 篇提到 EEZ 共 171 处）
- `docs/index.md:61-62` 的两行索引
- `GLOSSARY.md` 里对 EEZ 的引用（161-166 的 "Unified back navigation" / "Nav chrome" 等历史条目改写）

## 要保留的

`docs/adr/0008-eez-studio-adapter.md` **保留但加归档横幅**——ADR 是决策记录，删了就看不出"为什么曾经是 EEZ"。顶部加一行：

> 已归档（2026-10-18）：EEZ 链路已整体删除，本 ADR 仅作决策史保留。

## 收尾记录（2026-10-18）

**代码面**
- `git rm -r app/eez_ui/`（22 个文件）、`cmake/embark_eez_screens.cmake`、`cmake/embark_eez_vars.cmake`、`app/common/eez_ui_bridge.{h,cpp}`、`app/common/eez_ui_nav.{h,cpp}`
- 改名：`app/common/eez_ui_screen_names.h` → `app/common/ui_screen_names.h`（`eez_app_name_max`→`app_name_max`、`eez_ui_screen_is_subpage`→`ui_screen_is_subpage`、`eez_ui_app_name_for_screen`→`app_name_for_screen`）；`tests/kernel/test_eez_screen_names.cpp` → `test_ui_screen_names.cpp`
- `app/common/ui_backend.h`：删掉整个 `EMBARK_EEZ_UI_BRIDGE` 分支（155 → 107 行），只剩「定义 `EMBARK_AIDOGUI_UI` 走 AiDoGUI / 没定义 = 空实现」两态
- 六个 App 的 `#if defined(EMBARK_AIDOGUI_UI) || defined(EMBARK_EEZ_UI_BRIDGE)` 全部收成 `#if defined(EMBARK_AIDOGUI_UI)`
- `app/CMakeLists.txt` 删 `add_subdirectory(eez_ui)` / `embark::eez_ui` 链接 / `EMBARK_EEZ_UI_BRIDGE=1`；`platform/host/CMakeLists.txt` 删两处 `embark::eez_ui` 链接与两处 include 根；`cmake/embark_sources.cmake` 删两个源文件
- `platform/host/ui_demo.cpp`：删 `--eez` 开关、`eez_switch_frame/eez_home_frame/eez_quit_frame/eez_frames` 常量、`scroll_into_view()`、`options->eez` 合成输入分支与整块 82 行断言段；`--click` 改走 `aidogui_widget_center("launcher","Card_Clock",…)`；`--click` 断言改用 `aidogui_ui_nav_last_screen()` / `aidogui_ui_nav_click_hits()==2` / `aidogui_ui_bridge_current_screen()`。1893 → 1737 行
- `platform/host/ui_tour.cpp`：删 `eez_click_frame`、`eez_ui_bridge_screen_created` 调用；**两条屏生命周期断言改成 AiDoGUI 口径**——桥是「切走即释放」（`app/common/aidogui_ui_bridge.cpp:103 load_entry()` 里 `previous->destroy()`），没有 EEZ 那种 createAtStart 常驻屏。现在断言「回到 clock 后 clock 重建、launcher 已释放」（期望编码 10）。这不是回归，是 EEZ 时代断言口径的残留。
- `src/embark/framework.cpp`、`examples/`、顶层 `CMakeLists.txt`、`config/lv_conf.h` 注释清理

**脚本与文档面**
- `git rm tools/scaffold_app.py tools/scaffold_app_test.py`（脚手架只会生成 EEZ 时代的 App 骨架，留着误导 AI；「加 App 照 `app/music/` 抄 + 手工登记 `cmake/embark_sources.cmake`」写进文档）
- 新建 `docs/guides/tools.md` 取代 `docs/guides/scaffold-and-tools.md`（228 行 EEZ 时代内容）；删 `docs/guides/eez-ui-manual.md`、`docs/guides/eez-studio-guide.md`
- `docs/guides/quickstart.md` 整篇重写为 AiDoGUI 主线
- `docs/index.md`、`GLOSSARY.md`、`README.md`、`CONTRIBUTING.md`、`docs/concepts/{index,app-lifecycle}.md`、`docs/reference/{hooks-and-api,pitfalls}.md`、`.github/workflows/ci.yml`、`.gitignore` 全部清理
- ADR：`0008` 加归档横幅；`0006/0010/0011/0014/0017/0018` 里的 EEZ 措辞改成中性描述（保留决策史，不删原文结论）
- `CHANGELOG.md` 收尾：标题、界面来源条目、屏生命周期措辞、删脚手架条目、里程碑行

**验收实测（2026-10-18）**
- `git grep -n -i eez -- . ':!.scratch' ':!third_party' ':!*.ttf'` → 只剩 `docs/adr/0008-eez-studio-adapter.md`（归档横幅 + 决策史原文）与 `docs/adr/0011-aidogui-ui-source.md`（「把 EEZ 换成 AiDoGUI」本身就是该 ADR 的决策内容，必须提旧名）。`CHANGELOG.md` 是历史记录、`.scratch/` 是历史工单、`third_party/` 是上游，按同口径保留
- `cmake --build build` → exit 0（226 目标）
- `ctest --test-dir build --output-on-failure` → 2/2 通过
- `embark_host_ui --ui --frames 40` exit 0；`--ui-weather --delay 40` exit 0；`--ui-launcher-pages --delay 40` exit 0；`--click --delay 40` exit 0；`embark_host_tour --delay 40` exit 0（17/17）

## 验收

- `grep -ri "eez" --include=*.md --include=*.cpp --include=*.h --include=*.cmake --include=CMakeLists.txt .` 只剩 ADR 0008 与本票
- `cmake -G Ninja -B build` 与 `cmake --build build` 通过
- `ctest --test-dir build --output-on-failure` 通过
- `embark_host_ui --ui --delay 40` 不回归
