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
非前台的 App。今天**后台不是默认形态**：App 默认 `suspend`（退到后台什么都不跑），要后台体的 App 必须显式申请（见 **Retention**）；声明了策略也 ≠ 已经在跑——只有**武装过**的 App 才在后台动（默认在第一次进过前台时武装；声明 `ArmPolicy::at_boot` 的在 boot 就武装），见 **Arm**。
_Avoid_: 挂起的任务、休眠任务

**Background policy（后台策略）**：
每个 App 的自有设置（`AppSettings`），决定它退到后台之后的行为：`suspend`（默认）、按周期跑轻量逻辑（`tick`），或拥有自己的任务（`own_task`）。**后台是例外不是默认**：今天全仓没有 App 声明后台体，能不能吃后台由宿主验收当**不变量**断言（[ADR 0015](docs/adr/0015-app-retention-and-refresh.md)）。
_Avoid_: 优先级、调度策略

**Arm（后台武装 / Armed）**：
声明了后台策略的 App 在**第一次进过前台**（`onEnter` 同一帧）才真正开始跑后台：`tick` 策略此时才 `start` 已注册的定时器，`own_task` 策略此时才经 `ITaskSpawner` 创建任务（框架内部 `Framework::arm_background`，幂等；观测 `background_armed(id)` / `arm_failures()`）。武装一次长期有效 —— 之后退回后台照跑，与前后台无关；没被打开过的 App 后台完全不跑。默认前台（注册表第 0 个）在 boot 里就进过前台，所以 boot 当场武装它。声明 `AppSettings::arm == ArmPolicy::at_boot` 的 App 不等前台：boot 第 9 步（先 at_boot 轮、再默认前台轮）就武装，因为它由持久化状态驱动。语义见 ADR 0009（默认）与 ADR 0010（显式 eager）。**今天没有 App 申请后台体**，所以也没有 App 会被武装（[ADR 0015](docs/adr/0015-app-retention-and-refresh.md)）。
_Avoid_: 启动后台、初始化后台、后台开关、懒加载

**Background tick（后台节拍）**：
`BackgroundPolicy::tick` 策略的落地方式：框架在 UI 任务里用 `etl::callback_timer` 按 per-App 周期调用 `onBackgroundTick(now_ms)`；周期以 UI 循环周期（宿主 5 ms）为粒度向上取整，`period_ms == 0` 就是不跑。定时器在 boot 只**注册**不启动：默认第一次进前台（**Arm**）才 `start`，周期从那一刻起算；`at_boot` 的 App 在 boot 第 9 步就 `start`。**今天没有 App 走这条路**：clock 曾用 `tick` / 100 ms，2026-10-16 退役，改走**前台渲染型刷新**（[ADR 0015](docs/adr/0015-app-retention-and-refresh.md)）。
_Avoid_: 软件定时器、任务轮询

**Retention（留存申请）**：
「离开之后还需不需要**往前推进**」的声明：需要 = 显式申请后台体（`BackgroundPolicy::tick` / `own_task`），不需要 = 默认 `suspend`。留存申请是**例外的**，因为绝大多数 App 的「回来还在原页面」靠的是 **View state**，不是接着跑。见 [ADR 0015](docs/adr/0015-app-retention-and-refresh.md)。
_Avoid_: 常驻、保活、后台开关

**View state（视图状态）**：
App 里「下次回来要原样」的那部分状态（页码、翻到的月份、播放位置）。屏每次装载都是**新建**的（桥纪律「切走即 destroy / 回来即 init」，控件值就是设计稿的初值），所以视图状态必须由 App 自己存成成员变量：`onPause` 记下、屏装载后的重推里恢复。**不进 KV**（KV 是掉电才需要的持久化）。
_Avoid_: 缓存、会话、界面状态

**Frozen（冻结）**：
「离开即停 CPU、状态留 RAM、回来原样继续」的一种留存方式（红白机、编辑器这类状态大的 App）。冻结**不需要**申请后台体——因为离开后本就不推进；它要的是 **View state** 与内存预算。
_Avoid_: 挂起任务、休眠进程

**Foreground-rendered refresh（前台渲染型刷新）**：
「只在看得见时才需要刷新」的显示（主屏分钟、天气卡、clock 秒）。做法是 `onForegroundTick`（每帧、仅前台）+ 进屏 / 回屏重算 + 事件通知，**不占后台节拍**。判据两句话：离开后需要推进吗（不需要）、回来时需要原样吗（不需要，重算即可）。
_Avoid_: 后台刷新、轮询守卫

