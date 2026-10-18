# 5 分钟快速开始（Embark + AiDoGUI 屏）

> 目标：从零跑起宿主 UI，看到 **AiDoGUI Designer 导出的屏**（launcher → clock），并加一个自己的 App。
> 前提：已 `cmake --build build` 成功。**环境从零开始**（装工具 → `git clone --recursive` → 配置构建）
> 见 [../index.md](../index.md)「第一次来：三步」与根 [README.md](../../README.md) 的宿主构建一节。

## 第 1 分钟：跑起来

```sh
./build/platform/host/embark_host_ui.exe   # Windows；其他平台去掉 .exe
```

- 窗口里就是 AiDoGUI 屏（launcher 主屏，240×320），**不是手绘代码**：界面全部来自
  `app/aidogui_ui/export/` 的生成代码，App 只是薄壳（业务逻辑），不碰 LVGL。
- 点 launcher 屏上的时钟卡切到 clock 屏 —— 这是设计器里给控件挂的「调用函数」事件
  （`embark_ui_open_clock`）→ `request_switch("同名 App")`，链路在 `app/common/aidogui_ui_nav.cpp`。
- **不带参数 = 一直跑到关窗**（模拟单片机常驻）；想自己写 main（不退出、像真机烧录后
  一直跑）用 `embark_host_user`，骨架讲解见 [../../platform/host/user_main.cpp](../../platform/host/user_main.cpp) 的头部注释。

**完成标志**：窗口出现 launcher 屏，点按钮能切到 clock 屏。

> **改一行就想看见结果？** `pwsh -NoProfile -File tools/aidogui/dev.ps1 -Screen clock` ——
> 导出 → 构建 → 起窗口；之后改 C++ 它自动重建重启，`export/` 里的东西窗口内热重载
> （`--watch`），细节见 [tools.md](tools.md) §2。
## 第 2 分钟：最短验收

```sh
./build/platform/host/embark_host_ui.exe --click --delay 40   # 退出码 0 = launcher ⇄ clock 往返成功
./build/platform/host/embark_host_ui.exe --ui --delay 40      # 界面冒烟验收：屏表 / 控件表 / 字体 / 时钟文案
./build/platform/host/embark_host_tour.exe         # 一条用例看完整系统（17 项自检）
```

## 第 3–5 分钟：加一个自己的 App（薄壳 + AiDoGUI 屏）

```sh
node tools/new_app.mjs my_app --title 我的应用             # 只看计划：写哪两个文件、在哪 5 行插什么
node tools/new_app.mjs my_app --title 我的应用 --apply     # 真写 + 登记，然后构建
```

它读 `app/music/`（121 行的最薄样板：薄壳 + 同名屏 + 值重推）当模板做替换 —— **脚本不内嵌模板**，
所以模板只有一份真相，不会出现「脚本模板与真 App 走偏」。落盘后 `cmake -S . -B build` 再
`cmake --build build`，`--ui --frames 40 --delay 40` 应该仍绿。

**手工路线**（想理解结构就这么做）：照 `app/music/` 抄两个文件，然后**登记 5 行 / 3 个文件** ——
`cmake/embark_sources.cmake` 的两份清单、`platform/host/ui_demo.cpp` 的 include 与 App 表、
`app/common/aidogui_ui_hooks.h` 的宏。逐条清单与理由见 [new-app-guide.md](new-app-guide.md) §2，
漏一处就编不过（漏 include 的报错是 `'MyApp' is not a member of 'embark::demo'`）。

想要界面：**AiDoGUI Designer** 画一张同名屏（屏名 == App 名）导出到 `app/aidogui_ui/export/`，
重新 `cmake -B build` 即可生效（缺屏也能编译运行，只告警不崩）。

> 详细教程（30 分钟端到端）见 [new-app-guide.md](new-app-guide.md)；
> **之后每天怎么干活**（改哪儿、跑哪条验收、提交前四条自检）见 [daily-loop.md](daily-loop.md)；
> 界面链路与重导出见 [app/aidogui_ui/README.md](../../app/aidogui_ui/README.md)；
> 桥接口见 [../reference/hooks-and-api.md](../reference/hooks-and-api.md) §5。

## 常见问题速查

| 现象 | 原因 / 处理 |
| --- | --- |
| 屏上按钮点了没反应 | 屏名 ≠ App 名；或控件挂的是 CHANGE_SCREEN 动作（会绕过桥的按需 init/destroy）——改成「调用函数」`embark_ui_open_<App 名>` |
| 改了屏不生效 | 屏表是**配置期**生成的：改动后要重新 `cmake -B build`（或用 `tools/aidogui/dev.ps1`，它把导出 + 配置 + 构建串成一条） |
| 界面文字是方框 | 卡上的 `ui_heiti.ttf` 缺字——跑 `node tools/font/audit_glyphs.mjs` 看缺哪个字（ADR 0017） |
| 想给自己的 App 加数据上屏 | 按控件名拿 `lv_obj_t*` 后直接写：`aidogui_ui_bridge_widget("my_app", "Label_Count")`；控件值会随屏重建丢失，所以用 `ui_backend_bind_repaint` 声明重推（ADR 0015）|
| 真机上没有界面 | **不是这个原因** —— 真机也定义了 `EMBARK_AIDOGUI_UI=1`（`platform/esp32/project/components/embark/CMakeLists.txt`）。先看串口有没有 `没有屏 <名>` 告警：真机没界面几乎都是屏名 ≠ App 名，或改了 `export/` 却没重新配置构建 |
