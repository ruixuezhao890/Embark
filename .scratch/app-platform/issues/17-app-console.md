# 17 · App 管理台：只做「加 / 退 App」的网页面板（替换掉开发台）

Status: done（2026-10-23）· Blocked by: —（承接 14 / 15 两票的形态，范围收窄）
Supersedes: [14-dev-panel.md](14-dev-panel.md)、[15-devpanel-device-apps.md](15-devpanel-device-apps.md)

## 为什么

用户 m00063 把话题从「修一修旧面板」改成「**这是一次重构，不要修修补补**」。
旧面板 `tools/devpanel/` 从立项起就没有主线：十二个按钮平铺，等于把命令行又抄了一遍，
而且抄得比命令行**弱**：

| 旧面板的问题 | 事实 |
| --- | --- |
| 改一行要手点「构建」再点「运行」 | `taskBuild` 构建前先停窗口，**构建完不重启** —— 比 `tools/aidogui/dev.ps1` 的源码监视还退步 |
| 预览 2 fps、滞后 0.4~0.9 s | 宿主每 400 ms 覆盖写一张 307 KB BMP，网页每 500 ms 整张 GET |
| 面板不能点界面，得去点那个 SDL 真实窗口 | 宿主没有运行期外部输入注入通道，这是硬限制 |
| 加 / 退 App 是**第二份实现** | 面板自己拼参数，与 `tools/new_app.mjs` 两处维护 |
| 真机页签只是把两条命令包一层 | 串口独占、中文乱码、COM3/COM9 这些坑一条没少吃 |

结论：面板本身没坏，是**设计问题** —— 它想覆盖「全部十二件事」，于是每件都比命令行差。
收窄到一件命令行天生不擅长的事：加 / 退 App 要同时动 6 个文件、7 行登记 + `.aid` 一张占位屏，
这件事适合表单 + 计划预览 + 一键落盘。

## 决定（用户 m00102 之后全部「按推荐」）

| # | 问题 | 决定 |
| --- | --- | --- |
| 1 | 执行边界 | 改文件 → 自动 `export.ps1` → 自动 `cmake --target embark_host_ui`。**不跑**五条验收故事、不做实时画面、不做串口 / 烧录 |
| 2 | 真机表 | 表单勾选框，**默认勾上**；勾了就登记 `cmake` DEVICE 清单 + `main.cpp` 的 include 与 App 表（第 6 个面，旧 CLI 一直漏） |
| 3 | 删除安全 | 列出「要删目录 + 要撤的登记 + 撤哪块屏」→ 手动敲 App 名确认；宿主侧登记找不到就**拒绝并指出哪一处**；真机侧与 hooks **容忍缺失**（老 App 本来没有）；`launcher` 永不可删 |
| 4 | 界面指引 | 一页只读三步 + 三个按钮（打开设计器 / 导出 / 构建），文字只留三条不能错的：屏名 == App 名、`export/` 不许手改、返回键挂 `embark_ui_open_launcher` |
| 5 | 旧台子 | **全删**；真机烧录 / 串口回命令行，四条坑搬进 [pitfalls.md](../../../docs/reference/pitfalls.md) |
| 6 | 术语 | 词条写进 [GLOSSARY.md](../../../GLOSSARY.md)：**App 管理台**，`_Avoid_`: 开发台 |
| 7 | 单实现 | 给 `tools/new_app.mjs` 加 `--status` / `--json`；台子只做 UI + 起子进程，**不解析给人看的文本**，不加第二份登记逻辑 |
| 8 | 表单字段 | App 名（实时校验 `^[a-z][a-z0-9_]*$`、不许 `_sub` 结尾）、中文标题（>5 汉字只提醒）、`LV_SYMBOL_*` 下拉、真机勾选框；占位屏模板固定 `album` 不给选；**不给 accent 颜色字段** |
| 9 | 计划预览 | 清单 + 每处登记**插进去的片段**（带前后两行上下文），不做完整 diff |
| 10 | App 列表真相源 | 状态由 `new_app.mjs --status --json` 算，server 只转发 |
| 11 | 交付方式 | 一票搞定：新台子 → 自测跑通 → 删旧台子 → 改文档 → `node tools/check_docs.mjs` 通过 |
| 12 | 端口 | 沿用 7788（占用则 +1，最多 12 次） |