**Value repaint（值重推）**：
屏装载完成后，把控件值按 App 当前状态重写一遍的机制：`ui_backend_bind_repaint(屏名, fn)` 在 `onCreate` 声明一次（实现 `aidogui_ui_nav_bind_repaint`，容量 8 条），桥的屏观察者每次装载后回调——与点击绑定 / 逻辑动作绑定对称。**定位**：桥已规定「每次屏装载都要做的事」只挂在观察者一处，值重推是唯一还没挂上去的那件事，漏了就会看到控件停在设计稿（launcher 时间停在 `11:31` 就是这么来的）。
_Avoid_: 手动同步、onEnter 里清缓存

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
向 App 暴露芯片能力（显示、输入、时间、持久化、日志后端、系统控制），并且是**服务层的唯一注入面**。
**2026-10-18 起 HAL 不是最上层**：它之上还有一层 Service layer（设备服务 + 公共资源），
见 [ADR 0019](docs/adr/0019-service-layer-and-public-hardware.md)。
它**不承担业务语义**（播放列表、天气数据这类归 App），但「逐设备驱动」这件事现在有地方写了 —— 写在服务层，一遍，不让每个 App 各写一份。
_Avoid_: 驱动层、BSP、外设库

**Chip capability（芯片能力）**：
HAL 暴露的最小能力单元，例如"能显示""能取时间"；与具体芯片的寄存器、引脚无关。
_Avoid_: 设备、传感器、外设

**Service layer（服务层）**：
HAL 与 App 之间的一层，装**设备服务 + 公共资源**：`IImu` / `IAudio` / `INetwork` / `IGpio` 这类"每台设备都有的硬件"写成接口 + 平台实现，只写一遍，不让每个 App 各自写驱动（ADR 0019）。判据是**接口形状稳定**：「换一颗同类芯片，这个接口要改吗？」不需要才进服务层。
**硬规则：服务接口头里不许出现芯片型号名** —— `IImu` 可以、`IQmi8658` 不行（型号名只许出现在实现文件、`platform/esp32/src/esp32_board.h`、注释与文档里）。这条禁令没有编译器约束，靠 review 与 `AGENTS.md` 兜住。
**业务服务不进这一层**（`IWeather` / `IMusic` 出局）：业务是用户自己写的那一半，塞进基建等于把 token 成本又还给他。
服务接口头也放在 `include/embark/hal/`（`context.h` 是唯一注入点；另开 `embark::services::` 就要开第二个注入面，每个 App 得拿两份引用）—— 层名叫「服务层」，命名空间仍是 `embark::hal`。
_Avoid_: 中间件、适配层、驱动层、业务服务层

**ImuSample（姿态采样）**：
`IImu::read()` 交出来的一份读数，单位是**物理量**：加速度 g（1.0 = 一个重力）、角速度 dps（度/秒）、温度 °C。**不是原始 LSB** —— LSB 是量程的函数（同一颗芯片 ±2 g 与 ±16 g 差 8 倍），给 LSB 就等于把某颗芯片的细节写进接口（ADR 0002 的原始判据），换算落在实现里。
坐标系由实现定义（接口只承诺右手系 + 静止时合矢量模长 ≈ 1 g）：宿主是 +X 右 / +Y 上 / +Z 朝用户（屏幕朝上平放 az = -1.0），真机按芯片贴合方向换算，符号可能相反 —— 所以验收只要求 `az ≈ ±1.0`。
_Avoid_: 原始值、raw、六轴数据、传感器读数

**Host backend / Target backend（宿主后端 / 目标后端）**：
同一套 HAL 接口的两类实现：前者跑在 PC 上，用于仿真、调试与自动化测试；后者跑在真机上。
_Avoid_: 模拟器、假实现、stub

**Fake backend（测试替身后端）**：
只在单元测试里替代 HAL 后端的实现：记录调用、可注入故障、不碰硬件，与宿主后端共用同一套数据格式。
_Avoid_: 模拟器、mock、stub、宿主后端

