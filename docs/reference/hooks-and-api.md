# 钩子契约与 API 速查

> **这份文档回答**：这个钩子什么时候被调？在哪个线程？能做什么、不能做什么？
> 这个 API 叫什么、返回什么、失败时给什么错误码？
>
> 只讲**契约与签名**。想理解"为什么这么设计"去 [concepts/](../concepts/index.md)，
> 想照着做一个 App 去 [guides/new-app-guide.md](../guides/new-app-guide.md)。

---

## 1. 八个钩子：一次说清

**先记住一句话**：框架不认识「App 在干什么」。没有 `status` / `visible` / `running` 字段可查，
也不存在「先查询状态再决定调哪个钩子」——全部是
**事件驱动 + 定时器自发 + 每帧无条件**三类机制，外加一个「进过前台」的记账位（`entered_`）。
所以真正要知道的不是框架内部判定，而是下面这张**分派契约**表。

| 钩子 | 何时被调 / 几次 | 线程 | 该放什么 | 不该放什么 |
| --- | --- | --- | --- | --- |
| `onCreate(Framework&)` | boot 装配期，**恰好 1 次**；按注册顺序 | UI | 存 `fw_ = &fw`；注入 HAL 能力（`fw.hal()`）；`eez_ui_bridge_ensure_init()`；建静态资源 | **别 publish**——总线这时还没装配，发出去没人收（计 unknown + WARN，见 §3 第 1 条） |
| `onEnter()` | **≤ 1 次**：第一次成为前台时 | UI | 挂自己的 EEZ 屏（`enter_app_screen(name())`）；启动计时状态机 | 别假设「每次回前台都会调」——那是 `onResume` |
| `onPause()` | **每次**离开前台 | UI | 保存状态、停自己的节拍；打日志 | 碰 UI（框架不碰你的 UI，也不替你收尾） |
| `onResume()` | **每次**回到前台 | UI | 恢复显示；**再挂一次屏**（你的屏可能已被别的 App 覆盖） | 以为「屏还在」 |
| `onForegroundTick(now_ms)` | **每帧一次**，仅当前台 | UI | 推数据给 UI：先 `set_var_*` 再 `eez_ui_bridge_tick()`；EEZ App 驱动 `eez_flow_tick()` | 任何耗时工作——它就在 UI 循环里，卡住 = 掉帧 |
| `onBackgroundTick(now_ms)` | 武装之后按 `settings().period_ms` 周期；**默认没被打开过 = 一次都不跑** | **tick 策略 = UI 任务；own_task 策略 = 独立任务** | 轻活（tick）；阻塞 / 重计算（own_task） | own_task 里**碰 UI**、**publish**——回 UI 只能 `post` 信封 |
| `onMessage(const Message&)` | 每条广播一次 | UI | 按 `msg.get_message_id()` 自分发 | 以为收不到自己发的消息——**v1 是广播，自己发的也会回来**（§3 第 2 条） |
| `onExit()` | **恰好 1 次** | UI | 收尾、落盘、打日志 | 依赖它做日常清理——**v1 只有关机路径会触发**，没有别的退役路径 |

### 三个"最常见写错"（都有源码证据）

1. **`onCreate` 里 publish 会丢。**
   总线在 `onCreate` 之后才装配（`src/embark/framework.cpp:44-54`），
   早于它的广播被计为 unknown 并 WARN（`src/embark/bus.cpp:66-71`）。
   装配消息请放 `onEnter` 之后。

2. **`onMessage` 会收到自己发的消息。**
   `publish` 遍历所有订阅者，**发送方的 adapter 也在订阅表里**（`src/embark/bus.cpp:55-75`）。
   要防自回环就带个 `from` 字段自己过滤。

3. **同一个 `onBackgroundTick`，线程语义完全不同。**
   `tick` 策略在唯一 UI 任务里被调（`src/embark/framework.cpp:152` → `:196`），
   `own_task` 策略在自己的任务里被调（`:259` 一次性 / `:264` 常驻循环）。
   跨策略复制粘贴代码前先确认这条。

### 后台周期是帧粒度

`ceil(period_ms / ui_loop_period_ms)` 个帧——宿主 5 ms 一拍（`config/embark_limits.h:85`）。
所以 `period_ms = 100` 实际是 20 帧；宿主墙钟因 Windows 定时器粒度约 2×（见 spec §3）。

