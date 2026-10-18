# 06 · 开发台：屏级热重载 + 一键构建运行

Status: open
Blocked by: 04

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
