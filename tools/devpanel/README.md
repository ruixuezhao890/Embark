# Embark 开发台（本地网页面板）

不想敲命令行的时候用它：双击 **\`start.cmd\`**，浏览器会自动打开一个面板，之后加 App、画屏、构建、运行、看画面、跑验收都是鼠标。

- 只监听 \`127.0.0.1\`（默认端口 7788，被占就自动往后挪）。
- 零依赖：只用 Node 内置模块，不装任何 npm 包。
- **它不重写任何逻辑**，只是把已有脚本摆成按钮：\`new_app.mjs\` / \`export.ps1\` / \`html_to_aid.mjs\` / \`cmake\` / \`embark_host_ui\` / \`embark_host_tour\`。命令行一个都没少 —— 出问题时照样能查。

## 启动

双击 \`start.cmd\`（需要 Node 18+ 在 PATH 里）。等价命令：

    node tools/devpanel/server.mjs            # 起服务并自动开浏览器
    node tools/devpanel/server.mjs --no-open  # 只起服务
    EMBARK_DEVPANEL_PORT=9000 node tools/devpanel/server.mjs

关掉那个黑窗口（或 Ctrl+C）开发台就停了，设备窗口也会被一起关掉。

## 面板上每个按钮背后是什么

| 按钮 | 实际跑的命令 |
| --- | --- |
| ▶ 运行 / ■ 停止 | \`embark_host_ui --screen <屏> --shot-live build/devpanel-live.bmp --shot-live-ms 400\` |
| 构建 | \`cmake -S . -B build\` + \`cmake --build build --target embark_host_ui\` |
| 重新导出屏 | \`pwsh -NoProfile -File tools/aidogui/export.ps1\`（成功后接着构建） |
| 跑五条验收 | 五条验收故事 + \`embark_host_tour\`，逐条报退出码 |
| 加一个 App | \`node tools/new_app.mjs <名> [--title ..] [--icon ..] --dry-run\` / \`--apply\` |
| HTML 草稿 → 屏 | \`node tools/aidogui/html_to_aid.mjs --html <上传的临时文件> --screen <屏> --apply\` |

## 画面预览是怎么来的

宿主进程用 \`--shot-live\` 每 400 ms 把当前画面覆盖写到 **\`build/devpanel-live.bmp\`**（仓库内的相对路径 —— 宿主写仓库外的绝对路径会失败，见 \`docs/reference/pitfalls.md\`）。面板每 0.5 秒拉一张，\`image-rendering: pixelated\` + 240×320 原尺寸显示，不放大不糊。

所以：**鼠标点在设备窗口里点**，面板只是让你一边看画面一边点按钮。想点界面，去点那个真窗口。

## 它做不到什么（别误会）

- 不替代设计器：新建屏幕、拖控件、配置事件仍然在 AiDoGUI Designer 里做（或者写 HTML 草稿交给面板的「HTML 草稿 → 屏」）。
- 不做 C++ 热重载：改 \`.cpp\` 一定要重新构建（面板的「构建」会先把设备窗口关掉，否则 exe 被占用）。
- 不烧真机：烧写还是要 \`idf.py\`。
