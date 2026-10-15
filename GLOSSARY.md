# Embark

Embark 是一个可移植的嵌入式应用框架：把应用逻辑拆成若干 **App**，由唯一的 UI 任务统一驱动；硬件差异由 **HAL** 的芯片能力抽象隔开。

> 命名映射：框架名 = 仓库名 = `Embark`；C++ 命名空间 `embark::`、CMake 前缀 `embark_`、include 根 `embark/`、宏前缀 `EMBARK_*`。

## Language

**App（应用模块）**：
框架的能力单元，承载一块独立的应用逻辑。它是逻辑模块，不是 FreeRTOS 任务。
_Avoid_: 任务、线程、页面、界面

**Foreground app（前台 App）**：
同时持有输入焦点、渲染权与事件循环权的那个 App，全局至多一个。
_Avoid_: 当前页、活跃任务

**Background app（后台 App）**：
非前台的 App，按自身的后台策略运行；但声明了策略 ≠ 已经在跑 —— 只有**武装过**的 App 才在后台动（默认在第一次进过前台时武装；声明 `ArmPolicy::at_boot` 的在 boot 就武装），见 **Arm**。
_Avoid_: 挂起的任务、休眠任务

**Background policy（后台策略）**：
每个 App 的自有设置，决定它退到后台之后的行为：挂起、按周期跑轻量逻辑，或拥有自己的任务。
_Avoid_: 优先级、调度策略

**Arm（后台武装 / Armed）**：
声明了后台策略的 App 在**第一次进过前台**（`onEnter` 同一帧）才真正开始跑后台：`tick` 策略此时才 `start` 已注册的定时器，`own_task` 策略此时才经 `ITaskSpawner` 创建任务（框架内部 `Framework::arm_background`，幂等；观测 `background_armed(id)` / `arm_failures()`）。武装一次长期有效 —— 之后退回后台照跑，与前后台无关；没被打开过的 App 后台完全不跑。默认前台（注册表第 0 个）在 boot 里就进过前台，所以 boot 当场武装它。声明 `AppSettings::arm == ArmPolicy::at_boot` 的 App 不等前台：boot 第 9 步（先 at_boot 轮、再默认前台轮）就武装，因为它由持久化状态驱动。语义见 ADR 0009（默认）与 ADR 0010（显式 eager）。
_Avoid_: 启动后台、初始化后台、后台开关、懒加载

**Background tick（后台节拍）**：
`BackgroundPolicy::tick` 策略的落地方式：框架在 UI 任务里用 `etl::callback_timer` 按 per-App 周期调用 `onBackgroundTick(now_ms)`；周期以 UI 循环周期（宿主 5 ms）为粒度向上取整，`period_ms == 0` 就是不跑。定时器在 boot 只**注册**不启动：默认第一次进前台（**Arm**）才 `start`，周期从那一刻起算；`at_boot` 的 App 在 boot 第 9 步就 `start`。
_Avoid_: 软件定时器、任务轮询

**Bus（消息总线）**：
`embark::Bus`（基于 `etl::message_bus`）在框架内做同步广播；同一任务内 `Framework::publish()` 直达订阅者，无人订阅的消息丢弃并计数 + WARN。
_Avoid_: 事件系统、信号槽

**MessageQueue（消息队列）**：
跨任务单向消息队列（单生产者单消费者）：`etl::circular_buffer` 满时覆盖最旧 + `etl::mutex`；框架用它收 own task 发来的信封消息，在 UI 任务里抽干回派。
_Avoid_: 邮箱、管道、ring buffer

**Own task（自拥任务）**：
`BackgroundPolicy::own_task` 逃生舱：框架经 `ITaskSpawner` 接口（不 include FreeRTOS）创建 App 自己的任务，周期跑 `onBackgroundTick`；发往 UI 的消息走 `post(CrossTaskMessage)` 入队，由 UI 任务统一派发。
_Avoid_: 线程、工作线程、协程

