# app/aidogui_ui —— AiDoGUI 设计工程与生成代码

这个目录里是**界面代码的唯一来源**（ADR 0011）。EEZ Studio 时代是 `app/eez_ui/`，
现在换成 AiDoGUI Designer，生成物形状相同（LVGL C 代码），所以 App 侧只换接缝
（`app/common/ui_backend.h`），三条铁律不变：

1. **屏名 == App 名**（子页 `<app名>_<编号>_sub`）——App 通过屏名找到自己的屏；
2. 生成代码**不要手改**（`export/` 全是导出产物，下一次导出会覆盖）；
3. 数据接口走**控件表按名查找**（`aidogui_ui_bridge_widget(screen, widget)`），
   App 不直接引 `ui_<屏id>_<控件名>` 全局，控件改名不会静默断链。

## 目录

```
screen.aid      AiDoGUI 设计工程（用 AiDoGUI Designer「打开工程」编辑）
previews/       设计器保存的工程预览图（文件名 = projectId）
assets/font/    字体源 ttf（导出时按 (字体, 字号) 子集化出 BIN）
export/         AiDoGUI 的原生导出目录 —— GUI 导出向导与 export.ps1 都往这里写
  ui.c ui.h ui_helpers.* ui_comm.* ui_events.h filelist.txt CMakeLists.txt
  screens/ui_<屏id>.{c,h}    每屏一个文件：控件全局 + _screen_init/_screen_destroy
  components/ui_comp*.{c,h}  自定义组件（含自定义事件号声明）
  font/*.bin                 字体 BIN（运行时 S:/fonts/*.bin）
  image/*.bin                图片 BIN（运行时 S:/images/*.bin）
CMakeLists.txt  手写的构建胶水（GLOB export/**.c + 配置期建表）
```

- `S:` 盘由 `platform/common/aidogui_res_fs` 指向本目录的 `export/`（宿主）。
  生成代码写的是 `S:/fonts/`（复数），磁盘上是 `font/`（单数）——桥做了目录名兜底，
  **GUI 导出完不用改名**。
- 导航事件是生成代码里的裸调用（`embark_ui_open_*();`），原型在
  `app/common/aidogui_ui_hooks.h`（被 `-include` 注入，见「五块屏与导航」）。
- 屏表/控件表/字体表**不入库**：`cmake/embark_aidogui_tables.cmake` 在 CMake 配置期
  解析 `export/screens/ui_*.h` 与 `export/ui.c`，写到构建目录的
  `include/embark_aidogui_ui_tables.h`。改了界面并重导出后，构建会自动重跑配置。

## 改界面（推荐：GUI 里改）

1. 打开 AiDoGUI Designer → 首页「**打开工程**」→ 选本目录的 `screen.aid`
   （设计器会在这里建 `.ai-workspace/`，已 gitignore）；
2. 改界面，`Ctrl+S` 保存；
3. 导出代码：菜单里的「导出 C 代码」向导 → **输出目录选 `<仓库>/app/aidogui_ui/export`**
   （工程 exportSettings 里已预填本机绝对路径，换机器要在向导里重选），
   图片 / 字体格式都选 **BIN**；
4. 回到终端编译验收：

```powershell
cmake --build build --target embark_host_ui
build\platform\host\embark_host_ui.exe --ui --frames 40                  # 期待〔aidogui 验收通过〕
build\platform\host\embark_host_ui.exe --ui-nav --delay 40              # 期待〔aidogui 导航验收通过〕
build\platform\host\embark_host_ui.exe --ui-weather --delay 40          # 期待〔aidogui 天气验收通过〕
build\platform\host\embark_host_ui.exe --ui-launcher-pages --delay 40   # 期待〔主屏分页验收通过〕
build\platform\host\embark_host_ui.exe --ui-nav --delay 40 `
  --shot-frame 24 .scratch\ui_nav_clock.bmp                            # 抓 clock 屏那一帧
