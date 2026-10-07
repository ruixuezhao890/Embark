# 固定容量上限（`config/embark_limits.h`）

> **为什么有这份表**：Embark 的内核**零堆**——不 new、不 malloc、不 std::vector。
> 所有容量都是编译期常量，全部集中在 [`config/embark_limits.h`](../../config/embark_limits.h) 一处。
> 这份文档就是它的逐条解释：**每个数字是什么、为什么是这个值、改它会牵动什么**。
>
> 想改容量：改这一个头文件 → 重新构建。**不要**在别处再写一份数字。

---

## 1. App 与消息

| 常量 | 值 | 含义 | 改它意味着什么 |
| --- | --- | --- | --- |
| `max_apps` | `8` | 静态注册表的 App 上限 | 注册表是定容数组；超了 `app_registry<Apps...>()` 直接编译期 `static_assert` 失败（消息：「App 太多了…」）。调大 = 每个 App 多一份 `AppAdapter` + 记账位，内存增长很小；但**顺序就是默认前台顺序**，加 App 会改默认首页 |
| `max_background_timers` | `8` | 后台节拍定时器数量（`etl::callback_timer<max_background_timers>`） | 这是 `tick` 策略的硬上限。注意它**不是**「最多 8 个 tick App」——见下方「定时器 vs App」 |
| `max_bus_subscribers` | `8` | 总线订阅者上限（`etl::message_bus` 的 `MAX_ROUTERS`，镜像订阅表同容量） | 每个 App 启动时自动订阅一次，所以**有效上限 ≈ max_apps**。再挂自定义 router 就会挤掉别人 |
| `message_queue_depth` | `16` | 跨任务收件箱深度（own task → UI 任务） | 满了丢**最旧**并计数（`inbox_overflows()`）。宁可丢消息，不阻塞生产端。own task 高频 post 就调大 |

### 定时器 vs App：一个容易踩的边界

`tick` 策略的定时器是**按 App 逐个注册**的，`max_background_timers` 只是 ETL 那层的容量。
真正的约束是：**每个声明了 `tick` 的 App 占一个定时器**，所以「8 个定时器」大致等于「最多 8 个 tick App」。
超过时 `boot()` 的注册段拿不到 id，返回 `Error::no_space`（`src/embark/framework.cpp` boot 第 6 段）。

---

## 2. 显示与 LVGL

| 常量 | 值 | 含义 | 改它意味着什么 |
| --- | --- | --- | --- |
| `display_width` | `240` | 面板宽（px） | 与 `config/lv_conf.h`、ST7789 原生方向、宿主窗口三者必须一致。改它要同时改 `lv_conf.h` 与 `platform/esp32/src/esp32_board.h` |
| `display_height` | `320` | 面板高（px） | 同上。**竖屏是面板原生方向**（真机 `ROT_NONE` 不旋转），宿主窗口同向 |
| `display_bytes_per_pixel` | `2` | 每像素字节（RGB565） | 全仓库只认一种像素格式，中间不做格式转换。改成别的要动 `PixelFormat` 与所有 `flush` 路径 |
| `display_stride_bytes` | `display_width * display_bytes_per_pixel` | 一行字节数 | 派生值，别单独改 |
| `lvgl_draw_buf_lines` | `40` | 绘制缓冲行数 | 占用 = 行数 × 一行字节数 ≈ `40 × 240 × 2 ≈ 19 KB`。行数越多一次刷得越多、越省 SRAM 就调小 |
| `lvgl_alloc_budget_bytes` | `256 * 1024` | LVGL 堆预算 | `lv_conf.h` 用 `LV_MEM_CUSTOM=1` 把 LVGL 的分配全指到 `platform/host/host_lvgl_mem.cpp`，**超出即 fatal**。这是宿主值，真机另配 |
| `lvgl_input_queue_depth` | `16` | LVGL 端口的输入中转队列 | UI 循环每帧把 HAL 输入抽干塞进来，LVGL 的 `read_cb` 一个个取。满了丢最旧并计数 |

> 宿主窗口默认 **1:1**（1 个面板像素 = 1 个屏幕像素，不放大不发糊），见
> [`platform/host/host_display.h`](../../platform/host/host_display.h) 的 `window_scale`。
> 想要放大看细节就传 `window_scale = 2/3/…`（整数倍最近邻，仍然不糊）。

---

## 3. own task 与唯一 UI 任务