### 武装时机：为什么"声明了策略却一次都没跑"

声明后台策略**默认也不会**一上电就跑：

- 默认 `ArmPolicy::on_first_enter`：**第一次进过前台**（`onEnter` 同一帧）才武装，之后才可能出现 `onBackgroundTick`。
- 显式 `ArmPolicy::at_boot`：boot 期就武装（闹钟这类由持久化状态驱动的 App）。
- 查询：`fw.background_armed(id)`；武装失败的累计次数：`fw.arm_failures()`。
- 详见 [concepts/app-lifecycle.md](../concepts/app-lifecycle.md) §4。

---

## 2. App 基类：4 个元数据 + 8 个钩子

声明在 [`include/embark/app.h`](../../include/embark/app.h)。

### 元数据（都有默认实现，只有 `name()` 必须写）

| 成员 | 签名 | 默认 | 说明 |
| --- | --- | --- | --- |
| `name` | `[[nodiscard]] virtual const char* name() const = 0` | 无（**必须实现**） | 注册表键；**必须与 EEZ 屏名一致**；同名会编译期报错 |
| `title` | `virtual const char* title() const` | `name()` | 给人看的名字 |
| `icon` | `virtual const char* icon() const` | `nullptr` | `LV_SYMBOL_*`；空则用标题首字符兜底 |
| `accent` | `virtual std::uint32_t accent() const` | `design_tokens::accent` | 强调色。**禁止魔法颜色值**，从 design tokens 取 |

### 钩子

```cpp
virtual void onCreate(Framework& fw) = 0;          // boot 装配期，恰好 1 次
virtual void onEnter() = 0;                        // 第一次成为前台
virtual void onPause() = 0;                        // 每次离开前台
virtual void onResume() = 0;                       // 每次回到前台
virtual void onBackgroundTick(std::uint32_t now_ms);   // 武装后按周期（默认空实现）
virtual void onForegroundTick(std::uint32_t now_ms);   // 每帧，仅前台（默认空实现）
virtual void onMessage(const Message& msg);            // 每条广播一次（默认空实现）
virtual void onExit() = 0;                         // 恰好 1 次，v1 只有关机路径
```

> `App` 不可拷贝、不可赋值（`App() noexcept = default`、虚析构、拷贝/赋值 `= delete`）。
> 每个 App 类型在进程里**恰好一个实例**——由 `detail::app_instance<T>()` 的 local static 保证。

### 后台策略声明

```cpp
virtual AppSettings settings() const { return AppSettings{}; }   // 默认 = suspend

E_FMT_DERIVE(struct AppSettings {
  BackgroundPolicy background = suspend;   // suspend / tick / own_task
  std::uint32_t    period_ms = 0;          // tick / own_task 的周期；0 = 不跑
  std::uint16_t    task_stack_words = 0;   // 0 = 用 own_task_stack_words
  std::uint16_t    task_priority = 0;      // 0 = 平台默认
  ArmPolicy        arm = on_first_enter;   // on_first_enter / at_boot
});
```

**四字段写法照旧可用**（`arm` 有默认值）：`AppSettings{BackgroundPolicy::tick, 100U, 0U, 0U}`。

三种策略的语义、怎么选，见 [concepts/messages-and-background.md](../concepts/messages-and-background.md) §「后台策略」。

---

## 3. Framework：一个 App 能用的全部

声明在 [`include/embark/framework.h`](../../include/embark/framework.h)。
文件头 `:8-65` 是权威契约描述（boot 九步、step 七段、各条纪律）。

### 生命周期（平台入口调，App 一般不调）

| 方法 | 签名 | 说明 |
| --- | --- | --- |
| `boot` | `[[nodiscard]] Error boot()` | 装配 + 武装，**一次**。幂等：已 boot 再调返回 `none` |
| `step` | `void step()` | 一帧。`boot` 成功后调用；`shutdown` 后不要再调（内部总闸直接 return） |
| `shutdown` | `void shutdown()` | 关停。幂等；`shutdown` 后 `exit_requested()` 为 true |

### 前台切换

