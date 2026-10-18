# 12 · 文档：日常循环指南 + 分工硬规则 + 文档过期自检

Status: done（2026-10-18）
Blocked by: —（01–11 已全部 done；本票是那一轮改造的文档收口）

## 为什么

11 张票落地后，`docs/` 有两类毛病，且都不是「缺一篇教程」：

1. **说过期的话**（比如 `docs/guides/quickstart.md:35` 还写着「照样板抄（没有生成脚本）」+
   `cp -r app/music app/my_app`，注册只列 2 步；`docs/reference/pitfalls.md:186` 也只说
   「必须手工登记到 EMBARK_APP_SOURCES」）。真实现状是 `node tools/new_app.mjs --apply`
   一条命令、登记 **5 行 / 3 个文件**。quickstart 是「第一次来」的必经之路，从这里进来的人
   第一眼看到的就是过期做法 —— 正是 01 票要消灭的那种「误导」。
2. **没有一篇写「每天怎么干活」**。现有六篇里两篇是「第一天教程」、四篇是「查手册」，
   日常主线（起循环 → 改逻辑/改界面 → 跑验收）要跳 4 个文件自己拼。

还有一件事没写下来：**「AI 做基建 / 我写 App」这条分工**。它只散落在 ADR 0019 与 git 历史里，
下一个接手的 agent 读不到，就会又开始替你写 App 逻辑。

## 做什么

### ① 新增 `docs/guides/daily-loop.md`（日常循环，唯一主线）

只写「今天要改某处该动哪里」：一条命令起循环 → 「我要改什么 → 去哪」表 → 三条改界面的路
→ 改完跑哪几条 → 提交前四条自检 → 「改了没反应」速查。语气是日常，不是教程。

### ② 修两处过期

- `docs/guides/quickstart.md` §「第 3–5 分钟」：`new_app.mjs` 为首选，手工路线保留但把注册改成 5 行 / 3 个文件。
- `docs/reference/pitfalls.md`：同上，别只说 `EMBARK_APP_SOURCES`。

### ③ 分工写进 `AGENTS.md` + `docs/index.md`

- `AGENTS.md` 硬规则第二节：**默认不写 App 的逻辑与界面**，要动必须用户点名；基建/脚本/文档才是 agent 的活。
- `docs/index.md` 一张「两条赛道」小表（人读得到），指向 ADR 0019。

### ④ 新增 `tools/check_docs.mjs`（文档过期自检）

扫 `docs/**` + 根 README / CONTRIBUTING / AGENTS / `app/aidogui_ui/README.md` / `platform/esp32/README.md`，五类检查：

| 检查 | 抓什么 |
| --- | --- |
| md 链接 | `[x](路径.md)` 指向的文件/目录已不存在（改名忘更新） |
| 反引号里的仓库路径 | `tools/**.mjs`、`docs/**.md`、`app/...` 这类路径已不存在 |
| `embark_host_ui` 的开关 | `--ui-nav` 这类必须真在 `platform/host/ui_demo.cpp` 里 |
| 脚本的参数 | `dev.ps1 -SkipExport`、`new_app.mjs --apply` 必须真在那个脚本里 |
| 锚注释 | 反引号里带「锚点」的字符串必须真在源文件里 |

`--self-test` 用内存里的假样本验证每条检查真会报错（否则自查脚本自己烂掉没人知道）。

## 验收

- `node tools/check_docs.mjs` exit 0；`--self-test` exit 0。
- 故意把 daily-loop.md 里某条命令改错（如 `--scren`）→ `check_docs.mjs` exit 1 且**指名文件、行号、错在哪一项**；改回来 exit 0。
- quickstart / pitfalls 里不再出现「没有生成脚本」「只登记 EMBARK_APP_SOURCES」这类过期指引。
- `docs/index.md` 的 guides 表有 daily-loop 一行，且链接可达。
- `cmake --build build` exit 0 + `ctest` 2/2 + 五条故事 + tour 全绿（本票只改文档与新增一个工具，不该有行为变化）。

## 不做

- 不把 `new-app-guide.md` 扩成大指南（两种读者混一篇，必然越写越长）。
- 不改 `.scratch/**` 里的历史工单（一手记录，改它等于伪造）。
- 不把 `check_docs.mjs` 接进 CI（本仓 CI 不跑任何 Node 脚本；靠 CONTRIBUTING 的规矩 + 提交前手工跑）。

---

## 收尾记录（2026-10-18）

### 落地文件