| 常量 | 值 | 含义 | 改它意味着什么 |
| --- | --- | --- | --- |
| `max_own_tasks` | `2` | own_task 策略的**并发**上限（每个 App 至多一个任务） | 槽位由平台任务池运行时分配/回收（[`platform/common/pooled_task_spawner.h`](../../platform/common/pooled_task_spawner.h)）。这里只定「同时最多几个」；**池的静态 arena 大小由它乘出来**，调大 = .bss 直接变大 |
| `own_task_stack_words` | `512` | 后台任务槽的栈容量（单位：**按宿主字长算的字**） | 宿主 1 字 = 8 字节 → `512 字 = 4 KB`；真机用 `stack_word_bytes` 换算成同样字节数。App 的 `settings().task_stack_words` 为 0 时用它；**超过它 = 槽装不下**（`Error::no_space`） |
| `ui_task_stack_words` | `2048` | 唯一 UI 任务的栈（单位同上） | 宿主 `2048 字 = 16 KB`。留给 LVGL 回调 + 前台 App 逻辑。实测占用看宿主演示的栈高水位输出（`uxTaskGetStackHighWaterMark`） |
| `ui_task_priority` | `5` | UI 任务优先级 | 高于内核空闲任务与其它系统任务即可；真机按中断/任务布局重排 |
| `ui_loop_period_ms` | `5` | UI 循环节拍 | 空闲 `vTaskDelay` 让出、绝不忙等。**宿主墙钟约 2×**（Windows 定时器粒度，见 spec §3），tick 语义不变 |

### ⚠️ 栈深单位的坑（真机上最容易踩的一条）

`own_task_stack_words` 与 `ui_task_stack_words` 的单位是**「宿主字长的字」**，不是字节：

- 宿主：`StackType_t = size_t` → 1 字 = 8 字节 → `512 字 = 4 KB`
- 真机 xtensa：`StackType_t = uint8_t` → 照字面搬过去只剩 1/8

所以真机端口必须用 `platform/esp32/src/esp32_board.h` 里的 `stack_word_bytes` **换算成字节**再交给 IDF
（IDF 的 `ulStackDepth` 收的是字节）。**别在别处再乘一次 `sizeof(StackType_t)`**——
这是 issue 21 实际踩过的坑，详见 [pitfalls.md](../reference/pitfalls.md)。

---

## 4. HAL 持久化

| 常量 | 值 | 含义 | 改它意味着什么 |
| --- | --- | --- | --- |
| `persistence_max_key_bytes` | `16` | 键长上限 | 键值都走定长槽（宿主落文件、测试 fake 落内存，两边共用 `detail/kv_slot.h` 的编解码）。键超长 → `Error::no_space` |
| `persistence_max_value_bytes` | `64` | 单条值上限 | **故意给得小**：持久化只放「小对象」（校准值、上次页面、开关），大块数据不属于它 |
| `persistence_max_slots` | `32` | 槽位总数 | 写满后 `write` 返回 `Error::no_space`（**不静默覆盖**）。`capacity_bytes()` 可以运行时查 |

> 持久化的错误语义是完整的：键不存在 → `not_found`（不做静默成功）、`erase` 不存在也 → `not_found`、
> 整个区损坏 → `corrupt_data`（可用 `erase_all()` 重来）。见 [`include/embark/hal/persistence.h`](../../include/embark/hal/persistence.h)。

---

## 5. 测试替身

这几个常量**只被 `tests/fakes/` 读**，真机后端不看。放在这里是为了「容量都查得到」这条规矩不破例。

| 常量 | 值 | 含义 |
| --- | --- | --- |
| `fake_input_queue_depth` | `8` | 假输入的脚本队列深度 |
| `fake_bus_script_depth` | `8` | 假总线的脚本步骤数 |
| `fake_log_capture_bytes` | `4096` | 假日志 sink 的捕获缓冲 |

---

## 6. 想加一个常量

1. 先问：**能不能复用一个已有的？** 多数情况能。
2. 加在 `config/embark_limits.h`，**必须带一行注释说明它约束什么**（该文件的风格就是「每个数字都有理由」）。
3. 更新本文件对应表格。
4. 若它进了公开头文件或影响二进制布局，在 commit 信息里点明。

> 相关：[`docs/reference/pitfalls.md`](pitfalls.md)（定容行为的坑）、
> [`docs/concepts/app-lifecycle.md`](../concepts/app-lifecycle.md)（own task 生命周期）、
> [`docs/guides/hal-backend-guide.md`](../guides/hal-backend-guide.md)（写后端时哪些容量要遵守）。
