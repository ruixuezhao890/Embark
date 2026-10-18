# 界面控件表（设计器 ↔ 代码 对照）

> 自动生成，**别手改**：`node tools/aidogui/gen_widget_doc.mjs`（改完屏跑一次）。
> 来源：`app/aidogui_ui/screen.aid` 的 6 个屏 / 124 个控件，join `app`、`platform/host` 里的调用点。
> 设计器里改控件名，代码不会编译失败 —— 它只会静默拿到 nullptr。这张表就是那张对照表。

## 怎么用

1. **拿控件**：`lv_obj_t* obj = aidogui_ui_bridge_widget("屏名", "控件名");` —— 屏没装载时返回 `nullptr`，所以永远先判空。
2. **写值**：拿到 `lv_obj_t*` 之后就是普通 LVGL：`lv_label_set_text` / `lv_bar_set_value` / `lv_calendar_set_today_date` …（LVGL 自己会比对，值没变不重绘，见 `docs/concepts/index.md`）。
3. **点一下干什么**：看每行的「事件」列 —— 设计器里的「调用函数」事件已经写死了动作。要临时在 C 里挂，用 `lv_obj_add_event_cb`，但控件随屏销毁，每次装载都得重挂（时机是 `ui_backend_bind_repaint(屏名, 回调)`，见 ADR 0015）。

## 概览

| 屏（= App 名） | 控件 | 可点 | 死控件 |
| --- | --- | --- | --- |
| `launcher` | 14 | 1 | 3 |
| `clock` | 27 | 3 | 11 |
| `weather` | 37 | 3 | 11 |
| `music` | 7 | 1 | 7 |
| `album` | 7 | 1 | 7 |
| `settings` | 32 | 7 | 16 |

## launcher

屏名（写进 `aidogui_ui_bridge_widget` 的第一个参数）：`launcher`。

| 控件名 | 类型 | 几何 | 文本 | 事件 | 代码里谁在用 |
| --- | --- | --- | --- | --- | --- |
| `Desk` | lv_obj | 0,0 240×320 | — | — | — |
| `L_Title` | lv_label 14px | 14,5 100×20 | `启动器` | — | — |
| `L_Clock0` | lv_label 14px | 120,5 106×20 | `11:31` | — | app/launcher/launcher_app.cpp:187（表/常量） |
| `Card_Clock` | lv_obj | 14,30 212×96 | — | — | app/launcher/launcher_app.cpp:60 |
| `L_Time` | lv_label 56px | 12,10 188×64 | `11:31` | — | app/launcher/launcher_app.cpp:187（表/常量）<br>platform/host/ui_demo.cpp:563（验收）<br>+3 |
| `L_Date` | lv_label 12px | 14,77 184×18 | `2026年10月12日 星期一` | — | app/launcher/launcher_app.cpp:202 |
| `Band_Apps` | lv_obj | 0,202 240×56 | — | — | app/launcher/launcher_tiles.cpp:174 |
| `Dots_Row` | lv_obj | 0,258 240×6 | — | — | app/launcher/launcher_tiles.cpp:175 |
| `Card_Weather` | lv_obj | 14,134 212×56 | — | CLICKED→`embark_ui_open_weather` | 设计器事件 |
| `L_WIcon` | lv_label 22px | 12,12 46×30 | `晴` | — | app/launcher/launcher_app.cpp:227（表/常量）<br>platform/host/ui_demo.cpp:1083（验收） |
| `L_WTemp` | lv_label 28px | 60,8 68×36 | `26°` | — | app/launcher/launcher_app.cpp:226（表/常量） |
| `L_WCity` | lv_label 14px | 134,10 66×20 | `北京` | — | app/launcher/launcher_app.cpp:225（表/常量） |
| `L_WDesc` | lv_label 11px | 134,30 66×18 | `空气优` | — | app/launcher/launcher_app.cpp:228（表/常量） |
| `L_Hint` | lv_label 12px | 14,278 212×18 | `左右滑动翻页` | — | platform/host/ui_demo.cpp:851（验收） |

## clock

屏名（写进 `aidogui_ui_bridge_widget` 的第一个参数）：`clock`。

