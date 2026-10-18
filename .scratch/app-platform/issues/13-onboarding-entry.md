# 13 · 上手入口：一屏 30 秒 + 修三处入口文档过期 + 两个卡点 micro-fix

Status: done（2026-10-18）
Blocked by: —（本票来自用户「这么多文件，你让一个小白如何上手啊？」后的**零上下文实测走查**）

## 为什么

用户问：「这么多文件，你让一个小白如何上手啊？」。我先做了一次**真正的**验证而不是猜：
起一个**零上下文**的 subagent，只给两条任务（① 跑起来 + 改一个汉字看见屏上变了；② 加一个 App 并让它上屏），
禁止我给它任何提示，全程 12 分钟，最后它自己复原了全部改动（`git status` 逐行一致）。

结论有点意外：**劝退点不是「文档太多」，而是三个具体缺口** ——

| 卡点 | 证据 | 判断 |
| --- | --- | --- |
| 无头无法「看屏」 | README 只列了 `--click` / `--launch` / `--drag` / `--ui`，**没提 `--screenshot`**；他靠 `--help` 才摸到 | 真缺口：这是 agent / CI 唯一能证明「屏上变了」的通道 |
| `--screenshot` 失败不说路径 | 只有 `截图失败：写文件失败（Couldn't open ...）` | 真 bug：`ui_demo.cpp` 的 `save_screenshot` 只打 `SDL_GetError()`，不带 path |
| 改文案改哪个文件没判据 | 他手改了 `app/aidogui_ui/export/screens/ui_launcher.c` —— **下次导出就覆盖** | 真缺口：`.aid`（要 GUI）vs `export/`（编进二进制）没人说清 |
| 没有同名屏 = 一屏黑 | `--screen my_first` 进前台，桥 warn 一句「没有屏」，然后黑屏 | 缺「下一步」：该提示「开设计器建一张名为 X 的屏」 |
| 磁贴标题超宽静默裁 | 标题「我的第一个App」显示成「我的第／一个」 | 真缺口：标签宽 = 磁贴宽 + 默认换行，超了直接裁，零警告 |

## 做了什么

### ① 一屏入口（`README.md` 与 `docs/index.md` 顶部）

- `README.md`：新增 **「## 30 秒上手」** 放在简介之后、一切解释之前 —— 三条命令（编 / 看 / 验）
  + **「每天只碰这三个地方」** 表（界面 → `screen.aid`；界面让 AI 画 → HTML 草稿；App 逻辑 → `app/<名>/`）
  + 四条一句话：别手改 `export/`、`dev.ps1` 一条命令循环、`new_app.mjs` 加 App（标题 ≤5 汉字）、卡住了看 daily-loop。
  并把原「快速开始」改名为「从零 clone 一次」（只保留 clone 与装工具，去掉与 30 秒重复的命令）。
- `docs/index.md`：同一段落的精简版（命令 + 三个地方一句话），放在「第一次来：三步」之前。

### ② 修三处入口文档过期

| 位置 | 原文 | 改成 |
| --- | --- | --- |
| `README.md` | 「## 三个宿主入口」，但表里**四行** | 「## 四个宿主入口」 |
| `README.md` | `embark_host_ui` 只列 `--click` / `--launch` / `--drag` / `--ui` | 补 `--ui-nav` / `--ui-weather` / `--ui-launcher-pages`，并点名 `--screen` / `--screenshot` / `--watch` / `--delay` |
| `README.md` | `app/` 行只列 4 个示例 App（实际 6 个）；`tools/` 行只说 3 件事（实际 8 个脚本） | 补齐 music / album 与 `new_app.mjs` / `aidogui/` / `font/` 清单 |
| `docs/guides/quickstart.md` | FAQ「真机上没有界面：真机当前没定义 `EMBARK_AIDOGUI_UI`」 | **这条是错的** —— 真机早就定义了（`platform/esp32/project/components/embark/CMakeLists.txt`）。改成「先看串口有没有 `没有屏 <名>`：几乎都是屏名 ≠ App 名，或改了 `export/` 却没重新配置构建」 |
| `docs/guides/quickstart.md` | 最短验收 `--click` / `--ui` 没带 `--delay` | 补 `--delay 40`（合成输入必带，否则一趟过一趟不过） |

### ③ 两个卡点 micro-fix（都是基建，不碰 App）

- `platform/host/ui_demo.cpp` `save_screenshot()`：失败日志从 `写文件失败（<SDL 错误>）` 改成
  `写不进 <路径>（<SDL 错误>）` —— 从此报错自带路径；`--help` 与文件头注释同步说明「建议用相对路径」。
- `app/common/aidogui_ui_bridge.cpp` `aidogui_ui_bridge_load_screen()`：找不到屏时在原来那条 WARN 之后
  再补一条 `下一步：在 AiDoGUI 设计器里建一张名为 <名> 的屏并导出，然后重新配置构建` —— 黑屏不再没有出口。
- `tools/new_app.mjs`：`--title` 超过 5 个汉字时打一条提醒（**只提醒不拦**）——
  磁贴标签宽 = 磁贴宽、14px、默认换行、一行约 3 个汉字，超了会被裁且不报错。

### ④ `docs/reference/pitfalls.md` 新增一节

「`--screenshot` 写不进仓库外的绝对路径（实测 2026-10-18）」：用相对路径最稳；仓库外绝对路径在本机实测失败
  （同一目录 shell / node 能写，所以不是权限，**结论未定**），失败时日志现在会带完整路径。

## 验收（逐条实跑）

| 项 | 结果 |
| --- | --- |
| `node tools/check_docs.mjs` | exit 0〔文档自检通过〕 |
| `cmake --build build` | exit 0 |
| `ctest --test-dir build --output-on-failure` | 2/2 通过 |
| `--ui --frames 40 --delay 40` / `--click` / `--ui-nav` / `--ui-weather` / `--ui-launcher-pages` | 全 exit 0 |
| `embark_host_tour --delay 40` | exit 0，17 通过 / 0 失败 |
| 坏路径截图 | `截图失败：写不进 Z:/nope/probe.bmp（Couldn't open Z:/nope/probe.bmp）` —— 路径在 |
| `--screen nope` | 列出可选 App（既有行为）；App 存在但没同名屏时走桥的新 WARN |
| `--title 我的第一个App` | 打印「有 8 个字……建议 ≤5 个字」 |
| `clang-format --dry-run --Werror` | 两个改动过的 C++ 文件：改动行 0 违规（`ui_demo.cpp` 仍是 HEAD 就有的 27 条，未新增） |

## 没做（有意）

- 不动 `docs/concepts/` 与 `docs/adr/`（它们是「为什么」，新手不需要先读）。
- 不合并 guides / 不给 ADR 写索引：文件数不是主要问题，**入口缺失**才是；合并会破坏已建立的交叉引用。
- 不把 `--screenshot` 的仓库外绝对路径失败当成已解 bug：现象在，根因未定（见 pitfalls 那节）。
- 不给「建新屏」加命令行入口：`.aid` 的 schema 没有文档与校验，手写等于给下次导出埋雷；建屏仍是设计器的活（见 04 票）。

## 相关

- 走查任务与结论：用户 m03956「这么多文件，你让一个小白如何上手啊？」→ m03966 选 A（先实测再写入口）。
- 上位：[spec.md](../spec.md)；相邻票：[12-docs-daily-loop.md](12-docs-daily-loop.md)（日常循环 + 文档自检）。