**CrossTaskMessage（跨任务信封）**：
own task → UI 的唯一消息型：定长（`from_app` + `seq`）、可平凡拷贝；v1 约定跨任务只走这一型，载荷语义由收发双方约定。
_Avoid_: 自定义消息直传、共享指针

**UI Task（UI 任务）**：
全局唯一的 FreeRTOS 任务，承载前台 App 的事件循环与全部 UI 渲染；宿主上由 `start_ui_task()` 静态创建（TCB 与栈都在 BSS），5 ms 一跳。
_Avoid_: 主任务、GUI 线程、主循环

**App registry（App 注册表）**：
编译期静态数组（`embark::app_registry<Apps...>()`），注册顺序即默认前台顺序；每个可执行文件用 `EMBARK_APP_TABLE(...)` 声明一次全局注册表。零堆。
_Avoid_: 动态注册、插件表

**App contract（App 契约）**：
`embark::App` 的七个钩子（`onCreate`/`onEnter`/`onPause`/`onResume`/`onExit` + 后台 tick/消息，后两者可默认）与 `settings()`（后台策略）；切换只能经 `request_switch(id)` 请求，由框架在 step 边界执行。
_Avoid_: 生命周期回调、状态机

**UI port（UI 端口）**：
`IUiPort` 把"渲染/输入泵/LVGL 处理/关窗/收尾"封装成唯一 UI 任务循环的固定阶段（tick → pump_input → process）；宿主实现 = `LvglUiPort`（`lv_timer_handler` 的唯一调用点），测试实现 = `FakeUiPort`。
_Avoid_: 渲染循环、驱动包装

**HAL（芯片能力抽象层）**：
向 App 暴露芯片能力（显示、输入、时间、持久化、日志后端、系统控制）；不承担逐外设驱动。
_Avoid_: 驱动层、BSP、外设库

**Chip capability（芯片能力）**：
HAL 暴露的最小能力单元，例如"能显示""能取时间"；与具体芯片的寄存器、引脚无关。
_Avoid_: 设备、传感器、外设

**Host backend / Target backend（宿主后端 / 目标后端）**：
同一套 HAL 接口的两类实现：前者跑在 PC 上，用于仿真、调试与自动化测试；后者跑在真机上。
_Avoid_: 模拟器、假实现、stub

**Fake backend（测试替身后端）**：
只在单元测试里替代 HAL 后端的实现：记录调用、可注入故障、不碰硬件，与宿主后端共用同一套数据格式。
_Avoid_: 模拟器、mock、stub、宿主后端

**HAL Context（HAL 聚合体）**：
把七个能力接口装配成一个引用聚合体交给上层；每个平台一份实例（宿主一份、测试自搭一份），内核不持全局单例。
_Avoid_: 服务定位器、全局单例、容器

**KV slot（键值槽）**：
持久化后端的最小存储单元，定长 92 字节：magic `EKV1`、状态、键长、值长、CRC32 与 key/value 区。
_Avoid_: 记录、条目、行

**Pointer event（指针事件）**：
描述"指针在哪、是否按下"的输入事件；`key` 恒为 0，携带的是面板坐标。
_Avoid_: 鼠标事件、触摸事件

**Key event（按键事件）**：
描述"某个键被按下/抬起"的输入事件；`key` 非 0（0..255），坐标只是按键那一刻指针在哪。
_Avoid_: 键盘事件、按钮事件

**LVGL port（LVGL 端口）**：
把 HAL 的显示与输入接到 LVGL 驱动上的那一层：时基注入、区域刷新回调、输入读取回调、日志回调；它不持有任何界面对象。
_Avoid_: LVGL 驱动、GUI 层

**Middleware view（middleware 视图）**：
在构建目录里生成的链接视图，用来满足上游写死的 `<middleware/...>` 引用；源码树保持干净，绝不把依赖目录本身加进 include 路径。
_Avoid_: include 目录、符号链接目录