| 控件名 | 类型 | 几何 | 文本 | 事件 | 代码里谁在用 |
| --- | --- | --- | --- | --- | --- |
| `Desk` | lv_obj | 0,0 240×320 | — | — | — |
| `Btn_Back` | lv_obj | 8,4 64×28 | — | CLICKED→`embark_ui_open_launcher` | 设计器事件 |
| `Btn_Back_L` | lv_label 14px | 0,5 64×18 | `返回` | — | — |
| `L_Title` | lv_label 14px | 84,8 72×20 | `时钟` | — | — |
| `Feed` | lv_obj | 0,36 240×284 | — | — | — |
| `Card_Time` | lv_obj | 12,8 216×92 | — | — | — |
| `L_Time` | lv_label 48px | 12,6 192×62 | `11:31` | — | app/clock/clock_app.cpp:69（表/常量）<br>platform/host/ui_demo.cpp:606（验收） |
| `L_SW` | lv_label 12px | 12,68 192×18 | `秒 07，第 42 周` | — | app/clock/clock_app.cpp:69（表/常量） |
| `Card_Date` | lv_obj | 12,108 216×40 | — | — | — |
| `L_Date` | lv_label 14px | 12,10 192×20 | `2026年10月12日 周一` | — | app/clock/clock_app.cpp:69（表/常量） |
| `Card_Cal` | lv_obj | 12,156 216×184 | — | — | — |
| `L_MH` | lv_label 14px | 40,12 136×20 | `2026年10月` | — | app/clock/clock_app.cpp:70（表/常量）<br>platform/host/ui_demo.cpp:667（验收） |
| `Card_Year` | lv_obj | 12,348 216×84 | — | — | — |
| `L_Doy` | lv_label 12px | 12,8 192×18 | `第 285 天` | — | app/clock/clock_app.cpp:70（表/常量） |
| `Bar_Prog` | lv_bar | 12,30 192×10 | — | — | app/clock/clock_app.cpp:73（表/常量）<br>platform/host/ui_demo.cpp:608（验收） |
| `L_P1` | lv_label 12px | 12,46 192×18 | `剩 80 / 365 天` | — | app/clock/clock_app.cpp:70（表/常量） |
| `L_P2` | lv_label 12px | 12,64 192×16 | `距年末 80 天` | — | app/clock/clock_app.cpp:71（表/常量） |
| `Card_Tom` | lv_obj | 12,440 216×44 | — | — | — |
| `L_Tm` | lv_label 14px | 12,12 110×20 | `10月13日` | — | app/clock/clock_app.cpp:71（表/常量） |
| `L_Tw` | lv_label 14px | 130,12 74×20 | `周二` | — | app/clock/clock_app.cpp:71（表/常量） |
| `Card_Month` | lv_obj | 12,492 216×44 | — | — | — |
| `L_Dim` | lv_label 14px | 12,12 60×20 | `31 天` | — | app/clock/clock_app.cpp:72（表/常量） |
| `L_Fd` | lv_label 11px | 78,14 76×18 | `首日 周四` | — | app/clock/clock_app.cpp:72（表/常量） |
| `L_Fw` | lv_label 11px | 158,14 46×18 | `第 42 周` | — | app/clock/clock_app.cpp:72（表/常量） |
| `Cal` | lv_calendar 12px | 12,32 192×144 | — | — | app/clock/clock_app.cpp:402<br>platform/host/ui_demo.cpp:654（验收） |
| `Btn_Prev` | lv_label 14px | 12,12 24×20 | `<` | 可点（没绑动作） | app/clock/clock_app.cpp:440 |
| `Btn_Next` | lv_label 14px | 180,12 24×20 | `>` | 可点（没绑动作） | app/clock/clock_app.cpp:441 |

## weather

屏名（写进 `aidogui_ui_bridge_widget` 的第一个参数）：`weather`。