| 方法 | 签名 | 说明 |
| --- | --- | --- |
| `request_switch` | `[[nodiscard]] Error request_switch(AppId id)` | **只登记**，下一个 step 边界生效。`NotFound` = 编号无效；切到当前前台 = no-op（返回 `none`，不触发任何钩子） |
| `request_switch` | `[[nodiscard]] Error request_switch(const char* name)` | 按名字；名字无效 → `not_found` |
| `request_home` | `[[nodiscard]] Error request_home()` | 回默认前台（注册表第 0 个） |

> 导航壳已于 2026-10-06 退役：现在由 App 显式调用，或由 EEZ 屏观察者转成 `request_switch`
> （`app/common/eez_ui_nav.cpp`，仓库里唯一的 `request_switch` 调用者）。

### 消息

| 方法 | 签名 | 说明 |
| --- | --- | --- |
| `publish` | `void publish(const etl::imessage&) noexcept` | 总线广播。**只能在框架线程调用**（UI 任务 / tick / App 生命周期钩子） |
| `post` | `void post(const CrossTaskMessage&) noexcept` | 信封入收件箱。**own task 里唯一的回 UI 通道**；step 的派发段把收件箱抽干再广播 |
| `bus` | `[[nodiscard]] const Bus& bus() const` | 读总线计数（`published()` / `unknown()` / `subscriber_count()`） |
| `inbox_overflows` | `[[nodiscard]] std::uint32_t inbox_overflows() const` | 收件箱溢出计数（满了丢最旧） |

### own task

| 方法 | 签名 | 说明 |
| --- | --- | --- |
| `spawn_own_task` | `[[nodiscard]] Error spawn_own_task(AppId id)` | 运行期手动拉起（武装时框架自己会调）。`not_found` / `not_ready` / `unsupported`（无 spawner）/ `no_space`（池满）/ `busy`（已在跑） |
| `reap_finished_own_tasks` | `[[nodiscard]] std::size_t reap_finished_own_tasks()` | 回收已跑完的任务槽。step 第 7 段每帧调一次 |
| `own_tasks_spawned` | `[[nodiscard]] std::uint32_t own_tasks_spawned() const` | 累计创建次数 |
| `own_tasks_released` | `[[nodiscard]] std::uint32_t own_tasks_released() const` | 累计回收次数 |

### 观测（测试与 tour 用）

| 方法 | 说明 |
| --- | --- |
| `apps()` | `const AppRegistry&` |
| `booted()` | 是否已 boot |
| `foreground()` / `pending_foreground()` / `switch_pending()` | 当前 / 待切 / 是否有待切 |
| `frames()` / `switches()` | 累计帧数 / 前台切换次数 |
| `background_armed(AppId)` | 该 App 的后台是否已武装 |
| `arm_failures()` | 武装失败累计次数 |
| `exit_requested()` | 退出请求（UI 端口 + shutdown 都会置） |
| `app(AppId)` / `id_of(const App&)` | 编号 ↔ 实例 |
| `hal()` | `hal::Context&` —— **App 拿硬件能力的唯一入口** |

### 构造

```cpp
Framework(hal::Context& hal, AppRegistry apps,
          IUiPort* ui = nullptr,          // 可空 = 无界面平台 / 测试
          ITaskSpawner* spawner = nullptr // 可空 = 有 own_task App 时 boot 返回 unsupported
          ) noexcept;
```

---

## 4. 消息 API

声明在 [`include/embark/message.h`](../../include/embark/message.h) 与 [`include/embark/bus.h`](../../include/embark/bus.h)。

| 名字 | 定义 | 说明 |
| --- | --- | --- |
| `Message` | `using Message = etl::imessage` | 所有消息的基类 |
| `MessageT<Id>` | `template <MessageId Id> using MessageT = etl::message<Id>` | 定义消息类型：`struct MyMsg : MessageT<0x21> { ... }` |
| `AppId` | `using AppId = std::uint16_t` | **不是 `uint8_t`**：efmt 派生打印会把 1 字节整型当字符输出（`from_app = 1` 打成 `0x01`，`0` 写出 NUL 截断整行） |
| `invalid_app_id` | `0xFFFFU` | 无效编号 |
| `cross_task_message_id` | `0xFEU` | **保留**：跨任务信封专用，别拿来定义自己的消息 |
| `CrossTaskMessage` | `MessageT<0xFE>`，字段 `AppId from_app` / `std::uint32_t seq` | own task → UI 的信封；收到后自己再 `publish` 一条真正的业务消息 |
| `Bus::publish` | `void publish(const etl::imessage&) noexcept` | **无人 `accepts` 时不发**，计 unknown + WARN 一次 |
| `Bus::subscribe` / `unsubscribe` | 见头文件 | 每个 App 在 boot 时自动订阅一次 |
| `Bus::published` / `unknown` / `subscriber_count` / `reset_counters` | 计数观测 | 与 ETL 裸 bus 的区别就在 `unknown` 这个计数 |