```

> **带合成点击/手势的验收必须给 `--delay 40`（或更大）**：LVGL 约每 30 ms 读一次输入，
> 帧跑得比它快时，一帧里推的"按下 → 移动 → 抬起"会被采样进同一次读取 —— 表现是同一份脚本
> 一趟过一趟不过（点击晚十几帧生效、拖动看着没吸附）。`--ui` 没有合成输入，不用管。

## 改界面（可选：无头重导出）

没开设计器也能重导出（CI / 批处理 / AI 改完直接验）：

```powershell
pwsh -NoProfile -File tools/aidogui/export.ps1
```

它做三件事：找 AiDoGUI 安装目录 → 调 `tools/aidogui/gen_ui.cjs` 跑 AiDoGUI 自己的代码
生成器（读 `screen.aid`，BIN 格式）→ **清空并重建 `app/aidogui_ui/export/`**（只允许
这个目录，防止删错）。表不在这里生成，交给 CMake 配置期（上一节）。

设计器位置三级查找：`AIDOGUI_HOME` → 仓库根 `.aidogui-home` → `tools/aidogui/home.txt`
（本机路径，已 gitignore）。换机器时改最后一处即可。

## 六块屏与导航（第 2 步）

| 屏（id == name） | 控件 | 数据 | 回程 |
| --- | --- | --- | --- |
| `launcher` | 14（桌面：时间卡 / 天气卡 / **空磁贴带 `Band_Apps`** / **页码点行 `Dots_Row`** / 提示行） | 时间与天气卡由 `LauncherApp` 刷；**磁贴与页码点是运行时按 App 注册表建的**（`app/launcher/launcher_tiles.cpp`），每页 4 个、横向分页 + 吸附 | 磁贴点按 → 目标 App；天气卡 `Card_Weather` → weather |
| `clock` | 27（返回按钮 / 大时间 / 日期 / 真 `lv_calendar` 月历 `Cal` / 年进度 / 明日 / 本月） | `ClockApp` 每拍推 13 个文案 + 1 个条形值；月历走 `lv_calendar_set_today_date / set_showed_date / set_day_names` | `Btn_Back` → launcher |
| `weather` | 37（返回 / 城市栏 ◀ 城市 ▶ / 「现在」卡 / 5 天逐行 日期·天气字·一句话·温度） | `WeatherApp` 只在进屏/回屏/切城市时写（数据是桩，不会自己变）；城市存 `hal().storage` 的 `wx.city` | `Btn_Back` → launcher |
| `music` | 7（返回 / 标题 / 占位卡 + 两行字） | 无（占位屏，App 侧只有薄壳） | `Btn_Back` → launcher |
| `album` | 7（同上） | 无（占位屏） | `Btn_Back` → launcher |
| `settings` | 8（返回 / 标题 / 亮度卡 `Card_Bright` + 「亮度 +1」`Btn_Bright`） | `SettingsApp::bump_level()` 就地 +1（档位 0..3 循环）并广播 `BrightnessMessage`（ClockApp 收） | `Btn_Back` → launcher；`Btn_Bright` 是**逻辑动作**（`ui_backend_bind_action`），不切屏 |

### 一条不在设计器里的导航 + 逻辑动作绑定

- **主屏磁贴**：磁贴**不是设计稿里的控件** —— 设计稿只放一个空容器 `Band_Apps`，磁贴由
  `LauncherApp` 在每次屏装载后按注册表重建（顺序 = 注册顺序，跳过第 0 个 = 主屏自己），
  点按走 `aidogui_ui_nav_request_app()`。所以**新加一个 App 就自动多一个磁贴、自动能进**，
  不用动设计稿（超过 4 个自动多一页 + 多一个页码点）。分页几何是纯函数
  `app/launcher/launcher_pages.h`（每页 4 个 / 页数 / 页容器 x / 页码点 x），有 doctest 单测
  `tests/kernel/test_launcher_pages.cpp`。
- **逻辑动作绑定**：屏上按钮要**就地干点事**（不切屏，如设置屏的「亮度 +1」）时，App 在
  `onCreate` 里 `ui_backend_bind_action("屏名", "控件名", &回调)` 声明一次。它与 `bind_click` 共用
  一张表（上限 8 条，同一控件先后声明两种绑定时后写生效），回调由桥在每次装载后统一重挂，
  所以 App 里**不要**自己 `lv_obj_add_event_cb`（控件随屏 destroy 消失）。样板见
  `app/settings/settings_app.cpp`（[ADR 0014](../../docs/adr/0014-in-screen-back.md) 决策 2）。
- **控件值重推**：屏每次装载都是**新建**的（控件值 = 设计稿初值），所以「值是谁写的」也要声明一次 ——
  App 在 `onCreate` 里 `ui_backend_bind_repaint("屏名", 回调)`，桥在**每次屏装载后**回调它，由它把控件值
  按 App 当前状态重写一遍（[ADR 0015](../../docs/adr/0015-app-retention-and-refresh.md)）。样板见
  `app/clock/clock_app.cpp` 的 `repaint_screen()`（顺带保留用户翻到的月历月）与 `app/launcher/launcher_app.cpp`
  （跳回离开时的页码）。不声明的后果：控件停在设计稿（launcher 时间停在 `11:31` 就是这么来的）。

**回程没有第二条通道**：非主屏的回程就是屏里的 `Btn_Back` 挂设计器事件 `embark_ui_open_launcher`
（六张屏都是这么做的）。早先由桥给屏根挂 `LV_EVENT_GESTURE` 的「非主屏上滑返回」已按用户要求
删除（ADR 0014 撤回 ADR 0013 决策 4），主屏提示行文案随之改回「左右滑动翻页」。

导航**不挂 `CHANGE_SCREEN`**（`_ui_screen_change()` 会绕过桥的按需 init/destroy），而是给控件挂
「**调用函数**」动作，函数名约定 `embark_ui_open_<App 名>`：

| 屏 | 控件 | 动作 | 函数名（设计器里填） |
| --- | --- | --- | --- |
| launcher | Card_Weather | 调用函数 | `embark_ui_open_weather` |
| clock | Btn_Back | 调用函数 | `embark_ui_open_launcher` |
| weather | Btn_Back | 调用函数 | `embark_ui_open_launcher` |
| music | Btn_Back | 调用函数 | `embark_ui_open_launcher` |
| album | Btn_Back | 调用函数 | `embark_ui_open_launcher` |
| settings | Btn_Back | 调用函数 | `embark_ui_open_launcher` |
| settings | Btn_Bright | （设计器里不挂事件） | App 侧 `ui_backend_bind_action("settings", "Btn_Bright", ...)` → `SettingsApp::bump_level()` |

（磁贴不带设计器事件：它们是运行时建的，见上一节。设置屏的 `Btn_Bright` 也不挂设计器事件：
它是本 App 的逻辑动作，由导航桥在装载后重挂。）

生成代码会吐一句裸调用（`embark_ui_open_clock();`），原型在
`app/common/aidogui_ui_hooks.h`（该头由 `app/aidogui_ui/CMakeLists.txt` 用 `-include`
强制注入生成 C 文件，生成代码保持只读），实现转发到 `aidogui_ui_nav_request_app()` →
框架切前台 App。**加一个目标 App** = 设计器里改函数名 + 在 `EMBARK_AIDOGUI_HOOK_APPS` 表里
加一行；`export.ps1` 会数生成代码里的 `embark_ui_open_*()`：0 条告警、名字没声明直接报错。
不想在设计器里挂事件时，备用路径是 App 的 onCreate 里
`aidogui_ui_nav_bind_click(屏名, 控件名, App 名)` 声明一次；只想要逻辑动作（不切屏）就用
`aidogui_ui_nav_bind_action(屏名, 控件名, 回调)`。两者都是控件随屏销毁消失、由桥在每次装载后重挂；**控件值**走第三条 `ui_backend_bind_repaint(屏名, 回调)`（ADR 0015）。

数据也**不在设计器里绑变量**（AiDoGUI 没有变量层）：App 用
`aidogui_ui_bridge_widget(屏名, 控件名)` 拿 `lv_obj_t*`，值变了才 `lv_label_set_text` /
`lv_bar_set_value`。控件名与 App 里那张映射表（`app/clock/clock_app.cpp` 的
`clock_widget_for`）必须一致，改名要两边一起改。

## 字体

字体按 **(字体文件, 字号)** 各出一个 BIN（`export/font/`），只含**实际用到的字符**
（子集化）。换字体 = 在 AiDoGUI 里换 font 引用 + 把 ttf 放 `assets/font/`，然后重导出。
中文 label 必须显式指定 `textFont`（`ui_heiti.ttf`），否则退回内置字体、汉字全成方框。

**字符表是显式维护的**（工程里 `i18n.customCharsetEnabled = true` +
`i18n.customCharsetRows`，GUI 在「语言 / 自定义字符」页）。默认的「自动子集」只收
**设计期文字**：clock 屏设计稿里大时间是 `11:31`，导出后 48px 字体就真的只有
`1 3 :` 三个字形，运行时显示 `23:57` 会有一半是方框。所以每行 `{字体, 字号, symbols}`
必须是「设计期文字 ∪ 运行期字符」的超集，并且**每个 (字体, 字号) 都得有一行**，否则导出
直接报「字体 X (Npx) 未配置自定义字符范围」。当前 7 行（11/12/14/22/28/48/56px，CJK 按
该字号实际显示的字符配、ASCII 用 `range 0x20-0x7F`；两个大时间字号 48/56px 只放
`0123456789:#abcdef `——时间数字 + recolor 色码需要的字符，全 ASCII 会把这两只撑到
80 KB+ 撑爆宿主 256 KB 预算）合计 **57,364 B**（天气屏上线时补了 `多/雨/云/今/°` 与 `上海/广州/深圳/圳` 等城市字；
音乐/相册屏上线时 12px 行补了 `左右动翻页界面还没做`、22px 行补了 `占位屏`）。**加一个 App
要顺手补这张表**：磁贴标题走 14px 行、提示行与占位说明走 12px 行 —— 新 App 的标题字必须
在 14px 行的 `symbols` 里，否则主屏磁贴上是方框。加新字号、加会显示动态数字的控件、
换字体时这张表都要跟着改 —— 漏字符不会报错，只会画方框，所以验收侧还有一道 **字形守卫**
（`--ui-weather` / `--ui-launcher-pages` 用 `lv_font_get_glyph_dsc` 逐字符对表，见
[.scratch/launcher-pages/acceptance.md](../../.scratch/launcher-pages/acceptance.md)）。

当前用的 `ui_heiti.ttf` 是「时尚中黑简体」(TRENDS)，**商业授权待确认**；
若要用在正式产品里，换成 OFL 授权的 Noto Sans SC 更稳。

## 与真机的关系（step 3，尚未做）

真机现在没有文件系统：BIN 需要把 `export/font`、`export/image` 放进 SD 卡，再让
`aidogui_res_fs` 指向 `esp_vfs_fat` 的挂载点（`S:/fonts`、`S:/images`）。
在那之前宿主是唯一验收环境。