**HAL Context（HAL 聚合体）**：
把本平台提供的能力与服务装配成一个引用聚合体交给上层；每个平台一份实例（宿主一份、真机一份、测试自搭一份），内核不持全局单例。
两段组成：前半是**能力接口的引用**（time / storage / log / system / bus，必有），后半是**可空指针**（display / input / filesystem，以及服务层的 imu / audio / network —— 没有这块硬件就是 nullptr，用到的模块自己判空）。
三个服务槽都已接上：`imu`（issue 08，宿主 `HostImu` / 真机 `Esp32Imu`）、`audio`（issue 09，宿主 `HostAudio` / 真机 `Esp32Audio`）、`network`（issue 10，宿主 `HostNetwork` / 真机 `Esp32Network`）。语义与 display/input 一致：没有这块硬件才留 nullptr，App 一律先判空。
**加成员编译器不会报错**（C++17 聚合初始化允许初始化项少于成员数，缺的按值初始化成 nullptr），所以装配点靠清单 + grep 保证，不靠编译器（见 `.scratch/app-platform/issues/07-service-layer.md`）。
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

**In-screen back（屏内返回）**：
每个 App 的屏里自带一个「返回」按钮，点击走设计器事件 `embark_ui_open_launcher`（等效 `request_home()`）回主屏。回程的**唯一**通道：框架不提供统一手势或返回键，新 App 要自己摆按钮（六张现成屏可抄，见 `app/aidogui_ui/README.md`；[ADR 0014](docs/adr/0014-in-screen-back.md)）。
_Avoid_: 返回键、导航栈后退、上滑返回

**Swipe-up back（上滑返回）**：
（已退役，2026-10-16）非主屏界面向上滑 = 请求回主屏（`request_home()`）的统一回程手势，随 ADR 0013 引入、被 ADR 0014 撤回：手势与同屏的横向翻页/滚动共存时容易误触，且五张 App 屏本来就有返回按钮。回程现在由**屏内返回按钮**承担（见上一条），本条目为历史术语保留。
_Avoid_: 返回键、导航栈后退

**request_home（回主屏请求）**：
`request_switch(registry[0])` 的语义别名：从任何 App 切回注册表首位（即启动器）。注册顺序即主屏归属，首位不可被运行期改变。导航壳退役后它仍是公共 API：App 或 AiDoGUI 屏 action 需要时显式调用。
_Avoid_: 返回首页、退出到桌面

**Unified back navigation（框架统一返回）**：
（已退役，2026-10-06）非主屏 App 在前台时，框架曾在导航壳里渲染返回键，点击等效 `request_home()`。现在回主屏的返回由界面屏内的返回按钮承担（AiDoGUI 里给控件挂「调用函数」事件 → `embark_ui_open_launcher` → `request_switch`）；App 不自己画返回按钮。
_Avoid_: 各 App 自绘返回、导航堆栈

**Nav chrome（导航壳）**：
（已退役，2026-10-06）框架级叠加层（`lv_layer_top`）：曾承载状态行与统一返回键，跨 `lv_scr_load` 持久存在。职责已移交界面屏（屏内返回按钮、屏上状态控件）；本条目为历史术语保留。
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
页面由 AiDoGUI Designer 工程 `app/aidogui_ui/screen.aid` 导出（生成代码入库，见 ADR 0011）、业务逻辑由 C++ 实现的 App；它是薄壳——onCreate/onEnter 调 `aidogui_ui_bridge_load_screen_for_app(name())` 挂自己的屏，界面数据靠 `aidogui_ui_bridge_widget(屏, 控件名)` 拿到 `lv_obj_t*` 后直接写，不手写任何 LVGL 布局、也不 include 生成代码头。跨 App 切换在**设计器里给控件挂「调用函数」动作**声明（函数名约定 `embark_ui_open_<App 名>`，生成代码的裸调用落到 `app/common/aidogui_ui_hooks.h` 的 C 入口 → `aidogui_ui_nav_request_app()`）；**不要**用 CHANGE_SCREEN 动作（`_ui_screen_change()` 会绕过桥的按需 init/destroy）。备用路径 `aidogui_ui_nav_bind_click(屏名, 控件名, App 名)` 仍在：App 里声明一次，控件随屏销毁消失、由桥在每次装载后重挂回调；**控件值**同理，由 `ui_backend_bind_repaint(屏名, fn)` 在每次装载后重推（**Value repaint**，ADR 0015）。制作界面的唯一方式（手绘已退役）。
_Avoid_: 生成式 App、页面工程、设计器应用

