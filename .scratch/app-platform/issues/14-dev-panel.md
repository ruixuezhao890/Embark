# 14 · 开发台：一个键鼠操作的本地网页面板

Status: superseded（2026-10-23，被 [17-app-console.md](17-app-console.md) 取代：面板形态保留，范围收窄到「加 / 退 App + 导出 + 构建」，台子重建在 `tools/appconsole/`）
Blocked by: —

## 为什么

用户原话（2026-10-18）：「**可不可以不用命令行，你可以做一个菜单或者别的形式都可以，只要是键鼠交互，不要纯命令行**」。

这条要求不是临时起意，和既有偏好一致：管理/运维类操作走图形界面，命令行只当应急通道。
但基建这条线上「日常要干的四件事」全是命令行：构建、导出屏、起窗口、跑验收；新手第一天就被 `cmake` / `pwsh` / `node` 三套东西挡在门外（13 票的零上下文走查也印证了：他靠 `--help` 摸出 `--screenshot` 才看见屏）。

**不做**原生窗口程序（Qt / .NET）：要装运行时、要打包、要处理平台差异，为一个开发工具不值。
**做**本地网页面板：零安装、跨平台、改一次全部生效，而且天然是「按钮 + 预览 + 日志」的形状。

## 做了什么

### 1. 宿主加两个开关（`platform/host/ui_demo.cpp`）

| 开关 | 作用 |
| --- | --- |
| `--shot-live FILE` | 每隔 N 毫秒把当前画面覆盖写进 FILE（BMP）。进程照跑、窗口照点，写失败只告警不退出 |
| `--shot-live-ms N` | 覆盖写间隔（默认 500 ms） |

为什么必须做在宿主里：面板要从**正在运行的那个窗口**取画面（用户用鼠标在窗口里点，预览得跟着变）。
另起一个进程截图拿不到点击结果；视频流对这个用途是过度设计。

落点选 `build/devpanel-live.bmp`（仓库内相对路径）是因为**宿主写仓库外的绝对路径会失败**（[pitfalls](../../../docs/reference/pitfalls.md) 记过），不是随手挑的。

### 2. `tools/devpanel/`（新目录，零依赖）

| 文件 | 作用 |
| --- | --- |
| `server.mjs` | Node 内置模块写的 HTTP + SSE 服务，只监听 `127.0.0.1`（默认 7788，被占自动往后挪 12 个端口） |
| `index.html` | 单页界面：左栏按钮、上方 240×320 实时画面（1:1 + `image-rendering: pixelated`）、下方滚动日志 |
| `start.cmd` | 双击启动（纯 ASCII —— cmd 按 OEM 代码页读脚本，中文会乱码） |
| `README.md` | 每个按钮背后是哪条命令、预览怎么来的、它做不到什么 |

按钮 → 命令的映射（面板**不重写任何逻辑**，只调已有脚本）：

| 按钮 | 实际执行 |
| --- | --- |
| ▶ 运行 / ■ 停止 | `embark_host_ui --screen <屏> --shot-live build/devpanel-live.bmp --shot-live-ms 400` |
| 构建 | `cmake -S . -B build` → `cmake --build build --target embark_host_ui`（先关设备窗口，否则 exe 被占用写不进去） |
| 重新导出屏 | `pwsh -NoProfile -File tools/aidogui/export.ps1`，成功后接着构建 |
| 跑五条验收 | 五条验收故事 + `embark_host_tour --delay 40`，逐条报退出码，末尾汇总「验收全绿 N 项」 |
| 加一个 App | `node tools/new_app.mjs <名> [--title/--icon] --dry-run` / `--apply`；`--apply` 成功后自动构建 |
| HTML 草稿 → 屏 | 浏览器上传 .html → 写临时文件 → `node tools/aidogui/html_to_aid.mjs --html <临时> --screen <屏> --apply` → 删临时文件 |

另外两条：`/doc/<名字>.md` 把 `docs/guides/` 下的文档当纯文本给面板的「看文档」链接；
任务同一时刻只许跑一个（两个 `cmake` 抢 `build/` 是自找麻烦），第二个点击直接回一句「等它结束」。

### 3. 文档

- `docs/guides/tools.md`：§0 一览表加一行、§1 开发用开关补 `--shot-live`、新增 **§7 开发台**（原 §7 顺延为 §8）
- `docs/guides/daily-loop.md` §0：加一句「不想敲命令？双击 `tools/devpanel/start.cmd`」
- `README.md` 30 秒上手：加一条 bullet
- `docs/index.md`：三步说明里加一句 + tools 行摘要
- `.gitignore`：`tools/devpanel/.upload-*.html`（用完即删，留个兜底）

## 票面格式

    本票为新建票，无原始票面；上面「为什么」里的用户原话就是立项依据。

## 偏离与判断

1. **我原本要问用户三条（形态 / 范围 / 看画面方式）**，用户回「可以，按照你的推荐，不要在桌面上创建，在工作目录下创建一个新目录存放即可」⇒ 形态=本地网页面板、范围=含加 App 与 HTML 转屏、画面=按需抓图；**不做桌面快捷方式**，启动器放 `tools/devpanel/start.cmd`。
2. **没有为「看实时画面」新起一个截图进程**：改成宿主加 `--shot-live`。理由是只有同一个进程才拥有用户点击后的状态。
3. **`--shot-live` 写失败只告警不退出**（`--screenshot` 那条路径是失败即 `exit 1`）：预览偶发缺一张不该杀掉用户的窗口。

## 验收（逐条实跑）

| 验收项 | 结果 |
| --- | --- |
| 起服务 + `/api/state` 返回 App 与屏列表 | ✅ `{"apps":[…6个],"screens":[…6个],"exe":true,…}` |
| 点运行 → 设备窗口起来 + 预览刷新 | ✅ `POST /api/run {screen:clock}` 后 3 秒 `GET /api/live.bmp` → `200 image/bmp 307322 字节`（与 `--screenshot` 产物同尺寸） |
| 点停止 | ✅ 状态回 `running:false`，无残留 `embark_host_ui.exe` |
| 构建 | ✅ 日志 `cmake -S . -B build` → `cmake --build build --target embark_host_ui` → 退出码 0「构建好了」 |
| 跑五条验收 | ✅ 面板跑完汇总「验收全绿：6 项都通过」（五条故事 + tour 退出码全 0） |
| 加 App 预览（dry-run） | ✅ 打印 2 个文件 + 5 处登记，`git status` 无新增 |
| 中文标题经 JSON 上传 | ✅ `--title 探针` 原样到达脚本（早先用 curl 命令行测出的乱码是 shell 编码问题，不是服务端） |
| `/doc/daily-loop.md` | ✅ `200 text/plain; charset=utf-8` |
| 既有六条验收故事 | ✅ 全 exit 0；`ctest` 2/2 |
| `node tools/check_docs.mjs` | 见收尾记录 |

## 没做（有意）

- **不做 C++ 热重载**（06 票已排除）：改 `.cpp` 一律重新构建。
- **不做真机烧录**：`idf.py` 留在命令行。
- **不做鉴权 / 不监听 0.0.0.0**：本机开发工具，只听 `127.0.0.1`；要给局域网用再谈。
- **不做设计器替代**：新建屏幕、拖控件仍在 AiDoGUI 里（或走 HTML 草稿那条路）。
