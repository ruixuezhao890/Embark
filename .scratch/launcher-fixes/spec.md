# 规格：launcher 回程与两个界面 bug（2026-10-16）

Status: 已实现（2026-10-16）—— 验收剧本见 [acceptance.md](acceptance.md)，决策见
[ADR 0014](../../docs/adr/0014-in-screen-back.md)（回程）与 [ADR 0013](../../docs/adr/0013-launcher-paged-tiles.md)（分页）。

## 背景

用户在 Local 会话里一次报了四件事（本目录按提问顺序编号 Q1–Q4），随后说「按你的倾向来，在 worktree 开始动手」：

- Q1 launcher 时钟：每次从 App 返回 launcher，时间都不对；
- Q2 上滑返回：功能去掉，每个 App 都留一个「返回」按钮回 launcher；
- Q3 launcher 天气控件：一个字正常，两个字（如「雷雨」）就被遮住一半；
- Q4（由 Q2 推出，开工后确认）设置 App 没有回程：注册表里有 settings、主屏也有它的磁贴，
  但它没有同名屏，点进去只会「保持当前屏 + 一条 INFO 日志」。

## Q1 时钟：返回 launcher 后时间不对

- 根因：`app/launcher/launcher_app.cpp` 的 `refresh_clock()` 第一行是 `if (minute == last_minute_) return;`。
  屏每次重建都会把 `L_Time` 打回设计稿里写死的 `11:31`（`app/aidogui_ui/export/screens/ui_launcher.c` 的
  `lv_label_set_text(..., "11:31")`），而 `last_minute_` 还是上一轮的分钟值——同一分钟内回到 launcher，
  刷新被短路，屏上就停着 `11:31`。
- 修法：`onEnter` / `onResume` 里把 `last_minute_` 重置成 `0xFFFFFFFFU`（与相邻那行 `shown_city_ = 0xFFU`
  同样的「已失效」哨兵写法），强制下一次 `refresh_clock()` 真写一遍。
- 位置：`app/launcher/launcher_app.cpp`（`refresh_clock()` 短路点；`onEnter` / `onResume` / `onBackgroundTick` 调用点）。
- 证据：`--ui-nav` 打印「点返回后 launcher/L_Time = "07:38"」（不再是设计稿死值 `11:31`）。

## Q2 去掉上滑返回，改屏内返回按钮

- 现状：回程有两条通道——五张 App 屏里本来就有的 `Btn_Back`（设计器事件 `embark_ui_open_launcher`），
  以及 `app/common/aidogui_ui_nav.{h,cpp}` 给非主屏屏根挂的 `LV_EVENT_GESTURE`（`LV_DIR_TOP` → `request_home()`）。
- 做法：整条手势链路删除（`attach_swipe_back()` / `on_screen_gesture()` / `add_gesture_bubble()` /
  上滑命中计数 / `aidogui_ui_nav_swipe_back_hits()`），只留屏内返回按钮。主屏提示行文案改成「左右滑动翻页」
  （`screen.aid` 里那个 label 的 text，从「左右滑动翻页，上滑返回」改短；字符表未动）。
- 连带：`platform/host/ui_demo.cpp` 的上滑帧脚本与断言（提示行文案断言、分页验收里的上滑两段）同步改成
  返回按钮的路径；`docs/adr/0013-launcher-paged-tiles.md` 决策 4 标注被 ADR 0014 撤回。

## Q3 天气卡两字词被裁

- 根因：主屏天气卡的图标盒 `L_WIcon` 是 30×30、字号 22px；一个字（约 22px）放得下，两个字的词
  （`雷雨` / `多云` / `小雨` / `中雨` / `大雨`，见 `app/weather/weather_data.h`）约 44px，超出后被 LVGL 折行，
  第二行落到盒子外被裁掉——屏上看起来就是「被遮住一半」。
- 做法（方案 A：几何改动最小、不动字号）：设计稿 `screen.aid` 里 `L_WIcon` 加宽到 46，同卡温度标签
  `L_WTemp` 的 `x` 从 52 右移到 60，22px 字号不变；改完重导出生成代码。
- 回归断言（新增，防以后再被改窄）：`platform/host/ui_demo.cpp` 的 `aidogui_weather_card_ok()` 量 `L_WIcon`
  的内容宽，再遍历全部城市的今日天气字取最大文本宽（`lv_txt_get_width`），要求盒子宽 ≥ 文本宽，结果并入
  天气验收的返回值。实测日志：「主屏天气卡图标盒：宽 46，最长今日天气字 "雷雨" 量得 44px（装得下：是）」。

## Q4 设置屏与「控件 → 逻辑动作」

- 背景：注册表里 settings 从 ADR 0013 起就在（顺序 launcher, clock, settings, weather, music, album），
  主屏也有它的磁贴，但没有同名屏。
- 做法：`screen.aid` 加第 6 张屏 `settings`（8 控件，回程按钮与其余四屏同形）。`Btn_Back` 挂设计器事件
  `embark_ui_open_launcher`；`Btn_Bright`（亮度 +1）走新加的 `ui_backend_bind_action` →
  `SettingsApp::bump_level()`（档位 0..3 循环 +1 并广播 `BrightnessMessage`，ClockApp 收到后更新档位）。
- 为什么不用 `bind_click` 点自己、为什么不在 App 里自己 `add_event_cb`：见 ADR 0014 决策 2。

## 改动清单

| 面 | 文件 |
| --- | --- |
| 时钟 | `app/launcher/launcher_app.cpp`（`last_minute_` 哨兵） |
| 回程 | `app/common/aidogui_ui_nav.{h,cpp}`（删手势）、`app/common/ui_backend*`（加 `bind_action`） |
| 设置屏 | `app/aidogui_ui/screen.aid` + `export/`（第 6 张屏）、`app/settings/settings_app.{h,cpp}` |
| 天气卡 | `app/aidogui_ui/screen.aid` + `export/screens/ui_launcher.c`（`L_WIcon` 46 / `L_WTemp` x 60） |
| 验收 | `platform/host/ui_demo.cpp`（上滑段改返回段、设置屏三段、天气卡盒宽回归断言） |
| 文档 | `docs/adr/0014-in-screen-back.md`（新）、`docs/adr/0013-*`（状态行）、`GLOSSARY.md`、`docs/index.md`、`docs/guides/new-app-guide.md`、`app/aidogui_ui/README.md` |

## 非目标

- 不改天气数据来源与城市切换（[ADR 0012](../../docs/adr/0012-weather-data-source.md)）；
- 设置屏只做「亮度 +1」这一个逻辑动作样板，不做别的设置项；
- 不复活导航壳 / 框架级返回键（[ADR 0006](../../docs/adr/0006-launcher-and-nav-shell.md) 已退役）。
