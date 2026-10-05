# Embark v1 Spec —— 嵌入式通用框架脚手架

**状态**：v1 已定稿（2026-10-03 起草；同日用户拍板 §16.4 的两处设计选择与依赖形态，见 §16.4）
**相关**：[GLOSSARY.md](../../GLOSSARY.md)、ADR [0001 单一 UI 任务与逻辑模块式 App](../../docs/adr/0001-single-ui-task-logic-apps.md)、[0002 HAL 只抽象到芯片能力粒度](../../docs/adr/0002-hal-capability-granularity.md)、[0003 内核零动态分配、禁用异常与 RTTI](../../docs/adr/0003-no-heap-no-exceptions.md)

---

## 1. 目标

给嵌入式项目一个可直接复用的骨架：应用逻辑写成若干 App，由唯一的 UI 任务驱动；换硬件只换 HAL 后端，应用代码不动。

v1 必须做到：

1. 同一份应用与内核源码，在**宿主（PC 仿真）**与 **ESP32-S3 真机**两个目标上都能构建运行。
2. App 有明确契约：统一接口、编译期注册、前后台机制、生命周期钩子、后台策略。
3. HAL 覆盖 v1 能力清单，接口里没有任何一颗芯片的寄存器/引脚细节。
4. 内核零动态分配、无异常/RTTI，内存需求编译期可确定。
5. 一条本机就能跑通的验证路径：宿主窗口跑 demo + 自动化测试 + CI 双目标构建。

## 2. 非目标（v1 明确不做）

- 网络、OTA、Wi-Fi 配网（只在 HAL 里留出将来能挂的位置）。
- 设备驱动与传感器抽象（App 需要具体外设时自己在平台侧实现，见 ADR-0002）。
- App 的动态注册、热插拔、后台淘汰回收。
- LVGL 9.x；多 UI 任务。
- 堆分配、异常、RTTI 的全平台放开（仅宿主允许开关）。
- 第二颗 MCU 的真实后端（接口按"它一定会来"设计，但不实现）。

## 3. 平台矩阵

| 目标 | 环境 | 显示/输入后端 | 持久化 | 日志后端 | 构建 |
| --- | --- | --- | --- | --- | --- |
| `host-sim` | Windows + MinGW-w64 GCC 15.1 | SDL2 窗口/事件 + LVGL 8.3.11 | 宿主文件 | stdout | CMake + Ninja |
| `esp32s3` | ESP32-S3（Touch-LCD-2.8 参考板） | ST7789 + 触摸（I2C） | NVS | UART0 | ESP-IDF 5.4（先 `export.ps1`） |

宿主侧也跑 FreeRTOS（复用 `lvgl_template_laste` 已验证的 kernel + Windows port），为的是**让两个目标的任务模型完全一致**——同一份内核代码在两端走同一条调度路径，行为差异只来自 HAL 后端。**2026-10-04 已在 MinGW-w64 GCC 15.1 上实测跑通**（FreeRTOS V10.6.2 + `MSVC-MingW` 端口，2 任务 + 队列 + 5000 tick 长跑，见 `issues/02`）：调度、延时、队列语义与真机一致，但**时间轴不一致**——该端口逐拍 `Sleep()` 产生 tick，1 tick 实测 **2.00 ms**（标称 1 ms，线性不漂移）；因此宿主验收一律以 tick 相对量为判据，不拿墙钟时长当判据。宿主也没有「停调度器」这一步：`vTaskEndScheduler()` 会让进程挂死，生命周期跟着进程走（见 `issues/02`）。

## 4. 架构分层

```
App（应用模块：纯逻辑，不是任务）
  ↓ 只通过框架服务交互
内核（App 调度、前台/后台、事件循环、消息总线、时间基准、日志门面）
  ↓ 只依赖接口
HAL（芯片能力：显示 / 输入 / 时间 / 持久化 / 日志后端 / 系统控制）
  ↓ 编译期选定实现
平台后端（platform/host · platform/esp32）
```

硬规则：

- App 之间**禁止互相 include**，只能通过框架消息交流。
- App 不 include 任何平台后端头文件；HAL 能力由框架在 `onCreate` 时注入。
- 内核不 include 平台后端头文件；后端在编译期由 CMake target 选定。

## 5. App 契约

一个类实现 `embark::App`，在编译期静态注册（零堆；注册顺序即默认前台 App）。

**接口**（=`include/embark/app.h`，issue 06 落地）：

```cpp
namespace embark {

enum class BackgroundPolicy : std::uint8_t { suspend = 0, tick = 1, own_task = 2 };

struct AppSettings {
    BackgroundPolicy background = BackgroundPolicy::suspend;  // 默认：完全不跑
    std::uint32_t period_ms = 0;                              // Tick 策略的周期
    std::uint16_t task_stack_words = 0;                       // OwnTask 的栈深（字）
    std::uint8_t task_priority = 0;                           // OwnTask 的优先级
};

class App {
public:
    virtual ~App() = default;
    virtual const char* name() const = 0;
    virtual void onCreate(Framework& fw) = 0;          // 装配期，一次
    virtual void onEnter() = 0;                        // 成为前台
    virtual void onPause() = 0;                        // 离开前台（框架只通知，不碰 UI）
    virtual void onResume() = 0;                       // 再次成为前台
    virtual void onExit() = 0;                         // 退出（v1 只在关机路径触发）
    // 以下有默认实现：
    virtual void onBackgroundTick(std::uint32_t now_ms) {}  // 后台节拍，必须轻量
    virtual void onForegroundTick(std::uint32_t now_ms) {}  // 前台节拍（issue 19 / ADR 0008）：
                                                            // 前台 App 每帧一次，在 lv_timer_handler 之后；
                                                            // EEZ App 用它驱动 eez_flow_tick()
    virtual void onMessage(const Message& msg) {}           // 框架投递的消息
    virtual AppSettings settings() const;                   // 后台策略等 per-App 配置
};

}  // namespace embark
```

- **前后台语义**：前台 = 同时持有输入焦点 + 渲染权 + 事件循环权，全局至多一个。切换**只能由框架执行**，App 通过 `request_switch(id)` 请求，切换决策与时机由框架负责（框架只登记请求，在 `step()` 的循环边界生效：旧前台 `onPause` → 新前台首次 `onEnter` / 再次 `onResume`）。
- **UI 生命周期自决**：框架只发 `onPause` / `onResume`，不创建、不销毁、不隐藏 App 的 LVGL 对象。
- **后台策略**（per-App 配置，`settings()` 返回）：`suspend`（完全不跑）/ `tick(period_ms)`（按周期跑 `onBackgroundTick`，`period_ms == 0` 等价于 suspend）/ `own_task`（申请自己的 FreeRTOS 任务，栈深与优先级由 App 配置）。
- **常驻**：v1 不淘汰任何 App，全部常驻内存。

## 6. 执行模型

- 全局唯一 UI 任务承载：输入事件处理 → 前台 App 回调 → `lv_timer_handler()` → 后台 tick 调度 → 消息派发。**它是全工程唯一允许操作 LVGL 与调用 `lv_timer_handler()` 的地方**（issue 06 落地：`start_ui_task()` + `ui_main` 任务循环，`lv_timer_handler` 的唯一调用点在 `LvglUiPort::process()`）。
- 宿主 main 是任务跳板：`main` 只做参数解析、启动 UI 任务（静态分配，2048 字栈）并进入 `vTaskStartScheduler()`（永不返回）；收尾走 `exit_process(code)`（`std::_Exit`，不跑静态析构 —— 主线程卡在模拟中断循环里，`exit()` 会 join 挂死）。
- 循环节拍目标 5 ms 一跳（与既有两代框架一致），空闲时 `vTaskDelay` 让出；不忙等。（宿主上这一拍的实际墙钟约 2×，tick 语义不变，见 §3。）
- 后台 tick 在 UI 任务里执行，因此**必须轻量**；周期由 `etl::callback_timer<MAX_TIMERS>` 按 per-App 配置驱动（见 §7）。
- 逃生舱：`OwnTask` 策略的 App 由框架创建自己的任务，框架保证任务名唯一、启动时机在 `onCreate` 之后、消息经队列进出。
  **任务入口允许返回**（issue 15）：返回 = 任务结束，平台把它 park 在槽里等持有者（唯一 UI 任务）回收，`Framework::step()` 每帧自动收一次，槽位之后可再创建 —— WiFi 非阻塞连接就走这条路径（发起时 `spawn_own_task`，成功或超时后入口返回）。
- 硬约束：前台 App 的所有回调**不得阻塞**；任何阻塞或长耗时工作必须走 `OwnTask`。

## 7. 消息与事件（直接用 ETL 现成设施 + 一层薄封装）