> **v1 是全收广播**：`AppAdapter::accepts` 恒返回 true（`include/embark/framework.h:264`），
> 所以每条消息会进**每个** App 的 `onMessage`，包括发送者自己。按 `get_message_id()` 过滤。

---

## 5. EEZ 薄桥 API（App 侧）

完整说明见 [guides/eez-ui-manual.md](../guides/eez-ui-manual.md) §8。
声明在 [`app/common/eez_ui_bridge.h`](../../app/common/eez_ui_bridge.h)，命名空间 `embark::demo`。

| 组 | 函数 |
| --- | --- |
| 把 UI 跑起来 | `eez_ui_bridge_init()` / `ensure_init()`（幂等） / `tick()` / `load_current_screen()` / `current_screen()` |
| 屏名 → App | `screen_count()` / `screen_name(int)` / `screen_id(const char*)` / `screen_created(const char*)` |
| 切屏 | `load_screen(int)` / `load_screen_by_name(const char*)` / `load_screen_for_app(const char*)` / `enter_app_screen(const char*)` |
| Flow 变量 | `var_count()` / `var_name(int)` / `var_index(const char*)` / `set_var_int/float/bool/string` / `get_var_int/float/bool` |
| 反向通知 | `set_screen_observer(EezScreenObserver, void*)` |

哨兵值：`kEezScreenNone = -1`、`kEezVarNone = -1`。

> **纪律**：变量名不存在、或 `ui_init()` 还没跑 → 一律返回 `false` 且**无副作用**。
> 必须等 `ui_init()` 之后才能写变量（生成代码那时才分配全局变量的存储）。
> `enter_app_screen` 缺屏时**保持当前屏 + 一条 ELOG_WARN**，不黑屏。
> 桥自己发起的 `load_screen*` **不会**触发观察者回调。

配套：`app/common/eez_ui_nav.h`（`attach(Framework&)` / `detach()` / `switch_requests()` / `last_screen()`）、
`app/common/eez_ui_screen_names.h`（纯函数，可单测：`eez_ui_screen_is_subpage` / `eez_ui_app_name_for_screen`）。

---

## 6. HAL API（App 与平台侧）

接口声明在 [`include/embark/hal/`](../../include/embark/hal/)。**App 通过 `fw.hal()` 拿到的就是它。**

| 接口 | 关键方法 | 说明 |
| --- | --- | --- |
| `ITime` | `now_ms()` / `delay_ms(uint32_t)` / `epoch_ms()` | `now_ms` 单调，从后端初始化起算；无 RTC 时 `epoch_ms` 返回 `unsupported`（**不是致命错误**） |
| `IPersistence` | `read(key, span)` / `write` / `erase` / `erase_all` / `capacity_bytes()` | 键值定长槽。不存在 → `not_found`（**不静默成功**）；写满 → `no_space`；区损坏 → `corrupt_data` |
| `ILogSink` | `write(const char*, size_t)` / `flush()` | 收一整条已格式化记录。**不许阻塞太久、不许再打日志** |
| `ISystem` | `restart()` / `free_heap_bytes()` / `min_free_heap_bytes()` / `stack_high_water_bytes()` / `feed_watchdog()` / `fatal(const char*)` | 量不了的内存指标返回 0。**失败路径用 `fatal` 上报**（这类操作没有 Error 语义） |
| `IBus` | `i2c_write(addr7, span)` / `i2c_write_read(addr7, w, r)` / `spi_transfer(out, in)` | **只到"收发字节"**：设备驱动写在 App 或平台侧。NACK → `io_failure` |
| `IDisplay` | `info()` / `flush(Rect, span)` / `set_backlight(percent)` | `flush` 是**同步**语义：返回时缓冲区可复用 |
| `IInput` | `poll(InputEvent&)` → `expected<bool, Error>` | true = 取到 / false = 现在没有 / Error = 设备坏了。一次一个 |

