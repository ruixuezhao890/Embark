# 15 开发台二期：真机页签 + 换界面工程 + 退 App

Status: superseded（2026-10-23，被 [17-app-console.md](17-app-console.md) 取代：真机页签撤销，烧录 / 串口回命令行）· Blocked by: —（14 票已落地）

## 为什么

14 票把宿主侧变成了「点按钮」，但它只管**宿主**：改完界面还得开命令行烧真机、`new_app.mjs` 只能加不能退、
界面工程写死 `app/aidogui_ui/screen.aid`。用户在 m04263/m04286 追问的三件事：

> 「烧录真机的也可以这么用吗？」「能增加能不能减少呢？」「万一用户更换了 screen.aid 呢是吧也要能做选择吧」

## 做什么

| # | 能力 | 落点 |
| --- | --- | --- |
| ① | 真机：构建 / 烧录 / 看串口 | 新脚本 `tools/esp/idf.ps1`（注入 IDF 环境后转调 idf.py）、`tools/esp/serial.ps1`（纯文本串口）；面板「真机」组 |
| ② | 换界面工程 | 面板「界面工程」下拉（扫仓库内 `*.aid`）+ 打开目录 / 用设计器打开；选择记在 `tools/devpanel/.state.json` |
| ③ | 退一个 App | `new_app.mjs --delete`（撤 5 行登记 + 删目录）+ 面板「退掉一个 App」 |
| ④ | 记住选择 | localStorage（屏 / 端口 / App 名）+ 服务端 `.state.json`（.aid / 端口） |
| ⑤ | 看得见状态 | 状态条显示当前界面工程、前台 App（从宿主日志的 `App X onEnter/Resume` 推）、真机串口状态 |

## 与票面的分歧

1. **`.aid` 选择没有做「多工程并行」**：换的只是**源文件**，导出目录恒为 `app/aidogui_ui/export/`（构建只认那里）。
   并行多工程要先解决「导出到不同目录 + 构建期表从哪读」，那是 04 票的地盘，不在这一票。
2. **真机没有画面回传**：串口传一帧 240×320 要好几秒，做成实时图会把串口占满，直接不做（面板 hint 写明「画面看板子本身」）。
3. **烧录是长驻子进程**：`idf.py flash` 完会自己 reset，但串口监视是独立子进程，要能一键停 —— 所以面板有「停串口」，
   且烧录前会自动 `stopSerial()`（串口是独占的，不先停会 Access denied）。

## 实现期的坑（后人别重犯）

1. **IDF 环境必须自己注入**：直接跑 `export.ps1` 会按 PATH 里的 Python 3.14 去算命名的 venv 名而失败。
   `idf.ps1` 里写死 `IDF_TOOLS_PATH` / `IDF_PYTHON_ENV_PATH`（idf5.4_py3.11_env）、`PYTHONUTF8=1`，
   PATH 由 `idf_tools.py export --format key-value` 解析（不碰 `activate.py`：本机 `.py` 关联到 node.exe）。
2. **新 build 目录要先 `set-target esp32s3`**：非交互环境下 `idf.py build` 会卡在「选 target」的提示上。
3. **串口别用 `idf.py monitor`**：它发 ANSI、等键盘输入，塞进网页日志全是乱码 → 自己起了 `serial.ps1`（`DtrEnable/RtsEnable=false`，打开不复位板子）。
4. **`AID_DEFAULT` 必须正斜杠**：`listAids()` 返回正斜杠，`path.join` 出来的反斜杠和下拉里的值对不上，选中态会空。
5. **`launcher` 不许删**：删了就回不到桌面，`validate()` 里直接拒。
6. **撤登记要按整行精确比对**：猜不得 —— 有人手工改过那一行就报错让人看，比「大概删掉一行」安全。

## 验收实测（2026-10-18）

| 项 | 结果 |
| --- | --- |
| `node --check` server.mjs / new_app.mjs | 都 OK |
| `/api/state` | `aid: app/aidogui_ui/screen.aid`、`aids` 1 份、`esp` 三项（running/port/lastPort）都在 |
| `/api/ports` | 现枚举得到 `COM9` 与 `COM3`（不是写死） |
| `/api/aid` 正常 / 坏的 | 正常返回 ok:true；坏的返回 ok:false + 「没有这份 .aid：nope.aid」 |
| `/api/deleteapp zzz` | 日志打「错误：app/zzz 不存在 —— 没东西可删」，退出码 1；连点第二个任务被 busy 守卫拦下 |
| **真机烧录端到端** | 面板点「烧到板子上」→ 增量编 256 目标 → `Writing at … (100%)` / `Hash of data verified.` / `Leaving... Hard resetting via RTS pin...` → 自动挂串口 → 串口日志出现 `Embark 0.1.0` / `DisplayInfo {240,320}` / `SD16G 3504 MB` / `aiDoGUI … 66176 … 196608 … 147456`（03 票公式配额在真机生效） |
| `/api/esp stopserial` | esp 回到 running:false、port:null，lastPort 仍记着 COM3 |
| 退 App 往返 | `my_app --apply` 建 → `--delete --apply` 撤：`cmake/embark_sources.cmake` 与 `aidogui_ui_hooks.h` **零残留 diff** |
| 面板前端 | 内联脚本 `new Function` 编译 OK（9,229 字符）、34 个 `getElementById` 全存在、19 个按钮全绑上 |
| 宿主回归 | build exit 0 · ctest 2/2 · 五条故事（`--ui` / `--click` / `--ui-nav` / `--ui-weather` / `--ui-launcher-pages`）全 exit 0 · `embark_host_tour --delay 40` 17 通过 0 失败 |
| 文档自检 | `node tools/check_docs.mjs` exit 0 |

## 没做（有意）

1. **不打包成单文件 exe**：现在双击 `tools/devpanel/start.cmd` 就是「一个入口」。打包（Node SEA）只能省掉 Node 运行时，
   而编固件/烧板子仍然必须有 cmake + ESP-IDF + Python —— 收益不在体积，在于给不装环境的人用，而那种人根本跑不起来这一套。
   真要做，另立票（先用 SEA 或 `pkg` 试，注意 `server.mjs` 里 `spawn` 的相对路径要改成 exe 旁边的资源目录）。
2. **不做真机画面回传**（理由见上）。
3. **不做 WiFi 烧录 / OTA**：`INetwork` 只到 HTTP，OTA 是另一票。
4. **不加鉴权**：只 `listen('127.0.0.1')`，同机同用户才连得上；要跨机用必须自带反代 + 鉴权。
5. **不改 `.aid` 里的屏**：加 App 时不会自动建同名空屏（C 项先验：`.aid` 里插空屏结构上可行，但没验证设计器能否正常导出）。
