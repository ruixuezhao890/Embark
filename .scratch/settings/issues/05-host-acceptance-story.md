# 05 宿主验收故事跟着改

Status: done
Stage: 验收
Blocked by: 03

## 背景

`platform/host/ui_demo.cpp` 里那条 `--ui-launcher-pages` 故事现在完整地绑在旧设置屏上：它记下 `level()`、点 `Btn_Bright`（「亮度 +1」）、断言 `(before + 1) % 4`、再看 `clock->brightness() == 1`。这一整套在新屏上不成立 —— `Btn_Bright` 没了，档位也没了。故事必须改成「百分比」版本，否则验收台账会说谎。

## 做什么

### 断言函数

- `ui_demo.cpp:943` `aidogui_launcher_bright_check(before, after)`：期望值从 `(before + 1U) % 4U` 改成 `std::min(100U, before + 10U)`，日志里的「亮度档」改成「亮度百分比」。
- 同函数顺带把**钳位**也测掉：直接补两条纯函数断言 `aidogui_launcher_bright_check(100, 100)` 与 `aidogui_launcher_bright_check(0, 0)`（不需要加帧，端点行为就这么验）。
- `ui_demo.cpp:940-942` 的注释（「aidogui_ui_nav_bind_action → SettingsApp::bump_level」）要跟着改成「App 侧 dispatcher」。

### 帧表与落点

- `ui_demo.cpp:1469-1473` 找控件 `"settings", "Btn_Bright"` → 改成 `"Btn_Plus"`；兜底坐标 `ulp_bright_fallback_x/_y`（`:241-242`，当前 184/112）改成 **179 / 64**（Btn_Plus 在 (162,12) 34×32，屏内 y +36 的 Body 偏移）。
- 帧常量 `:216-220`（`ulp_bright_frame = 138` / `ulp_bright_check_frame = 144`）不用改，落点变了但节奏不变。
- `ui_demo.cpp:1464` `ulp_level_before = settings->level()` → `ulp_bright_before = settings->brightness_percent()`；:1479-1481、:1535、:1547、:1629、:1655 一并改名。
- `:1547` `clock->brightness() == 1` 现在是**百分比**：从默认 70 点一次 ＋ ⇒ 期望 `80U`。

### 逻辑入口

- `:1194` `->bump_level()` → `->bump_brightness(+10)`。即 `SettingsApp` 保留一个**公开**的 `void bump_brightness(int delta)` 作为唯一逻辑入口，dispatcher 的 ±/时间按钮都走它 —— 这样验收故事不必造触摸事件就能驱动逻辑。

### 新增的设置屏断言（帧 132，进屏之后）

- `L_BrightVal` 文本 == 屏内当前百分比（默认 `"70%"`）——证明屏真刷了值，不是画的壳。
- `L_Platform` 文本非空且含 `"："` ——证明「关于本机」搬进来了。
- `lv_obj_get_scroll_bottom(body) > 0` ——证明 Body 是滚动容器（内容 422 > 视口 276）。

### 注释同步

`:15`、`:26`、`:111`、`:293`、`:1143`、`:1149` 六处提到「亮度 +1 / bump_level / 0..3」的说明改成新口径。

## 验收

- `build\platform\host\embark_host_ui.exe --ui-launcher-pages --delay 40` 全绿（退出码 0）。
- 故意把期望值改回 `(before+1)%4` 跑一次应当失败 —— 证明断言真的在断言。
- `--launch --delay 40` 与 `--ui --frames 40` 仍然通过。
## Comments

- **落地**：`platform/host/ui_demo.cpp` 里新增 `aidogui_settings_panel_check(expect_percent)`（亮度文本 == `%u%%`、
  关于首行含「：」、`lv_obj_get_scroll_bottom(Body) > 0`）、`aidogui_launcher_bright_check` 改成 `clamp_percent(before+kBrightStep)`、
  `aidogui_launcher_bright_clamp_check()`（`clamp_percent(110)==100`、`clamp_percent(-10)==0`）。点击目标改成 `Btn_Plus`。
- **帧序修正（真 bug）**：亮度点击必须排在滚动拖动**之前** —— 滚到底之后「亮度」卡已在视口外，点不到。
  现在的帧表：132 进屏 → 136 点 ＋ → 142 查 → 148/164 拖动 → 170 查滚动 → 176 返回 → 186 查主屏 → 194 退出。
- **脏状态坑**：宿主持久化是文件支持、跨进程留存，写死「70 → 80」第二次运行必然假失败。
  现在 `--launch` / `ui_tour` 用进程内捕获的起点算期望值；`--ui-launcher-pages` 在帧 132 先把亮度拨到 50% 再断言 50% → 60%，
  连跑两遍稳定。
- **顺手救活了 `--launch`**（AiDoGUI 迁移后就一直红）：它原先按 EEZ 对象名 + 屏幕坐标找按钮（`clock_button_fallback_x=188, _y=295`），
  与 AiDoGUI 版面不符，switches 恒为 0。现在用 `aidogui_widget_center("clock","Btn_Back")` / `("launcher","Card_Clock")` 取中心点，
  判定改用 `aidogui_ui_nav_click_hits() == 3`（**不是** `switch_requests` —— 设计器事件/点击绑定走的是 click-hits 计数）。
- 验收：`--ui` / `--ui-nav` / `--ui-weather` / `--ui-launcher-pages` / `--launch` 五条全绿；`ctest --test-dir build` 2/2。
  `build\platform\host\embark_host_tour.exe`（ui_tour.cpp 那套 EEZ 剧本）仍红，属迁移前既有状态，不在本工单范围。
