# App 管理台（tools/appconsole/）

加 App / 退 App 的网页控制台。双击 `start.cmd` 打开 http://127.0.0.1:7788/。

它只做三件事，别的都不做：

1. **看**：每个 App 的类名、标题、图标、宿主登记 4/4、真机登记 3/3、.aid 里的屏是什么状态。
2. **加 / 退**：表单填完先给计划（改哪几个文件、插进去什么片段），确认后才落盘。
3. **顺手**：建完自动跑 `tools/aidogui/export.ps1` 再 `cmake --build --target embark_host_ui`，确认编得过。

## 它不做什么（有意）

| 不做 | 去哪做 |
| --- | --- |
| 实时画面 / 在浏览器里点界面 | 跑 `tools/aidogui/dev.ps1`，直接在 SDL 窗口里点 |
| 串口 / 烧真机 | `device/tools/idf.ps1 -Task flash -Port COM3`、`device/tools/serial.ps1 -Port COM3` |
| 跑那五条验收故事 | `host/embark_host_ui.exe --ui-nav` 等，见 [tools.md](../../docs/guides/tools.md) |
| 画界面 | 「打开设计器」按钮，或 `tools/aidogui/html_to_aid.mjs` 走 HTML 草稿 |

## 它不存第二份知识

「加一个 App 要动哪几处登记」只写在 `tools/new_app.mjs` 里。这个台子把它的 `--status --json` / `--json` 结果画出来，落盘也是起子进程跑它 —— 所以命令行和网页永远是同一套行为：

```
node tools/new_app.mjs --status                    # 人看的表
node tools/new_app.mjs music_second --title 音乐二  # dry-run 计划
node tools/new_app.mjs music_second --title 音乐二 --apply
node tools/new_app.mjs music_second --delete        # 退掉（同样先看计划）
```

## 端口 / 开关

| 项 | 说明 |
| --- | --- |
| 端口 | 默认 7788，被占就自动 +1（最多试 12 次）；`EMBARK_APPCONSOLE_PORT=7790` 指定 |
| `--no-open` | 不自动开浏览器 |
| 撤销 | 关掉窗口 / Ctrl+C 即可，不留后台进程，不写状态文件 |

## 一个时刻只跑一个任务

导出会**先清空 `app/aidogui_ui/export/` 再重新生成**，两个任务撞上就废了，所以服务端有全局锁：有任务在跑时按钮是灰的，接口返回 409。

## 三条不能记错的

1. 屏名必须 == App 名（设计器里那块屏叫 `music_second`，App 就叫 `music_second`）。
2. `app/aidogui_ui/export/` 不许手改 —— 每次导出先清空重建。
3. 返回键挂 `embark_ui_open_launcher`。

更多：`docs/guides/tools.md` §7 与 `docs/guides/daily-loop.md`。