**结论：不自己造轮子。** 下表每一项都在本机 ETL 副本（`E:\01_Workspace\00_Active_projects\00_code\01_cpp_code\etl\include\etl\`）里逐文件核实过；这些都是 ETL 的老组件。**仓库现已锁 ETL 20.49.0**（见 §12 / §16.3），下表行号已按 20.49.0 重标，复核明细见 `issues/01-etl-version-verify.md`。

| 需求 | 用 ETL 的什么 | 核实到的关键事实 |
| --- | --- | --- |
| 消息本体 | `etl::message<ID>`（`message.h`） | 编译期 ID，可平凡拷贝 |
| 显式订阅 / 派发 | `etl::message_router<TDerived, T1..T16>`（`message_router.h`） | 路由是**编译期 `switch (T::ID)`** → `on_receive(const T&)`，未命中走 `on_receive_unknown`；零存储、零堆 |
| 一对多（多订阅者） | `etl::message_bus<MAX_ROUTERS>`（`message_bus.h:409`） | `etl::vector<etl::imessage_router*, MAX_ROUTERS_> router_list`；`bool subscribe(etl::imessage_router&)` / `void unsubscribe(id)` / `void unsubscribe(router&)`；只收 `is_consumer() == true` 的路由器；满时走断言；**同步派发，本身不带队列** |
| 丢最旧的环形缓冲 | `etl::circular_buffer<T, SIZE>`（`circular_buffer.h:1198`） | **ETL 原生就是"满时覆盖最旧"**：注释原文 `/// If the buffer is filled then the oldest item is overwritten.`（`:953` / `:977`），`void push(...)` 无返回值。全库唯一自带该语义的容器 —— 丢弃逻辑不用自己写 |
| 跨任务 / 中断队列 | `queue_spsc_locked` / `queue_spsc_atomic` / `queue_spsc_isr` | `bool push(...)`，**满时返回 false（丢新元素，不阻塞、不覆盖旧值）**；三者的锁来源分别是"注入的 `etl::ifunction<void>` 关/开中断对"、"无锁（`etl::atomic` 读写索引）"、"模板参数 `TAccess::lock()/unlock()`" |
| 后台节拍 | `etl::callback_timer<MAX_TIMERS>`（`callback_timer.h:828`） | `ETL_STATIC_ASSERT(MAX_TIMERS_ <= 254)`；`bool start(etl::timer::id::type, bool immediate)`；单任务模型下由 UI 任务循环喂 tick |
| 串行化用的锁 | `etl::mutex`（`etl/mutex/` 下已有 `mutex_freertos.h`、`mutex_std.h`、`mutex_gcc_sync.h`、`mutex_cmsis_os2.h` 等后端） | 平台后端现成，不用自己发明 |

**唯一必须包一层的地方（两个反差点）**：① 朴素的 `etl::queue::push()` 返回 `void`，满检查包在 `ETL_CHECK_PUSH_POP` 之内（20.49.0 在 `queue.h:320/:335`，宏本身定义在 `error_handler.h:537-543`）——不定义该宏就**什么都不检查**，往满队列里 push 会直接写在 `in` 位置并推进游标（新元素被塞到"最旧"位，语义崩坏）；定义后是「报错 + 提前返回」。② 四个 SPSC 队列的 `push` 虽然返回 false，但**丢的是新元素**，不是最旧。所以框架提供 `embark::MessageQueue`，只做三件事：

- 存储用 `etl::circular_buffer<T, SIZE>`（**丢弃由它自己做**，语义就是"满时覆盖最旧"）；跨任务时外加 `etl::mutex` + `etl::lock_guard`（单生产者单消费者），中断路径用 `circular_buffer_ext`（`volatile` 索引）或 `queue_spsc_isr`；
- 溢出计数：`push` **之前**查一次 `full()`，满了就 `++overflow_count` 并打一条 WARN；
- 容量是编译期常量；**任何路径都不阻塞 UI 任务。**

其余口径：

- App 在 `onCreate` 里声明订阅的消息类型；App 之间只走消息，禁止互相 include。
- **同任务内直接派发**（`receive()` 同步进入 `on_receive`）；只有跨任务才入队，队列是单生产者单消费者。
- 消息载荷必须是定长、可平凡拷贝的数据（ETL 定容容器 / `etl::span`），**不允许携带堆指针**。
- **日志串行化**：上游 elog 没有任何锁 → 框架在 sink 外面挂一层（真机用 `etl::mutex`，宿主按配置不加锁）；App 只走 `ELOG_*` 宏；中断上下文不许打日志（v1 不提供中断日志缓冲）。
- **已确认的硬约束（写代码前必须处理）**：`etl::callback_timer` 与 `etl::message_timer` 强制二选一宏（`ETL_CALLBACK_TIMER_USE_ATOMIC_LOCK` / `ETL_CALLBACK_TIMER_USE_INTERRUPT_LOCK`，`message_timer` 是完全平行的一套），`profiles/*.h` 里**一个默认值都没有**，不定义就是硬 `#error`（20.49.0：`callback_timer.h:54` / `:58`）；选 INTERRUPT_LOCK 还要自定 `ETL_CALLBACK_TIMER_DISABLE_INTERRUPTS` / `..._ENABLE_INTERRUPTS`（`:69/:70`）。这些宏放进框架的 `config/`。
- 待实测（接入时确认，不猜）：`etl::mutex` / `etl::atomic` 的宏开关与后端选择（`ETL_HAS_MUTEX` 按 OS 与编译器分派，`ETL_HAS_ATOMIC` 同）；`message_bus` 同一 message id 挂多个订阅者时的派发顺序。
- **版本风险（已消）**：原风险是「本机副本 20.40.0 与上游 20.49.0 差约 9 个小版本，而 20.39.4 → 20.40.0 之间发生过破坏性变更（`queue::emplace` 返回值 `void` → `reference`、`message_packet` 的非虚消息支持、`error_handler.h` 新增 `ETL_USE_ASSERT_FUNCTION`）」。20.49.0 已按 §16.3 的 12 条逐条复核（零回归 + `expected` 组合子 + `mutex_freertos.h` 认 ESP-IDF 布局），仓库已切到 20.49.0，本节经此复核定稿；以后再换版本请重跑 §16 复核（`config/embark_config.h` 的 static_assert 会在版本被换掉时直接编译报错）。

**落地实况（issue 07，与上表有偏差的地方以此为准）**：

- **订阅派发**：没有采用 `etl::message_router<TDerived, T1..T16>` 的编译期路由。落地 = 框架（`framework.cpp`）为每个 App 造一个 `AppAdapter : etl::imessage_router`（`is_consumer() == true`、`accepts()` 恒 true），boot 时全部 `subscribe` 到 `embark::Bus`；`receive()` 转调 `App::onMessage(const Message&)`，App 自己在 `onMessage` 里按 `message.get_message_id()` 分发（v1 简化：全部 App 收全部消息，声明式订阅留给后续版本）。理由：路由中继需要额外存储，且 per-App 路由表与「App 是逻辑模块、框架管装配」的架构不合。
- **总线**：`embark::Bus : etl::message_bus<embark::max_bus_subscribers>`，自带参数 `publish(const etl::imessage&)`：无人订阅（镜像表按 `accepts` 数 == 0）→ 丢弃 + `unknown_` 计数 + 一条 WARN（覆盖 ETL「静默丢弃」的口径）；有订阅者 → 走基类广播。ETL 的 `subscribe` 按 router id 排序插入**且不查重**（重复订阅会重复插），所以 Bus 自持镜像表先查重。
- **跨任务信封**：own task → UI 单向队列的元素是**定长信封** `CrossTaskMessage`（`cross_task_message_id = 0xFE`，成员 `from_app` + `seq`，可平凡拷贝）。v1 约束：跨任务只走这一个信封类型（载荷语义由收发双方约定，如 TickerApp 用 `seq` 计数）；自定义消息类型只用于同任务内 `Framework::publish`。
- **队列语义**：`MessageQueue<T, MaxSize, TMutex = etl::mutex>` 用 `etl::circular_buffer`（容量 = MaxSize），满时覆盖**最旧**且 FIFO 相对顺序保持；溢出 = `push` 前 `full()` → 计数 + 一条 WARN。跨任务实例默认带 `etl::mutex`（FreeRTOS 静态信号量）；不动 ETL 的 `queue_spsc_*`（它们的语义是满时丢**新**元素）。
- **后台节拍**：`etl::callback_timer<max_background_timers>`（宏 `ETL_CALLBACK_TIMER_USE_ATOMIC_LOCK` 已定义于 `config/embark_config.h`）；UI 任务每帧喂 `timers_.tick(1)`，周期以 `ui_loop_period_ms` 为粒度向上取整（`period_ticks = (period_ms + ui_loop_period_ms - 1U) / ui_loop_period_ms`，即周期精度 = UI 循环周期）。**构造后必须先 `timers_.enable(true)` 再 `start()`**（构造默认 `enabled = false`）。`period_ms == 0` 不注册定时器（等价 suspend）。
- **own task 逃生舱**：框架不 include FreeRTOS —— 任务创建走注入接口 `embark::ITaskSpawner`（`spawn_task(name, entry, argument, stack_words, priority)`，纯虚）；宿主实现 `HostTaskSpawner`（BSS 静态槽：`max_own_tasks` 个 TCB + `own_task_stack_words` 字栈，App 配置栈深超出槽 → `no_space`）。boot 时机：全部 `onCreate` → 默认前台 `onEnter` → spawn own task。任务体 = `while (true) { delay(period_ms); onBackgroundTick(now_ms); }` 永不返回；v1 不做优雅停止（宿主 `_Exit` 兜底，真机停止策略留给 issue 09/11）。
- **日志串行化**：维持 §7 口径（宿主 `EMBARK_LOG_SERIALIZE=0` 不加锁；真机 issue 11 置 1 走 `etl::mutex`）；中断不打日志（v1 不提供中断日志缓冲）。
- **其余已实测**：`etl::mutex`（FreeRTOS 分支）构造即 `xSemaphoreCreateMutexStatic`（静态信号量，无需调度器），`take/give` 在单线程测试里走快路径不挂；`message_bus` 同 id 多订阅者按 router id 升序派发（订阅插入即排序）。