| 控件名 | 类型 | 几何 | 文本 | 事件 | 代码里谁在用 |
| --- | --- | --- | --- | --- | --- |
| `Desk` | lv_obj | 0,0 240×320 | — | — | — |
| `Btn_Back` | lv_obj | 8,4 64×28 | — | CLICKED→`embark_ui_open_launcher` | 设计器事件 |
| `Btn_Back_L` | lv_label 14px | 0,5 64×18 | `返回` | — | — |
| `L_Title` | lv_label 14px | 84,8 72×20 | `天气` | — | — |
| `CityBar` | lv_obj | 12,36 216×28 | — | — | — |
| `Btn_CityPrev` | lv_label 14px | 4,4 28×20 | `<` | 可点（没绑动作） | app/weather/weather_app.cpp:123 |
| `L_City` | lv_label 14px | 38,4 140×20 | `北京` | — | app/weather/weather_app.cpp:151（表/常量） |
| `Btn_CityNext` | lv_label 14px | 184,4 28×20 | `>` | 可点（没绑动作） | app/weather/weather_app.cpp:126 |
| `Card_Now` | lv_obj | 12,68 216×88 | — | — | — |
| `L_NIcon` | lv_label 22px | 16,14 60×60 | `晴` | — | app/weather/weather_app.cpp:152（表/常量） |
| `L_NTemp` | lv_label 28px | 84,10 112×46 | `26°` | — | app/weather/weather_app.cpp:153（表/常量） |
| `L_NDesc` | lv_label 11px | 84,58 112×18 | `空气优` | — | app/weather/weather_app.cpp:154（表/常量） |
| `Card_D0` | lv_obj | 12,160 216×30 | — | — | — |
| `L_D0_Dow` | lv_label 12px | 8,6 52×18 | `今天` | — | app/weather/weather_app.cpp:159（拼名） |
| `L_D0_Icon` | lv_label 14px | 62,5 34×20 | `晴` | — | app/weather/weather_app.cpp:161（拼名） |
| `L_D0_Desc` | lv_label 11px | 100,6 58×18 | `空气优` | — | app/weather/weather_app.cpp:163（拼名） |
| `L_D0_Range` | lv_label 12px | 158,6 52×18 | `18°/26°` | — | app/weather/weather_app.cpp:165（拼名） |
| `Card_D1` | lv_obj | 12,191 216×30 | — | — | — |
| `L_D1_Dow` | lv_label 12px | 8,6 52×18 | `周六` | — | app/weather/weather_app.cpp:159（拼名） |
| `L_D1_Icon` | lv_label 14px | 62,5 34×20 | `晴` | — | app/weather/weather_app.cpp:161（拼名） |
| `L_D1_Desc` | lv_label 11px | 100,6 58×18 | `空气优` | — | app/weather/weather_app.cpp:163（拼名） |
| `L_D1_Range` | lv_label 12px | 158,6 52×18 | `18°/26°` | — | app/weather/weather_app.cpp:165（拼名） |
| `Card_D2` | lv_obj | 12,222 216×30 | — | — | — |
| `L_D2_Dow` | lv_label 12px | 8,6 52×18 | `周六` | — | app/weather/weather_app.cpp:159（拼名） |
| `L_D2_Icon` | lv_label 14px | 62,5 34×20 | `晴` | — | app/weather/weather_app.cpp:161（拼名） |
| `L_D2_Desc` | lv_label 11px | 100,6 58×18 | `空气优` | — | app/weather/weather_app.cpp:163（拼名） |
| `L_D2_Range` | lv_label 12px | 158,6 52×18 | `18°/26°` | — | app/weather/weather_app.cpp:165（拼名） |
| `Card_D3` | lv_obj | 12,253 216×30 | — | — | — |
| `L_D3_Dow` | lv_label 12px | 8,6 52×18 | `周六` | — | app/weather/weather_app.cpp:159（拼名） |
| `L_D3_Icon` | lv_label 14px | 62,5 34×20 | `晴` | — | app/weather/weather_app.cpp:161（拼名） |
| `L_D3_Desc` | lv_label 11px | 100,6 58×18 | `空气优` | — | app/weather/weather_app.cpp:163（拼名） |
| `L_D3_Range` | lv_label 12px | 158,6 52×18 | `18°/26°` | — | app/weather/weather_app.cpp:165（拼名） |
| `Card_D4` | lv_obj | 12,284 216×30 | — | — | — |
| `L_D4_Dow` | lv_label 12px | 8,6 52×18 | `周六` | — | app/weather/weather_app.cpp:159（拼名） |
| `L_D4_Icon` | lv_label 14px | 62,5 34×20 | `晴` | — | app/weather/weather_app.cpp:161（拼名） |
| `L_D4_Desc` | lv_label 11px | 100,6 58×18 | `空气优` | — | app/weather/weather_app.cpp:163（拼名） |
| `L_D4_Range` | lv_label 12px | 158,6 52×18 | `18°/26°` | — | app/weather/weather_app.cpp:165（拼名） |

