# 日常循环：改一个 App 的每一天

> 这一篇只回答一件事：**今天要改某处，动哪个文件、敲哪条命令、跑哪几条验收。**
> 第一次来先走 [getting-started.md](getting-started.md)（跑起来）与 [new-app-guide.md](new-app-guide.md)（加第一个 App）；
> 这一篇是那之后的「每天都用」。
>
> 前提是一条分工线：**基建归 agent**（HAL、服务层、脚本、文档），**App 的逻辑与界面归你**。
> 见 [AGENTS.md](../../AGENTS.md) 与 [ADR 0019](../adr/0019-service-layer-and-public-hardware.md)。

## 0. 一条命令起循环

```powershell
pwsh -NoProfile -File tools/aidogui/dev.ps1 -Screen clock
```

它按顺序做三件事：`export.ps1` 导出界面 → `cmake` configure + 构建 `embark_host_ui` →
起窗口 `--screen clock --watch`。之后它一直在看：

| 你改了什么 | 谁让它生效 | 窗口 |
| --- | --- | --- |
| `.aid` 的**文案与几何**（导出成 C 代码、编进二进制） | `dev.ps1` 的第 1、2 步：重新导出 + 重新构建 | 重启 |
| `app/` `core/src/` `host/` `device/` 里的 `.cpp` / `.h` | `dev.ps1` 的源码监视：停窗口 → 构建 → 再起 | 重启 |
| `export/` 里的**运行期资源**（图片 BIN 这类，从 `S:` 盘读） | `--watch`：进程内卸掉当前屏再装上 | **不重启** |

**不想敲命令？** 加 App / 退 App 这一件事有网页控制台：双击 `tools/appconsole/start.cmd`（列表 / 看计划 / 建它 / 退掉 / 打开设计器 / 导出屏 / 构建宿主）。它背后跑的就是上面这些命令，见 [tools.md](tools.md) §7。画面与烧录仍在命令行。

只改 C++、不想每次导出：加 `-SkipExport`；只想构建不想起窗口：`-NoRun`。
这两件事混起来会以为热重载坏了 —— 完整解释在 [tools.md](tools.md) §2。

## 1. 我要改什么 → 去哪

| 我的意图 | 动哪个文件 | 怎么确认没坏 | 想深入 |
| --- | --- | --- | --- |
| 屏上的文案 / 位置 / 多一格 | `app/aidogui_ui/screen.aid`（设计器 GUI，或 §2 的 HTML 草稿） | `--ui --delay 40` + 自己看一眼 | [app/aidogui_ui/README.md](../../app/aidogui_ui/README.md) |
| 业务逻辑（数据、状态机、定时） | `app/<名>/<名>_app.cpp` | `ctest` + §3 对应的故事 | [reference/hooks-and-api.md](../reference/hooks-and-api.md) |
| 把数据写到屏上 | 同上：`aidogui_ui_bridge_widget("屏", "控件")` + `ui_backend_bind_repaint` | 屏装载后值不退回设计稿 | [reference/ui-widgets.md](../reference/ui-widgets.md) |
| 加一个新 App | `node tools/new_app.mjs <名> --apply`（连 7 行登记 + `.aid` 占位屏一起做，之后要重新导出） | §3 全部 | [new-app-guide.md](new-app-guide.md) §2 |
| 加一个设置项 | `app/settings/` | `--ui-launcher-pages --delay 40` | [settings-and-preferences.md](settings-and-preferences.md) |
| 用状态机 / 消息 / 后台任务 | `app/<名>/` | `ctest` | [state-machine-and-messages.md](state-machine-and-messages.md) |
| 屏内按钮就地干点事（不切屏） | `app/<名>/*.cpp` 的 `bind_sinks()` 一类，每次屏装载后重挂 | 点一下 | [ADR 0018](../adr/0018-settings-truth-and-growth.md) |
| 屏上是方框 = 缺字 | 换文案，或把字加进字符表 | `node tools/font/audit_glyphs.mjs` | [tools.md](tools.md) §4 |
| 接一块新硬件 | `core/include/embark/hal/` + 服务层实现 | `--imu-log` / `--audio-test` / `--net-test` | [hal-backend-guide.md](hal-backend-guide.md) |
| 烧到真机上 / 看板子日志 | `device/tools/idf.ps1 -Task flash -Port COM3` / `device/tools/serial.ps1 -Port COM3` | 板子自己亮起来 + 串口里有 `Embark 0.1.0` | [tools.md](tools.md) §7.3 |
| 退掉一个 App | `node tools/new_app.mjs <名> --delete --apply`（连 7 行登记一起撤） | `cmake --build build` 过 + 磁贴少一个 | [new-app-guide.md](new-app-guide.md) §2 |

## 2. 改界面：三条路，选一条

| 路 | 怎么做 | 什么时候选它 |
| --- | --- | --- |
| **① 设计器 GUI**（主路径） | 打开 `app/aidogui_ui/screen.aid` → 改 → 导出向导输出到 `app/aidogui_ui/export` | 你已经开着设计器；或者要改的东西多 |
| **② 无头重导出** | `pwsh -NoProfile -File tools/aidogui/export.ps1` | 没开设计器 / 想敲一条命令 / CI 里重生成 |
| **③ HTML 草稿** | 写一份 `data-*` 方言的 HTML → `node tools/aidogui/html_to_aid.mjs --html 草稿.html --screen <屏>` → 加 `--apply` 才写盘 | 只想写语义、不想手量坐标（工具用无头浏览器量真 rect） |

