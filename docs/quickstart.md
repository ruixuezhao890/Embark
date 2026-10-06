# 5 分钟快速开始（Embark + EEZ 屏）

> 目标：从零跑起宿主 UI，看到 **EEZ Studio 导出的屏**（launcher → clock），并加一个自己的 App。
> 前提：已 `cmake --build build` 成功（构建环境见根 [README](../README.md)「宿主构建」）。

## 第 1 分钟：跑起来

```sh
./build/platform/host/embark_host_ui.exe   # Windows；其他平台去掉 .exe
```

- 窗口里就是 EEZ 屏（launcher 主屏，240×320），**不是手绘代码**：界面全部来自
  `app/eez_ui/src/ui/` 的生成代码，App 只是薄壳（业务逻辑），不碰 LVGL。
- 点 launcher 屏上的按钮切到 clock 屏 —— 这是 EEZ 按钮的 SetPage 动作 + 屏观察者
  把切屏翻成 `request_switch("同名 App")`，链路在 `app/common/eez_ui_nav.cpp`。

**完成标志**：窗口出现 launcher 屏，点按钮能切到 clock 屏。

## 第 2 分钟：最短验收

```sh
./build/platform/host/embark_host_ui.exe --click   # 退出码 0 = launcher ⇄ clock 往返成功
./build/platform/host/embark_host_tour.exe         # 12 项自检全过
./build/platform/host/embark_host_ui.exe --eez     # EEZ 屏按钮往返 + 变量桥自检
```

## 第 3–5 分钟：加一个自己的 App（薄壳 + EEZ 屏）

1. 复制 `app/clock/` → `app/my_app/`，类名改 `MyApp`、`name()` 返回 `"my_app"`；
2. `app/CMakeLists.txt`：`embark_demo_apps` 源列表加 `my_app/my_app.cpp`；
3. `platform/host/ui_demo.cpp`：`EMBARK_APP_TABLE(...)` 加 `embark::demo::MyApp`
   （`LauncherApp` 之后，首位必须是启动器）；
4. `cmake --build build` 重新构建，`--frames 90 --click` 验收仍绿；
5. 想要界面：**EEZ Studio** 画一张同名屏（屏名 == App 名）导出到 `app/eez_ui/src/ui/`，
   重新 `cmake -B build` 即可生效（缺屏也能编译运行，只告警不崩）。

> 详细教程（30 分钟端到端）见 [new-app-guide.md](new-app-guide.md)；
> 桥接口手册见 [eez-ui-manual.md](eez-ui-manual.md)；
> EEZ Studio 操作见 [eez-studio-guide.md](eez-studio-guide.md)。

## 常见问题速查

| 现象 | 原因 / 处理 |
| --- | --- |
| 屏上按钮点了没反应 | 屏名 ≠ App 名；或按钮用了「回上一屏」（空栈 no-op）——改成 SetPage 到目标屏 |
| 改了屏 / 变量不生效 | 屏表 / 变量表是**配置期**生成的：改动后要重新 `cmake -B build` |
| 界面文字是方框 | EEZ 屏用 LVGL 默认字体（Montserrat），无中文——文案用 ASCII |
| 想给自己的 App 加数据上屏 | Flow 全局变量：Studio 声明 + 绑定控件，`eez_ui_bridge_set_var_*` 推值 |
| 真机上没有界面 | 真机 EEZ 接入待办（`EMBARK_EEZ_UI_BRIDGE` 未定义 = 桥调用 no-op），见根 README 状态 |