## music

屏名（写进 `aidogui_ui_bridge_widget` 的第一个参数）：`music`。

| 控件名 | 类型 | 几何 | 文本 | 事件 | 代码里谁在用 |
| --- | --- | --- | --- | --- | --- |
| `Desk` | lv_obj | 0,0 240×320 | — | — | — |
| `Btn_Back` | lv_obj | 8,4 64×28 | — | CLICKED→`embark_ui_open_launcher` | 设计器事件 |
| `Btn_Back_L` | lv_label 14px | 0,5 64×18 | `返回` | — | — |
| `L_Title` | lv_label 14px | 84,8 72×20 | `音乐` | — | — |
| `Card_Placeholder` | lv_obj | 14,84 212×120 | — | — | — |
| `L_Placeholder` | lv_label 22px | 12,28 188×36 | `占位屏` | — | — |
| `L_PlaceholderHint` | lv_label 12px | 12,76 188×18 | `界面还没做` | — | — |

## album

屏名（写进 `aidogui_ui_bridge_widget` 的第一个参数）：`album`。

| 控件名 | 类型 | 几何 | 文本 | 事件 | 代码里谁在用 |
| --- | --- | --- | --- | --- | --- |
| `Desk` | lv_obj | 0,0 240×320 | — | — | — |
| `Btn_Back` | lv_obj | 8,4 64×28 | — | CLICKED→`embark_ui_open_launcher` | 设计器事件 |
| `Btn_Back_L` | lv_label 14px | 0,5 64×18 | `返回` | — | — |
| `L_Title` | lv_label 14px | 84,8 72×20 | `相册` | — | — |
| `Card_Placeholder` | lv_obj | 14,84 212×120 | — | — | — |
| `L_Placeholder` | lv_label 22px | 12,28 188×36 | `占位屏` | — | — |
| `L_PlaceholderHint` | lv_label 12px | 12,76 188×18 | `界面还没做` | — | — |

## settings

屏名（写进 `aidogui_ui_bridge_widget` 的第一个参数）：`settings`。