## 8. HAL 接口清单（v1）

| 能力 | 接口要点 | 宿主后端 | 目标后端 |
| --- | --- | --- | --- |
| 显示输出 | init / 分辨率 / 区域刷新 / 背光 | SDL2 窗口 + 纹理 | ST7789（esp_lcd） |
| 输入 | 事件读取（按键、触摸坐标） | SDL2 事件 | 触摸 I2C + 按键 GPIO |
| 时间 | 单调毫秒时基 + 延时 | chrono / SDL | esp_timer |
| 持久化 | 小对象键值读写（长度有上限） | 宿主文件 | NVS |
| 日志后端 | sink 的写出函数 | stdout | UART0 |
| 系统控制 | 重启、栈/堆水位查询、看门狗喂狗 | 空实现 + 日志 | `esp_restart` 等 |
| 总线（最薄） | I2C/SPI 的 raw 字节读写 | 无（或空实现） | ESP-IDF 驱动 |

- 接口形态：**纯虚抽象类**（运行期多态、好替换好打桩），后端在**编译期**由 CMake target 选定，运行期不做任何注册/查表。
- 测试时链 fake 后端，接口不变。
- 引脚、总线参数、屏幕型号一律走平台后端的编译期配置，**不进 HAL 接口**。
- 装配：上层只拿一个 `hal::Context`（`ITime&` / `IPersistence&` / `ILogSink&` / `ISystem&` / `IBus&` + 可空的 `IDisplay*` / `IInput*`），**每个平台一份实例** —— 宿主是 `HostHal::instance()`，测试自己就地搭 fake 聚合（`tests/fakes/fakes.h` 的 `FakeHal`）。内核不持有全局单例，宿主后端 target 只被可执行文件链接，测试只链 `embark::core` + fake（否则宿主与测试各自的 `embark::fatal` / `assert_failed` 定义会撞车）。
- 宿主后端已全部到位：时间 / 持久化 / 日志 sink / 系统控制 / 总线五个基础后端在 issue 04 落地；显示（SDL2 窗口 + 纹理）与输入（SDL2 事件）在 **issue 05** 落地，并接上 LVGL 8.3.11。接口与 fake 在 issue 04 已定死，两批后端都只改 `platform/host/`，`include/embark/hal/` 一行未动。
- 宿主显示/输入的定稿契约（细节见 `issues/05-host-display-input-lvgl.md` 的 `## Answer`）：**单一像素格式 RGB565**（`IDisplay::flush` 的数据是紧凑布局，pitch = 区域宽 × 2；别的格式返回 `unsupported`）；**指针事件 key 恒为 0、按键事件 key 非 0**（0..255，此时 x/y 只是"按键那一刻指针在哪"——宿主鼠标的 button 号不许占用 `key`，否则上层会把点击当键盘事件整条丢掉）；**退出信号由输入后端捎带**（`SDL_QUIT` / 窗口关闭 → `IInput` 的退出查询，因为 SDL 只有一个事件队列、泵在 `poll()` 里，UI 循环必须抽干队列）。

## 9. 错误处理

- 可失败操作统一返回 `etl::expected<T, embark::Error>`（无返回值时返回 `embark::Error`）。
- 错误取值集合在 `include/embark/error.h` 定稿（10 个：`none` / `not_ready` / `invalid_argument` / `not_found` / `no_space` / `io_failure` / `timeout` / `unsupported` / `corrupt_data` / `busy`）。**ETL 的 `expected` 不接受裸 `Error`**，构造失败值要写 `embark::unexpected(Error::x)`（`error.h` 里的助手）。映射约定：参数非法 → `invalid_argument`；没 init 就用 → `not_ready`；键/条目不存在 → `not_found`；容量不够或目标缓冲太小 → `no_space`；落盘/总线 IO 失败 → `io_failure`；数据校验不过（magic / 长度 / CRC）→ `corrupt_data`；功能在该后端上不存在 → `unsupported`。
- 没有 `Error` 返回语义的能力（`ISystem` 的重启 / 喂狗 / 水位、`ISystem::fatal`）失败路径一律走 `embark::fatal` 上报，不假装成功；测试侧用 `FakeSystem` + 测试版 `embark::fatal` 符号（记录 + abort）覆盖这些路径。
- 编程错误（不可能发生）用 `EMBARK_ASSERT`；release 下的行为是**尽力写最后一条日志 → halt**，不静默继续。
- 致命错误统一走 `embark::fatal(reason)`，由平台后端决定重启还是进安全态。
- 容量不足的边界行为：整条丢弃 + 计数（日志行、消息队列一致），不产生半条数据。
- **错误码的名字只有一份（issue 13）**：`Error` 枚举在声明处用 `E_FMT_DERIVE_ENUM` 登记打印方式，不提供 `to_string`；日志直接 `{}` 填空（输出带全名，如 `embark::Error::not_found`），只吃 `const char*` 的出口（`fprintf` / `embark::fatal`）用 `char text[24]; embark::error_text(text, error);`。加错误码 = 加一行枚举值，不用同步第二张名字表。
- **日志单行有硬上限（issue 13）**：`ELOG_MAX_RECORD_SIZE` 默认 384 字节（含时间戳/位置前缀），放不下就**整行丢弃**（不截断、不报错）；这块缓冲还是 `log_at` 的局部数组，每行占调用者栈 385 字节（与 `own_task_stack_words` 互相牵制）。结论：一条日志只放一个整对象，长对象拆两条。

## 10. 内存与语言子集

- 内核：零动态分配；容器一律 ETL 定容版；`-fno-exceptions -fno-rtti`。
- App 层默认同规则；宿主仿真可用编译开关放开（开关名实现时定）。
- LVGL：**已按此配置落地（issue 05）**——`config/lv_conf.h` 由本仓库自持（锁 8.3.11，只钉影响内存/尺寸/构图/可观测性的项，其余交给 `lv_conf_internal.h` 的 `#ifndef` 默认）；`LV_MEM_CUSTOM 1` + 我们自己的 `embark_lvgl_alloc/free/realloc`（`platform/host/host_lvgl_mem.cpp`，真机换成静态池实现），**带记账与预算上限**（`config/embark_limits.h` 的 `lvgl_alloc_budget_bytes`，默认 256 KB；超预算或分配失败返回 nullptr，由 `LV_USE_ASSERT_MALLOC` 带分配点行号进 `embark::fatal`）。真机上"UI 对象总量有上限"仍待 issue 11 用实测数字定，宿主侧已有 `lvgl_outstanding_bytes()` / `lvgl_peak_bytes()` 两个观测点。两处硬约束：`lv_conf.h` 的头护栏必须叫 `LV_CONF_H`；`LV_ASSERT_HANDLER` 宏必须自带结尾分号（LVGL 的展开是裸语句）。
- 每个容器的容量上限必须是**代码里可查的常量**，集中放在 `config/`。
- **任务创建：静态槽位 + 固定块池，运行期创建/回收已落地（2026-10-04，ADR 0005，issue 15）**。所有任务都用 `xTaskCreate*Static`（UI 任务在 `platform/host/ui_task.cpp` / `platform/esp32/src/esp32_ui_task.cpp`，App 的 own_task 走 `ITaskSpawner`，实现是 `platform/common/pooled_task_spawner.h` 的 `PooledTaskSpawner<Kernel>`：`.bss` 里一块 `alignas(16)` 的 arena，按 `max_own_tasks` 个槽切给各平台 Kernel）；宿主配 `configSUPPORT_STATIC_ALLOCATION 1` + `configSUPPORT_DYNAMIC_ALLOCATION 0` 且 `heap_4.c` 刻意不参与编译，所以 `pvPortMalloc/vPortFree` 在**符号层面就不存在** —— 零动态分配是结构保证而非纪律。
  **生命周期**（issue 15）：任务入口返回 → 平台 trampoline 标 `finished` 并 park（`vTaskSuspend(nullptr)`，不再被调度）→ 持有者（唯一 UI 任务）`release_task` 确认已停稳后 `vTaskDelete`，槽位还池、`generation` +1。回收**只能由持有者做**，任务自己删自己会踩 FreeRTOS 的"删除中"中间态（静态 TCB 会被空闲任务延迟释放，此时复用存储会写出致命别名）。失败点仍唯一且确定：池满 / 栈深超过槽容量 → `Error::no_space`，参数非法 → `invalid_argument`，正在跑 → `busy`。
  取舍：静态把"内存够不够"从运行期问题挪成链接期问题（账目在 map 文件 / 真机 `check_sizes.py` 里可核对、0 碎片、0 堆锁），代价是预留即占用（默认 `max_own_tasks = 2` 个槽；宿主一槽 = 登记项 + `StaticTask_t` + 512 字栈 ≈ 4 KB，真机一槽 ≈ 512 字节栈 + TCB）与栈尺寸一刀切（App 声明的栈深超过 `own_task_stack_words` 直接 `no_space`）。静态 vs 动态的逐项对比在 `docs/adr/0005-static-task-slots-and-pool.md`，实现与验收见 `.scratch/embark-v1/issues/15-runtime-task-lifecycle.md`。