## 做什么

- 新增 `tools/appconsole/`：`server.mjs`（零依赖，Node 内置 `http` + SSE）、`index.html`（单页）、`start.cmd`、`README.md`。
- `tools/new_app.mjs` 扩成这件事的唯一实现：
  - 登记从 4 处扩到 **6 个锚点 / 7 行**（新增 `platform/esp32/project/main/main.cpp` 的 include 与 App 表）；`--device` 默认开。
  - 新增 `--status`（每个 App 的 7 处登记 + `.aid` 屏状态）与 `--json`（stdout 只有一段 JSON）。
  - 删除时 `hooks` / 真机侧缺失不报错，记进 `absent`；App 表的撤除兼容「跟别人挤在同一行」（`album` 就挨着 `music`）。
  - 摘掉对 `tools/devpanel/.state.json` 的依赖（`.aid` 只认 `--aid`，否则默认文件）。
- 删 `tools/devpanel/`（5 个文件）与 `.gitignore` 里那两行；改 9 个文件里的引用；`ui_demo.cpp` 5 处、`idf.ps1` / `serial.ps1` 注释改口径（开关与脚本本身都不动）。

## 没做（有意）

- **实时画面 / 在网页里点画面**：宿主没有输入注入通道，而且 `dev.ps1` + SDL 窗口已经够用；要做就得改宿主，收益不抵风险。
- **串口 / 烧录**：包一层只会再吃一遍独占、编码、端口那些坑，而它没有任何「表单化」的收益。
- **打包成单文件 exe**：省不掉 Node / cmake / ESP-IDF 环境，收益不在于此（14 票已论证过）。

## 验收记录

1. `node tools/new_app.mjs --self-check` 通过（6 个锚点 / 7 行改动）。
2. 端到端：起台子 → POST `/api/apply` 建 `zz_demo`（中文标题「测试二」）→ `export` + `build` 均 `ok:true`；
   `--status` 显示 `zz_demo` 宿主 4/4、真机 3/3、屏 placeholder(7)。
3. 反向：POST `/api/apply` 删 `zz_demo` → `export` + `build` 均 `ok:true`；
   `cmake/embark_sources.cmake`、`ui_demo.cpp`、`main.cpp`、`aidogui_ui_hooks.h`、`screen.aid`
   五个文件的 sha256 与建之前**逐字节相同**（round trip 零残留）。
4. `app/aidogui_ui/export/` 与 `screen.aid` 用备份还原后，`git status --porcelain` 与开工前一致（69 行逐字节相同）。
5. `node tools/check_docs.mjs` 通过。

## 遇到的问题

- **旧 CLI 漏了第 6 个面**：`new_app.mjs` 从来没碰 `platform/esp32/project/main/main.cpp`，
  所以命令行建的 App 真机上没有磁贴（`main.cpp` 注释还写着「注册顺序要与 `ui_demo.cpp` 同序」）。本票补上，且默认开。
- **表项撤除的两种形状**：宿主表里 `AlbumApp` 跟 `MusicApp` 同一行（`..., MusicApp, AlbumApp)`），
  只认「逗号 + 换行 + 缩进 + 类名」会让删除把尾逗号留下 → `EMBARK_APP_TABLE(..., )` 编不过，报
  `template argument 7 is invalid`。改成「按相邻逗号撤」：前一项有逗号就撤逗号到本项末尾，本项是表头第一个就撤本项到后面的逗号。
- **别用 curl 传中文测**：git-bash 调 Windows 版 curl 会把 argv 按系统 ANSI 码页转换，
  `--title 测试二` 到服务端就成了乱码字节。测试改用 Node 的 `fetch`。