**Derived printing（派生打印 / 整对象日志）**：
类型在**声明处**用 efmt 的 `E_FMT_DERIVE`（结构体）或 `E_FMT_DERIVE_ENUM`（枚举）登记打印方式；调用点只写 `{}` 加对象本身，加字段不必改日志行（类里有基类/构造函数时在类型体内写 `E_FMT_FIELDS(...)`）。框架里没有 `to_string` —— 名字只有一份。注意单条日志上限 384 字节、1 字节整型成员会被当字符打（见 `docs/reference/pitfalls.md`）。
_Avoid_: 手写 to_string、逐字段拼日志

**Error text（错误码文本）**：
给只吃 `const char*` 的出口（`fprintf`、`embark::fatal`）用的助手：`char text[24]; embark::error_text(text, error);`，内部就是派生打印，输出带全名（`embark::Error::not_found`）。
_Avoid_: to_string(Error)

**Launcher app（启动器 App）**：
注册表首位的管理型 App：它是主屏，也是 `request_home()` 的唯一目标，并且**拥有 App 列表**——用户在启动器里能进入的所有 App 入口都由它呈现（交互在主屏自己的屏内完成）。历史上的扇形半环自绘已下线（见 ADR 0006 的后续说明与 2026-10-06 后记：框架导航壳也已退役）。
_Avoid_: 桌面、主菜单、App 列表页

**App list（应用列表）**：
启动器向用户呈现的可进入 App 的有序集合，顺序即注册顺序，不含启动器自身；它是「有哪些 App」的唯一真相。
_Avoid_: 桌面图标、收藏夹、手写的磁贴清单

**Tile（磁贴）**：
启动器里代表单个 App 的可点击方块，文字是该 App 的中文标题；一个磁贴对应 App 列表里的一项。
_Avoid_: 图标、按钮、App 卡片

**Page（页）**：
应用列表在启动器上的分组单位：一页装不下时按固定容量分页，同一时刻只显示一页，左右滑动换页。
_Avoid_: 滚动列表、标签页

**Swipe-up back（上滑返回）**：
在非主屏界面上向上滑动 = 请求回主屏（等效 `request_home()`）。设备没有返回键，这是统一的回程手势，对之后新增的 App 一视同仁，不在各屏里各自实现。
_Avoid_: 返回键、导航栈后退

**request_home（回主屏请求）**：
`request_switch(registry[0])` 的语义别名：从任何 App 切回注册表首位（即启动器）。注册顺序即主屏归属，首位不可被运行期改变。导航壳退役后它仍是公共 API：App 或 AiDoGUI 屏 action 需要时显式调用。
_Avoid_: 返回首页、退出到桌面

**Unified back navigation（框架统一返回）**：
（已退役，2026-10-06）非主屏 App 在前台时，框架曾在导航壳里渲染返回键，点击等效 `request_home()`。现在回主屏的返回由 EEZ 屏内按钮承担（Flow SetPage 回 launcher 屏 → 屏观察者 `request_switch`）；App 不自己画返回按钮。
_Avoid_: 各 App 自绘返回、导航堆栈

**Nav chrome（导航壳）**：
（已退役，2026-10-06）框架级叠加层（`lv_layer_top`）：曾承载状态行与统一返回键，跨 `lv_scr_load` 持久存在。职责已移交 EEZ 屏（屏内按钮 SetPage 回程、屏上状态控件）；本条目为历史术语保留。
_Avoid_: 页面框架、装饰层

**Status bar（状态行）**：
（随导航壳退役，2026-10-06）原导航壳顶部的信息条：左侧时间（宿主墙钟；真机 RTC 未接时显示占位），右侧当前前台 App 的后台策略名（`悬` / `tick:100ms` / `own:50ms`）；不渲染节拍计数。本条目为历史术语保留。
_Avoid_: 顶部栏、通知栏

**App metadata（App 元数据）**：
注册表条目的编译期附加信息：中文标题、图标（LV_SYMBOL 码点，可选）、主题色（可选）；随 `EMBARK_APP_TABLE` 展开为零堆 constexpr 表。
_Avoid_: 运行时描述符、注册文件