## 11. 目录结构与构建

```
embark/
├─ CMakeLists.txt                 顶层：host / esp32 两个 target
├─ include/embark/                公开头（App、Framework、HAL、Message、Error）
├─ src/                           内核实现
├─ platform/host/                 宿主后端（SDL2 显示/输入 + LVGL 端口 + 宿主 FreeRTOS port 待并入）
├─ platform/esp32/                ESP32-S3 后端（IDF 组件形式）
├─ app/                           v1 自带示例 App（demo 用）
├─ tests/                         doctest 测试（tests/hal/ 能力、tests/fakes/ 假后端、tests/detail/ 内部工具）
├─ config/                        lv_conf.h + LVGL 钩子声明、容量上限、平台开关
├─ third_party/{efmt-elog,etl,doctest,lvgl}/   submodule（锁 commit / tag）
└─ docs/                          使用文档（入口 docs/README.md）
```

- include 约定：内部一律写 `<middleware/efmt/...>`、`<middleware/elog/...>` 与 `<middleware/etl/...>`；efmt-elog 的**仓库根不作为 include 根**，所以没有 `<elog/elog.hpp>` 这种写法（要用 eserde/ecli 时，再给它们各加一条视图链接，而不是把仓库根整个摊开）。`middleware/` 视图在**构建目录里生成**（Windows 用 junction），实际路径是 `<build>/include/middleware/{etl,efmt,elog}`，**include 根给 `<build>/include`** —— 视图本身就叫 `middleware/`，include 根不能再指到它头上，否则 `<middleware/etl/version.h>` 会被解析成 `<build>/middleware/middleware/etl/version.h`（骨架第一次配置就是这么挂的）。源码树保持干净；**绝不把 `etl/` 目录本身加进 include 路径**（同名 `string.h` 会遮蔽标准头）。eserde / ecli 暂不接入。
- ETL 用上游的 `etl::etl` INTERFACE target；efmt/elog 上游没有 CMake，由我们包一层 `embark_efmt`（INTERFACE）。LVGL 上游自带 CMake：`LV_CONF_PATH` 指到 `config/lv_conf.h`（用 `CACHE PATH`，LVGL 用 `option()` 声明它，已有缓存值不会被覆盖），它在默认构建里会顺带建出空的 `lvgl_examples` / `lvgl_demos`，我们用 `EXCLUDE_FROM_ALL` 把它们挡在默认构建外；`embark_lvgl` 包一层（INTERFACE，链 `lvgl::lvgl`）。**LVGL 不进 `middleware/` 视图**：视图的唯一理由是满足上游写死的 `<middleware/...>` 互相引用，LVGL 按 `<lvgl.h>` 引入即可（再造 junction 只会多出第二套写法）。
- 命名：类型 PascalCase、函数/变量 snake_case、文件名 snake_case、宏 `EMBARK_*`、命名空间 `embark`。
- **源列表有两份（一份源码树、两条构建路径的代价）**：宿主在 `src/CMakeLists.txt`（`app/`、`platform/common/` 各有一份），真机在 `platform/esp32/project/components/embark/CMakeLists.txt` 的 `EMBARK_KERNEL_SOURCES` / `EMBARK_APP_SOURCES` / `EMBARK_COMMON_SOURCES`。加/删内核 TU 要两边一起改 —— 显式列出的源文件不存在时 CMake 不会跳过，而是在 configure 阶段报 `Cannot find source file`（issue 13 删 `src/embark/error.cpp` 时踩到，已写进 `docs/common-pitfalls.md`）。

## 12. 依赖与版本

| 依赖 | 版本策略 | 引入方式 |
| --- | --- | --- |
| efmt-elog（EFmt + ELog） | 锁 **commit SHA**（上游无 tag、无版本声明） | submodule → `embark_efmt` |
| ETLCPP/etl | **锁 20.49.0**（submodule `7d604f2e4f7fa79ff49bf675c089656943f9171b`；已按 §16.3 的 12 条复核：零回归 + `expected` 组合子 + `mutex_freertos.h` 认 ESP-IDF 布局，见 `issues/01`） | submodule（锁 commit）→ `etl::etl` |
| doctest | v2.4.11（锁 tag/commit） | submodule → `doctest::doctest`（只进测试目标） |
| LVGL | **锁 v8.3.11**（8.3 线最后补丁，submodule gitlink `74d0a816a440eea53e030c4f1af842a94f7ce3d3`） | submodule → `embark_lvgl`（INTERFACE 包 `lvgl::lvgl`）；`config/lv_conf.h` 自持，`LV_CONF_PATH` 指过去 |
| FreeRTOS | 真机用 ESP-IDF 5.4 自带；宿主用 kernel + Windows port | 平台侧 |

许可证 **MIT**；版本从 `0.1.0` 起、在 `main` 上打 tag，API 稳定前不承诺兼容。

## 13. 测试与 CI

- 测试框架 **doctest**（单头、编译快、无堆友好）。
- 宿主测试覆盖：前台切换、后台 tick 周期、消息溢出策略、日志门面、错误路径（`expected`）、App 注册表顺序、任务池（创建 / 槽满 `no_space` / 入口返回 / 回收 / 槽位复用 / 失败回滚，issue 15）。
- HAL 能力测试（`tests/hal/`）：七个能力各一条成功 + 一条失败路径，用 `tests/fakes/` 的假后端；fake 与宿主持久化后端**共用 `include/embark/detail/kv_slot.h` 的同一套槽编解码**（magic `EKV1` / state / key_len / value_len / CRC32 / key[16] / value[64] = 92 字节），任何一方改格式，另一方的测试立刻红。
- 内部工具测试（`tests/detail/`）：`AllocationCounter` 六个用例（累计与峰值、释放保留峰值、realloc 按差值、释放对不上夹 0、`over_budget` 严格大于、`reset` 清空）——LVGL allocator 的记账与预算就是靠它（`include/embark/detail/allocation_counter.h`，header-only）。
- LVGL 端口不做单测（它必须是"一个进程一份"的全局状态），靠宿主可执行文件的自动验收开关覆盖；LCD 真机那条线在 issue 11。
- 日志串行化层必须按**整行**工作：elog 的一条记录会分 3 次 `sink.write`（带颜色时）+ 1 次换行写（`elog.hpp:222/224`），逐次加锁挡不住交错；`LogSinkBinder` 攒成整行后一次 `write` + 一次 `flush`，并以 `lines_written()` / `bytes_written()` / `pending_bytes()` 暴露观测点（测试断言一条记录 = 后端一次 `write` = 一次 `flush`）。
- 宿主时间轴：FreeRTOS Windows port 的 1 tick 实测 ≈ **2.00 ms**（`issues/02`），所以时间相关断言只认 tick 相对量（回调次数 / 周期 / 溢出计数），**不写墙钟时长断言**；确需墙钟的用例单独标 `host-timing` 并给放宽系数。
- CI（GitHub Actions）三个 job：`host build + test`（ubuntu）、`esp32 build`（espressif/idf 容器，只编不烧）、`clang-format --dry-run`（仅 CI 跑；本机无 clang-format，不强制）。
- UI 不做 CI 里的无头截图测试，但宿主可执行文件自带**自动验收开关**（issue 05 起）：`--frames N`（跑满 N 帧即退）、`--screenshot FILE`（存 BMP，走 `SDL_RenderReadPixels`，须在 Present 之前读，否则后备缓冲内容未定义）、`--click [X,Y]`（用 `SDL_PushEvent` 把合成点击塞进 SDL 真事件队列，等于"人点了一下"；按钮没被触发则以退出码 2 失败）、`--quit-at N`（第 N 帧合成 `SDL_QUIT`，等价于点窗口 ×）。CI 仍只跑 `ctest`，窗口类验收在本机用这些开关做（截图证据见 `.scratch/embark-v1/evidence/`）。
- 找不到 SDL2 时不失败：只跳过 `embark_host_ui` 目标并打一行 STATUS，内核 / 后端 / 测试照常构建（CI 的 ubuntu job 就是这样过的）。

