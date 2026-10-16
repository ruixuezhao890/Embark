# 屏内返回按钮（撤回上滑返回）+ 设置屏落地

用户 2026-10-16 提出（原话）：「上滑返回功能去掉，每个 app 都留一个返回按钮回 launcher」；
同一轮还报了两个界面 bug（launcher 时钟从 App 返回后不对、天气卡两字词被裁掉一半），
修复过程记在 [`.scratch/launcher-fixes/spec.md`](../../.scratch/launcher-fixes/spec.md)。本 ADR 只决定
**回程怎么走**：撤回 [0013](0013-launcher-paged-tiles.md) 决策 4 的非主屏上滑手势，回到
「每个 App 的屏里自带一个返回按钮」，并顺手把 settings 屏与「控件 → 逻辑动作」绑定通道一起落地。

1. **撤回上滑返回，回程 = 屏内「返回」按钮**。删掉 `app/common/aidogui_ui_nav.cpp` 里的
   `attach_swipe_back()` / `on_screen_gesture()` / `add_gesture_bubble()` 与上滑命中计数，
   头文件里的 `aidogui_ui_nav_swipe_back_hits()` 观测 API 一并删除；桥不再给屏根挂
   `LV_EVENT_GESTURE`。理由：① 用户 2026-10-16 明确要求去掉；② 五张 App 屏本来就有 `Btn_Back`
   （设计器「调用函数」`embark_ui_open_launcher`），手势是同一件事的第二条通道；③ `LV_EVENT_GESTURE`
   与同屏的横向拖动/滚动共存时容易误触——0013 决策 4 自己就记着「指针压在任何**可滚动**对象上时
   `indev_gesture()` 直接 return」这条反直觉机理。**对 0013 的修订**：决策 4 作废，「以后新加的
   App 自动有回程」不再成立，新 App 要自己在屏里摆一个返回按钮（模板：clock / weather / music /
   album / settings 五张现成屏）。主屏提示行文案改回「左右滑动翻页」（设计稿 `screen.aid` 里那个
   label 的 text 从「左右滑动翻页，上滑返回」改成「左右滑动翻页」，**字符表不动**——子集里多留的
   字不影响渲染）。

2. **新增「控件 → 逻辑动作」绑定 `aidogui_ui_nav_bind_action(屏名, 控件名, fn)`**。屏上有个按钮要
   「就地干点事」（设置屏的「亮度 +1」）时，`aidogui_ui_nav_bind_click` 不够用：它只认目标 App 名，
   而目标是**自己**时 `aidogui_ui_nav_request_app()` 会因 `target == foreground` 早退成 no-op；
   App 也不能在 `onCreate` 里自己 `lv_obj_add_event_cb`——控件随屏 destroy 消失，回调必须由导航桥在
   **每次装载后**统一重挂。所以新动作与 `bind_click` 共用同一张表（上限 8 条，同一控件先后声明
   两种绑定时后写生效），三个后端（aidogui / eez / null）同步加 `ui_backend_bind_action`。
   否决「给设置屏再来一张子屏、靠切屏触发逻辑」：把一次逻辑动作变成一次切屏，还绕开桥的按需
   init/destroy（[0011](0011-aidogui-ui-source.md)）。

3. **settings 屏落地（第 6 张屏）**。`screen.aid` 从 5 张屏加到 6 张：新增 `settings`（8 控件：
   返回 `Btn_Back`、标题 `L_Title`、亮度卡 `Card_Bright` 里的「亮度 +1」`Btn_Bright` 等）。
   注册表里 settings 从 [0013](0013-launcher-paged-tiles.md) 就存在（顺序 launcher, clock, settings,
   weather, music, album），只是**一直没有同名屏**——点它的磁贴只会「保持当前屏 + 一条 INFO 日志」。
   现在：`Btn_Bright` 走 `bind_action` → `SettingsApp::bump_level()`（档位 0..3 循环 +1 并广播
   `BrightnessMessage`，ClockApp 收到后更新自己的档位），`Btn_Back` 走设计器事件
   `embark_ui_open_launcher`，与其余四屏完全一致。

**验收**（短路径构建目录下执行，退出码全 0；剧本见
[`.scratch/launcher-fixes/acceptance.md`](../../.scratch/launcher-fixes/acceptance.md)）：
`ctest --test-dir S:/build-short --output-on-failure` 1/1 通过；四条宿主验收口径行分别是
`〔aidogui 验收通过〕`、`〔aidogui 导航验收通过〕（前台 clock，switches=3，导航命中 3 次，月历基准 2026-10）`、
`〔aidogui 天气验收通过〕（… 切城 1 次，城市 广州）`、
`〔主屏分页验收通过〕（磁贴 5/页 2/点 2；设置屏 已进 亮度档 0→1；前台 launcher，switches=4）`。
其中两条是本 ADR 的直接证据：`--ui-launcher-pages` 的「设置屏 已进 亮度档 0→1」（进屏 → 点
`Btn_Bright` → 真发 `BrightnessMessage` → 点 `Btn_Back` 回主屏）；`--ui-nav` 的
「点返回后 launcher/L_Time = "07:38"」（launcher 不再是设计稿死值 `11:31`，见同目录里的时钟缓存修复）。

**否决过的方案**：保留手势但换成下滑/双击（用户说的是「去掉」，换手势不解决误触）；每个 App 自己
在 `onCreate` 里声明上滑（0013 已否决，本次连手势一起撤）；把返回做成框架叠加层（导航壳
2026-10-06 已退役，[0006](0006-launcher-and-nav-shell.md)，不回头）；用设计器 `CHANGE_SCREEN`
动作回主屏（绕过桥的按需 init/destroy）；`bind_click` 点自己当逻辑动作用（no-op，见决策 2）。

**代价与影响**：新 App 多了一道手续——屏里要摆返回按钮并在设计器里挂 `embark_ui_open_launcher`，
模板现成但漏了就会「进得去出不来」；`aidogui_ui_nav_swipe_back_hits()` 删除，旧验收里依赖它的
断言（[`.scratch/launcher-pages/acceptance.md`](../../.scratch/launcher-pages/acceptance.md) 的上滑段）
随之过期，上滑以外的部分仍有效。**六块屏**的屏表与事件表更新在
`app/aidogui_ui/README.md`，新 App 操作手册更新在 `docs/guides/new-app-guide.md`。

**交叉阅读**：[0013](0013-launcher-paged-tiles.md)（决策 4 被本 ADR 撤回，其余仍然有效）、
[0011](0011-aidogui-ui-source.md)（界面来源与按需建屏）、[0006](0006-launcher-and-nav-shell.md)（导航壳退役）、
`app/common/aidogui_ui_nav.{h,cpp}`（屏 ↔ App 导航桥）、`app/settings/settings_app.cpp`（逻辑动作的样板）、
`app/aidogui_ui/README.md`（六块屏与事件表）、`docs/guides/new-app-guide.md`（加 App 手册）。