**Screen name convention（屏名约定）**：
AiDoGUI 里 screen 名 == 同名 App 的名字（子页 `<app名>_<编号>_sub`）；桥据此把 AiDoGUI 的切屏翻成框架的 `request_switch`，也据此为 App 找同名屏。屏表由 `cmake/embark_aidogui_tables.cmake` 在 CMake 配置期从 `app/aidogui_ui/export/` 解析、写到构建目录的 `include/embark_aidogui_ui_tables.h`，加屏不用改 C++（重建屏要按目标 id 建：改名必须同步改所有子控件的 `parentId`，漏改的控件会被静默丢弃）。
_Avoid_: 编号映射表、手工维护的屏清单

**UI bridge（界面桥）**：
`app/common/aidogui_ui_bridge.h` 的双向接线：Embark → 界面是 `load_screen` / `load_screen_for_app`（按屏名约定载屏，**切走即释放**）、`unload_current`（显示器交给桥的空屏兜底）与 `widget(屏, 控件名)`（**控件表 = 界面数据接口**，按名拿 `lv_obj_t*`）；界面 → Embark 是屏观察者：**每次装载屏都回调**（桥自己发起的 `load_screen*` 也通知——观察者靠这条把点击绑定重挂到新控件上、并调 App 的值重推钩子（**Value repaint**）；请求切到「已是前台」的 App 时框架直接 return，不会成环）。字体也由桥加载（现在是 SD 卡上的整份 TTF，见 Runtime font；BIN 时代走同一条 'S' 盘），失败只降级告警。App 只调桥，不 include 生成代码头。
_Avoid_: 生成代码直连、App 里 include ui.h

**Style scope（样式域）**：
样式定义的归属边界：手写 C++ 只用 design tokens；AiDoGUI 页面只在设计工程（`screen.aid`，随生成代码入库）里定义样式。同一视觉常量不得跨域散落。
_Avoid_: 魔法颜色、主题对象

**Font charset table（字体字符表）**：
设计工程里 `i18n.customCharsetEnabled + i18n.customCharsetRows`（GUI：「语言 / 自定义字符」页）显式列出每个 (字体, 字号) 要打进 BIN 的字符。**已退役**（ADR 0017）：TTF 全量 cmap 不存在「要打进 BIN 的字符集」这个概念，这张表只在还导出 BIN 的旧工程里才有意义。它当年治的病是「自动子集只收设计期文字，动态数字（时间、日期、天数）缺字形变方框」——TTF 侧这个病由 Coverage audit 直接兜住。
_Avoid_: 靠设计稿文字凑字符、运行时才发现方框

**Foreground tick（前台节拍）**：
App 契约的可选钩子 onForegroundTick(now_ms)：框架在 UI 任务每跳调用前台 App 的它（在 lv_timer_handler 之后）。AiDoGUI 生成代码没有「每帧把变量刷进控件」的变量层，所以界面 App 通常不需要它（`aidogui_ui_bridge_tick()` 是保留契约形状的空实现）。默认空实现，手写 App 可忽略。
_Avoid_: UI tick、渲染回调

**Static subset font（静态子集字库）**：
lv_font_conv 生成、编译进 flash 的 C 数组字库，只收录 UI 实际用到的字符；覆盖范围由清单文件声明、构建期审计。
**ADR 0017 之后它的角色变了**：不再是"界面的字"，只是**无卡时的兜底**（Runtime font 是主路径）；清单 `tools/font/embark_zh_14.json` 只是"这份兜底字库收了哪些字"的存档 —— 缺字审计已改为对照 TTF cmap，不再读它。
当前唯一一份：`assets/fonts/embark_zh_14.c`（Noto Sans SC 子集，14 px / 4 bpp / 压缩位图，184 字形 = ASCII 95 + 界面文案 84 + 图标 5），清单 `tools/font/embark_zh_14.json`，生成脚本 `tools/font/gen_font.mjs`；已在 `config/lv_conf.h` 里接成 **LVGL 默认字体**（`LV_FONT_CUSTOM_DECLARE` + `LV_FONT_DEFAULT`），链接时要整体收进归档（宿主 `--whole-archive`、真机 `WHOLE_ARCHIVE`），否则成员会被丢弃、字库根本进不了镜像。
_Avoid_: 全量字库、外挂字体文件