## 14. 验收标准（v1 完成的定义）

1. 宿主构建后能跑出 SDL 窗口，demo 里有 **≥2 个 App**，可切换前台，后台 tick 可观测（日志/计数）。
2. 宿主测试全绿（见 §13 覆盖点；时间相关断言只认 tick 相对量）。
3. ESP32-S3 目标 `idf.py build` 通过；烧写后能显示 demo 的第一屏。
4. 内核与 App 无动态分配：宿主构建下用分配 hook 统计为 0（或等价的审计方式）。
5. **换后端不动 App**：切到 esp32 后端时，`app/` 与 `include/embark/` 一行不改。
6. 文档到位：根 `README.md` + `docs/README.md` 索引 + 一条新手最短路径（跑起来 → 敲起来 → 改起来）。

## 15. 未决项与风险

- efmt-elog 无 tag、无版本约束 → 只能锁 SHA；上游 API 变动只在"主动更新 + 编译"时才会暴露。
- 上游 elog 的 `basic_string_stream` 万能 `operator<<` 兜底可能静默接受错误参数——接入时实测确认。
- **ETL 版本风险：已消（2026-10-03）**。本机副本（20.40.0 / 20.39.4）曾都旧于上游 20.49.0，现已按 §16.3 的 12 条对 20.49.0 逐条复核并把它锁成依赖（`issues/01-etl-version-verify.md`），spec §7 / §16 的行号与结论已同步。唯一仍开放的小项：完全离线构建时是否改为 vendor 本机 20.40.0 副本（不阻塞 v1，需要时再议）。
- ESP32-S3 板型与触摸控制器型号：**已定（2026-10-04，issue 11）**。用户给了板子手册（`ESP32-S3-Touch-LCD-2.8.pdf`）并确认走参考板：模组 ESP32-S3R8（16 MB flash + 8 MB 八线 PSRAM）、屏 ST7789（原生 240×320 竖屏，**框架直接用这个方向**：真机 ROT_NONE、宿主窗口同向 —— 2026-10-04 由横屏 320×240 改为竖屏，用户要求"像手机竖着拿"）、触摸 CST328（I2C `0x1A`）、板载 I2C 设备（IMU/RTC）在 IO10/IO11。板级常量集中在 `platform/esp32/src/esp32_board.h` 一处，实机方向/颜色不对只动那几个开关（见 `issues/11-esp32s3-backend.md` 的 `## Answer`）。
- IDF 侧三条硬约束（第一次 `idf.py build` 就栽在前两条上，已写进 `docs/common-pitfalls.md` 与 `docs/hal-backend-guide.md`）：① `platform/esp32/project/{sdkconfig.defaults,partitions.csv}` **必须是纯 ASCII**（IDF 的 `kconfgen` / `gen_esp32part.py` 按宿主编码读，中文 Windows 上是 GBK，直接 `UnicodeDecodeError`）② `CONFIG_FREERTOS_HZ` 必须 ≥1000（100 Hz 下 `pdMS_TO_TICKS(5)==0` ⇒ UI 循环退化成忙等）③ 栈深单位：IDF 的 `xTaskCreate*` 收字节、`uxTaskGetStackHighWaterMark()` 返回字。
- **真机侧还有两件事必须人工确认**（本机没有板子，issue 11 只能做到"编得过"）：① 烧写后能显示 demo 第一屏（`idf.py -C platform/esp32/project -B build-esp32 flash monitor`）② 触摸/按键能切前台。判定与调法都在 `platform/esp32/README.md` 的 bring-up 清单里：启动日志会打出 CST328 自报的 `RES_X/RES_Y`，据此定轴方向。
- 宿主 FreeRTOS port 来自 `lvgl_template_laste`：**已在 GCC 15.1 上验证可编可跑（2026-10-04，见 `issues/02`）**，源码清单 / CMake 片段 / `FreeRTOSConfig.h` 必改项都在该 issue 的 `## Answer`，证据与探针源码留档在 `.scratch/embark-v1/spikes/02-host-freertos/`。残留两个小项（不阻塞）：① 该端口 tick 比墙钟慢约 2×（见 §3 / §13）；② vendor 进仓库后是否顺手消掉上游的 2 条严格警告（`queue.c:489`、`port.c:249`）。
- LVGL 补丁版号：**已定 v8.3.11（2026-10-04，issue 05）**。8.3 线上游已停更，选它是因为它与既有两代工程（8.3.6 / 8.3.x）的 API 一致、且是 8.3 线最后的补丁；`LV_MEM_CUSTOM 1` 下上游**没有 `lv_deinit`**（`lv_obj.h:206-214` 的门是 `LV_ENABLE_GC || !LV_MEM_CUSTOM`），所以进程内 LVGL 只初始化一次、退出时靠 `lvgl_outstanding_bytes()` 观测是否有泄漏。
- **运行时任务生命周期：已落地（2026-10-04，issue 15）**。`ITaskSpawner` 补上 `release_task(TaskToken)`，两种平台实现都换成 `PooledTaskSpawner<Kernel>`（固定块池 + 每槽 `Task`，仍是 `xTaskCreateStatic*`、仍然零堆）。任务入口**允许返回**：返回后平台标 `finished` 并 park，持有者（唯一 UI 任务）在 `Framework::step()` 的回收段 `release_task` 确认停稳后删任务、还槽位、`generation` +1。三个语义点：① 回收**只能由持有者做**（任务自己删自己会踩 FreeRTOS 的删除中间态，静态 TCB 被延迟释放时复用存储是致命别名）② park 用 `vTaskSuspend(nullptr)` 而不是 `vTaskDelay` 循环 —— 挂起的任务永远不会被选中，`is_parked` 一旦为真就稳定为真，没有"已从就绪表摘掉但还在让出"的交接窗口 ③ 真机跨核的罕见窗口（已在挂起链表上、但仍是某核的 `pxCurrentTCB`）由 `is_parked` 的第二条判据挡掉，`release_task` 此时返回 `busy`，下一帧重试即好。第一个真实用例（WiFi 非阻塞连接）所需的机制已具备；WiFi 能力面本身仍不在 v1 范围内。

## 16. ETL 可复用组件清单（开工前定稿，避免边写边改）

对本机 ETL **20.40.0**（`E:\01_Workspace\00_Active_projects\00_code\01_cpp_code\etl`，208 个顶层头 + `atomic/ mutex/ private/ profiles/ generators/` 子目录）逐族过了一遍，判据三条，缺一不用。**2026-10-03 仓库 submodule 已升到 20.49.0**（依据见 §16.3），下表行号已按 20.49.0 重新实测；20.40.0 的行号留档在 `issues/01-etl-version-verify.md` 的 `## Answer`：

1. **零堆**：容量由模板参数在编译期给定，不依赖 `new`/`malloc`（ETL 的 `memory_model.h` 区分静态/动态内存模型，本项目一律静态）。
2. **无异常**：不依赖 `throw`，出错走错误处理函数或断言，与 `-fno-exceptions` 一致。
3. **可静态配置**：锁、原子、时钟等外部依赖通过宏或模板参数注入，平台侧能给得起。

### 16.1 结论总表

