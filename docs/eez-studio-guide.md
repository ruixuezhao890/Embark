# EEZ Studio 一条龙

用 EEZ Studio + EEZ Flow 画界面，生成的 LVGL 代码入库、应用照常注册。
决策记录见 [adr/0008-eez-studio-adapter.md](adr/0008-eez-studio-adapter.md)，
落地示例在 `app/eez_ui/`（16 个生成文件）+ `app/eez_demo_app.{h,cpp}`。

## 背景

Embark 的 UI 是手写 LVGL 8.3.11（见 [README](../README.md)）。
EEZ Studio 是 EEZ Flow 的配套编辑器，生成的代码经过 eez-framework（MIT，
已作为 submodule 纳入仓库）驱动 LVGL。

## 安装与使用

1. 装 EEZ Studio（官网下载，Electron 应用）。
2. 新建工程，画布尺寸设为 240×320（宿主竖屏）。
3. 加页面、控件、User Action、Flow 变量。
4. 导出代码到 `app/eez_ui/`（生成文件：actions.cpp / fonts.h / images.c /
   screens.c / screens.h / styles.c / structs.h / ui.c / vars.cpp + 图片 C 文件）。
5. `app/eez_ui/CMakeLists.txt` 里把新源加进 `embark_eez_ui`。

## 应用侧怎么接

以 `app/eez_demo_app.cpp` 为模板：

- `onCreate` 里调 `embark::demo::eez_ui_bridge_init()`（= `ui_init()`）。
- `onEnter`/`onResume` 里调 `eez_ui_bridge_load_current_screen()`（把当前
  screen 载入，和 Flow 的自动 load 动画按指针判等去重）。
- `onForegroundTick` 里调 `eez_ui_bridge_tick()`（= `ui_tick()`），
  Flow 状态机每帧推进。

User Action 和变量桥：

- User Action 落在 `app/eez_ui/actions.cpp`（Flow 里建的 action 在这里生成）。
- Flow 变量在 `app/eez_ui/vars.cpp` 生成全局量；读值从 `app/eez_ui_bridge.cpp`
  走（`eez_ui_bridge_counter()` 之类），别改生成文件。

## 注意事项

- 生成代码是**生成产物**：改页面去 Studio 改再导出，不手改生成文件
  （例外：屏尺寸 240×320 需要改 screens.c 两处；文案全 ASCII）。
- CI 格式门禁已排除 `app/eez_ui/`；编译用 `-w`（生成代码警告不归零）。
- 缺字审计（issue 17）会把 `app/eez_ui/` 的文案纳入扫描范围。
