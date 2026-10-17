# 设置中心：吸收「关于本机」，并长成设备设置的入口

决策见 `docs/adr/0018-settings-truth-and-growth.md`（随 issue 06 落地）。本目录是它的落地拆分。

## 目标

`settings` 从「最小样板」变成真正的设置中心：一张可纵向滚动的设置页，首屏容纳三块内容 —— **屏幕亮度**、**手动设时间**、**关于本机**；`AboutApp` 整体退役，它的 7 行信息成为设置页最末一张卡。

## 边界（不在本 spec 内）

- **12 小时制**：用户 2026-10-18 拍板「不做」；原提议的「24 小时制」开关随之取消（不做 12 小时制，也就没有格式二选一）。
- **触摸校准、I2C 数据校准、WiFi 连接、自动对时开关**：本期不做，但形态已在文末「后续阶段」定下来，免得下次重新吵一遍。
- 亮度消息的第二个真实消费者：`clock` 现在只是收下打印，不扩。

## 三条定稿决策

1. **亮度真值在 HAL，不在 App。** `IDisplay` 加 `backlight()` 读回；App 只持百分比并落 KV。口径从「0..3 四档」改成「0..100 百分比，步进 10，两端钳位」。
2. **墙上钟可写。** `ITime` 加 `set_epoch_ms()`：真机写 PCF85063 并重锚，宿主改内存基准；没有 RTC 的装配返回 `unsupported`，设置页如实显示「本机无 RTC」而不是假装成功。
3. **屏内交互归 App，不进绑定表。** 设置页的按钮全部由 App 自己 `lv_obj_add_event_cb` 挂一个 dispatcher。理由：`kMaxClickBindings = 8`（`app/common/aidogui_ui_nav.cpp:28`），现已用掉 2 个，设置项一多就爆表；而设计器的 `CALL_FUNCTION` 只有「切 App」一种动作，本就不该承担屏内交互（分工见 GLOSSARY）。

另有一条不由本 spec 引入、但必须守住的既有事实：**屏是每次进前台重建的**，所以「显示什么」一律在 repaint 回调里现算，不缓存、不靠消息同步（`app/launcher/launcher_app.cpp` 的 `last_minute_ = 0xFFFFFFFFU` 那一套就是这个做法）。

## 版面（240×320，一像素都不放大、不缩放）

固定头部 + 可滚动正文：

| 区域 | 位置 / 尺寸 | 内容 |
| --- | --- | --- |
| `Btn_Back` | (8,4) 64×28 | 「返回」14 px，设计器事件 → `embark_ui_open_launcher`（唯一返回路径） |
| `L_Title` | (84,8) 72×20 | 「设置」14 px |
| `Body` | (0,36) 240×276 | 可滚动：`scrollable:true`、`scroll_dir:VER`、`scrollbar_mode:AUTO` |
| `L_SecDisplay` | Body 内 (14,0) 212×16 | 「显示」11 px `#7f8aa6` |
| `Card_Bright` | Body 内 (14,18) 212×56 | 「亮度」14 px + `Btn_Minus`(72,12)34×32「−」 + `L_BrightVal`(110,19)48×20 居中「70%」 + `Btn_Plus`(162,12)34×32「＋」 |
| `L_SecTime` | Body 内 (14,82) 212×16 | 「时间」11 px |
| `Card_Time` | Body 内 (14,100) 212×84 | `L_TimeVal`(12,10)188×24 居中「2026-10-17 06:21」14 px；`L_TimeField`(12,44)40×18「年」12 px；`Btn_TimeMinus`(56,40)34×26「−」；`Btn_TimePlus`(94,40)34×26「＋」；`Btn_TimeNext`(134,40)66×26「下一字段」12 px |
| `L_SecAbout` | Body 内 (14,192) 212×16 | 「关于本机」11 px |
| `Card_Info` | Body 内 (14,210) 212×212 | 7 行 12 px 标签，x=12、y=10+30i、188×18（沿用 about 屏那 7 个值，一个字不改） |

正文内容合计 422 px，viewport 276 px ⇒ 首次出现就要滚 146 px。滚动容器是 LVGL 的纵向滚动（`clock` 屏的 `Feed` 是活样板）；卡片自己不可滚动，手势穿到父容器。

之所以选滚动而不是分页/子页：322 px 的内容塞 276 px 的洞，压行距照样溢出（上一轮算过，压到 20 px 行距还差 14 px）；而新增设置项必然还要更长，滚动是唯一不会越改越挤的口径。

## 亮度口径

- 范围 0..100（百分比），步进 10，**两端钳位**（100 再 ＋ 不动、0 再 − 不动）——不取模，取模会让「最亮再点一下变全黑」。
- 落盘键 `ui.bright`，一个字节（`IPersistence` 键 ≤16 B、值 ≤64 B，`docs/reference/limits.md`）。
- 开机由 `SettingsApp::onCreate` 读 KV 并直接调 `hal().display->set_backlight()`；**不走消息**——`onCreate` 在总线装配之前，此刻 publish 没人收（`src/embark/framework.cpp` 里那行注释写的就是这个）。
- 断电后仍记得：ESP32 每次开机背光都回到板级默认 70%（`platform/esp32/src/esp32_board.h` 的 `backlight_default_percent`），所以亮度这一项**必须**存 KV；时间则由 RTC 电池守，不存 KV。

## 时间口径

