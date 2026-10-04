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
非前台的 App，按自身的后台策略运行。
_Avoid_: 挂起的任务、休眠任务

**Background policy（后台策略）**：
每个 App 的自有设置，决定它退到后台之后的行为：挂起、按周期跑轻量逻辑，或拥有自己的任务。
_Avoid_: 优先级、调度策略

**Background tick（后台节拍）**：
`BackgroundPolicy::tick` 策略的落地方式：框架在 UI 任务里用 `etl::callback_timer` 按 per-App 周期调用 `onBackgroundTick(now_ms)`；周期以 UI 循环周期（宿主 5 ms）为粒度向上取整，`period_ms == 0` 就是不跑。
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

**Middleware view（middleware 视图）**：

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
