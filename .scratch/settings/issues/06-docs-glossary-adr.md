# 06 文档、术语与 ADR

Status: done
Stage: 文档
Blocked by: 01、02、03、04

## 背景

这轮改动引入了三个仓库以前没有的概念（设置中心、设置项、亮度百分比），并且把「关于」从 App 降级成一块卡片 —— 正是 `GLOSSARY.md` 该记的东西。另外「屏内交互不走绑定表」「设置真值分散在 HAL 与 KV，而不是一个 SettingsService」是**以后每次加设置项都会撞上**的判断，值得一条 ADR。

## 做什么

### `GLOSSARY.md`（在 `## Language` 段内按字母/笔画顺序插入）

- **设置中心（Settings Center）** —— 承载「这台机器怎么表现」的全部可调项的 App，屏名 `settings`。设置项之间不共享状态，各自把真值存在该真值该在的地方。
  _Avoid_: 设置页面、配置面板。
- **设置项（Setting）** —— 设置中心里一个可独立增删的调整单元，形如「一个节标题 + 一张卡」。新加一个设置项不该改动已有设置项的代码。
  _Avoid_: 选项、参数、开关（开关只是设置项的一种形态）。
- **亮度百分比（Brightness percent）** —— 背光强度 0..100 的整数百分比。**真值在 HAL**（`IDisplay::set_backlight` / `backlight()`），设置中心只持一份镜像用于显示与持久化。
  _Avoid_: 亮度档、等级（0..3 四档的旧口径已废弃）。
- **关于本机（About this device）** —— 只读的机器自述：平台、版本、可用堆、字库、栈水位、存盘、对时。它是设置中心的一张卡，**不是一个 App**。
  _Avoid_: 关于页面、关于 App、系统信息（那会与 HAL 的 `ISystem` 混淆）。
- 顺便把 `BrightnessMessage` 相关条目里的「0..3 共四档」改成「0..100 百分比」（若原条目存在）。

### 新 ADR `docs/adr/0018-settings-truth-and-growth.md`

四条判断，每条都要写清「不这么做会怎样」：

1. **亮度真值在 HAL，不在 App。** App 只持有百分比镜像；开机时 App 在 `onCreate` 里读 KV 再调 `set_backlight`（`onCreate` 早于总线装配，所以不能用消息）。
2. **设置真值放 KV，键统一 `ui.` 前缀**（`ui.bright`）。不建 `SettingsService`：设置项彼此无关，一个集中服务只会让每加一项都要改服务。
3. **屏内交互归 App，不进绑定表。** `ui_backend_bind_action` 的 8 个槽是给「跨 App/跨屏跳转」用的；设置页有 5 个以上按钮，全塞进去会溢出。屏内按钮用 `lv_obj_add_event_cb` + 一个 dispatcher。
4. **加一个设置项的固定三步**：设计器加卡 → `settings_prefs.h` 加键 → `SettingsApp` 加一个 `Sink` 分支。三步之外不该有任何改动。

### `app/aidogui_ui/README.md`

- 屏幕表：settings 一行的控件数与描述改成新屏（12 个控件，含 `Body` 滚动容器）；**删掉 about 行**（如果 02 之后还有残留）。
- 绑定表：`settings | Btn_Back | 调用函数 | embark_ui_open_launcher` 保留；**移除** `Btn_Bright | bind_action` 那一行，并补一句「设置页的屏内按钮由 App 用 `lv_obj_add_event_cb` 挂，不进本表」。
- `:99` 起「最小样板是 settings_app.cpp」的说法改成 `album_app.cpp`（04 已改代码，这里同步文档）。
- 「怎么再导出一遍」段落补一条：改完 `screen.aid` 必须跑 `tools/aidogui/export.ps1`，否则 `export/` 与设计器不一致。

### `docs/adr/0014-in-screen-back.md`

正文不动（它是历史记录），在末尾追加一行：`> 2026-10-18 补：设置页已不再是单按钮样板，Btn_Bright 与 0..3 亮度档随 issues 02/03 退役，改见 ADR 0018。`

### 新手册 `docs/guides/settings-and-preferences.md`

面向「我要再加一个设置项」的人，一页纸：

1. 设计器里在 `Body` 内加一节标题 + 一张卡，控件名进命名表；导出。
2. `app/settings/settings_prefs.h` 加 `inline constexpr const char* kXxxKey = "ui.xxx";` 与一对 `load/save`（照抄 `load_bright`）。
3. `SettingsApp` 加一个 `Sink` 枚举值 + dispatcher 分支 + repaint 里刷文本；需要的话在 `onCreate` 里做「开机生效」。

再补一节「哪些东西不属于设置中心」：需要网络的（WiFi/对时）、需要新 HAL 的（触摸校准）、跨 App 的导航 —— 它们各自要先有底座。

### 其他

- `docs/guides/eez-ui-manual.md:75` 的 `settings_app.cpp` 引用 04 已改，这里复查。
- `docs/reference/hooks-and-api.md` 若有 `SettingsApp::bump_level` 的描述，改成 `bump_brightness`。

## 验收

- `GLOSSARY.md` 里 grep「亮度档」只剩 _Avoid_ 行。
- `docs/adr/0018-settings-truth-and-growth.md` 存在且被 `docs/adr/` 的索引（如果有）收录。
- 新手册里的三步能照着走通（拿「再加一个音量项」当假想例子自检一遍）。
## Comments

- **GLOSSARY.md**：在 `## Language` 里新增四条 —— `Settings App（设置 App / 设置中心）`、`Setting row（设置项）`、
  `Brightness percent（亮度百分比）`、`About this device（关于本机）`，各带 `_Avoid_` 行（「亮度档 0..3」只留在 _Avoid_ 里）。
- **docs/adr/0018-settings-truth-and-growth.md** 新建：四条决策（亮度真值在 HAL / 设置真值在 KV `ui.` 前缀、不建 SettingsService /
  屏内交互归 App、不进导航绑定表（含滚动容器必须既 SCROLLABLE 又 CLICKABLE 的推理）/ 加设置项固定三步）+ 墙上钟可写，
  另有「否决过的方案」四条与「代价与影响」四条。
- **docs/adr/0014-in-screen-back.md**：末尾追加「后记（2026-10-18）」，说明决策 3 的 settings 屏形态退役、决策 1/2 仍有效。
- **app/aidogui_ui/README.md**：屏表 settings 行改成 32 控件三段结构；新增「滚动页：容器必须既 SCROLLABLE 又 CLICKABLE」一节
  （含「合成拖动骗过验收」的复盘）；事件表删掉 `Btn_Bright` 行并说明屏内交互归 App；`bind_action` 段落改为「今天没有消费者」。
- **docs/guides/settings-and-preferences.md** 新建：三段版面表、亮度一次点击的时序、偏好键表、加一个设置项三步、验收命令、五个坑。
- **docs/index.md**：补 0016 / 0017 / 0018 三行与设置指南一行；0014 行标注「该屏形态已被 0018 取代」。
- **docs/guides/new-app-guide.md**：屏内交互口径改成 `lv_obj_add_event_cb` + 重挂（原文推荐 `bind_action`），并说明它为何不通用。
- 与本文的偏差：ADR 0014 补的是**完整后记**（不是一行引用），因为只写一行会让读者以为「返回按钮也一起退役了」。