| 族 | 拟结论 | 代表组件 | 核实状态 |
| --- | --- | --- | --- |
| 定容容器 | 直接复用 | `etl::vector`、`etl::array`、`etl::string<N>`、`etl::map`/`multimap`、`etl::flat_map`/`flat_set`、`etl::deque`、`etl::priority_queue`、`etl::intrusive_list`/`intrusive_queue`/`intrusive_stack`、`etl::multi_vector`、`etl::indirect_vector`、`etl::bitset` | **已逐字核实**（20.49.0 行号）：`vector.h:1830`（`template <typename T, const size_t MAX_SIZE_> class vector : public etl::ivector<T>`，:1824 注释即「最大元素数」）、`string.h:62`（注释「uses a fixed size buffer」）、`deque.h:2349`（**坑：注释写明 `The deque allocates one more element than the specified maximum size.`**）、`flat_map.h:1278`、`priority_queue.h:526`、`intrusive_list.h:470`、`indirect_vector.h:1421`；`reference_flat_*` 存引用、对象须自持生命周期，v1 不用 |
| 错误与类型 | 直接复用 + 一层薄壳 | `etl::expected<T,E>`、`etl::unexpected<E>`、`expected<void,E>` 特化、`etl::optional`、`etl::variant`、`etl::result`、`etl::error_handler`、`etl::integral_limits` | **expected.h 已逐字核实**（20.49.0：:342 / :93 / :1200，整体在 `#if ETL_USING_CPP17` :246 内；20.49.0 还新增了 `and_then`/`or_else`/`transform` 组合子）→ §9 写法不用改；**但 `value()` 仍没有任何断言**（:693/:701/:725 三个重载直接 `return ...`），只有 `operator->`（:922/:932）与 `operator*`（:942/:952）用 `ETL_ASSERT_OR_RETURN_VALUE(has_value(), ETL_ERROR(expected_invalid), ETL_NULLPTR)`（:924/:934）与 `ETL_ASSERT`（:944/:954）挡着 → 取错值仍是未定义行为，**必须由我们的 `embark::Result` 包装自己断言**；`operator bool` 带 `ETL_EXPLICIT`（:742，void 特化 :1313），`if (r)` 可用、`bool b = r;` 编译不过 |
| 内存 / 池 | 薄封装 | `etl::pool`、`etl::generic_pool`、`etl::variant_pool`、`etl::imemory_block_allocator` + 固定块实现、`etl::memory_model` | **已逐字核实**（20.49.0）：`pool.h:53`（`class pool : public etl::generic_pool<sizeof(T), etl::alignment_of<T>::value, VSize>`）、`generic_pool.h:55`、`pool_ext` / `generic_pool_ext`（缓冲由外部提供）、`variant_pool.h:45`（20.49.0 改为 `class variant_pool : public etl::generic_pool<etl::largest<Ts...>::size, etl::largest<Ts...>::alignment, MAX_SIZE_>`）/:96（`_ext`）；只用定长对象池，LVGL 侧仍用 LVGL 自己的静态池 |
| 消息 / 事件 | 直接复用 + 一层薄壳 | `etl::message<ID>`、`etl::message_router`、`etl::message_bus`、`etl::message_packet`、`etl::message_timer`、`etl::message_broker`、`etl::message_router_registry` | **前五项已逐字核实**（见 §7）；**新发现已核**（20.49.0 行号）：`message_broker.h:44`（`class message_broker : public etl::imessage_router`）、`message_router_registry.h:504`（按名索引路由的注册表）、`message_packet.h:49`（`message_packet<>` 在 :374；**20.49.0 已从「十几个按类型数展开的特化」压成单一模板**，API 兼容）、`message_bus.h:409`（`bool subscribe(etl::imessage_router&)` :89）；`reference_counted_message*`、`shared_message` 需要池，v1 不用。**未处理消息不会断言**：`message_router` 类型表全不中时转调 `on_receive_unknown(msg)`（20.49.0 在 :521/:556；20.40.0 是 34 处）→ 框架必须覆写它做 WARN + 计数，否则消息静默消失 |
| 环形缓冲与队列 | 直接复用 | `etl::circular_buffer<T,N>`（**原生丢最旧**）、`etl::queue_spsc_locked` / `queue_spsc_atomic` / `queue_spsc_isr` / `queue_mpmc_mutex`（满时返回 false、丢新元素） | **已逐字核实**（见 §7）：`circular_buffer.h:953`/`:977` 注释原文 `/// If the buffer is filled then the oldest item is overwritten.`、`class circular_buffer` :1198；`queue.h` 的满检查写成 `ETL_ASSERT_CHECK_PUSH_POP_OR_RETURN(!full(), ETL_ERROR(queue_full))`（:320/:335，**20.49.0 里是「提示后提前返回」而不是断言**）、`class queue : public etl::iqueue<T, MEMORY_MODEL>` :624；`etl::queue` 仍不用（满时语义不达预期） |
| 定时与调度 | 直接复用（宏必须给） | `etl::callback_timer`（+ `_locked`/`_atomic`/`_interrupt`）、`etl::message_timer`、`etl::callback_service`、`etl::timer` | **callback_timer.h 强制宏已核实**（20.49.0 行号 :50/:54/:57/:58；选 INTERRUPT 版还要自定 `ETL_CALLBACK_TIMER_DISABLE_INTERRUPTS`/`..._ENABLE_INTERRUPTS`，缺了就 `#error` 在 :69/:70）；**其余已核**：`scheduler.h:229`（`class ischeduler`）、`scheduler.h:353`（`class scheduler : public etl::ischeduler, protected TSchedulerPolicy`——策略式）、`task.h:58`、`callback_service.h:49`（单类，无接口）、`class callback_timer` :828；`timer.h` 只是 id/state 辅助头，不是实现。另见 §16.4 |
| 状态机 / 回调 | 直接复用 | `etl::state_chart<TObject,TParameter>`、`etl::fsm`、`etl::hfsm`、`etl::delegate`、`etl::observer`、`etl::function`、`etl::callback` | **已逐字核实**（20.49.0 行号）：`state_chart.h:221/:411/:602/:851`（状态表用成员函数指针）；**`fsm.h:438`：`class fsm : public etl::imessage_router`——ETL 的 FSM 本身就是消息路由，可以直接当订阅者挂到 `message_bus` 上**；`ifsm_state` :315、`fsm_state` :713（特化 :956）、`hfsm.h:41`、`observer.h:303`（`observer<void>` :337、展开版 :370+）、`function.h:53/:72` + 8 个变体（`function` :93/:133/:168/:201、`function_mp` :235、`_mv` :273、`_imp` :311、`_imv` :336、`_fp` :360、`_fv` :390）、`delegate` 实体在 `private/delegate_cpp11.h:120`，`delegate.h` 只是转发头；**20.49.0 把 `delegate_observer.h` 改名为 `delegate_observable.h`（:47）** |
| 同步 | 直接复用（宏必须给） | `etl::mutex` + `etl::lock_guard`（后端按平台分派到 freertos/std/gcc_sync/cmsis_os2…）、`etl::atomic` | **分派规则已核实**（20.49.0）：`mutex.h:34-56` 依次判 `ETL_TARGET_OS_CMSIS_OS2`（:34）→ `ETL_TARGET_OS_FREERTOS`（:37）→ `ETL_TARGET_OS_THREADX`（:40）→ `ETL_USING_STL && ETL_USING_CPP11`（:43）→ `ETL_COMPILER_ARM5..8`（:46）→ `ETL_COMPILER_GCC`（:49）→ `ETL_COMPILER_CLANG`（:52），全不中则 `ETL_HAS_MUTEX 0`（:56）；`lock_guard` 在 :72；`ETL_HAS_ATOMIC` 由 `platform.h:616-629` 派生（`ETL_NO_ATOMICS` / Cortex-M0(+) / `__STDC_NO_ATOMICS__` → 0；否则 ARM5-8、GCC、CLANG 或 CPP11+STL → 1，ESP32-S3 与宿主 GCC 都走 `atomic_gcc_sync.h`） |
| 校验 / 工具 | 直接复用 | `etl::crc8_*`/`crc16`/`crc32`/`crc64` 全家、`etl::checksum`、`etl::frame_check_sequence`、`etl::fnv_1`、`etl::murmur3`、`etl::jenkins`、`etl::pearson`、`etl::bloom_filter`、`etl::base64`、`etl::bit_stream`、`etl::byte_stream`、`etl::endianness`、`etl::mem_cast`、`etl::enum_type`、`etl::flags`、`etl::cyclic_value`、`etl::debounce`、`etl::to_string`、`etl::string_stream`、`etl::string_view`、`etl::version` | **部分已核**（20.49.0 行号）：`crc16.h:47` / `crc32.h:47`（`class crc16_t : public etl::crc_type<etl::private_crc::crc16_parameters, Table_Size>`，表大小可调）、`checksum.h` 五种（`checksum`/`bsd_checksum`/`xor_checksum`/`xor_rotate_checksum`/`parity_checksum`，全部基于 `frame_check_sequence` 策略）、`debounce.h:417`（`class debounce : public private_debounce::debounce4`，另有 `debounce2/3`）、`version.h:41`（`ETL_VERSION_MAJOR 20`）/`:42`（`ETL_VERSION_MINOR 49`）/`:62`（`ETL_VERSION_VALUE`）；`enum_type`/`flags`/`cyclic_value`/`string_view`/`to_string` 只确认头文件在（低风险，用前扫一眼即可）。**顺带更正一条旧顾虑**：`basic_string_stream` 在 20.40.0/20.49.0 都没有「万能 `operator<<` 模板」（全是具体 friend 重载），所以「写错参数会静默走 `to_string`」不成立。配置持久化的 CRC（§8）、HAL 值类型包装（`enum_type`/`flags`）、按键去抖（`debounce`）计划直接用 |

> 核实方式：本节凡标「已逐字核实」的，都是我打开头文件读到的逐字声明 + 行号（子代理报告与此不一致时一律以文件为准——已发生过两次：`message_bus` 的 `subscribe` 返回类型、`queue::push` 的返回值）。**本轮签名级核实已完成，"待核"项全部核完或注明为低风险项**；20.49.0 复核（§16.3）也已跑完，本节行号已按仓库现在锁定的 20.49.0 重标并冻结。

