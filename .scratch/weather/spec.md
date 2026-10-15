# 天气 App（issue：weather）

## 目标

在 AiDoGUI 界面路线上加一个**天气 App**，两条入口都通：

1. launcher 主屏上的**天气卡**（`Card_Weather`）——点进去就是天气屏，卡片本身显示当前城市；
2. 天气 App 自身（屏名 `weather` == App 名），可以从主屏卡片或将来任何界面上按同一约定进。

天气屏要能**选城市**（◀ ▶ 切，选择落盘），并显示**最近 5 天**（今天 + 后 4 天）。

## 非目标

- **不做真实天气数据**：框架没有网络（无 WiFi/HTTP/socket/NTP），本轮是编译期桩，
  决策与理由见 [docs/adr/0012-weather-data-source.md](../../docs/adr/0012-weather-data-source.md)。
- 不做城市搜索/输入（桩表只有 4 个城市，环形切换）。
- 不做天气预警、空气质量详情页、生活指数页。
- 不做真机（ESP32）资源落盘：步 3（BIN 资源进 SD 卡）尚未开始，与 ADR 0011 一致。

## 设计要点

| 面 | 决定 |
| --- | --- |
| 界面来源 | AiDoGUI 设计工程 `app/aidogui_ui/screen.aid`，新增屏 `weather`（37 控件）；生成代码入库 |
| 入口 | 主屏 `Card_Weather` 的设计器「调用函数」事件 `embark_ui_open_weather`（同时把该卡的 CLICKABLE 放开）；回程是天气屏 `Btn_Back` 的 `embark_ui_open_launcher` |
| 数据 | 编译期桩 `app/weather/weather_data.h`（4 城 × 5 天） |
| 城市切换 | 天气屏 `Btn_CityPrev` / `Btn_CityNext`（`lv_label` 当按钮，App 侧 `lv_obj_add_event_cb`，与 clock 屏翻月历同款）；设计器事件在本工程里只用来**切 App**，切城市是 App 内行为 |
| 持久化 | `hal().storage` 键 `wx.city`，1 字节城市下标；主屏卡片与天气屏共用 |
| 主屏卡片数据 | `LauncherApp::refresh_weather()`，读同一个键、同一张表；值没变不写控件 |
| 5 天布局 | 页头（返回 + 标题）→ 城市栏（◀ 城市 ▶）→「现在」卡（大天气字 + 大温度 + 一句话）→ 5 行（日期 / 天气字 / 一句话 / 温度区间），一屏不滚动 |
| 字号 | 14px 城市/标题/逐日天气字；12px 星期标签与温度区间；11px 一句话；22px「现在」天气字；28px 大温度 |
| 字符表 | 每个用到的字号都要在 `screen.aid` 的 `i18n.customCharsetRows` 里补上运行期字符（本轮补了 多/雨/云/今/° 等；14px 还补了 上海广州深圳等城市字） |
| 后台策略 | `suspend`（数据是桩，不会自己变；不占 tick） |

## 验收

见 [acceptance.md](acceptance.md)。新增开关 `embark_host_ui --ui-weather`；
`--ui` / `--ui-nav` 必须不回归。
