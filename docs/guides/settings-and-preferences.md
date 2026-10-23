# 设置中心与偏好键

设置 App（`app/settings/`，屏名 `settings`）是这台机器的**设置中心**：屏幕亮度、墙上钟、关于本机七行都在它一块屏里。
设计取舍见 [ADR 0018](../adr/0018-settings-truth-and-growth.md)；本文说**怎么用、怎么加一项**。

## 一屏三段

屏 240×320，头部固定，正文（`Body`）是**滚动容器**：视口 276 px（`(0,36) 240×276`），内容 422 px，
所以进屏就能往下拖 146 px 看到「关于本机」。

| 区 | 控件 | 行为 |
| --- | --- | --- |
| 头部 | `Btn_Back` / `L_Title` | 返回走设计器事件 `embark_ui_open_launcher`（回主屏的唯一通道） |
| 显示 | `Card_Bright`：`L_BrightTitle`「亮度」/ `Btn_Minus`「−」/ `L_BrightVal`「70%」/ `Btn_Plus`「＋」 | 步进 10，两端钳位（不会从 100 绕回 0）；改完立刻写 HAL 并落 KV |
| 时间 | `Card_Time`：`L_TimeVal`（当前墙上钟）/ `L_TimeField`（当前字段名）/ `Btn_TimeMinus` / `Btn_TimePlus` / `Btn_TimeNext` | 「下一字段」在 年→月→日→时→分 之间循环，加减只改选中字段，越界按该字段回绕（如月 12 → 1） |
| 关于本机 | `Card_Info` 七行：`L_Platform` / `L_Version` / `L_Heap` / `L_Font` / `L_Stack` / `L_Disk` / `L_Clock` | 纯展示，量不到就显示「量不了 / 未挂载 / 未对时」，不假装 0 |

正文里的按钮**都没有设计器事件**：它们是屏内交互，由 App 用 `lv_obj_add_event_cb` 挂，
并在每次屏装载后的重推里重挂（控件随屏销毁）。设计器事件在本工程里只干一件事 —— 切 App。

## 一个设置项怎么工作（以亮度为例）

```
用户点 ＋
  → SettingsApp::on_sink_clicked（SinkKind::bright_plus）
  → bump_brightness(+10)：percent_ = clamp_percent(percent_ + 10)                  ← 两端钳位
  → display->set_backlight(percent_)                                              ← 真值进 HAL
  → publish(BrightnessMessage{percent_})                                          ← 只报「值变了」
  → save_bright(hal().storage, percent_)                                          ← 真值进 KV
  → write_brightness_text()                                                       ← 屏上的「70%」
```

**真值有两个落点，没有第三个**：HAL（硬件真的变亮）和 KV（下次开机还是这个亮度）。App 成员 `percent_` 只是
这两处的内存投影；从屏上离开再进来会走 `on_screen_reloaded()` 重推，值不会退回设计稿的 `70%`。

开机时 `onCreate` 先 `load_bright()` 读到 KV 里的值再 `set_backlight()` —— **不用**发消息恢复（boot 期总线还没装配，
`onCreate` 里 publish 的消息没人收）。

## 偏好键

| 键 | 类型 | 值域 / 默认 | 谁读 |
| --- | --- | --- | --- |
| `ui.bright` | 1 字节 | 0..100，默认 `kBrightDefaultPercent` = 70 | `SettingsApp::onCreate`（恢复背光）；`--ui-launcher-pages` 验收读它 |

键与读写助手都在 `app/settings/settings_prefs.h`（header-only，`embark::demo` 命名空间）：
`kBrightKey` / `kBrightDefaultPercent` / `kBrightStep` / `clamp_percent(int)` / `load_bright(IPersistence&)` /
`save_bright(IPersistence&, uint8_t)`。**不建 SettingsService**：每个设置项要么落到既有 HAL，要么只存一个标量，
中间那层今天没有第二个消费者（[ADR 0018](../adr/0018-settings-truth-and-growth.md) 第 2 条）。新增键沿用 `ui.` 前缀。