### 16.2 必须给的宏（不给就编译不过或行为不达预期）

| 宏 | 出处 | 说明 |
| --- | --- | --- |
| `ETL_USING_CPP17` | `platform.h` 按 `__cplusplus` 派生 | `etl::expected` 全部在该宏门内（`expected.h:246`）；C++17 目标天然满足，spec §9 依赖它 |
| `ETL_CALLBACK_TIMER_USE_ATOMIC_LOCK` **或** `ETL_CALLBACK_TIMER_USE_INTERRUPT_LOCK` | `callback_timer.h` | 二选一，否则 `#error`（20.49.0：:50、:54、:58）；选 INTERRUPT 版还要自定 `ETL_CALLBACK_TIMER_DISABLE_INTERRUPTS` / `..._ENABLE_INTERRUPTS`，缺了再报一次 `#error`（:69/:70） |
| `ETL_MESSAGE_TIMER_USE_ATOMIC_LOCK` **或** `..._INTERRUPT_LOCK` | `message_timer.h` | 同上；`profiles/*.h` 里没有任何默认值，8 处引用全在该头内 |
| `ETL_TARGET_OS_FREERTOS` | **我们自己定义**（ETL 全库只消费不定义；自带 profiles 给的都是 `ETL_TARGET_OS_NONE/WINDOWS/LINUX`） | `mutex.h:37` 的分支键。不给就会落进 `ETL_COMPILER_GCC` → `mutex_gcc_sync.h`（靠 `__sync_*`），Cortex-M 上不可用。**但给了要看平台有没有 FreeRTOS 头**：定义它以后 `<etl/callback_timer.h>` → `timer.h` → `atomic.h` → `atomic_gcc_sync.h` → `mutex.h` → `mutex_freertos.h` 会去 include `FreeRTOS.h`，没有该头的平台连 `callback_timer` 都编不过（20.40.0 报 `fatal error: FreeRTOS.h`，20.49.0 有一句友好 `#error`；实测见 issues/01 的 Answer）。所以改成按平台能力开关：`EMBARK_PLATFORM_HAS_FREERTOS`（顶层 CMake，宿主在 FreeRTOS Windows port 落地前 OFF，ESP32 侧 ON），两个目标最终都要开 |
| `ETL_HAS_MUTEX` / `ETL_HAS_ATOMIC` | 由 `mutex.h:34-56` / `platform.h:616-629` 派生 | 用 `etl::mutex`、`etl::atomic`、`queue_mpmc_mutex` 的前提；派生规则见 §16.1「同步」行 |
| `ETL_CHECK_PUSH_POP`（+ 20.49.0 新增的两个同类开关） | `error_handler.h:537-565` 集中定义，调用点散布在 `vector.h`/`deque.h`/`list.h`/`stack.h`/`queue.h`/`forward_list.h`/`priority_queue.h`/intrusive 族/`indirect_vector.h`/`private/pvoidvector.h`（20.49.0 实测 `include/etl` 树下 90 处） | **不是只给 queue 用的**：它守着 `push_back`/`pop_back`/`push`/`pop` 的满空检查。20.49.0 把边界检查收拢成三个开关：`ETL_CHECK_PUSH_POP`（:537-543）、`ETL_CHECK_INDEX_OPERATOR`（`operator[]`，:546-554）、`ETL_CHECK_EXTRA`（front/back 非空、insert/erase 迭代器区间、span 子视图，:557-565）。不定义 → 往满容器写入既无断言也无日志，直接写坏内存。**v1 定义前两个**（第三个留空，注释里写了怎么开）；注意 20.49.0 的 `_OR_RETURN` 变体是「报错后提前返回」而不是放弃执行 |
| `ETL_LOG_ERRORS` | `error_handler.h:46` | **`class etl::error_handler` 只在定义了它（或 `ETL_IN_UNIT_TEST`）时才存在**。不定义：debug 走 `assert()`（20.49.0 分支 :481-507）、release 下 `ETL_ASSERT` 直接空展开（:511-528），ETL 内部护栏全部失效。**必须定义**；再把 `error_handler::set_callback`（:84）接到我们自己的致命通道——`ETL_LOG_ERRORS` 分支（:355-402）里 `ETL_ASSERT` 只是 `etl::error_handler::error(e)` 回调（:359/:368/:378/:387/:392/:398），**不中断执行**，要「最后一条日志 + halt」就得由我们的回调来做（§9）；需要返回值的位置用 `ETL_ASSERT_OR_RETURN`（:364） |
| `ETL_USE_ASSERT_FUNCTION` / `ETL_THROW_EXCEPTIONS` | `error_handler.h:313` / `platform.h:287` | **两个都不要定义**：前者把断言交给 `etl::set_assert_function` 且优先级高于 `ETL_LOG_ERRORS`（分支 :314-353）；后者让 `ETL_ASSERT` 走 `throw`（内核禁异常）。我们要的就是 `error_handler.h:355-402` 那条分支 |
| `ETL_ISTRING_REPAIR_ENABLE` / `ETL_IVECTOR_REPAIR_ENABLE` / `ETL_IDEQUE_REPAIR_ENABLE` / `ETL_ICIRCULAR_BUFFER_REPAIR_ENABLE` | `platform.h:255-263` | 容器对象被 `memcpy` 到别处（跨任务传对象、放共享内存）后修复内部指针。**Embark 跨任务只传 POD 负载或引用，不搬容器** → 不定义；将来若真要搬，必须打开 |
| `ETL_MESSAGE_ID_TYPE` / `ETL_FSM_STATE_ID_TYPE` | `message_types.h:39-42` / `fsm.h:56-59` | 默认都是 `uint_least8_t`。v1 保持默认（消息类型 < 255、状态数 < 255） |
| 平台 profile | `profiles/cpp17.h` / `profiles/auto.h` | **没有 ESP32/Xtensa/RISC-V 专用 profile**（35 个 profile 里最近的是 `gcc_generic.h`/`cpp17.h`）。**我们不提供 `etl_profile.h`**：`platform.h:56-62` 因此走 `ETL_NO_PROFILE_HEADER` 分支，`ETL_USING_CPP17` 由 `platform.h:193` 引入的 `profiles/determine_compiler_language_support.h:157` 按 `__cplusplus` 派生（C++17 目标天然为 1），`ETL_USING_STL` 由 `platform.h:97-101` 按 `ETL_NO_STL` 直接覆盖 —— 所以"不给 profile 会偷偷用上 STL"不成立（已按代码核实）。上面这些 `ETL_TARGET_OS_*`、定时器宏仍由我们自己补 |
| `ETL_NO_EXCEPTIONS` / `ETL_NO_STL` 等 | `platform.h` | 按目标设置；内核代码两端编译选项必须一致，宿主不得悄悄用上 STL |

### 16.3 版本差异（已核实的 A/B，以及最终锁定的 20.49.0）

- A = 本机 20.40.0（`…\01_cpp_code\etl\include\etl`），B = `…\01_Archives\lvgl_template_laste\framework\utils\etl` 20.39.4。
- **文件级差异（我实测：剥掉 30 行版本横幅后逐文件比对）**：A 独有 10 个（`function_traits.h`、`singleton_base.h`、`type_list.h`、`uncopyable.h` + `experimental/` 5 个 + `deprecated/factory.h`），B 独有 0 个；同名文件中**实质不同 48 个**（含 `atomic/atomic_gcc_sync.h`、`private/delegate_cpp11.h`、`private/bitset_*.h`、`generators/*` 等子目录）。上一版这里写「只有 6 个文件有差异」是错的，已按实测更正。
- 其中对清单有实质影响的 6 处：`expected.h`（`operator bool` 加 `ETL_EXPLICIT` + `ETL_NOEXCEPT`；`value()` 仍无断言——我已核实）、`error_handler.h`（A 新增 `ETL_USE_ASSERT_FUNCTION` 档——我已核实 :313）、`queue.h`（`emplace` 返回 `reference`）、`optional.h`（`emplace` 返回 `T&`）、`span.h`/`array_view.h`/`memory.h`（改用 `etl::to_address`，空 span 更正确）、`ipool.h`（A 才把 free-list 指针写入逻辑放出来并新增 `max_item_size()` → **B 的对象池是残缺的**）；`string.h` 另加 `operator=(string_view)`。**当时的结论是「用 A（20.40.0）」**；两者都晚于上游 20.49.0，最终按下面的复核结果锁 20.49.0。
- **20.49.0 复核清单（拿到新版本后逐条核，别猜）**：① `expected` 是否新增 `and_then`/`or_else`/`transform`；② `expected::value()` 是否仍无断言；③ 是否新增 `bitmap`；④ 是否新增 `interrupt_guard`；⑤ `mutex/` 或 `profiles/` 是否新增 ESP-IDF/FreeRTOS 后端并自带 `ETL_TARGET_OS_FREERTOS`；⑥ `task` 接口是否变化；⑦ `queue::push` 是否仍返回 void、`ETL_CHECK_PUSH_POP` 是否仍在；⑧ `message_bus` 是否新增 `publish`；⑨ `delegate` 存储是否变化；⑩ `pool`/`ipool` free-list 是否又被改；⑪ `ETL_VERSION_VALUE` 编码是否仍是 `major*10000+minor*100+patch`；⑫ `expected<void,E>` 的 API 是否增删。
- 上游 20.49.0 本机原本没有 → 当时先按 20.40.0 开写（依赖锁 SHA）：**这一步已经走完**，见下一条与 `issues/01-etl-version-verify.md`。
- **20.49.0 复核已完成（2026-10-03）**：12 条全部实测、零回归，另有两点利好（`expected` 有了 `and_then`/`or_else`/`transform`；`mutex_freertos.h` 用 `__has_include` 认 ESP-IDF 的 `<freertos/...>` 布局）→ 用户已拍板升级，仓库现在锁 **20.49.0**（submodule 指针 `7d604f2e4f7fa79ff49bf675c089656943f9171b`），本节行号已按它重标；明细与换版动作见 `issues/01-etl-version-verify.md` 的 `## Answer`。
- `version.h:41-52` 给了 `ETL_VERSION_MAJOR/MINOR/PATCH`、`ETL_VERSION`、`ETL_VERSION_VALUE` → `config/embark_config.h` 里有一条 `static_assert(ETL_VERSION_MAJOR == 20 && ETL_VERSION_MINOR == 49, "Embark v1 按 ETL 20.49.0 核实过，换版本请重跑 §16 复核")`（强制包含进每个 TU）：版本被悄悄换掉时直接编译报错，比"人记得"可靠，也是**唯一**的版本守卫（测试里不再重复断言）。

