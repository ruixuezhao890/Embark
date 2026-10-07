# 可移植性：HAL 与"同一份代码跑两个平台"

> **这一层回答"为什么 HAL 长这样、换块板子要动什么"**。
> 要动手写后端去 [../guides/hal-backend-guide.md](../guides/hal-backend-guide.md)（有逐步骨架与 IDF 坑清单）。

---

## 1. 可移植性靠什么成立

三条一起成立才行，缺一条就会破功：

| 机制 | 做法 | 破了会怎样 |
| --- | --- | --- |
| **接口在链接期选择** | 没有"后端注册表"：宿主可执行文件链宿主后端、测试链假后端、真机链 ESP32 后端。内核不查表、不动态选择 | 一旦有运行期选择，就需要堆与虚表管理，"零堆"与"换后端不动 app/"两条验收同时不成立 |
| **App 不 include platform** | `app/` 下不出现任何 `platform/` 头（spec §14.5 的验收项） | 后端一换 App 就得改，可移植性只剩口号 |
| **能力粒度只到芯片** | HAL 只抽象"这块芯片能做什么"，**不做设备驱动** | 一抽象具体外设，接口就被某颗芯片的细节绑死，可移植性反而下降 |

第三条是**有意的取舍**，不是遗漏——见 [ADR 0002](../adr/0002-hal-capability-granularity.md)：
代价是"上层要用某个具体外设时，得自己在 App 侧或平台侧写驱动"。

---

## 2. 七个能力接口 + 一个装配点

`hal::Context` 是**引用聚合体**，不是服务定位器，也不是全局单例：

```cpp
struct Context {
  ITime&        time;      // 时间（now_ms / delay_ms / epoch_ms）
  IPersistence& storage;   // 持久化（定长 KV 槽）
  ILogSink&     log;       // 日志后端（收整条已格式化记录）
  ISystem&      system;    // 系统控制（restart / 内存指标 / fatal）
  IBus&         bus;       // 芯片总线（I2C / SPI 的原始字节读写）
  IDisplay*     display;   // 可空：v1 允许无屏平台
  IInput*       input;     // 可空
};
```

**为什么是引用成员**：它不能默认构造、不能拷贝，于是"装配点只有启动入口一处"成了编译期事实。
每个平台一份实例（宿主一份、测试自搭一份），内核不持全局单例。

**为什么 `display` / `input` 是裸指针可空**：内核不该被"必须有屏"绑住——
`Framework` 的构造参数 `IUiPort* ui = nullptr` 与之对应，无界面平台与单元测试都能跑内核。

---

## 3. 逐条能力的边界

每条接口都有一条"**为什么停在这里**"的线：

| 接口 | 提供 | **故意不提供** | 理由 |
| --- | --- | --- | --- |
| `ITime` | `now_ms()` 单调时钟、`delay_ms()` | 日历时间（`epoch_ms` 无 RTC 时如实返回 `unsupported`） | 无 RTC 的板子不该因此无法启动；`unsupported` **不是错误用法** |
| `IPersistence` | 定长 KV 槽（键 ≤ 16B、值 ≤ 64B、32 槽） | 文件系统、大对象存储 | 持久化只放"小对象"（校准值、上次页面、开关）。写满返回 `no_space`，**不静默覆盖** |
| `ILogSink` | `write(const char*, size_t)` 收整条已格式化记录 | 格式化、分级、过滤 | 那些是上游 `elog` 的职责；sink 只管把字节送出去，**不许阻塞太久、不许再打日志** |
| `ISystem` | 重启、内存/栈水位、喂狗、`fatal` | 任务管理、锁 | 系统类操作没有 Error 语义，**失败路径用 `fatal` 上报** |
| `IBus` | `i2c_write` / `i2c_write_read` / `spi_transfer` | **任何设备驱动**（IMU、RTC、触摸……） | 驱动写在 App 或平台侧；HAL 只到"收发字节"。NACK → `io_failure` |
| `IDisplay` | `info()` / `flush(Rect, span)` / `set_backlight()` | 双缓冲、异步完成回调、脏区合并 | `flush` 是**同步语义**：返回时缓冲区必须可复用。宿主因此不做双缓冲，只重画脏区 |
| `IInput` | `poll(InputEvent&)` → `expected<bool, Error>` | 回调、事件队列 | v1 只有 UI 任务碰输入，`poll` 一次取一个就够；队列在端口层 |

> **指针事件与按键事件只用一个字段区分**：`InputEvent.key` 为 0 = 指针（触摸/鼠标/触控板），
> 非 0 = 按键（键号 0..255），此时 `x/y` 是"按键那一刻指针在哪"。
> 上层**只看 `key` 就能分开两类**，不需要额外的事件类型字段。

---

## 4. 三个后端，一份源码

```
                include/embark/hal/*.h      ← 接口（七个纯虚）
                        │
        ┌───────────────┼───────────────┐
   platform/host/   tests/fakes/   platform/esp32/src/
   （PC 仿真）      （单元测试）    （ESP32-S3 真机）
        │               │               │
   platform/common/  ← 三边共用：lvgl_port / lvgl_ui_port / static_pool / pooled_task_spawner
        │
   app/  ← 一行都不用改
```