**Design tokens（设计令牌）**：
命名化视觉常量（底色/面板/文字/强调色、圆角、间距、线宽），手写 LVGL 的唯一取色来源；框架壳与手写界面共用（启动器界面改由 AiDoGUI 设计工程提供），风格为深色科技风。
_Avoid_: 魔法颜色、主题对象

**AiDoGUI App（AiDoGUI 应用）**：
页面由 AiDoGUI Designer 工程 `app/aidogui_ui/screen.aid` 导出（生成代码入库，见 ADR 0011）、业务逻辑由 C++ 实现的 App；它是薄壳——onCreate/onEnter 调 `aidogui_ui_bridge_load_screen_for_app(name())` 挂自己的屏，界面数据靠 `aidogui_ui_bridge_widget(屏, 控件名)` 拿到 `lv_obj_t*` 后直接写，不手写任何 LVGL 布局、也不 include 生成代码头。跨 App 切换在**设计器里给控件挂「调用函数」动作**声明（函数名约定 `embark_ui_open_<App 名>`，生成代码的裸调用落到 `app/common/aidogui_ui_hooks.h` 的 C 入口 → `aidogui_ui_nav_request_app()`）；**不要**用 CHANGE_SCREEN 动作（`_ui_screen_change()` 会绕过桥的按需 init/destroy）。备用路径 `aidogui_ui_nav_bind_click(屏名, 控件名, App 名)` 仍在：App 里声明一次，控件随屏销毁消失、由桥在每次装载后重挂回调。制作界面的唯一方式（手绘已退役）。
_Avoid_: Studio 页面、生成式 App、Flow 应用（EEZ 时代的叫法是「EEZ App」，随 ADR 0008 归档）

**Screen name convention（屏名约定）**：
AiDoGUI 里 screen 名 == 同名 App 的名字（子页 `<app名>_<编号>_sub`）；桥据此把 AiDoGUI 的切屏翻成框架的 `request_switch`，也据此为 App 找同名屏。屏表由 `cmake/embark_aidogui_tables.cmake` 在 CMake 配置期从 `app/aidogui_ui/export/` 解析、写到构建目录的 `include/embark_aidogui_ui_tables.h`，加屏不用改 C++（重建屏要按目标 id 建：改名必须同步改所有子控件的 `parentId`，漏改的控件会被静默丢弃）。
_Avoid_: 编号映射表、手工维护的屏清单

**UI bridge（界面桥）**：
`app/common/aidogui_ui_bridge.h` 的双向接线：Embark → 界面是 `load_screen` / `load_screen_for_app`（按屏名约定载屏，**切走即释放**）、`unload_current`（显示器交给桥的空屏兜底）与 `widget(屏, 控件名)`（**控件表 = 界面数据接口**，按名拿 `lv_obj_t*`）；界面 → Embark 是屏观察者：**每次装载屏都回调**（桥自己发起的 `load_screen*` 也通知——观察者靠这条把点击绑定重挂到新控件上，备用路径；主路径是设计器里挂的「调用函数」事件；请求切到「已是前台」的 App 时框架直接 return，不会成环）。字体也由桥加载（BIN 走 'S' 盘），失败只降级告警。App 只调桥，不 include 生成代码头。
_Avoid_: 生成代码直连、App 里 include ui.h

**Style scope（样式域）**：
样式定义的归属边界：手写 C++ 只用 design tokens；AiDoGUI 页面只在设计工程（`screen.aid`，随生成代码入库）里定义样式。同一视觉常量不得跨域散落。
_Avoid_: 魔法颜色、主题对象

**Font charset table（字体字符表）**：
设计工程里 `i18n.customCharsetEnabled + i18n.customCharsetRows`（GUI：「语言 / 自定义字符」页）显式列出每个 (字体, 字号) 要打进 BIN 的字符。默认的自动子集只收**设计期文字**，动态数字（时间、日期、天数）会缺字形变方框；开了这张表就完全以表为准——每行必须是「设计期文字 ∪ 运行期字符」的超集，且每个用到的 (字体, 字号) 都得有一行。
_Avoid_: 靠设计稿文字凑字符、运行时才发现方框

