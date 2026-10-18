# 06 · 开发台：屏级热重载 + 一键构建运行

Status: done（2026-10-18）
Blocked by: 04（已解锁）

## 为什么

Q3=(B)。用户要"像在电脑上开发应用一样"的循环：改一行 → 看到结果，而不是"改 → 构建 → 手动点开屏 → 手动导航到目标屏"。

不做 C++ 完整热重载（Q3 排除 (C)）——改 `.cpp` 一律重新构建。

## 已有的地基（不用重造）

- `platform/host/ui_demo.cpp:595` 已有屏级重新装载：`aidogui_ui_bridge_load_screen("launcher")` —— 销毁并重建当前屏
- `tools/aidogui/export.ps1`（164 行）已能无头重导出（`ELECTRON_RUN_AS_NODE=1 AiDoGUIDesigner.exe`）
- `embark_host_ui` 已有 14 个开关（`--frames / --scale / --delay / --screenshot / --shot-frame / --quit-at / --click / --drag / --launch / --ui / --ui-nav / --ui-weather / --ui-launcher-pages / --help`；`--eez` 已随 01 号票删除）
- **没有任何文件监视/热重载实现**（grep `watch|reload|hot|inotify|ReadDirectoryChanges` 在 `platform/host/` 只命中 `feed_watchdog` 与 `--screenshot`）

## 做什么

**① 新增 `--watch` 开关**：监视 `app/aidogui_ui/export/` 的时间戳，变了就 `aidogui_ui_bridge_load_screen(当前屏名)`。Windows 上先做最朴素的每 200 ms 轮询 `GetFileAttributesEx`（能跑再优化成 `ReadDirectoryChangesW`）。

**② 新增 `--screen <名>` 开关**：启动直接进某块屏，省掉手动导航。这一条比热重载更省时间。

**③ 一条命令的循环** `tools/aidogui/dev.ps1`：跑 export → 若成功则 build → 若成功则起 `embark_host_ui --screen <名> --watch`。

## 验收

- 跑 `dev.ps1`，改 `.aid` 里某个 label 的文字并重新导出 → 窗口里的文字变了，**不重启进程**
- 改 `.cpp` → 脚本重新构建并重启
- `--watch` 缺省关闭，既有四条验收命令行为不变

---

## 收尾记录（2026-10-18）

### 落地的东西

| 文件 | 变更 |
| --- | --- |
| `platform/host/ui_demo.cpp` | 新增 `--screen NAME` / `--watch`；新增 `ExportStamp` / `export_dir_stamp()` / `reload_current_aidogui_screen()` 三个 helper；主循环里 200 ms 轮询 |
| `tools/aidogui/dev.ps1` | 新建（154 行）：导出 → 配置 + 构建 `embark_host_ui` → 起窗口 → 盯源码变了就停窗口/重建/再起 |
| `docs/guides/tools.md` | 一览表加 `dev.ps1` 行；新增 §2「日常循环」；`--screen` / `--watch` 进开发用开关；原 §2–§5 顺延为 §3–§6 |
| `docs/guides/quickstart.md` | 第 1 分钟加 dev.ps1 提示；FAQ「改了屏不生效」指到 dev.ps1 |
| `app/aidogui_ui/README.md` | 「无头重导出」节后加 dev 循环命令与能力边界 |

### 与票面的偏离（都是实现时才看清的）

1. **热重载不能只调 `load_screen(当前屏)`**。票面①写的就是那句，但 `app/common/aidogui_ui_bridge.cpp:116`
   对「已经是当前屏」直接 `return true` 不重建 —— 那样写是空操作。实际必须
   `unload_current()` → `load_screen(同名)`：先卸（显示器交给空屏、不留悬空 `act_scr`），
   再装（观察者重挂点击绑定 + 让 App 重推值）。