| 控件名 | 类型 | 几何 | 文本 | 事件 | 代码里谁在用 |
| --- | --- | --- | --- | --- | --- |
| `Desk` | lv_obj | 0,0 240×320 | — | — | — |
| `Btn_Back` | lv_obj | 8,4 64×28 | — | CLICKED→`embark_ui_open_launcher` | 设计器事件 |
| `Btn_Back_L` | lv_label 14px | 0,5 64×18 | `返回` | — | — |
| `L_Title` | lv_label 14px | 84,8 72×20 | `设置` | — | — |
| `Body` | lv_obj | 0,36 240×276 | — | 可点（没绑动作） | platform/host/ui_demo.cpp:940（验收）<br>platform/host/ui_demo.cpp:984（验收）<br>+2 |
| `L_SecDisplay` | lv_label 11px | 14,0 212×16 | `显示` | — | — |
| `Card_Bright` | lv_obj | 14,18 212×56 | — | — | — |
| `L_BrightTitle` | lv_label 14px | 14,18 56×20 | `亮度` | — | — |
| `Btn_Minus` | lv_obj | 72,12 34×32 | — | 可点（没绑动作） | app/settings/settings_app.cpp:53（表/常量） |
| `Btn_Minus_L` | lv_label 14px | 0,7 34×18 | `-` | — | — |
| `L_BrightVal` | lv_label 14px | 110,19 48×20 | `70%` | — | app/settings/settings_app.cpp:323（表/常量）<br>platform/host/ui_demo.cpp:938（验收） |
| `Btn_Plus` | lv_obj | 162,12 34×32 | — | 可点（没绑动作） | app/settings/settings_app.cpp:54（表/常量） |
| `Btn_Plus_L` | lv_label 14px | 0,7 34×18 | `+` | — | — |
| `L_SecTime` | lv_label 11px | 14,82 212×16 | `时间` | — | — |
| `Card_Time` | lv_obj | 14,100 212×84 | — | — | — |
| `L_TimeVal` | lv_label 14px | 12,10 188×24 | `2026-10-17 06:21` | — | app/settings/settings_app.cpp:334（表/常量） |
| `L_TimeField` | lv_label 12px | 12,44 40×18 | `年` | — | app/settings/settings_app.cpp:327（表/常量） |
| `Btn_TimeMinus` | lv_obj | 56,40 34×26 | — | 可点（没绑动作） | app/settings/settings_app.cpp:55（表/常量） |
| `Btn_TimeMinus_L` | lv_label 12px | 0,4 34×18 | `-` | — | — |
| `Btn_TimePlus` | lv_obj | 94,40 34×26 | — | 可点（没绑动作） | app/settings/settings_app.cpp:56（表/常量） |
| `Btn_TimePlus_L` | lv_label 12px | 0,4 34×18 | `+` | — | — |
| `Btn_TimeNext` | lv_obj | 134,40 66×26 | — | 可点（没绑动作） | app/settings/settings_app.cpp:57（表/常量） |
| `Btn_TimeNext_L` | lv_label 12px | 0,4 66×18 | `下一字段` | — | — |
| `L_SecAbout` | lv_label 11px | 14,192 212×16 | `关于本机` | — | — |
| `Card_Info` | lv_obj | 14,210 212×212 | — | — | — |
| `L_Platform` | lv_label 12px | 12,10 188×18 | `平台：host` | — | app/settings/settings_app.cpp:358（表/常量）<br>platform/host/ui_demo.cpp:939（验收） |
| `L_Version` | lv_label 12px | 12,40 188×18 | `版本：0.1.0` | — | app/settings/settings_app.cpp:359（表/常量） |
| `L_Heap` | lv_label 12px | 12,70 188×18 | `可用堆：-- KB` | — | app/settings/settings_app.cpp:365（表/常量） |
| `L_Font` | lv_label 12px | 12,100 188×18 | `字库：--/-- 份` | — | app/settings/settings_app.cpp:373（表/常量） |
| `L_Stack` | lv_label 12px | 12,130 188×18 | `栈水位：-- KB` | — | app/settings/settings_app.cpp:379（表/常量） |
| `L_Disk` | lv_label 12px | 12,160 188×18 | `存盘：未挂载` | — | app/settings/settings_app.cpp:385（表/常量） |
| `L_Clock` | lv_label 12px | 12,190 188×18 | `对时：未对时` | — | app/settings/settings_app.cpp:400（表/常量） |

## 死控件（设计器里有，代码里没人取）

共 49 个。容器（`lv_obj`）没人写是常态 —— 它是给子控件当爹的，不需要代码。真正要看的是 `lv_label` / `lv_bar` 这类：没人写就意味着界面那一格永远停在设计器初值上。