**IFileSystem（文件服务）**：
框架级只读字节流接口（open/read/seek/close/size），句柄 `FileHandle = uint16_t`（0 = 无效）。两个后端同形（定长槽 `filesystem_max_open_files`、拒绝空路径 / 绝对路径 / `..` / 含 `:`）：宿主 `platform/host/host_filesystem.{h,cpp}`（根 = `EMBARK_HOST_FS_ROOT`，默认可执行文件旁 `embark_host_fs/`）、真机 `platform/esp32/src/esp32_filesystem.{h,cpp}`（根 = SD 挂载点，由 `attach_root()` 在挂载成功后接上）。SD 本身在 `platform/esp32/src/esp32_sd.{h,cpp}`：SPI 模式 `sdspi` + FATFS（`esp_vfs_fat_sdspi_mount`，SPI2 主机，挂载点 `/sdcard`），板级开关 `EMBARK_SD_ENABLED` 默认 0、四个信号脚默认 `-1`（`esp32_board.h` 末尾有 `static_assert` 门槛），没插卡就一行日志跳过、其余功能不受影响。在 `hal::Context` 里它是**可空指针**：真机只在 SD 挂载成功时指向真后端，否则是 `nullptr`（与 display/input 同类的可空语义，而不是"给了个恒 `not_ready` 的实例"）。
_Avoid_: 文件系统、SD 驱动

**Font ledger（字库记账）**：
**已退役**（ADR 0017）：它记的是「路径 + 文件字节」，而 TTF 时代记账对象是**字形缓存字节 + 字号**（Glyph cache budget），路径概念整个消失；它唯一的消费者是 `platform/esp32/src/esp32_bringup.cpp:130` 的探针，桥从来没走过。以下为历史形态（保留作参照）——
当时的形态：字节预算 + 引用计数，包装 `lv_font_load` 与 `lv_font_free`，独立于 LVGL 全局堆预算；超预算返回 `no_space`。内核层 `include/embark/font_ledger.h` + `src/embark/font_ledger.cpp`（零堆、定长表、不依赖 LVGL）+ 平台侧 `platform/common/font_service.{h,cpp}`（把 LVGL 的 `'F'` 盘注册到 `hal::IFileSystem`；同路径复用只 +1 引用，引用归零才 free）。容量见 `config/embark_limits.h`：`font_budget_bytes` 100 KB、`font_max_loaded` 4、`font_path_bytes` 64。真机侧曾映射到 **PSRAM 定容池**（见 Font alloc scope）：字库不占 LVGL 的 48 KB 静态池，加载前后 `lvgl_peak_bytes` 不变。**issue 04 已把这四个文件与三个常量一起删掉**（含 `tests/kernel/test_font_ledger.cpp`、`tests/host/test_font_service.cpp`）。
_Avoid_: 字体缓存、字库池

**Font alloc scope（字库分配作用域）**：
包住「建字号 / 栅格化字形」的一段"接下来这批分配算字体的"标记：`font_alloc_scope_begin()` / `font_alloc_scope_end()` + 观测 `font_alloc_pool_bytes()` / `font_alloc_outstanding_bytes()`（`platform/common/font_alloc_scope.h`）。为什么需要它：LVGL 8.3.11 的 `lv_font_loader.c` 全部走 `lv_mem_alloc`，而钩子是我们自己的 —— 只能按"当前是否在字库作用域里"决定这次分配落 LVGL 静态池还是真机的 PSRAM 池；**归还**不看作用域、只看指针落在哪块池。真机实现在 `platform/esp32/src/esp32_lvgl_mem.cpp`（`heap_caps_malloc(MALLOC_CAP_SPIRAM)` 懒建 `font_psram_bytes` = **192 KB** 定容池），宿主实现是空操作（有真实堆，作用域退化为空标记）。ADR 0017 之后调用方从 `font_service` 换成桥里的字体加载（整份 TTF 常驻 + 每字号字形缓存都落这块池），同一头文件再长出两个入口：`font_alloc_pool_budget_bytes()` 报「计划容量」（池懒建、首次分配才 malloc，而配额必须在建字体前定下，所以不能问 `font_alloc_pool_bytes()`）、`font_buffer_alloc()` / `font_buffer_free()` 收「常驻对象」（3.85 MB TTF 缓冲；两者都 `[[nodiscard]]`、`noexcept`，失败返回 `nullptr` 由调用方降级）。
_Avoid_: 字体内存钩子、PSRAM 分配器