| 文件 | 说明 |
| --- | --- |
| `docs/guides/daily-loop.md`（新，116 行） | §0 一条命令起循环 / §1「我要改什么 → 去哪」/ §2 改界面三条路 / §3 改完跑什么 / §4 提交前四条自检 / §5「我改了没反应」速查 / §6 相关 |
| `tools/check_docs.mjs`（新，约 350 行） | 五类检查 + `--verbose` + `--self-test`；零依赖 Node ESM |
| `docs/guides/quickstart.md` | §3–5 分钟重写：`new_app.mjs` 为首选，手工路线改成 5 行 / 3 文件 |
| `docs/reference/pitfalls.md:186-190` | 改成「新加 App 不必手工登记（new_app.mjs 一次改完 5 行 / 3 文件）」，并给出漏 include 的报错原文 |
| `docs/index.md` | 新增「两条赛道：基建 vs App」表；三步后加「之后每天 → daily-loop.md」；guides 表加 daily-loop |
| `AGENTS.md` | 分工硬规则（默认不替用户写 App 逻辑与界面）+「改了工具或开关就跑一次 check_docs」 |
| `docs/guides/tools.md` | 一览表加 `check_docs.mjs`（并更正计数为十个）；新增 §6「文档自检」；修 `### 3.1/3.2` 编号（应为 4.1/4.2）；§5 补第 6 条（Node 脚本别用 `process.exit()`） |
| `CONTRIBUTING.md:101` | 「加一个带设计器界面的 App」从「照 app/music 抄」改为 `new_app.mjs --apply` |

### 三处票面偏离（都是实测逼出来的）

1. **`check_docs.mjs` 对 ADR 只查链接**。首跑抓出 40+ 条，其中 19 个 ADR 文件大量命中已删文件（0006 的 `launcher_geometry`、0007 的 `font_ledger`/`font_service`/`lv_font_loader`、0011 的 `aidogui_ui_bridge`、0016 的 `gen_fonts_bin.mjs`）。ADR 是决策史，本来就该提已删的东西 —— 让它挡住真正要抓的过期指路，这个脚本就没人跑了。
2. **加了两个逃生口**：行内 `check_docs:skip` 标记（`GLOSSARY.md:235` 那行在讲「这些文件已退役」的真实历史）、`ALLOW_MISSING` 白名单（`include/embark_aidogui_ui_tables.h` 是 cmake 配置期生成到 `build/include/` 的，仓库里本来没有）。
3. **`src/` 这个根按路径后缀兜底**。文档里 `src/esp32_hal.{h,cpp}` 指的是 `platform/esp32/src/`（简写），而 `src/embark/...` 指的是仓库根 —— 同一个前缀两种意思，只对它做「路径后缀」二次匹配。

### 首跑抓出的真问题（已修）

- `docs/guides/html-to-screen.md:4` 与 `:121` 的 `[app/aidogui_ui/README.md](../app/aidogui_ui/README.md)` 是**真·坏链**：从 `docs/guides/` 出发要退两层（`../../`），原来的写法解析成 `docs/app/aidogui_ui/README.md`。
- `docs/guides/tools.md` 新增行的 `[AGENTS.md](../AGENTS.md)` 同样少退一层。

### 五类检查的实际口径（后续维护看这里）

- 权威开关集合从 `platform/host/ui_demo.cpp` 的字符串字面量抓（`/"(--[a-z0-9-]+)"/g`），新增开关不用改脚本。
- 脚本参数在脚本源码里 `includes` 查；命令里 `embark_host_ui` 之前出现 `cmake`/`pwsh`/`node` 就整段跳过（`cmake --build ... --target embark_host_ui` 不是调用它）。
- 路径检查只认顶层目录（`tools docs app platform cmake config include src tests examples assets .github third_party`），避免把 URL 片段当路径；`xxx.{h,cpp}` 这类模板写法、纯模块名（`host_filesystem` ⇒ 试常见扩展名）都算存在。
- 锚注释检查：反引号里以 `#` 开头且含「锚点」二字，才去源文件语料里找字面串（对应 `new_app.mjs` 按字面匹配锚点注释那件事）。

### 验收实测

| 验收项 | 实测 |
| --- | --- |
| `node tools/check_docs.mjs` | **exit 0**，verbose：扫了 44 个文件（19 个 ADR 只查链接）· 346 条链接 · 538 个路径 · 243 个主机开关 · 71 处脚本调用 · 4090 段代码 |
| `node tools/check_docs.mjs --self-test` | **exit 0**，五类检查各自「本该报错就报错」+ 正向样本零误报 |
| 故意把 daily-loop.md 的 `--ui-nav` 改成 `--scren` | **exit 1**，`〔过期〕docs/guides/daily-loop.md:74  embark_host_ui 没有这个开关：--scren`；改回 exit 0 |
| 过期指引复查 | `grep` 只剩 `pitfalls.md:190`「加普通 `.cpp`（不是 App）仍必须手工登记」—— 这条是对的，不是 App |
| `cmake --build build` | exit 0（ninja: no work to do —— 本票没动 C++） |
| `ctest` | **2/2 passed**（0.26 s） |
| 五条验收故事 | 全 exit 0：`--ui`〔aidogui 验收通过〕/ `--click` / `--ui-nav` / `--ui-weather` / `--ui-launcher-pages` |
| `embark_host_tour --delay 40` | exit 0，**17 项 [通过] / 0 项 [失败]** |

### 没做的（票面已写明）

- 不把 `new-app-guide.md` 扩成大指南（两种读者混一篇必然越写越长）。
- 不接 CI（本仓 CI 不跑 Node 脚本；靠 `AGENTS.md` + `daily-loop.md` §4 的规矩）。
- 不改 `.scratch/**` 历史工单。