**Foreground tick（前台节拍）**：
App 契约的可选钩子 onForegroundTick(now_ms)：框架在 UI 任务每跳调用前台 App 的它（在 lv_timer_handler 之后）。AiDoGUI 生成代码没有「每帧把变量刷进控件」的变量层，所以界面 App 通常不需要它（`aidogui_ui_bridge_tick()` 是保留契约形状的空实现）。默认空实现，手写 App 可忽略。
_Avoid_: UI tick、渲染回调

**Static subset font（静态子集字库）**：
lv_font_conv 生成、编译进 flash 的 C 数组字库，只收录 UI 实际用到的字符（框架壳文本全走它）；覆盖范围由清单文件声明、构建期审计。
_Avoid_: 全量字库、外挂字体文件

**IFileSystem（文件服务）**：
框架级字节流接口（open/read/seek/close）；宿主后端读可执行文件旁目录（与持久化同模式），真机 SD 后端待后续 issue（需先加 HAL 面，当前 `spi_transfer` 如实返回 `unsupported`）。
_Avoid_: 文件系统、SD 驱动

**Font ledger（字库记账）**：
运行时字库的显式资源管理：字节预算 + 引用计数，包装 `lv_font_load` 与 `lv_font_free`，独立于 LVGL 全局堆预算；超预算返回 `no_space`。真机侧后续映射到 PSRAM 显式分配。
_Avoid_: 字体缓存、字库池

**Coverage audit（缺字审计）**：
构建期检查：扫描源码中的 UI 字符串，对照静态子集字库的覆盖清单，发现字库外字符即失败（CI 门禁），从源头防豆腐块。
_Avoid_: 运行时缺字检查、字体检测

**Weather App（天气 App）**：
薄壳 App，界面 = AiDoGUI 屏 `weather`（屏名约定 == App 名）。数据来自编译期桩 `app/weather/weather_data.h`；城市选择存 `hal().storage` 的 `wx.city`（一个字节）。屏上 `L_City` / `L_NIcon` / `L_NTemp` / `L_NDesc` 与 `L_D{i}_{Dow,Icon,Desc,Range}`（i = 0..4，0 = 今天）由 App 按名写；`Btn_CityPrev` / `Btn_CityNext` 是 App 侧 `lv_obj_add_event_cb` 挂的回调 —— 设计器事件在本工程里只用来**切 App**，屏内交互归 App。
_Avoid_: 天气组件、天气页

**Weather card（天气卡）**：
主屏 launcher 上的 `Card_Weather`（`L_WIcon` / `L_WTemp` / `L_WCity` / `L_WDesc`），既是显示控件也是进天气 App 的入口（设计器「调用函数」`embark_ui_open_weather`）。它不是第二份数据：`LauncherApp::refresh_weather()` 读同一个持久化键、同一张桩表 —— 卡片只是同源数据的另一种画法（在天气屏换了城市，回主屏卡片跟着变）。
_Avoid_: 天气控件、天气小组件

**Weather stub（天气数据桩）**：
`app/weather/weather_data.h` 里的编译期常量表（4 城 × 5 天：日期标签 / 天气字 / 一句话 / 温度区间），零堆零 IO。框架当前没有任何网络能力，所以天气是桩不是真数据 —— 决策与将来怎么换成真数据见 docs/adr/0012-weather-data-source.md。
_Avoid_: mock 数据、假数据源

**City table（城市表）**：
天气桩里的城市维度（北京 / 上海 / 广州 / 深圳），下标就是持久化键 `wx.city` 的值；切换是环形的（`weather_shift_city`），没有搜索与输入。
_Avoid_: 城市列表、地区表

**Glyph coverage guard（字形守卫）**：
`--ui-weather` 验收里的检查：把控件当前文本逐字符喂给 `lv_font_get_glyph_dsc`，缺字形即判失败并打出 U+ 码点。字体按 (字体, 字号) 子集化，漏字符**不会**编译或运行报错，屏幕上只是一个方框而验收照样「通过」—— 这条把方框变成可断言的失败（字符表见 Font charset table）。
_Avoid_: 缺字检测、字体验证
