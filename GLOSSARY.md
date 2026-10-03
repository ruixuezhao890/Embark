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

**UI Task（UI 任务）**：
全局唯一的 FreeRTOS 任务，承载前台 App 的事件循环与全部 UI 渲染。
_Avoid_: 主任务、GUI 线程、主循环

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
