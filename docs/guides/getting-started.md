# 从这里开始：把 Embark 跑起来

> **这是唯一的入门文档。** 目标：半小时内 —— 装工具 → 编出宿主 UI → 看见 launcher 屏 → 点进 clock 屏
> → 跑一次最短验收。这条线走完，你就可以自己写 App 了。
> 卡住了再查别的：文档地图 [../index.md](../index.md)，术语 [../../GLOSSARY.md](../../GLOSSARY.md)。

## 0. 你要装什么

| 工具 | 要求 |
| --- | --- |
| Git | 任意新版本（clone 要带 `--recursive`，子模块不能漏） |
| CMake | ≥ 3.24 |
| Ninja | 任意 |
| C++17 编译器 | GCC / Clang / MSVC 任选一个 |
| SDL2 | **只在想看窗口时要**：Windows 装 `SDL2-devel-*-mingw`，Linux `sudo apt install libsdl2-dev` |

## 1. 拿代码

```sh
git clone --recursive https://github.com/ruixuezhao890/Embark.git
cd Embark
```

> 已经 clone 过但没带子模块：`git submodule update --init --recursive`。

## 2. 配置一次，然后编

```sh
cmake -G Ninja -B build        # 只有这一步要 -G；build/ 一旦生成，之后只 build
cmake --build build            # 全量一次（宿主约几十秒）
```

之后日常只编你要的那个目标：`cmake --build build --target embark_host_ui`。

## 3. 跑起来看

```sh
cd build/host
./embark_host_ui.exe           # Windows；其他平台去掉 .exe。不带参数 = 一直跑到你关窗
```

**完成标志**：出现 240×320 的竖屏窗口，里面是 launcher 主屏；点时钟卡切到 clock 屏。

- **界面不是手绘代码**：全部来自 `app/aidogui_ui/export/` 里的生成代码，App 只是薄壳（业务逻辑），不碰 LVGL。
- **无头环境也能看**：`./embark_host_ui.exe --frames 60 --screenshot probe.bmp`（写出 240×320 的 BMP）。
- **想自己写 main**：用 `embark_host_user`，骨架讲解在 [../../host/user_main.cpp](../../host/user_main.cpp) 头部注释。

## 4. 最短验收：三条命令

```sh
./embark_host_ui.exe --click --delay 40   # 退出码 0 = launcher ⇄ clock 往返成功
./embark_host_ui.exe --ui --delay 40      # 界面冒烟：屏表 / 控件表 / 字体 / 时钟文案
./embark_host_tour.exe                    # 一条用例看完整系统（17 项自检）
```

都在 `build/host/` 里。**合成输入必须带 `--delay 40`**，否则点击会在窗口就绪前发出、白丢。

## 5. 改一行就想看见

```sh
pwsh -NoProfile -File tools/aidogui/dev.ps1 -Screen clock
```

它把「导出 → 配置 → 构建 → 起窗口」串成一条：之后改 C++ 自动重编重启，`export/` 里的东西窗口内热重载，
不用反复关窗口。**加 App / 退 App 不想敲命令**：双击 `tools/appconsole/start.cmd` —— 列表 / 看计划 /
建它 / 退掉 / 打开设计器 / 导出屏 / 构建宿主，都在浏览器里点。脚本全表见 [tools.md](tools.md)。

## 6. 每天只碰这三个地方

| 想改什么 | 打开 | 改完 |
| --- | --- | --- |
| 界面（文字 / 坐标 / 颜色 / 控件） | `app/aidogui_ui/screen.aid`（设计器 GUI） | `pwsh -File tools/aidogui/export.ps1`，再编 |
| 界面（让 AI 画，绕开 GUI） | 一份 HTML 草稿 | `node tools/aidogui/html_to_aid.mjs --html x.html --screen clock --apply`，再导出 |
| App 逻辑 | `app/music/music_app.cpp` 这类薄壳（`app/<名>/`） | 直接编，不用导出 |

`app/aidogui_ui/export/` 是导出产物，**别手改** —— 下次导出就覆盖。

## 7. 下一步读什么

- **加一个自己的 App**（给只会 Arduino 的人：从一条命令建骨架讲到秒表完整例子）→ [new-app-guide.md](new-app-guide.md)
- **之后每天怎么干活**（改哪儿动哪个文件、跑哪几条验收、提交前自检什么）→ [daily-loop.md](daily-loop.md)
- **它到底是怎么跑起来的**（一帧怎么走、后台策略、消息）→ [../concepts/index.md](../concepts/index.md) 与
  [../concepts/app-lifecycle.md](../concepts/app-lifecycle.md)
- **写代码时查表**（钩子契约、API、容量上限、踩坑）→ [../reference/hooks-and-api.md](../reference/hooks-and-api.md)

## 8. 卡住了：先查这张表

| 现象 | 原因 / 处理 |
| --- | --- |
| 屏上按钮点了没反应 | 屏名 ≠ App 名；或控件挂的是 CHANGE_SCREEN 动作（绕过了桥的按需 init/destroy）——改成「调用函数」`embark_ui_open_<App 名>` |
| 改了屏不生效 | 屏表是**配置期**生成的：重新 `cmake -B build`（或直接用 `tools/aidogui/dev.ps1`，它把导出 + 配置 + 构建串起来） |
| 界面文字是方框 | 卡上的字库缺字——跑 `node tools/font/audit_glyphs.mjs` 看缺哪个字（见 [ADR 0017](../adr/0017-font-ttf-from-sd.md)） |
| 想给自己的 App 加数据上屏 | 按控件名取 `lv_obj_t*` 后直接写：`aidogui_ui_bridge_widget("my_app", "Label_Count")`；控件值随屏重建丢失，所以用 `ui_backend_bind_repaint` 声明重推（见 [ADR 0015](../adr/0015-app-retention-and-refresh.md)） |
| 真机上没有界面 | 真机也定义了 `EMBARK_AIDOGUI_UI=1` —— 先看串口有没有「没有屏 <名>」告警：几乎都是屏名 ≠ App 名，或改了 `export/` 却没重新配置构建 |