> **指针事件 vs 按键事件**：`InputEvent.key` 为 0 = 指针（触摸/鼠标/触控板），非 0 = 按键（键号 0..255），
> 此时 `x/y` 是"按键那一刻指针在哪"。**只看 `key` 就能分开两类**。

`hal::Context` 是装配点：`{ ITime& time; IPersistence& storage; ILogSink& log; ISystem& system; IBus& bus; IDisplay* display; IInput* input; }`
—— `display` / `input` 可空（v1 允许无屏平台）。

> **宿主的 `IBus` 一律返回 `unsupported`**——不是"没实现"，而是"这台机器本来就没有物理总线"，
> 正好让上层写好的错误分支在宿主机上被真跑一遍。写后端的完整指南见
> [guides/hal-backend-guide.md](../guides/hal-backend-guide.md)。

---

## 7. 错误码

`include/embark/error.h` 的 `enum class Error : std::uint8_t`：

| 值 | 含义（框架里的典型场景） |
| --- | --- |
| `none` | 成功 |
| `not_ready` | 前置条件没满足（如未 boot、定时器没起来） |
| `invalid_argument` | 参数非法（空指针、越界、`percent > 100`） |
| `not_found` | 名字/编号/键不存在（`erase` 不存在的键也给这个，不静默成功） |
| `no_space` | 容量用尽（池满、槽满、栈深超上限） |
| `io_failure` | 设备/IO 层失败（I2C NACK 等） |
| `timeout` | 超时 |
| `unsupported` | 该平台没有这个能力（**不是错误用法**：宿主无总线、无 RTC 都走它） |
| `corrupt_data` | 数据损坏（持久化区） |
| `busy` | 当前状态不允许（已在跑、任务还没停稳） |

`error_text(buffer, err)` 把它写成字符串（**不要自己手抄名字表**）；
`unexpected(Error)` 造一个 `etl::unexpected`。

---

## 8. 平台入口 API（写 `main` / 换平台才用）

| 位置 | 名字 | 说明 |
| --- | --- | --- |
| `platform/host/ui_task.h` | `start_ui_task(UiTaskEntry, void*, UiTaskConfig)` | 建唯一 UI 任务（静态分配）。`busy` = 已建过 |
| | `start_scheduler()` | 启动调度器，**永不返回** |
| | `ui_loop_delay(period_ms)` | 每拍让出（默认 `ui_loop_period_ms`） |
| | `exit_process(int)` | `_Exit`——主线程卡在调度器的模拟中断循环里，`exit()` 会挂死 |
| `platform/host/host_context.h` | `HostHal::instance()` / `init()` / `attach_display` / `attach_input` | 宿主 HAL 聚合。`attach_*` 必须在 `init()` **之前** |
| `platform/common/lvgl_ui_port.h` | `LvglUiPort(context, exit_query, exit_context)` | LVGL 的 `IUiPort` 实现。`exit_query` 为空 = 本平台没有退出信号（真机 v1 就是这样：一直跑） |
| `platform/common/pooled_task_spawner.h` | `PooledTaskSpawner<Kernel>` | own task 的静态定容池。**借一块 → 跑完 → 还回去**；跨任务只允许 `mark_finished` 这一处写 |
| `include/embark/task_spawner.h` | `ITaskSpawner` / `TaskToken{slot, generation}` | 平台实现接口；`TaskToken` 的世代号防"过期句柄误释放" |

> 一帧的九段顺序、boot 的九步，见 [concepts/app-lifecycle.md](../concepts/app-lifecycle.md) §1–§2。

---

## 9. 相关

- 容量数字（`max_apps` / 栈深 / 队列深度…）：[limits.md](limits.md)
- 常见坑（定容行为、消息非聚合、保留 id、无异常无堆、日志上限、派生打印…）：[pitfalls.md](pitfalls.md)
- 内核契约测试（**比任何文档都新，冲突以它为准**）：[`tests/kernel/`](../../tests/kernel/)
