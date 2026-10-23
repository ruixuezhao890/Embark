# 启动器分页磁贴 + 非主屏上滑返回

> **状态（2026-10-16）**：决策 4（非主屏上滑返回）已被 [0014](0014-in-screen-back.md) 撤回
> ——回程改为每个 App 屏里自带的「返回」按钮；决策 1 / 2 / 3 / 5 / 6 / 7 仍然有效，本 ADR 保留作历史记录。

用户 2026-10-15 提出（原话）："我后续可能要增加新的app，这不能滑动根本选不到后面的app，
帮我修改一下，让它可以滑动选择app"。前一轮 [0011](0011-aidogui-ui-source.md) 把界面来源换成
AiDoGUI 之后，主屏的 4 个磁贴是**设计稿里手摆的固定控件**——加第 5 个 App 就没有位置，
加第 6 个更是只能去设计器里再摆。这条 ADR 决定主屏的 App 入口怎么长，以及"返回主屏"这条
最常用的回程怎么自动适配新 App。拷问三轮（裁决表见
[`.scratch/launcher-pages/spec.md`](../../.scratch/launcher-pages/spec.md)）后定稿。

1. **磁贴运行时按注册表生成，设计稿只留两个空容器**。launcher 屏里现在只有
   `Band_Apps`（0,202 240×56，横向可滚）与 `Dots_Row`（0,258 240×6），磁贴与页码点由
   `app/launcher/launcher_tiles.cpp` 在每次屏装载后重建：**跳过注册表第 0 项**（主屏自己），
   磁贴数 = App 数 − 1，顺序 = 注册顺序，文本 = 各 App 的 `title()`。**加 App 从此不用改设计稿**：
   `EMBARK_APP_TABLE(...)` 多一个名字，主屏就自动多一个磁贴（超过 4 个自动多一页）。
   否决了"在设计器里手摆更多磁贴"：那等于把"加 App"变成"改工程 + 重导出 + 编生成代码"，
   正是本次要消除的动作。
2. **分页几何是纯函数**：`app/launcher/launcher_pages.h`（每页 4 个、磁贴 48×48 @ y=202
   x=12/68/124/180、页宽 240、页码点 6×6 间隔 2 居中）不含 LVGL、不含 App，
   doctest 直接测 0/1/2/3/4/5/11/12 个 App 的磁贴数、页数、`page_of`/`slot_of` 双射与几何
   （`core/tests/kernel/test_launcher_pages.cpp`）。页面几何与设计稿对得上：`tile_x(3)+48+12=240`、
   `dots_row_x(3)=109` 就是老设计稿里 `Dot1` 的 x。磁贴绘制层（`launcher_tiles.cpp`）只做
   "读注册表 → 问纯函数 → 建控件"，所以磁贴行为不需要靠截图验证。
3. **翻页手感 = 吸附式连续滚动**（跟手拖、松手吸附最近页），**不做循环、不做点页码跳页**。
   关键发现：**不用写一行拖动代码**——LVGL 给每个对象默认加了 `LV_OBJ_FLAG_SNAPPABLE`
   （`third_party/lvgl/src/core/lv_obj.c:437`），松手时 `_lv_indev_scroll_throw_handler`
   （`third_party/lvgl/src/core/lv_indev_scroll.c:112`，水平吸附分支 :169）用
   `find_snap_point_x()` 找带 SNAPPABLE 的子件（我们的"每页一个 240×56 页容器"正好是它们）
   再 `lv_obj_scroll_by(..., LV_ANIM_ON)`。我们只是把设计稿的 `Band_Apps` 设成
   横向可滚 + `SNAP_START` + 关滚动条，运行时再写一遍（防漂移）。
4. **上滑返回挂在桥的屏观察者里，不挂在设计器里**：`app/common/aidogui_ui_nav.cpp` 的
   `attach_swipe_back()` 在每次屏装载后给**非主屏**的屏根挂 `LV_EVENT_GESTURE`，
   `LV_DIR_TOP` → `framework.request_home()`（= 注册表第 0 个 App）。**主屏不挂**（在主屏上滑
   什么也不发生）。App 与设计器都不用声明，**以后新加的 App 自动有**。
   实现细节由 LVGL 8.3.11 的机理逼出来（三条都是读源码 + 实测确认的）：
   `indev_gesture()` 沿 `LV_OBJ_FLAG_GESTURE_BUBBLE` 父链**跳过带该标志的对象**，落到第一个
   不带标志的对象才派发（`third_party/lvgl/src/core/lv_indev.c:1126-1131`）——所以
   **回调挂屏根、只给子控件加 BUBBLE**；若屏根自己也带 BUBBLE，父链走到 NULL，事件**完全
   不发**（:1133）。另外 `lv_obj_hit_test()` 对非 `CLICKABLE` 对象直接返回 false
   （`third_party/lvgl/src/core/lv_obj_pos.c:955`）→ 屏根必须保证可点，否则空白处上滑没有
   `act_obj`、连 PRESSING 都不会开始。还有一个反直觉的坑：指针压在任何**可滚动**对象上时
   `indev_gesture()` 直接 return（:1123），所以"主屏不响应上滑"这条断言必须把起点放在磁贴带
   之外，否则会以错误的原因通过。
