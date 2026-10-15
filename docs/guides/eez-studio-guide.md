# EEZ Studio 一条龙

> ⚠️ **已废弃（EEZ 时代，见 [ADR 0011](../adr/0011-aidogui-ui-source.md)）**：界面来源已换成
> AiDoGUI Designer，本页只作历史记录保留。当前口径见 [new-app-guide.md](new-app-guide.md)
> 与 [app/aidogui_ui/README.md](../../app/aidogui_ui/README.md)；EEZ 链路（`app/eez_ui/`、
> `third_party/eez-framework`）在迁移第二步退役。

用 EEZ Studio + EEZ Flow 画界面，生成的 LVGL 代码入库、应用照常注册。
**App 侧怎么接接口 → 先读 [eez-ui-manual.md](eez-ui-manual.md)（用户手册）**；
本页只讲 Studio 侧的安装、建工程与导出。
决策记录见 [adr/0008-eez-studio-adapter.md](../adr/0008-eez-studio-adapter.md)。

## 背景

Embark 的 UI 现全部由 EEZ 提供（手绘 LVGL 已退役）：EEZ Studio（编辑器）+ 
EEZ Flow（生成代码）经 eez-framework（MIT，submodule）驱动 LVGL 8.3.11。

## 安装与导出

1. 装 EEZ Studio（官网下载，Electron 应用）。
2. 打开 Embark 的 EEZ 源工程 `app/eez_ui/embark.eez-project`（**入库**：它是
   Studio 二进制但体积小（本工程约 23 KB），与导出产物 `app/eez_ui/src/ui/` 一起提交；
   改完别忘导出 + 重新 cmake 配置）；画布尺寸 240×320（宿主竖屏）。
3. 加页面（**屏名 == 对应 App 名**：主屏 = launcher、时钟 = clock……）、控件的交互、
   Flow 全局变量（命名 `<app名>_<字段>`，如 `launcher_tap_count`）。
4. **导出代码到 `app/eez_ui/src/ui/`**（15 个生成文件：actions.h / eez-flow.{cpp,h} / 
   fonts.h / images.{c,h} / screens.{c,h} / structs.h / styles.{c,h} / ui.{c,h} / vars.h）。
   **外加 `ui_font_sim.c`**（第 16 个，见下节「字体文件」）。
5. 重新 cmake 配置 + 构建：`cmake -S . -B build && cmake --build build`。
   配置日志会打印 `EEZ 屏表：N 个` 与 `EEZ 变量：N 个`（构建期从生成代码解析，
   加屏/加变量不用改 C++、不用动 CMakeLists）。

### 字体文件 `ui_font_sim.c`（2026-10-12 踩坑记录）

`screens.c` 里 `&ui_font_sim` 是**外部符号**，缺了它链接直接失败：
`undefined reference to 'ui_font_sim'`。这个文件由 Studio 的字体资源（`fonts[0]`，
name=sim，源字体 simhei.ttf）经 lv_font_conv 生成，**但两种"构建"的行为不同**：

| 入口 | 行为 | 会写 `ui_font_sim.c` 吗 |
| --- | --- | --- |
| Studio 里 Export / Build（`buildProject(store, "buildFiles")`） | 走 `copyFontFiles()` | **会** |
| CLI `EEZ Studio.exe --build-project <工程>` | 只走 `t.build()` → `"buildFiles"` 之外的分支 | **不会**，且会把 manifest 里的 `ui_font_sim.c` 当孤儿删掉 |

而且 `saveManifest/deleteOrphanedFiles` 是成对的：只要 manifest（`.eez-project-build`）
里出现过 `ui_font_sim.c`，下一次 CLI 构建就会**把它删掉**（日志里的
"Deleted orphaned file: ui_font_sim.c"）。

**补字流程**（改 `fonts[0].lvglSymbols` 之后必须走这条）：
```
① Studio 里改字体资源的字符集（或 eez_update 写 lvglSymbols）
② 让内存里的字体缓存失效：f._lvglFontDefinition = undefined;
   await f.buildLvglFontDefinition(f._lvglExtractFontParams);   // 重跑 lv_font_conv
③ 用 buildFiles 模式落盘：await bb.build(store, "buildFiles");
   （bb = require("project-editor/build/build.js")）
④ cmake --build build
```
**别用 CLI `--build-project` 代替 ③** —— 它不生成字体，还会顺手删掉已有的那份。
工程设置 `cacheFonts: false`，所以 ② 每次都会真的重跑 lv_font_conv（无缓存命中）。

## Studio 侧注意事项

- **按钮跳转一律用 SetPage → 目标屏**（非栈式导航，不要用「回上一屏」——屏栈恒空，
  点了没反应，见手册第 6 节）。
- Flow 全局变量 = UI 显示数据接口：控件属性里绑变量，生成代码每帧 `tick_screen_*()`
  把变量值刷进控件（App 侧 `eez_ui_bridge_tick()` 驱动）。
- 生成文件是**导出产物**：改页面去 Studio 改再导出，不手改生成文件
  （例外：屏尺寸 240×320 需要改 screens.c 两处；文案全 ASCII）。
- **滚动容器记得关掉 snap**：容器属性 `Scroll snap Y = center` 会让拖动被吸附限制在
  ±1 个吸附点内，列表滚不到底（launcher 的 apps_list 内容 387 高、视口 195 高，
  「闹钟」行在屏外——2026-10-12 合成点击打不中就是这个原因）。列表类容器用
  `none`。
- **合成点击不要硬编码坐标**：屏幕会随工程重画而移动。宿主的 `embark_host_ui` /
  `embark_host_tour` 已经链上 `embark::eez_ui` 并 include 生成的 `screens.h`，
  用 `widget_center(objects.<控件>)`（内部 `lv_obj_get_coords`）取真实中心、
  用 `lv_obj_scroll_to_view` 先把屏外的行滚进视口再点。加屏/挪控件后验收自动跟着走。
- CI 格式门禁已排除 `app/eez_ui/`；编译用 `-w`（生成代码警告不归零）。
- 缺字审计（issue 17）会把 `app/eez_ui/` 的文案纳入扫描范围。

### 屏生命周期（内存）

工程已开启 **Screens lifetime support**（Settings → Build 勾选），并按页设置了生命周期：

- **launcher**（启动屏）：General 里 **createAtStart 开**（默认）、**Delete on unload 关** → 常驻；
- **clock 等其余屏**：**createAtStart 关**、**Delete on unload 开** → 首次进入才建、离开即回收
  （object 表清零 + Flow 状态释放），下次进入再重建。

新页默认 createAtStart=true、Delete on unload=false；**除启动屏外务必改成上述组合**，
否则「启动全建」会按屏累积内存（空壳实测 ≈1 KB/屏，真实控件设计可到几十 KB）。
导出后重新 cmake 配置 + 构建（配置日志仍打印屏表/变量表），验收走
`embark_host_tour`（含「离开即回收、回来即重建」断言）。

## 应用侧怎么接（摘要，细节见手册）

App 只需在 4 个钩子里调用桥接口（`app/common/eez_ui_bridge.h`，详见用户手册）：

```cpp
onCreate        -> eez_ui_bridge_ensure_init();        // 幂等启动生成代码
onEnter/onResume-> eez_ui_bridge_enter_app_screen(name());  // 挂自己的屏
onForegroundTick-> eez_ui_bridge_set_var_*(...); eez_ui_bridge_tick();  // 推数据+推进 Flow
```

主屏（launcher）额外在 onCreate 里 `eez_ui_nav_attach(fw)`：EEZ 里 SetPage 切屏
= 框架 `request_switch` 切到同名 App。