**Runtime font（运行期字体）**：
`lv_tiny_ttf` 从 SD 卡上的**整份** TTF 建出来的 `lv_font_t`：每个字号一个实例，`fallback` 恒 NULL（单 face，ADR 0017），位深恒 8 bpp。TTF 数据必须常驻（`third_party/lvgl/src/extra/libs/tiny_ttf/lv_tiny_ttf.c:206` 只存指针不复制）——真机放 PSRAM，宿主直接读仓库里的文件。与 Static subset font 的分界：前者从卡里"读字"，后者从 flash 里"查表"。
_Avoid_: TTF 字库、动态字体

**Font slot（字号槽）**：
AiDoGUI 生成代码里的全局 `lv_font_t*`，**每个不同的 `textFontSize` 一个**（今天是 `ui_font_ui_heiti_{11,12,14,22,28,48,56}` 共 7 个，加字号 = 设计器里改一下重导出，cmake 零改动），`app/aidogui_ui/export/screens/*.c` 有 75 处直接把它们塞进控件。**槽是间接层**：换字体实现（BIN → TTF）只换槽里塞的指针，7 张屏零改动。`export/ui.c` 是设计器生成物（`tools/aidogui/export.ps1:62` 会清空 `export/` 重生），所以加载逻辑要落在桥或后处理里，手改会被覆盖。
_Avoid_: 字体句柄表、ui_font 全局变量

**Glyph cache budget（字形缓存预算）**：
每个字号实例的 LRU 字节上限（传进 `lv_tiny_ttf` 的 `cache_size`，`third_party/lvgl/src/extra/libs/tiny_ttf/lv_tiny_ttf.c:214` 收字节，`third_party/lvgl/src/misc/lv_lru.c:85-86` 记账）。8 bpp ⇒ 单字形 ≈ w×h 字节，所以预算是「该字号一屏最大并发字形数 × w×h」，不是「字体文件多大」。**预算必须严格小于可用池**，否则每帧都在重新栅格化（ADR 0016 那次 64 KB > 59 KB 可用池，首帧 716 ms）。
已定稿（`app/common/font_quota.h`，ADR 0017 第 6 条）：**`quota(px) = max(4 KB, 8 × px²)`**，不是一张手写的字号表 —— 字号是设计器决定的（每个不同 `textFontSize` 一个槽），表只能覆盖"今天这几个"，一旦出现表外字号就会漏算。现有 7 档（11/12/14/22/28/48/56 px）算下来是 4/4/4/4 KB 与 6,272/18,432/25,088 B，合计 **66,176 B ≈ 64.6 KB**（旧实测表是 118 KB，随 issue 03 删除）。两条硬约束：配额 ≤ px² 是 `lv_lru` 取模**除零 panic**（`lv_lru.c:83` 桶数 = `cache_size/px²`、`:301` 取 `h % 桶数`），所以 `8×` 保底 `≥ 2×px²`；4 KB 下限同时兜住"单字形位图装不下 ⇒ 整字不画"。配额总和只按字库区容量的 **3/4** 收（字形在池里实占「16 字节对齐位图 + 48 字节块头」≈ 记账值 1.3 倍，再加每字号一个 `lv_lru` 哈希桶）；超了由 `font_quota::fit()` 按**字号从大到小**砍半、下限 4 KB，砍到底仍装不下只 `ELOG_ERROR` 报三个数字，**不 assert 不复位**（ADR 0016 的复位教训）。算法是纯整数算术（不 include LVGL），单测 `tests/kernel/test_font_quota.cpp` 钉住四条：≥ 4 KB、> px²、单调不减、大字号装得下 8 个字形。init 会打两行账：配额合计、以及「已用 / 字库区」；命中率不作数（`lv_lru_t` 藏在私有 `ttf_font_desc_t` 里，为读它复制私有布局或改 third_party 都不值）。
_Avoid_: 字体缓存、cache 大小