5. **容量 8 → 12**：`core/config/embark_limits.h` 的 `max_apps` 改成 12
   （= 主屏 + 11 个磁贴 = 3 页），同步 `docs/reference/limits.md`。注册表越界本来就是编译期
   `static_assert`，调大只是多几份 `AppAdapter`。
6. **磁贴样式走新增令牌**：`design_tokens::tile_bg = 0x202B47` / `tile_text = 0xC7D0E6` /
   `tile_radius = 10`。**没有复用现有令牌族**（`panel`/`text_primary`/…）——设计稿的色族与令牌族
   取值本来就不同，硬套会让设计稿和真机对不上；取值照设计稿现值，磁贴保持纯文字不加图标。
7. **新增"音乐""相册"两个占位 App**（`app/music/`、`app/album/` + 设计器里两张 7 控件的极简屏），
   注册顺序 launcher, clock, settings, weather, music, album。它们不是产品功能，而是**把
   "加一个 App"这条链路当场跑通并留成样例**：新 App 的定义文件、屏、导航事件、字符表、
   真机源清单、验收一处不漏（也正是 `docs/guides/new-app-guide.md` 的样板）。

**验收（`embark_host_ui --ui-launcher-pages --delay 40 --frames 132`，退出码 0）**：磁贴 5（期望 5）/
页 2 / 点 2；顺序 = 时钟·设置·天气·音乐·相册；第 5 格文本「相册」；几何对（磁贴 x 12/68、
第 2 页容器 x 240、点 x 113/121）；每个磁贴文本 + 提示行逐字符 `lv_font_get_glyph_dsc`
**字形全在**；**真手势轨迹**横向拖动后 `scroll_x` 吸附到 240（停在第 1 页）；点第 2 页「相册」
磁贴 → 前台/当前屏 album 且 launcher 已释放；album 屏上滑 → 回 launcher（launcher 已重建、
album 已释放、上滑命中 1 次、磁贴重新建出 5 个）；**主屏上滑 → 仍停在 launcher、命中数仍为 1**。
`ctest -R embark_tests` 通过（含分页纯函数单测），`--ui` / `--ui-nav` / `--ui-weather` 三条
老验收在新注册表（6 个 App）下不回归。剧本见
[`.scratch/launcher-pages/acceptance.md`](../../.scratch/launcher-pages/acceptance.md)。

**否决过的方案**：设计器里手摆更多磁贴（见决策 1）；竖向滚动列表（用户裁决分页——240×320 是
桌面隐喻，一页 4 格不用"找"）；循环翻页与点页码跳页（用户明确不做，先不做）；给磁贴加图标
（字符表/资源都要跟着涨，收益不明）；把上滑返回做成"每个 App 自己在 onCreate 里声明"
（默认就该有，声明式会漏）；给屏根也加 `GESTURE_BUBBLE` 图省事（事件会彻底消失，见决策 4）；
让生成代码自己处理分页（生成物不允许手改，且注册表在 C++ 侧）。

**代价与影响**：主屏磁贴从此**不在设计稿里可见**——设计器里只看到两个空容器，
"磁贴长什么样"要去 `app/launcher/launcher_tiles.cpp` 看（`app/aidogui_ui/README.md` 已写明）；
磁贴是运行时对象、没有 `lv_obj` 名字，验收只能走 `launcher_tiles_*` 观测 API；
带合成点击/手势的验收**必须给 `--delay 40`**（LVGL 约每 30 ms 读一次输入，否则同一份脚本
一趟过一趟不过）；加 App 要顺手补 `screen.aid` 的 `customCharsetRows`（14px 行放磁贴标题、
12px 行放提示行）并重导出，漏了不会报错、只会是方框（验收侧有字形守卫兜底）。

**交叉阅读**：[0006](0006-launcher-and-nav-shell.md)（启动器与回程壳）、
[0011](0011-aidogui-ui-source.md)（界面来源与被本 ADR 改掉的手摆磁贴）、
[0007](0007-font-management.md)（字体红线与子集化）、
`app/launcher/launcher_pages.h`（分页几何）、`app/launcher/launcher_tiles.cpp`（磁贴绘制）、
`app/common/aidogui_ui_nav.cpp`（屏 ↔ App 导航桥）、`docs/guides/new-app-guide.md`（加 App 的操作手册）。

**2026-10-16 追加**：决策 4 的上滑返回已删除，回程改由屏内返回按钮承担 —— 见 [0014](0014-in-screen-back.md)。
