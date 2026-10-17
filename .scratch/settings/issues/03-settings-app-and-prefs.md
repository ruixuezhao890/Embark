# 03 偏好模块与 SettingsApp 重写

Status: done
Stage: App
Blocked by: 01、02

## 背景

屏改好之后，`SettingsApp` 得从「一个按钮 + 一个 0..3 计数器」变成真正的设置中心。三处约束先摆清楚：

- **屏每次进前台重建**，控件指针会全部失效，所以「挂事件」和「刷数值」都必须发生在**屏装载之后** —— 正好是 `ui_backend_bind_repaint("settings", ...)` 的时机（ADR 0015）。一个 repaint 钩子同时干这两件事。
- **绑定表只有 8 个槽**（`app/common/aidogui_ui_nav.cpp:28`），所以本 App **不用** `ui_backend_bind_action`，改走屏内 `lv_obj_add_event_cb`（weather 已有先例，`app/weather/weather_app.cpp:120-130`）。
- **onCreate 在总线装配之前**（`src/embark/framework.cpp` 有注释明写）：读 KV、直接调 HAL 都可以，publish 消息没人收。

## 做什么

### 偏好模块 `app/settings/settings_prefs.h`（header-only）

``c++
inline constexpr const char* kBrightKey = "ui.bright";
bool load_bright(embark::hal::IPersistence& storage, std::uint8_t& out) noexcept;  // >100 视为无效
void save_bright(embark::hal::IPersistence& storage, std::uint8_t percent) noexcept;
``

照 `app/weather/weather_data.h` 的 `weather_load_city` / `weather_save_city` 那个形状：一个字节读、一个字节写、写失败用 `(void)` 丢弃（不因为存不下而挡住操作）。键长 8 B、值 1 B，远在 `docs/reference/limits.md` 的 16 B / 64 B 之内。

### SettingsApp 重写

字段：`percent_`（0..100）、`time_field_`（0..4 = 年/月/日/时/分）、`time_ok_`。

1. `onCreate`：记框架句柄 → `ui_backend_bind_repaint("settings", &SettingsApp::on_screen_reloaded)` → 读 KV，有效就 `hal().display->set_backlight(percent_)`（先判 `display != nullptr`）。
2. `on_screen_reloaded()`（每次屏装载后）：刷三处文本（亮度百分比、时间值、当前字段名）+ 重挂五个按钮的 `LV_EVENT_CLICKED`。
3. 事件派发：**一个** `static void on_action(lv_event_t*)` + 一张 `static constexpr Sink kSinks[5]`，注册时把 `&kSinks[i]` 当 `user_data` 传进去。不要整数转指针（实现定义），也不写五个重复的小函数。
4. 亮度：±10、两端钳位 → `set_backlight()` → `publish(BrightnessMessage{percent})` → `save_bright()` → 刷文本。
5. 时间：每次 −/＋ 都**现读** `epoch_ms()`，拆成本地年月日时分，只改当前字段（日按当月天数钳位、时 0..23 环绕、分 0..59 环绕），用 `app/common/local_clock.h` 的 `floor_div` / `days_from_civil` / `civil_from_days` 换算，再 `set_epoch_ms()` 写回。**不留草稿态、不做「保存」按钮** —— 每次点击立即生效，也就没有「改了一半退出」的中间态。
6. 写失败 / 没有 RTC：`time_ok_ = false`，`L_TimeVal` 显示「本机无 RTC」，按钮点了没反应（不弹窗、不 assert）。

### 消息字段改名

`app/common/app_messages.h:18-21` 的 `BrightnessMessage::level` → `percent`（注释里的「0..3 共四档」一起改）。接收处只有 `app/clock/clock_app.cpp:242-248`，跟着改。

## 验收

- 宿主手点 `--ui-launcher-pages`：亮度 70 → 80 → … → 100，到 100 再 ＋ 不动；减到 0 再 − 不动；退出设置页再进来数值保持。
- 时间：改「分」＋ 一次，`epoch_ms()` 精确 +60000；改「年」在 2024-02-29 这类边界上不出非法日期；无 RTC 的装配显示「本机无 RTC」且不崩。
- 关于本机 7 行与退役前的 about 屏逐行一致（平台 / 版本 / 可用堆 / 字库 / 栈水位 / 存盘 / 对时）。
- `ctest` 全绿；`--ui --frames 40` 不 assert。
## Comments

- **落地**：`app/settings/settings_prefs.h`（`kBrightKey="ui.bright"` / `kBrightDefaultPercent=70` / `kBrightStep=10` /
  `clamp_percent(int)` / `load_bright(IPersistence&) -> uint8_t` / `save_bright(...)`；`load_bright` 直接返回归一化后的值
  ——比本文原本设想的 `bool load(..., uint8_t& out)` 少一个参数，非法值时回落默认，调用点只有一行）。
- `app/settings/settings_app.{h,cpp}` 重写（约 413 行）：`bump_brightness(int)` / `nudge_time_field(int)` / `next_time_field()` /
  `refresh_display()` / `write_brightness_text()` / `static on_screen_reloaded()`；`enum class SinkKind` + `kSinks[5]` +
  `on_sink_clicked`，`bind_sinks()` 从 `refresh_display()` 调（每次屏装载后重挂）。
- 时间**不用草稿态**：每次 −/＋ 现读 `epoch_ms()`、拆本地时间、只改当前字段（日按当月天数、时分回绕）、`set_epoch_ms()` 写回；
  `time_ok_=false` 时 `L_TimeVal` 显示「本机无 RTC」，按钮点了无害。
- `BrightnessMessage::level` → `percent`（`app/common/app_messages.h`），`clock_app` 成员 `level_` → `bright_percent_`、
  访问器 `brightness()` 不变（`--launch` 与 `ui_tour` 都在用它）。
- 验收（宿主，退出码 0）：`--ui-launcher-pages` 亮度 **50% → 60%**（起点由测试自己拨到 50，见 05 的 Comments）、
  `--launch` 的 `clock->brightness() == clamp(before+10)`、`--ui --frames 40` / `--ui-nav` / `--ui-weather` 全绿，`ctest` 2/2。
- 真机：`onCreate` 日志「App settings onCreate（亮度 70%…）」，进屏后「屏 settings 已装载（32 个控件）」。
- 与本文的偏差：**没有**新增 `L_TimeField` 之外的时间控件；`Btn_TimeNext` 是本文第 5 条之外的第三个按钮（切字段），
  否则「手动设时间」得靠猜当前改的是哪一位。
