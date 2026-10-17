# 02 设置屏重排 + 删 about 屏（改 .aid 再重新导出）

Status: done
Stage: 屏
Blocked by: 01

## 背景

要动的两张屏都在 AiDoGUI 工程里：`app/aidogui_ui/screen.aid`。而 `app/aidogui_ui/export/` 是**生成物**，铁律二写明不许手改（`app/aidogui_ui/README.md`）—— 所以路径只能是「改 .aid → 重新导出」。

两条已探明的事实决定了具体做法：

- **设计器当前画布是旧的**：`get_editor_state` / `list_screens` 只报 6 屏，没有 `about`（.aid 文件里其实有 7 屏）。此刻用 MCP 写任何东西都会把 about 屏冲掉。所以这一步走**脚本改 .aid**，不走 MCP。
- **滚动必须在 .aid 里开**：导出的每个控件都带 `lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE)`，因为设计器里 `flags.scrollable:false`。要让 `Body` 能滚，必须在 .aid 里给它 `flags.scrollable: true` + `scroll_dir: "VER"`，导出后才会变成 `lv_obj_add_flag(...)`。

两个现成脚本可照抄手法：`.scratch/add_about_screen.cjs`（括号扫描 + 文本插入，避免整体重排 JSON）、`.scratch/retitle_font_row.cjs`（每次替换前断言目标串唯一、写前 `JSON.parse` 校验）。

## 做什么

1. 写 `.scratch/settings/rebuild_screens.cjs`：
   - 删 `screens[]` 里 `name === "about"` 的整块（**向后兼容**：已经没有就跳过，脚本可重复跑）。
   - 重排 `settings` 屏到 spec「版面」那张表的形状：`Card_Bright` 挪进新的 `Body`，删 `Btn_Bright` / `Btn_Bright_L`，加 `Btn_Minus` / `Btn_Plus` / `L_BrightVal`、三节标题、`Card_Time` 五件、`Card_Info` 七行。
   - 每次改动前断言目标串在文件里只出现一次；写前 `JSON.parse(next)` 校验，校验不过就不写。
2. 重新导出：`pwsh -NoProfile -File tools/aidogui/export.ps1`（它会**清空并重建** `export/`，这是它本来的设计，不是事故）。
3. 导出后逐条核对（别信「应该没问题」）：
   - `export/screens/ui_settings.c` 里 `Body` 那一行必须是 `lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLLABLE)`，**不能**是 `lv_obj_clear_flag`。
   - `export/screens/ui_about.c` / `ui_about.h` 必须消失。
   - `export/ui.c` 里 `ui_about_screen_init()` / `ui_about_screen_destroy()` 两处引用必须消失（导出现在在 `app/aidogui_ui/export/ui.c:78` 和 `:90`）。
   - 控件总数 = 32：Desk / Btn_Back / Btn_Back_L / L_Title / Body 共 5；三节标题 3；亮度卡 5（三个控件 + 两个按钮子标签）；时间卡 11（三个标签 + 四个按钮及其子标签）；关于卡 8。对不上说明脚本漏了东西。

## 验收

- 上面 4 条核对全过。
- `cmake --build build` 通过（`cmake/embark_aidogui_tables.cmake` 在配置期 glob `export/screens/ui_*.h`，屏表自动少掉 about）。
- 重新用 AiDoGUI Designer 打开工程：6 屏、settings 是新版、about 已不存在（人工确认，这条只能人看）。
- `--ui --frames 40` 能起屏、不 assert。

## Comments