容量约束在 `core/config/embark_limits.h`：键 ≤ 16 字节、值 ≤ 64 字节、32 个槽；槽满时写入返回 `no_space`，
**不会**静默覆盖旧键（见 [reference/limits.md](../reference/limits.md) §4）。

## 加一个设置项（三步）

1. **设计器**：在设计工程 `app/aidogui_ui/screen.aid` 的 `settings` 屏里加控件（放在 `Body` 里，坐标是相对 Body 的）。
   尺寸参考：卡片 212 宽、行高 30、按钮 34×32。加完重导出：
   `pwsh -NoProfile -File tools/aidogui/export.ps1`，然后 **重新 configure**（屏表在 CMake 配置期生成）：
   `cmake -S . -B build`。
2. **App**：在 `app/settings/settings_app.cpp` 的 `enum class SinkKind` 加一个值、`kSinks[]` 加一行
   `{控件名, SinkKind::…}`、`on_sink_clicked` 的 `switch` 加一个分支（改值 → 落 KV / 落 HAL → 重写屏上的字）。
3. **验收**：在 `host/ui_demo.cpp` 的 `aidogui_settings_panel_check()` 里加一条断言（读控件文本，
   与 App 的成员比对）。**别写死期望值** —— 见下面第一个坑。

版面不用重排：`Body` 已经开了滚动，内容变长只是能拖得更远。

## 验收

```powershell
cmake -S . -B build; cmake --build build
build\host\embark_host_ui.exe --ui --frames 40                 # 〔aidogui 验收通过〕
build\host\embark_host_ui.exe --ui-launcher-pages --delay 40    # 设置屏 已进 / 正文滚动 146 px / 亮度 50%→60%
build\host\embark_host_ui.exe --launch --delay 40               # 〔launch 验收通过〕
ctest --test-dir build
```

真机：`pwsh -NoProfile -File .scratch/font-ttf/hw/build_flash.ps1`（COM3），抓日志
`& C:\Python314\python.exe .scratch\font-ttf\hw\capture.py COM3 20 .scratch\settings\boot.log`。
进设置屏后日志里应出现「屏 settings 已装载（32 个控件）」，且**没有**缺字告警与 LVGL assert。

## 坑

- **宿主持久化是文件支持的、跨进程留存**。写死「点一下从 70% 变 80%」的断言在第二次运行就假失败。
  正确姿势：先在测试里把亮度拨到一个确定值（`--ui-launcher-pages` 在帧 132 拨到 50%），再断言相对变化 + 上界钳位。
- **滚动容器必须既 `scrollable` 又 `clickable`**。只开 scrollable 时，按在卡片空白处会命中屏本身，滚动链爬不上去，
  容器拖不动 —— 而合成拖动若恰好压在一个可点按钮上反而会「通过」。详见
  [`app/aidogui_ui/README.md`](../../app/aidogui_ui/README.md) 的「滚动页」一节。
- **屏上字号只从 7 个槽里选**（11/12/14/22/28/48/56 px，见 [ADR 0017](../adr/0017-font-ttf-from-sd.md)）。
  设置屏用了 11（节标题）/ 12（正文行）/ 14（标题与值）。新文案要过缺字审计（`font-audit`），
  生僻字先查卡上那份 TTF 的 cmap。
- **`--ui-launcher-pages` 的点击帧必须排在滚动帧之前**：滚到底之后「亮度」卡已经滚出视口，点不到。
- **关于本机是展示不是设置**：不要给它加可点控件、不要进 KV、验收只断言首行是「键：值」的形状。

## 交叉阅读

- 设计与否决过的方案：[ADR 0018](../adr/0018-settings-truth-and-growth.md)
- 屏内返回按钮（回程唯一通道）：[ADR 0014](../adr/0014-in-screen-back.md)
- 值重推（`ui_backend_bind_repaint`）：[ADR 0015](../adr/0015-app-retention-and-refresh.md)
- 界面怎么改（设计器 / 导出 / 字体）：[`app/aidogui_ui/README.md`](../../app/aidogui_ui/README.md)
- 加一个新 App（端到端）：[new-app-guide.md](new-app-guide.md)
