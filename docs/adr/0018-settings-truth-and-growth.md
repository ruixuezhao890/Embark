# 设置中心：亮度真值在 HAL、设置真值在 KV、屏内交互归 App

**状态**：已定（`2026-10-18`）。**取代** [ADR 0014](0014-in-screen-back.md) 决策 3 里 settings 屏的落地形态
（8 控件 + 「亮度 +1」走 `bind_action` + 0..3 四档广播）。0014 的其余决策（屏内返回按钮唯一回程、
`bind_action` 这个机制本身）仍然有效。

## 背景与触发

「关于」与「设置」曾是两块各管一件事的小屏：设置屏 8 个控件、一个「亮度 +1」按钮、0..3 四档循环广播；
关于屏七行只读。用户要求把关于的显示**融进**设置，并把设置做成**长期设置中心**（未来清单：WiFi 连接、
手动设时间、自动对时开关、亮度、屏幕校准、IIC 校准）。

版面算术先逼出了第一个结论：内容 422 px vs 视口 276 px（320 − 36 头部 − 8 底边距），即使行距压到 20 仍溢出，
所以设置屏必须是**滚动页**（`clock` 的 `Feed` 是活样板）。而滚动在 LVGL 里有个硬前提 —— 见决策 3 的后半段。

四个必须一次定死的问题：亮度的真值放哪、设置的真值放哪、屏内交互走哪条绑定、以后加设置项要动几处。

## 决策

1. **亮度真值是 HAL**。`IDisplay` 补一个读数 `backlight()`：只有 setter 时 App 只能自己记一份「以为的」亮度，
   与硬件不同步（换屏、别的 App 改了、HAL 初始化时写过默认值都会错位）。App 只持百分比并转发：
   `set_backlight(percent)` → 落 KV → 通知。值域 **0..100、步进 10、两端钳位**（不是循环）：0..3 循环是演示品，
   用户要的是真亮度。宿主后端**不是空操作** —— `host/host_display.cpp` 按百分比缩放每帧像素，
   所以亮度在仿真里可见（0 = 全黑）。
2. **设置真值在 KV，键前缀 `ui.`**（亮度 = `ui.bright`），**不建 SettingsService**。每个设置项要么落到既有 HAL
   （亮度、墙上钟），要么只存一个标量；中间加一层服务得先回答「服务的生命周期归谁」「它比 KV 多做了什么」，
   而今天答案是零。KV 容量（32 槽 × 64 字节）够装几十个标量，`wx.city` 已经是同一个形状。
3. **屏内交互归 App，不进导航绑定表**。导航的 `ui_backend_bind_action` 表只有 **8 槽**（`kMaxClickBindings`，
   见 `app/common/aidogui_ui_nav.cpp`），6 个设置项 × 2~3 个按钮会把它打满；而且它只收裸函数指针，
   回调里拿不到 `this`。做法与 weather 一致：`lv_obj_add_event_cb` + `lv_event_get_user_data`，在
   `refresh_display()` 里重挂（控件随屏销毁）。**设计器事件仍然只干一件事 —— 切 App**。
   滚动页的配套结论：容器必须**既 SCROLLABLE 又 CLICKABLE**。LVGL 命中测试只认可点对象
   （`third_party/lvgl/src/core/lv_obj_pos.c:955`），容器不可点时，按在卡片空白处 / 文字上会命中**屏本身**，
   而滚动链只从「当前按下的对象」往上爬（`third_party/lvgl/src/core/lv_indev_scroll.c:258`）—— 从屏出发没有父级，
   容器就是拖不动的。设计器默认给每个控件 `flags.clickable: false`，必须在 .aid 里显式打开。
4. **加一个设置项固定三步**：设计器里摆控件 → App 里加一个 `SinkKind` 与处理函数 → 宿主验收加一条断言。
   版面不用再动结构（Body 已经开滚动：视口 276 px、内容 422 px），加项只是内容变长。
5. **墙上钟可写**：`ITime` 补 `set_epoch_ms`；无 RTC 的后端返回 `unsupported`，屏上显示「本机无 RTC」
   而不是假装成功。真机实现复用既有 PCF85063 驱动写完**再读一次**、重新锚定基准时间（不新写驱动、
   不碰 0x02 校准寄存器 —— 板上没有温度基准）。

## 否决过的方案

- **给设置屏做子页 `settings_1_sub`**：屏名约定支持（`app/common/ui_screen_names.h`），但设计器动作只有
  「调用函数」一种，进子页要么加个 C 钩子要么 App 侧回调 —— 等于把一次「改个值」换成一次切屏，还得自己管屏生命周期。
- **单屏不滚动、把关于七行压成 4 行**：信息量缩水，而且第二个设置项进来就又溢出。
- **`BrightnessMessage` 里带真值**：总线是同步广播、不保证有人收（boot 期在 `onCreate` 里发更是无人收），
  把状态放在消息里等于把真值放在一个没有持久化、没有读者的地方。消息现在只报「值变了」（clock 记下自用）。
- **抬 `kMaxClickBindings` 到 24**：那张表是「控件 → App / 逻辑动作」的声明式捷径，设置屏每个按钮都要读 `this` 的状态，
  抬容量只是把「回调没有上下文」的问题推后。

## 代价与影响

- **设置屏不再是「最小样板」**：薄壳纪律的样板指向改成 `app/album/album_app.cpp` / `app/music/music_app.cpp`（最小），
  `ui_backend_bind_action` 今天**没有消费者**，保留为备用路径（`docs/guides/new-app-guide.md` 已改口径）。
- **「关于」App 退役**：注册表 7 → 6 个 App，主屏磁贴 6 → 5；`app/aidogui_ui/screen.aid` 里 `about` 屏删除。
  七行信息搬进设置屏的 `Card_Info`（`L_Platform` … `L_Clock`），控件名不变。
- **KV 键 `ui.bright` 是新的持久化对象**：宿主后端是文件支持、**跨进程留存** ⇒ 宿主验收里任何写死的亮度期望值
  都是脏状态彩票。`--ui-launcher-pages` 因此先在帧 132 把亮度拨到 50% 再断言 50% → 60%。
- 设置屏控件 8 → 32；字体槽未新增（设置屏只用到 11 / 12 / 14 px 三档）。

## 验证

宿主四条验收 + `--launch` 全绿（详见 [`.scratch/settings/spec.md`](../../.scratch/settings/spec.md)）；
滚动量 146 px（内容 422 − 视口 276）、亮度 50% → 60%、关于首行形如「平台：host」。真机：设置屏装载 32 个控件、
无缺字告警、拖动可达「关于本机」。

## 交叉阅读

[0014](0014-in-screen-back.md)（屏内返回按钮与 `bind_action` 的来源，其 settings 样板被本 ADR 取代）、
[0015](0015-app-retention-and-refresh.md)（值重推）、[0003](0003-no-heap-no-exceptions.md)（零堆）、
[0011](0011-aidogui-ui-source.md)（界面来源）、`app/settings/settings_app.cpp`、`docs/guides/settings-and-preferences.md`。