### 16.4 两处设计选择 + 依赖形态（2026-10-03 用户已拍板）

1. **后台节拍怎么驱动 → 定 A**：框架定时器（`etl::callback_timer<MAX_TIMERS>`）+ `onBackgroundTick(now_ms)`，前后台全由唯一 UI 任务驱动。否决 B（`etl::scheduler` + `etl::task`）：会给框架塞进第二套调度语义，且「App 前后台」与 ETL 的「任务请求工作」不是一回事。
2. **App / UI 内部状态 → 定「不绑范式」**：`embark::App` 基类不规定状态怎么表达。框架把 `etl::state_chart<TObject, TParameter>` 当推荐工具、在 demo 里示范一次；需要「收消息即切状态」的 App 自己用 `etl::fsm`（`fsm.h:438` 的 `fsm` 本身继承 `etl::imessage_router`，可直接挂 `message_bus`，与 §7 无缝）。否决 B/C 作为强制约定：那会让每个小 App 都被迫声明一堆状态类。
3. **依赖形态 → 定「锁 20.49.0 开写」**：`third_party/` 用 git submodule 锁 commit，ETL 现在 = **20.49.0**（`7d604f2e4f7fa79ff49bf675c089656943f9171b`）。原计划「先 20.40.0 开写、把拉 20.49.0 立为第 0 个 issue」已执行完毕（`issues/01-etl-version-verify.md`，12 条复核零回归），用户 2026-10-03 拍板升级，故不必再等；离线构建需要时再改 vendor。

### 16.5 ETL 没给、必须自写

| 需要的东西 | 结论 | 说明 |
| --- | --- | --- |
| 临界区 RAII（`interrupt_guard`） | **自写** | 全库无此物（`mutex.h:72` 只有依赖 `etl::mutex` 的 `lock_guard<TMutex>`）。ESP32 包 `portENTER_CRITICAL`/`portEXIT_CRITICAL`，宿主用 `std::mutex`（或单线程时空实现） |
| 位图（`bitmap`） | **自写** | 全库无 `bitmap`，只有编译期位宽的 `etl::bitset`（`private/bitset_new.h`） |
| 总线发布语义（`publish`） | **自写** | `message_bus` 只有 `subscribe`（已核实：全文件无 `publish`）→ 框架提供 `embark::Bus::publish()`，遍历订阅者调 `receive()` |
| 日志与格式化 | 用 efmt + elog | ETL 只有 `to_string`（数字→定容字符串）与 `format_spec`，无 printf 语义、无日志 |
| 任务/线程/时钟/HAL/UI/配置存储 | 自备 | `etl::task` + `etl::scheduler` 只是**合作式调度骨架**（无栈、无线程、无系统调用）；RTOS 任务、时基、HAL 后端、LVGL、NVS/文件存储都由 Embark 提供 |
| `etl::ifunction` 的头文件 | 注意 | **没有 `ifunction.h`**，`ifunction` 在 `function.h`（:53 / :72）里 |
| 宿主/设备统一构建 | 自备 | ETL 只有头文件与 profile 宏，不含任何构建脚本；两 target 的宏必须在我们的 `platform_config.h` 里集中给定 |

### 16.6 几轮复核记下的反直觉点

1. **定容容器没有堆版本**：全库 `std::allocator`/`malloc`/`operator new` 零命中，容量一律是编译期模板参数（需要运行期容量时用 `_ext` 族 + 调用方提供缓冲）。所以「不小心用 ETL 分配堆内存」这件事不存在，**唯一的坑是容量必须编译期确定**。
2. **`etl::memory_model` 与堆无关**：它只决定 `size_type` 宽度（`memory_model.h:49-73`，SMALL→`uint_least8_t` … HUGE→`uint_least64_t`）。别当成「内存模型开关」。
3. **`etl::task` 不是 RTOS 任务**：`task.h:80/:85` 只有 `task_request_work()` / `task_process_work()`，由 `etl::scheduler` 轮询驱动，无栈无线程 —— 它只能当「后台节拍的一种可选实现」（见 16.4 第 1 条），替代不了 FreeRTOS 任务。
4. **ETL 的宏会顺着 include 链跑到别的模块去**：`ETL_TARGET_OS_FREERTOS` 表面上只是「选互斥实现」（`mutex.h:37`），实际会把 `callback_timer.h` 也拖下水（→ `timer.h` → `atomic.h` → `atomic_gcc_sync.h` → `mutex.h` → `mutex_freertos.h` → 要 `FreeRTOS.h`）。教训：加 ETL 宏之前，先用最小 TU 单独 include 一遍要用的头（骨架就是这么发现宿主编不过的）。

## 17. 打印约定（issue 13 落地记录）

- **类型的名字只有一份**：能进日志的枚举/结构体在声明处登记打印方式，不再手写 `to_string`：
  - `include/embark/error.h`：`Error` → `E_FMT_DERIVE_ENUM`（原 `src/embark/error.cpp` 的 34 行 switch 删除，该文件已不存在）；只吃 `const char*` 的出口用新助手 `error_text(buffer, error)`（模板，内部 `e_fmt::format_to`，snprintf 语义）。
  - `include/embark/hal/types.h`：`Rect` / `DisplayInfo` / `InputEvent` → `E_FMT_DERIVE`，`PixelFormat` / `InputEventKind` → `E_FMT_DERIVE_ENUM`。
  - `include/embark/app.h`：`AppSettings` → `E_FMT_DERIVE`，`BackgroundPolicy` → `E_FMT_DERIVE_ENUM`。
  - `include/embark/message.h`：`CrossTaskMessage` 有基类与构造函数、不能整体派生，改用类型体内的 `E_FMT_FIELDS(from_app, seq);`。
- **1 字节整型字段的坑（本仓库规避 + 已登记上游）**：efmt 的派生输出把 `std::uint8_t` / `std::int8_t` / `unsigned char` 成员走**字符通道**（值为 0 时写出 NUL，把整行日志截断），顶层参数不受影响。所以框架里会进日志的 1 字节字段一律抬到 `std::uint16_t`：`AppId`（`message.h`，`invalid_app_id` 同步改 `0xFFFF`）、`AppSettings::task_priority`、`InputEvent::key`，`ITaskSpawner::spawn_task` 的 `priority` 参数跟着改。efmt-elog 是独立仓库（submodule 钉 commit），本仓库**不改 third_party**，缺陷细节、复现与实测输出见 `.scratch/embark-v1/evidence/13-format-derive/`。
- **整对象日志的示范点**：`src/embark/framework.cpp` boot 段打每个 App 的 `AppSettings`；`platform/host/ui_demo.cpp` 打 `DisplayInfo` 与整屏 `Rect`（拆两条，理由见 §9 的 384 字节上限）；`app/demo_apps.cpp` 的 ticker 收发路径打整个 `CrossTaskMessage`；`platform/host/host_input.cpp` 的键盘分支打 `InputEvent`；`platform/esp32/project/main/main.cpp` 打 `DisplayInfo`。
- **验收**：`tests/kernel/test_format_derive.cpp`（5 个用例）钉住 8 个登记类型的输出串与 `error_text` 行为；全量 85 用例 / 629 断言绿（issue 13 的 `## Answer` 有完整记录）。