三条路最后都落到同一个 `screen.aid`，再导出成 C 代码 —— **生成代码永远不手改**
（`export/` 每次整体覆盖）。改完记得跑一次控件表：

```sh
node tools/aidogui/gen_widget_doc.mjs          # 重写 docs/reference/ui-widgets.md
node tools/aidogui/gen_widget_doc.mjs --check  # 只比对，不写
```

控件改名**不会编译失败** —— 代码只会静默拿到 `nullptr`，那一格从此停在设计稿初值上。

## 3. 改完跑什么

| 你改了什么 | 最少跑 |
| --- | --- |
| 只改了某个 App 的 `.cpp` | `ctest --test-dir build --output-on-failure` + `--ui --frames 40` |
| 动了界面（`screen.aid` / `export/`） | 上面两条 + `--ui --delay 40` |
| 动了主屏磁贴、导航、屏内按钮 | 上面两条 + `--ui-nav --delay 40` + `--ui-launcher-pages --delay 40` |
| 动了天气屏 | + `--ui-weather --delay 40` |
| 动了 HAL / 服务层 | + `embark_host_tour`（17 项自检）+ 对应的 `--imu-log` / `--audio-test` / `--net-test` |
| 交 PR 之前 | **上表全跑 + `embark_host_tour`** |

```sh
ctest --test-dir build --output-on-failure
./build/host/embark_host_ui --ui --frames 40 --delay 40
./build/host/embark_host_ui --ui-nav --delay 40
./build/host/embark_host_ui --ui-launcher-pages --delay 40
./build/host/embark_host_tour --delay 40
```

退出码：`0` 正常 / `1` 后端起不来或用法错 / `2` **验收失败**（日志写明哪一步）。

> **带合成点击/手势的故事必须给 `--delay 40`（或更大）**：LVGL 约每 30 ms 读一次输入，
> 帧跑得比它快时「按下 → 移动 → 抬起」会被采样进同一次读取 —— 同一份脚本会一趟过一趟不过。
> 这类偶发失败先怀疑延迟，不要先去改代码。

## 4. 提交前四条自检

```sh
node tools/check_docs.mjs                       # 文档里的路径 / 开关 / 锚点还对得上吗
node tools/aidogui/gen_widget_doc.mjs --check   # 控件表过期了吗
node tools/font/audit_glyphs.mjs                # 界面文案有缺字吗（CI 门禁）
node tools/new_app.mjs --self-check             # 加 App 的模板与锚点还在吗
```

四条都 exit 0 才提交。前两条专治「工具改了、文档没跟上」——这个仓库栽过一次
（工具早就换成本脚本了，老教程还在教 `cp -r`）。

## 5. 「我改了，没反应」速查

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 屏上一格没变 | 屏表 / 控件表是**配置期**生成的 | 重新 `cmake -S . -B build`，或用 `dev.ps1`（它把导出 + 配置 + 构建串成一条） |
| 文案变了、位置没变 | 改了 `.aid` 但没重新导出 | `dev.ps1` 不加 `-SkipExport`，或手跑 `export.ps1` |
| 值停在设计稿 | 没有声明重推 | 在 `onCreate` 里 `ui_backend_bind_repaint("屏", 回调)`（[ADR 0015](../adr/0015-app-retention-and-refresh.md)） |
| 点按钮没反应 | 屏名 ≠ App 名；或控件挂的是 CHANGE_SCREEN 动作（绕过桥的按需 init/destroy） | 改成「调用函数」`embark_ui_open_<App 名>` |
| 文字是方框 | `ui_heiti.ttf` 缺字 | `node tools/font/audit_glyphs.mjs` 看缺哪个字 |
| 磁贴少一个 | 新 App 没进注册表 | `new_app.mjs --apply` 会自动做；手加就是 7 行 / 6 个文件（宿主 4 + 真机 3 行里，`hooks` 那行老 App 可能本来没有） |
| 真机上没有界面 | 屏名 ≠ App 名（真机早就定义了 `EMBARK_AIDOGUI_UI`，见 `device/project/components/embark/CMakeLists.txt`）；或改完屏没重新配置构建（屏表是配置期生成的） | 串口里看 `没有屏 <名>` 那句 warn，再按它给的下一步做 |

## 6. 相关

- [tools.md](tools.md)：十二个脚本各自干什么、验收开关全表、App 管理台按钮、给仓库加新脚本的规矩
- [new-app-guide.md](new-app-guide.md)：第一次加 App 的 30 分钟端到端
- [html-to-screen.md](html-to-screen.md)：HTML 草稿方言与安全网
- [app/aidogui_ui/README.md](../../app/aidogui_ui/README.md)：设计工程目录、字体、导航与控件表
- [reference/pitfalls.md](../reference/pitfalls.md)：踩过的坑（按症状查）
- [index.md](../index.md)：文档三层怎么分工