2. **`--watch` 盯的是「内容指纹」而不是目录 mtime**。目录自己的 mtime 只在新建/删除文件时变，
   **覆盖写已有文件不改父目录 mtime**（设计器导出向导是原地覆盖，只有 `export.ps1` 才整目录重建）。
   所以 `export_dir_stamp()` 递归取「最新文件时间 + 文件数」。票面写的 `GetFileAttributesEx` 方向对、粒度错。
3. **`dev.ps1` 在 export 之后必须显式 `cmake -S . -B build`**。`app/aidogui_ui/CMakeLists.txt` 用
   `file(GLOB ... CONFIGURE_DEPENDS)`，只在**文件列表**变化时重跑 configure；控件改名（文件名不变）
   不触发，而屏表/控件表/字号表是 configure 期由 `cmake/embark_aidogui_tables.cmake` 生成的。
4. **`--watch` 的能力边界写进了头注释与文档**：生成的是 C 代码、编进二进制，所以 `export/` 里能
   当场热重载的只有运行期才从 `S:` 盘读的东西（图片 BIN）以及「屏还装得出来」这件事；文案与几何
   必须重新导出 + 重新构建。不写清会让人以为热重载坏了。
5. 票面说 `--screen` 「启动直接进某块屏」——实现走的是 `framework.request_switch(name)`（屏名 == App 名），
   而不是直接 `load_screen`：App 的 `onEnter` / `onResume` 照常跑、界面照常一步步装出来。
   名字不存在时打印可选的 App 列表并 `exit 1`。

### 实现期的一个坑（值得记）

`std::filesystem::last_write_time` 在 MinGW（libstdc++）下返回的 `file_time_type` **纪元与 Unix 不同**，
实测 2026 年的时间戳是**负数**（`-4645346659000000000`）。首版把 `newest_ticks` 初值写成 `0`，于是
「最新时间」永远停在 0、永远判「没变」——`--watch` 静默失效。改成
`std::numeric_limits<std::int64_t>::lowest()` 才对。

### 验收实测

| 项 | 结果 |
| --- | --- |
| `--screen clock --frames 30 --delay 10` | exit 0；`前台 clock；switches=1`，日志含「屏 launcher 已释放 / 屏 clock 已装载（27 个控件）」 |
| `--screen nope --frames 5` | exit 1；`--screen：没有名叫 nope 的 App。可选：launcher / clock / settings / weather / music / album` |
| `--screen clock --watch --quit-at 400 --delay 10` + 中途 `touch export/screens/ui_clock.h` | exit 0；日志 `--watch：export/ 变了（25 个文件），重建当前屏` → `屏 clock 已释放（显示器交给空屏）` → `屏 clock 已装载（27 个控件）` → `值重推：屏 clock 已装载` |
| `dev.ps1 -NoRun -SkipExport` | exit 0，导出跳过、构建 `embark_host_ui` 成功 |
| `dev.ps1 -SkipExport -Screen clock` + 中途 `touch app/clock/clock_app.cpp` | 日志 `源码变了 → 停窗口、重新构建、再起一个` → configure → build → 起窗口（第二轮） |
| 既有故事 5 条 | 全 exit 0：`--ui --frames 40 --delay 40`〔aidogui 验收通过〕、`--click --delay 40`、`--ui-nav --delay 40`（前台 clock，switches=3，导航命中 3）、`--ui-weather --delay 40`、`--ui-launcher-pages --delay 40` |
| `embark_host_tour --delay 40` | exit 0，17 项 `[通过]`、0 失败 |
| `cmake --build build` + `ctest` | exit 0；2/2 通过 |

### 没做（有意）

- `ReadDirectoryChangesW` / `inotify`：200 ms 轮询在本机（25 个文件）开销测不出来，先不做。
- C++ 热重载：Q3 已排除；改 `.cpp` 走整进程重启。
- 监视 `tests/` 与 `examples/`：不算「改一行看结果」的那条路，`dev.ps1` 不管它们。