**Boot self-check screen（启动自检屏）**：
开机前约 9 秒的占位屏（`app/common/aidogui_ui_bridge.cpp` 的 `splash_*` 系列）—— 那 9 秒在同步读 3.85 MB 的 TTF，LVGL 一帧也刷不出来，全黑看着像死机。形态是 Linux 开机自检（POST）：`[OK]/[..]/[!!]/[  ]` 逐行打勾 + 整行配色 + 右下角 `t=<elapsed> ms`；**形式像自检，内容全真**（查不到就 `[!!]` 打红，不假装绿）。硬件行由平台注入：`aidogui_ui_bridge_set_boot_checks()` 收最多 6 条 `{label, detail, ok}`（`main.cpp` 填 flash / psram / display / touch / sdcard / font pool），不注入时桥退回自知的 3 行。**节奏靠读卡进度免费带出来**：每 256 KB 回一次进度回调，顺带把「到点」的行翻绿 ⇒ 零额外启动时间（实测 60/1323/2580/3214/4470/5106 ms，翻绿只能落在读块边界上）。两个硬约束：文案常量 `static_assert(ascii_only(...))` 钉死（那会儿只有内置 14 px 的 184 字形，中文必方框）；算时间**不能用 `lv_tick_get()`** —— `LvglPort::tick()` 只在 UI 循环里推进，阻塞期恒 0，得由 `set_boot_checks()` 注入平台时钟（`esp_timer_get_time()/1000`）。
_Avoid_: splash、开机动画、loading 页

**Line breaking gap（断行缺口）**：
LVGL 8.3.11 的已知排版短板，**已实测钉进 `tests/host/test_text_wrap.cpp`（issue 07，宿主 ctest 里跑）**：
① CJK 逐字断行只认 U+4E00–U+9FA5（`third_party/lvgl/src/misc/lv_txt.h:165-184`），假名/谚文/CJK 扩展区/全角标点都不算断点；
② 全仓没有**行首禁则**（kinsoku）⇒ 断点只能落在断字符之前，「，」「。」因此被推到下一行行首（实测行首出现标点）；
③ 找不到断点的长串（假名串、拉丁长单词）按宽度**硬切**：不溢出，但也没有连字符、没有词边界保护。
实测数字（14 px、行宽 100 px）：汉字 / 假名 / 全角标点各 14 px 宽、`A` 11 px，一行 7 个汉字（98 px）。
阅读器先用它跑起来（ADR 0017 第 10 条）；真要治只能 reader 自算断行 —— `_lv_txt_get_next_line()` 就是 `lv_label` 排版时走的同一个函数，可以先切成行再塞控件（要独立量宽度用 `lv_txt_get_size()`）。
_Avoid_: 排版 bug、换行问题

**Coverage audit（缺字审计）**：
构建期检查：扫描源码里所有会上屏的字符串，逐字符问**最终字体**认不认识，缺字即失败（CI 门禁），从源头防豆腐块。ADR 0017 之后对照物是**卡上那份 TTF 的 cmap**（`app/aidogui_ui/assets/font/ui_heiti.ttf`，脚本自己解 format 4/12），不再读子集清单 `tools/font/embark_zh_14.json`。已落地 `tools/font/audit_glyphs.mjs`（Node、零依赖）：扫 `app/` `src/` `platform/` 里 18 个会上屏的 setter（`UI_TEXT_SETTERS`），缺字打印字符 + 文件:行号并 exit 1；另有 `--print-required`（当前文案用到的全部字符）、`--selftest`、`--ttf`、`--include-symbols`、`--roots`。只换数据源不清白名单会把"没扫到"误报成全绿，所以脚本带 `SUSPECT_PATTERNS` 守卫：名字像 setter 却不在白名单的调用会被点名并 exit 1。CI 里是 `font-audit` job。LVGL 图标区 `U+F000–U+F8FF` 默认豁免（内置字形，不进 cmap），`--include-symbols` 可关掉豁免。
_Avoid_: 运行时缺字检查、字体检测

**Settings App（设置 App / 设置中心）**：
设置中心的载体 App（屏名 `settings`），App 列表里的普通一员。它承载全部设置项，同时也是「关于本机」七行只读信息的所在地 —— 原「关于」App 已退役。设置项的共同形态是「标题 + 当前值 + 就地加减」：改一下立刻生效、立刻落 KV，**没有**「保存」按钮、没有草稿态、也没有设置服务层（[ADR 0018](docs/adr/0018-settings-truth-and-growth.md)）。首屏即滚动页（视口 276 px / 内容 422 px）。
_Avoid_: 系统设置、偏好页、SettingsService

