# EEZ Studio 一条龙

用 EEZ Studio + EEZ Flow 画界面，生成的 LVGL 代码入库、应用照常注册。
**App 侧怎么接接口 → 先读 [eez-ui-manual.md](eez-ui-manual.md)（用户手册）**；
本页只讲 Studio 侧的安装、建工程与导出。
决策记录见 [adr/0008-eez-studio-adapter.md](adr/0008-eez-studio-adapter.md)。

## 背景

Embark 的 UI 现全部由 EEZ 提供（手绘 LVGL 已退役）：EEZ Studio（编辑器）+ 
EEZ Flow（生成代码）经 eez-framework（MIT，submodule）驱动 LVGL 8.3.11。

## 安装与导出

1. 装 EEZ Studio（官网下载，Electron 应用）。
2. 打开仓库内的工程文件（`app/eez_ui/` 下的 `.eez-project`，随仓库入库）；
   画布尺寸 240×320（宿主竖屏）。
3. 加页面（**屏名 == 对应 App 名**：主屏 = launcher、时钟 = clock……）、控件的交互、
   Flow 全局变量（命名 `<app名>_<字段>`，如 `launcher_tap_count`）。
4. **导出代码到 `app/eez_ui/src/ui/`**（15 个生成文件：actions.h / eez-flow.{cpp,h} / 
   fonts.h / images.{c,h} / screens.{c,h} / structs.h / styles.{c,h} / ui.{c,h} / vars.h）。
5. 重新 cmake 配置 + 构建：`cmake -S . -B build && cmake --build build`。
   配置日志会打印 `EEZ 屏表：N 个` 与 `EEZ 变量：N 个`（构建期从生成代码解析，
   加屏/加变量不用改 C++、不用动 CMakeLists）。

## Studio 侧注意事项

- **按钮跳转一律用 SetPage → 目标屏**（非栈式导航，不要用「回上一屏」——屏栈恒空，
  点了没反应，见手册第 6 节）。
- Flow 全局变量 = UI 显示数据接口：控件属性里绑变量，生成代码每帧 `tick_screen_*()`
  把变量值刷进控件（App 侧 `eez_ui_bridge_tick()` 驱动）。
- 生成文件是**导出产物**：改页面去 Studio 改再导出，不手改生成文件
  （例外：屏尺寸 240×320 需要改 screens.c 两处；文案全 ASCII）。
- CI 格式门禁已排除 `app/eez_ui/`；编译用 `-w`（生成代码警告不归零）。
- 缺字审计（issue 17）会把 `app/eez_ui/` 的文案纳入扫描范围。

## 应用侧怎么接（摘要，细节见手册）

App 只需在 4 个钩子里调用桥接口（`app/eez_ui_bridge.h`，详见用户手册）：

```cpp
onCreate        -> eez_ui_bridge_ensure_init();        // 幂等启动生成代码
onEnter/onResume-> eez_ui_bridge_enter_app_screen(name());  // 挂自己的屏
onForegroundTick-> eez_ui_bridge_set_var_*(...); eez_ui_bridge_tick();  // 推数据+推进 Flow
```

主屏（launcher）额外在 onCreate 里 `eez_ui_nav_attach(fw)`：EEZ 里 SetPage 切屏
= 框架 `request_switch` 切到同名 App。