- 编辑对象是「本地时间」（固定 UTC+8，`app/common/local_clock.h` 的 `kLocalOffsetSeconds`），显示也按本地。
- 五个字段：年 → 月 → 日 → 时 → 分，`Btn_TimeNext` 循环切换；每次 −/＋ 立即写 RTC（没有「保存」按钮，也不做草稿态）。
- 进位/借位一律走 `app/common/local_clock.h` 的 `floor_div` / `days_from_civil` / `civil_from_days`；日按当月天数钳位，时 0..23、分 0..59 环绕。
- 没有 RTC 或写失败：`L_TimeVal` 显示「本机无 RTC」，按钮点了没反应（`Error::unsupported` 不是致命错误，见 `include/embark/hal/time.h`）。

## Issues

| # | 标题 | 阶段 | 阻塞 | 状态 |
| --- | --- | --- | --- | --- |
| 01 | HAL 底座：背光读回 + 墙上钟可写 | 底座 | — | done |
| 02 | 设置屏重排 + 删 about 屏（改 .aid 再重新导出） | 屏 | 01 | done |
| 03 | 偏好模块与 SettingsApp 重写 | App | 01、02 | done |
| 04 | 退役 AboutApp 并清理全局接线 | 清理 | 03 | done |
| 05 | 宿主验收故事跟着改 | 验收 | 03 | done |
| 06 | 文档、术语与 ADR | 文档 | 01、02、03、04 | done |

## 后续阶段（形态已定，不在本期）

| 项 | 已定形态 | 还缺什么 |
| --- | --- | --- |
| 触摸校准 | 把今天那三个编译期开关（`touch_mirror_x` / `touch_mirror_y` / `touch_swap_xy`）搬上屏：点四角看触点落在哪个象限，直接切 mirror/swap 位并落 KV，开机读 KV 覆盖常量 | `IInput` 要能吐**原始**坐标（现在只吐变换后的点）；一套常量校验与回退 |
| I2C 数据校准 | 扫一遍板载总线、把回话的设备地址列出来（复用 `hal::IBus`，纯读不写），用来排查 IMU/RTC/触摸掉线 | 一次只读地址遍历；一份「期望在线地址」对照表（`esp32_board.h` 已有 `rtc_address = 0x51` 这类常量可当基线） |
| 连接 WiFi | 独立设置项：扫 AP → 选一个 → 密码输入（`lv_textarea` + `lv_keyboard`，设计器目录里都有）→ 连接状态 | 一块无线 HAL（新面）；凭据存储（KV 单值上限 64 B，放不下要另立分区）；同时要推翻 v1 spec 里「网络 / OTA / Wi-Fi 配网 为非目标」这条边界 |
| 自动对时 | 开关**等 WiFi 落地后才出现**——一个点了没反应的开关比没有更糟。出现时配套一行「上次对时：…」 | SNTP 客户端；以及和 RTC 写权限的对齐（谁的时钟是权威） |

## 验收（整条赛道）

- 宿主：`cmake --build build` + `ctest` 全绿；三条界面故事 `--ui-nav` / `--ui-launcher-pages` / `--ui` 全绿（`--delay 40` 是必须的，LVGL 约 30 ms 采一次输入）。
- 亮度：点 ＋ 五次从 70% 到 100%，第 6 次不动；减到 0 后再点 − 不动；退出设置页再进来数值保持；不开新进程无法验「重启后保持」时，用「KV 读回值 == 屏上显示值」代替。
- 时间：改「分」+1 后 `epoch_ms()` 精确前进 60000；改「年」在 2024-02-29 这类边界上不出非法日期；无 RTC 的装配显示「本机无 RTC」且不崩。
- 关于本机：7 行全部有值或明确的「量不了 / 未挂载 / 未对时」兜底，与退役前的 about 屏逐行一致。
- 滚动：Body 能滚到底（`lv_obj_get_scroll_bottom() > 0`），卡片上手势能带动父容器。
- 真机：亮度肉眼可见变化；改完时间拔电重插仍走（RTC 电池在）；串口无 assert。
## 落地状态（2026-10-18）

六个 issue 全部 done，落地面貌与本文一致。与计划的偏差只有两处，都记在对应 issue 的 `## Comments` 里：

1. **滚动容器必须既 SCROLLABLE 又 CLICKABLE**。设计器默认给每个控件 `flags.clickable:false`，导出的容器于是 `lv_obj_clear_flag(..., LV_OBJ_FLAG_CLICKABLE)`；LVGL 命中测试只认 clickable 的控件（`third_party/lvgl/src/core/lv_obj_pos.c:955`），手指落在卡片空白处被判定成「按在屏上」，而滚动链只从按下的对象往上找（`third_party/lvgl/src/core/lv_indev_scroll.c:258`），屏上面没有 `Body` ⇒ 容器冻住。修法：`Body` 在 `.aid` 里同时开 `scrollable` 与 `clickable`，`rebuild_screens.cjs` 加了这条断言。
2. **宿主验收的亮度断言必须自己造基线**。宿主 `IPersistence` 是文件支持的，跨进程留存，所以「点一下加 10」的期望值会随上次运行的残留值飘；`--ui-launcher-pages` 进设置屏后先把亮度拨到 50% 再断言 50%→60%，连跑两遍稳定。

真机走查（COM3 烧录 + 串口）记录见 `boot-*.log`；亮度、时间、关于七行、滚动四项均由用户实机确认。