**Setting row（设置项）**：
设置屏上的一行可改项：一个标题 + 当前值 + 至多两个加减控件，值变了就生效。加一行有固定的三步：设计器里摆控件 → App 里加一个 `SinkKind` 与处理函数 → 宿主验收加一条断言。真值**不在 App 里**：能落到 HAL 的（亮度、墙上钟）以 HAL 为准，HAL 没有的才只存 KV。
_Avoid_: 选项、开关项、配置条目

**Brightness percent（亮度百分比）**：
屏幕背光的亮度值，`0..100` 整数百分比，10 为一步、两端**钳位**（不是循环）。真值是 HAL：`IDisplay::set_backlight(percent)` 写、`backlight()` 读（[ADR 0018](docs/adr/0018-settings-truth-and-growth.md) 第 1 条）；设置 App 只是把同一个数转发给 HAL 并落进 KV 键 `ui.bright`，供下次开机恢复。`BrightnessMessage` 只报「值变了」这一事实（clock 记下自用），不携带真值。宿主后端**不是空操作**：它按百分比缩放每次 flush 的像素，所以 0% 是全黑。
_Avoid_: 亮度档、背光等级、level 0..3

**About this device（关于本机）**：
设置屏底部的七行只读信息（平台 / 版本 / 可用堆 / 字库 / 栈水位 / 存盘 / 对时），值来自 HAL 与界面桥自报，量不到就显示占位（「量不了」「未挂载」「未对时」）而不是假装 0。它是**展示**不是设置项：没有可点控件、不进 KV、验收只断言首行是「键：值」的形状。
_Avoid_: 关于页、设备信息页、系统信息

**Weather App（天气 App）**：
薄壳 App，界面 = AiDoGUI 屏 `weather`（屏名约定 == App 名）。数据来自编译期桩 `app/weather/weather_data.h`；城市选择存 `hal().storage` 的 `wx.city`（一个字节）。屏上 `L_City` / `L_NIcon` / `L_NTemp` / `L_NDesc` 与 `L_D{i}_{Dow,Icon,Desc,Range}`（i = 0..4，0 = 今天）由 App 按名写；`Btn_CityPrev` / `Btn_CityNext` 是 App 侧 `lv_obj_add_event_cb` 挂的回调 —— 设计器事件在本工程里只用来**切 App**，屏内交互归 App。
_Avoid_: 天气组件、天气页

**Weather card（天气卡）**：
主屏 launcher 上的 `Card_Weather`（`L_WIcon` / `L_WTemp` / `L_WCity` / `L_WDesc`），既是显示控件也是进天气 App 的入口（设计器「调用函数」`embark_ui_open_weather`）。它不是第二份数据：`LauncherApp::refresh_weather()` 读同一个持久化键、同一张桩表 —— 卡片只是同源数据的另一种画法（在天气屏换了城市，回主屏卡片跟着变）。
_Avoid_: 天气控件、天气小组件

**Weather stub（天气数据桩）**：
`app/weather/weather_data.h` 里的编译期常量表（4 城 × 5 天：日期标签 / 天气字 / 一句话 / 温度区间），零堆零 IO。立项时框架没有任何网络能力，所以天气是桩不是真数据 —— 决策见 docs/adr/0012-weather-data-source.md；`INetwork` 已在 [ADR 0019](docs/adr/0019-service-layer-and-public-hardware.md) 立项，换真数据排在 issue 10。
_Avoid_: mock 数据、假数据源

**City table（城市表）**：
天气桩里的城市维度（北京 / 上海 / 广州 / 深圳），下标就是持久化键 `wx.city` 的值；切换是环形的（`weather_shift_city`），没有搜索与输入。
_Avoid_: 城市列表、地区表

**Glyph coverage guard（字形守卫）**：
`--ui-weather` 验收里的检查：把控件当前文本逐字符喂给 `lv_font_get_glyph_dsc`，缺字形即判失败并打出 U+ 码点。字体缺字符**不会**编译或运行报错，屏幕上只是一个方框而验收照样「通过」—— 这条把方框变成可断言的失败。ADR 0017 之后缺字的成因从"子集没打进去"变成"字体 cmap 没这个码点"（谚文、CJK 扩展区最可能），守卫本身不变：它问的是字体对象，不关心字从哪来。
_Avoid_: 缺字检测、字体验证