- 2026-10-17 落地：新增 `.scratch/settings/rebuild_screens.cjs`（139 行）。做法是**文本手术**而不是整体重排 JSON —— 括号扫描定位 `screens[]` 里每张屏对象的字节区间，`settings` 屏整块重建、`about` 屏整块删除，其余 5 屏原字节不动（整体 `JSON.stringify` 会把空 `events` 数组拆成两行，产生无谓 diff）。控件模板从现存屏克隆（`Card_Bright` / `L_Title` / `Btn_Back`），样式键序与原来一致。
- 重排后 `settings` 共 **32 个控件**：Desk / Btn_Back / Btn_Back_L / L_Title / Body 共 5；三节标题 3；亮度卡 7（Card + 标题 + −/+ 两个按钮及其子标签 + 数值）；时间卡 9（Card + 时间值 + 字段名 + 三个按钮及其子标签）；关于卡 8（Card_Info + 七行）。`Body` = (0,36) 240×276、`flags.scrollable: true`、`scroll_dir: 纵向`。
- 重新导出：6 屏（about 消失），四条核对全过 —— (a) `Body` 是 `lv_obj_add_flag(..., LV_OBJ_FLAG_SCROLLABLE)` 且 `scroll_dir = LV_DIR_VER`；(b) `ui_about.c` / `ui_about.h` 已不存在；(c) `export/ui.c` 里 about 引用 0 处；(d) `ui_settings.h` 控件 32 个。
- 验证：`cmake -S . -B build`（配置期重新生成屏表）→ `cmake --build build` 通过 → `--ui --frames 40` 打出〔aidogui 验收通过〕、屏表 6 屏；`ctest` 2/2 绿。实拍存在 `.scratch/settings/settings_shot.png`（240×320；首屏是 显示 / 亮度 70% / 时间卡 / 关于本机前两行，正文合计 422px > 视口 276px，滚动条按设计出现）。
- 副产物：.aid 现在就是 6 屏，设计器重新打开工程后画布与文件一致，此前「画布缺 about、一写就冲掉」的风险消失。
- **已知红，属 issue 03**：`--ui-launcher-pages` 现在报〔主屏分页验收失败〕（亮度档 0→0）—— `Btn_Bright` 已从屏上删除，而 `app/settings/settings_app.cpp` 还在给 `settings/Btn_Bright` 挂逻辑动作。issue 03 会把这处绑定换成 `Btn_Minus` / `Btn_Plus` + 屏内回调，并把验收脚本里「4 档循环」的断言改成百分比步进。

## Comments（真机走查后的补修）

- 真机反馈：设置屏**拖不动**，滚不到「关于本机」。宿主验收却是绿的 —— 那条绿是假的。
- 根因：LVGL 的命中测试第一行就要求对象 `CLICKABLE`（`third_party/lvgl/src/core/lv_obj_pos.c:955`）。设计器给每个容器都发 `flags.clickable: false`，于是手指落在「关于本机」卡的空白处或标签上时，`lv_indev_search_obj` 逐层回退，最后被按到的是**屏自己**；`find_scroll_obj`（`third_party/lvgl/src/core/lv_indev_scroll.c:258`）只从被按对象**往上**找滚动容器，从屏往上没有 Body，滚动链没起点 —— 容器即使标了 SCROLLABLE 也一动不动。
- 宿主为什么假通过：合成拖动的路径 `(120,250) → (120,90)` 途中扫过 `Btn_TimePlus`（Card_Time 里 94,40 34×26，屏坐标 y 176..202），LVGL 在按点移动时重新搜索命中了这个**可点按钮**，滚动链才从按钮往上爬到 Body。实测探针（`lv_indev_t* in = lv_indev_get_next(nullptr); in->proc.types.pointer.act_obj`）显示 press 后 act 先为 `SCREEN`，第 142 帧变成该按钮，第 143 帧 `scroll_obj` 才是 Body。
- 修法：`.scratch/settings/rebuild_screens.cjs:55` 给 Body 加 `clickable: true`（脚本里写明为什么必须这样），重跑脚本 → 重新导出，`app/aidogui_ui/export/screens/ui_settings.c:157` 变成 `lv_obj_add_flag(ui_settings_Body, LV_OBJ_FLAG_CLICKABLE);`。
- 验收：`--ui-launcher-pages --delay 40` 现在报「正文滚动 0 px → 146 px，剩余可滚 0 px」（改前是 0→133，起手那段是空转）；`platform/host/ui_demo.cpp` 的注释同步改成正确解释；`ctest` 2/2 绿；真机重烧 950912 字节，启动日志 7/7 字体就绪、launcher 装载、`[AUTODEBUG_PASS] SD_FONT_OK`，无 assert。