| 屏 | 控件 | 类型 | 设计器初值 |
| --- | --- | --- | --- |
| `launcher` | `Desk` | lv_obj | — |
| `launcher` | `L_Title` | lv_label | `启动器` |
| `clock` | `Desk` | lv_obj | — |
| `clock` | `Btn_Back_L` | lv_label | `返回` |
| `clock` | `L_Title` | lv_label | `时钟` |
| `clock` | `Feed` | lv_obj | — |
| `clock` | `Card_Time` | lv_obj | — |
| `clock` | `Card_Date` | lv_obj | — |
| `clock` | `Card_Cal` | lv_obj | — |
| `clock` | `Card_Year` | lv_obj | — |
| `clock` | `Card_Tom` | lv_obj | — |
| `clock` | `Card_Month` | lv_obj | — |
| `weather` | `Desk` | lv_obj | — |
| `weather` | `Btn_Back_L` | lv_label | `返回` |
| `weather` | `L_Title` | lv_label | `天气` |
| `weather` | `CityBar` | lv_obj | — |
| `weather` | `Card_Now` | lv_obj | — |
| `weather` | `Card_D0` | lv_obj | — |
| `weather` | `Card_D1` | lv_obj | — |
| `weather` | `Card_D2` | lv_obj | — |
| `weather` | `Card_D3` | lv_obj | — |
| `weather` | `Card_D4` | lv_obj | — |
| `music` | `Desk` | lv_obj | — |
| `music` | `Btn_Back_L` | lv_label | `返回` |
| `music` | `L_Title` | lv_label | `音乐` |
| `music` | `Card_Placeholder` | lv_obj | — |
| `music` | `L_Placeholder` | lv_label | `占位屏` |
| `music` | `L_PlaceholderHint` | lv_label | `界面还没做` |
| `album` | `Desk` | lv_obj | — |
| `album` | `Btn_Back_L` | lv_label | `返回` |
| `album` | `L_Title` | lv_label | `相册` |
| `album` | `Card_Placeholder` | lv_obj | — |
| `album` | `L_Placeholder` | lv_label | `占位屏` |
| `album` | `L_PlaceholderHint` | lv_label | `界面还没做` |
| `settings` | `Desk` | lv_obj | — |
| `settings` | `Btn_Back_L` | lv_label | `返回` |
| `settings` | `L_Title` | lv_label | `设置` |
| `settings` | `L_SecDisplay` | lv_label | `显示` |
| `settings` | `Card_Bright` | lv_obj | — |
| `settings` | `L_BrightTitle` | lv_label | `亮度` |
| `settings` | `Btn_Minus_L` | lv_label | `-` |
| `settings` | `Btn_Plus_L` | lv_label | `+` |
| `settings` | `L_SecTime` | lv_label | `时间` |
| `settings` | `Card_Time` | lv_obj | — |
| `settings` | `Btn_TimeMinus_L` | lv_label | `-` |
| `settings` | `Btn_TimePlus_L` | lv_label | `+` |
| `settings` | `Btn_TimeNext_L` | lv_label | `下一字段` |
| `settings` | `L_SecAbout` | lv_label | `关于本机` |
| `settings` | `Card_Info` | lv_obj | — |

## 只被验收脚本用（App 自己没写）

这些控件只有 `platform/host/` 下的验收故事在读它们。如果它该随业务变，那 App 里少了一段代码。

| 屏 | 控件 | 谁在用 |
| --- | --- | --- |
| `launcher` | `L_Hint` | platform/host/ui_demo.cpp:851（验收） |
| `settings` | `Body` | platform/host/ui_demo.cpp:940（验收）<br>platform/host/ui_demo.cpp:984（验收）<br>+2 |

## 孤儿引用（代码里取了，设计器里没有）

没有。

## 认不出来的调用点（动态拼名 / 表里取的字段）

这些调用点的控件名是变量，静态看不出来。工具改用「同文件里出现过的字面量」兜底，所以上面的控件可能被算成「在用」而其实没人写 —— 名字对不上时先看这里。

| 文件 | 行 | 调用 |
| --- | --- | --- |
| `app/clock/clock_app.cpp` | 83 | `aidogui_ui_bridge_widget(kClockScreen, entry.widget)` |
| `app/launcher/launcher_app.cpp` | 188 | `aidogui_ui_bridge_widget("launcher", widget)` |
| `app/launcher/launcher_app.cpp` | 231 | `aidogui_ui_bridge_widget("launcher", item.widget)` |
| `app/settings/settings_app.cpp` | 130 | `aidogui_ui_bridge_widget(kScreen, sink.widget)` |
| `app/settings/settings_app.cpp` | 305 | `aidogui_ui_bridge_widget(kScreen, widget_name)` |
| `app/weather/weather_app.cpp` | 134 | `aidogui_ui_bridge_widget(kScreen, widget_name)` |
| `platform/host/ui_demo.cpp` | 1008 | `aidogui_ui_bridge_widget("weather", widget)` |
| `platform/host/ui_demo.cpp` | 1067 | `aidogui_ui_bridge_widget("launcher", widget)` |

## 导出目录过期（.aid 里有，生成的 C 里没这个符号）

没有：`.aid` 里每个控件在导出的 C 里都有对应符号。