| 后端 | 目录 | 特点 |
| --- | --- | --- |
| **宿主** | `platform/host/` | 一个能力一对 `host_<cap>.{h,cpp}`；SDL2 显示与输入；持久化落文件；`IBus` 一律返回 `unsupported` |
| **测试替身** | `tests/fakes/` | 记录调用、可注入故障、不碰硬件；与宿主后端**共用同一套数据格式**（`detail/kv_slot.h`） |
| **真机** | `platform/esp32/src/` | ST7789 + `esp_lcd`、CST328 触摸、NVS、`esp_timer`；板级常量集中在 `esp32_board.h` |

**宿主的总线为什么一律返回 `unsupported`**：这不是"没实现"，而是"这台机器本来就没有物理总线"。
好处是**上层写好的错误分支在宿主机上会被真跑一遍**——而不是等到上板才发现某条分支从没执行过。
宿主的无头自检程序（`platform/host/main.cpp`，目标 `embark_host`）就把这条当断言查：
`i2c_write(0x3C, ...)` 必须返回 `unsupported`，否则计一次失败。

---

## 5. 共用层：什么该提上来，什么不该

`platform/common/` 放的是**平台无关**、被宿主与真机同时使用的部分：

| 文件 | 作用 | 为什么是共用 |
| --- | --- | --- |
| `lvgl_port.{h,cpp}` | 把 `IDisplay`/`IInput` 接到 LVGL 驱动（时基、刷新回调、输入读取回调、日志回调） | 不持有任何界面对象，也不认识任何平台类型 |
| `lvgl_ui_port.{h,cpp}` | `IUiPort` 实现：`tick → pump_input → process`，`lv_timer_handler` 的唯一调用点 | "退出请求"来源改成注入的 `ExitQuery` + 上下文：宿主传"关窗查询"，真机传 `nullptr`（永远为假，一直跑） |
| `static_pool.{h,cpp}` | 定容静态池（16 字节粒度、地址序双向链表、首次适配、相邻块合并） | 两个用户：真机拿它当 LVGL 堆的 arena，`pooled_task_spawner.h` 拿它当 own task 的槽位池 |
| `pooled_task_spawner.h` | `ITaskSpawner` 的平台无关实现 `PooledTaskSpawner<Kernel>` | **不含任何 FreeRTOS 类型**：`StaticTask_t`/`StackType_t` 留在各平台 Kernel 里，靠 CRTP 挂钩 |

**不该提上来的**：`ui_task`（唯一 UI 任务的静态创建，各平台的任务原语不同）、
`own_task_spawner.h` / `esp32_task_spawner.h`（只写 Kernel 契约 + 各自的任务类型定义）、
以及全部 `host_*` / `esp32_*` 实现。

> 新平台要提供的 Kernel 契约（照这个清单实现即可）：`slot_bytes()`、`header_of()`、
> `make_task()`、`is_parked()`（任务真的停稳了吗）、`delete_task()`、
> `suspend_self()`（入口返回后永久挂起）。

---

## 6. 跨平台的三个口径坑（实测踩过）

1. **栈深单位是"字"，不是字节**。框架里 `*_stack_words` 一律按**宿主字长**算
   （`512 字 = 4 KB`），后端负责换算成平台要的字节数：IDF 的 `xTaskCreate*` 收**字节**，
   vanilla FreeRTOS 的 `ulStackDepth` 收的才是字。xtensa 的 `StackType_t` 是 `uint8_t`，
   **照字面搬过去只剩 1/8**（issue 21 就这么把 16 KB 的 UI 任务变成 2 KB）。
   别在别处再乘一次 `sizeof(StackType_t)`。
2. **宿主上"1 tick = 1 ms"不成立**。Windows 定时器粒度把 FreeRTOS 一拍拉到约 2 ms（issue 02 实测），
   所以断言只能比 tick 的相对量，不能比墙钟毫秒。
3. **配置文件必须纯 ASCII**。IDF 的 `kconfgen` / `gen_esp32part.py` 按**宿主本地编码**解码
   （中文 Windows = GBK），`sdkconfig.defaults` 与 `partitions.csv` 里带中文注释会直接
   `UnicodeDecodeError` 打断配置阶段。中文文档写进 `README.md`（走 UTF-8）。

详见 [../reference/pitfalls.md](../reference/pitfalls.md) 与
[../guides/hal-backend-guide.md](../guides/hal-backend-guide.md) 的坑清单。

---

## 7. 从这里往哪走

- **写一个新后端**：[../guides/hal-backend-guide.md](../guides/hal-backend-guide.md)（骨架 + 七条 IDF 坑）
- **真机端口细节**：[`platform/esp32/README.md`](../../platform/esp32/README.md)（板级参数、烧录、bring-up 清单）
- **HAL 的 API 签名**：[../reference/hooks-and-api.md](../reference/hooks-and-api.md) §6
- **为什么不做设备抽象**：[../adr/0002-hal-capability-granularity.md](../adr/0002-hal-capability-granularity.md)
