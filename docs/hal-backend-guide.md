# 写一个 HAL 后端（宿主 / 真机骨架）

HAL 是"芯片能力"的抽象（spec §8）：七个纯虚接口 + 一个引用聚合体
`hal::Context`，不暴露寄存器与引脚。**同一份 App 代码不 include 任何
`platform/` 头**（spec §14.5 验收项）——后端在链接期被选进来就行。

接口清单（`include/embark/hal/`）：

| 头文件 | 能力 | 宿主实现 | 真机实现（ESP32-S3） | 测试假实现 |
| --- | --- | --- | --- | --- |
| `display.h` | 显示（区域刷新、表面捕获） | `platform/host/host_display.{h,cpp}` | `platform/esp32/src/esp32_display.{h,cpp}`（ST7789 + `esp_lcd`） | `tests/fakes/fake_display.h` |
| `input.h` | 输入（指针/按键事件） | `platform/host/host_input.{h,cpp}` | `platform/esp32/src/esp32_input.{h,cpp}`（CST328 直连 I2C） | `tests/fakes/fake_input.h` |
| `time.h` | 时间（now_ms、delay_ms） | `platform/host/host_time.{h,cpp}` | `platform/esp32/src/esp32_time.{h,cpp}`（`esp_timer` + `vTaskDelay`） | `tests/fakes/fake_time.h` |
| `persistence.h` | 持久化（KV 槽） | `platform/host/host_persistence.{h,cpp}` | `platform/esp32/src/esp32_persistence.{h,cpp}`（NVS） | `tests/fakes/fake_persistence.h` |
| `log_sink.h` | 日志后端（线级输出） | `platform/host/host_log_sink.{h,cpp}` | `platform/esp32/src/esp32_log_sink.{h,cpp}`（UART0 = stdout） | `tests/fakes/fake_log_sink.h` |
| `system.h` | 系统控制（重启、fatal） | `platform/host/host_system.{h,cpp}` | `platform/esp32/src/esp32_system.{h,cpp}`（`heap_caps` / WDT / `abort`） | `tests/fakes/fake_system.h`（fatal 桩在 fakes.cpp） |
| `bus.h` | 系统总线（外设/模块间，与消息总线不同） | `platform/host/host_bus.{h,cpp}` | `platform/esp32/src/esp32_bus.{h,cpp}`（`i2c_master`；`spi_transfer` 仍 `unsupported`） | `tests/fakes/fake_bus.h` |
| `types.h` | 共享类型（坐标、尺寸、颜色等） | — | — | — |
| `context.h` | `hal::Context` 引用聚合体 | `platform/host/host_context.{h,cpp}` | `platform/esp32/src/esp32_hal.{h,cpp}`（七个成员 + 装配） | 测试自搭 |

> 注：测试的假后端都是独立文件（fake_*.h，fatal/assert 桩在 fakes.cpp）；
> 最完整的"一个类一个文件"对照看 `tests/fakes/fake_display.h` 与
> `platform/host/host_display.{h,cpp}`。

## 宿主后端骨架（照着 platform/host/ 抄）

一个后端 = 一对 `host_<cap>.{h,cpp}`：

1. **头文件**：`class HostXxx final : public embark::hal::IXxx`，实现接口的全部
   纯虚函数（`override` 全部写上，漏一个就编不过——这正好是清单）。
2. **源文件**：实现。纪律：
   - 纯函数式的小能力（now_ms 等）保持无状态；
   - 有状态的能力（持久化、显示表面）把状态放成员，**不要**全局单例；
   - 构造失败要能表达：返回 `embark::Error` 或经 `unexpected(...)`（spec §9），
     不要在构造函数里静默吞掉；
   - 日志一律走框架日志门面 `ELOG_*`，不要 printf（宿主端口做了行级串行化）。
3. **装配**：`host_context.{h,cpp}` 聚合出 `embark::hal::Context` 实例
   （`Context{display, input, time, persistence, log_sink, system, bus}` 引用聚合，
   见 `include/embark/hal/context.h`），入口 `main.cpp` 持有它并传给 Framework。

宿主与真机共用的层（`platform/common/`，issue 11 从 `platform/host/` 提上来的）：

- `lvgl_port.{h,cpp}`：把 `IDisplay`/`IInput` 接到 LVGL 驱动（时基、刷新回调、
  输入读取回调、LVGL 日志回调）；不持有任何界面对象，也不认识任何平台类型。
- `lvgl_ui_port.{h,cpp}`：`IUiPort` 实现——UI 任务循环的固定阶段
  （tick → pump_input → process，`lv_timer_handler` 的唯一调用点）。
  "退出请求"来源改成注入的函数指针 `ExitQuery` + 上下文：宿主传"关窗查询"，
  真机传 `nullptr`（永远为假）。
- `static_pool.{h,cpp}`：定容静态池（16 字节粒度、地址序双向链表、首次适配、
  相邻块合并）。两个用户：真机拿它当 LVGL 堆的 arena，以及 `pooled_task_spawner.h`
  拿它当 own task 的槽位池。
- `pooled_task_spawner.h`：`ITaskSpawner` 的平台无关实现 `PooledTaskSpawner<Kernel>` ——
  `.bss` 里一块 `alignas(16)` 的 arena 按 `max_own_tasks` 个槽切给 Kernel，管槽位状态机
  （free / running / finished）、世代号、以及"入口返回 → 标 finished → 持有者回收"的回收协议。
  **不含任何 FreeRTOS 类型**：`StaticTask_t` / `StackType_t` 全留在各平台 Kernel 里，靠 CRTP 挂钩。
  Kernel 要提供的契约（新平台照这个清单实现即可）：`slot_bytes()`、`header_of()`、
  `make_task()`（`xTaskCreate*Static`）、`is_parked()`（任务真的停稳了吗）、`delete_task()`、
  `suspend_self()`（入口返回后永久挂起）。宿主侧另加 `Kernel::KernelEntry`（`void (*)(void*)`，
  注意 FreeRTOS 的 `TaskFunction_t` 不带 `noexcept`）。
- 平台相关的两块在 `platform/host/` 与 `platform/esp32/` 各写一份：
  `ui_task.{h,cpp}` + `freertos/`（唯一 UI 任务的静态创建——TCB/栈在 BSS、
  `configSUPPORT_STATIC_ALLOCATION`、FreeRTOSConfig 与钩子）、
  `own_task_spawner.h` / `esp32_task_spawner.h`（只写上面那份 Kernel 契约 + 各自的任务类型定义，
  `using HostTaskSpawner = PooledTaskSpawner<HostTaskKernel>` / `Esp32TaskSpawner` 是最终实现）。

## 真机后端骨架（ESP32-S3，issue 11 已落地）

八个能力的实现都在 `platform/esp32/src/esp32_*.{h,cpp}`，板级常量（引脚、时序、
方向开关）集中在 `src/esp32_board.h`，装配与 `hal::Context` 在 `src/esp32_hal.{h,cpp}`，
入口是 IDF 工程的 `platform/esp32/project/main/main.cpp`（`app_main` → 建唯一 UI 任务）。
**上板前要动的开关、bring-up 清单、串口日志样例都写在 [platform/esp32/README.md](../platform/esp32/README.md)。**

落地时踩到的坑（同类后端都会遇到，照抄结论即可）：

1. **配置文件必须纯 ASCII**：`project/sdkconfig.defaults` 与 `project/partitions.csv`
   由 IDF 的 `kconfgen` / `gen_esp32part.py` 读取，它们按**宿主本地编码**解码
   （中文 Windows = GBK）⇒ 带中文注释会直接 `UnicodeDecodeError: 'gbk' codec ...`
   并把配置阶段打断。中文文档写进 `README.md`（Markdown 走 UTF-8，没问题）。
2. **`CONFIG_FREERTOS_HZ` 必须 ≥ 1000**：UI 循环周期 5 ms，100 Hz 下
   `pdMS_TO_TICKS(5) == 0`，`vTaskDelay(0)` 不让出 CPU ⇒ UI 任务忙等、空闲任务被
   拖住 ⇒ 触发任务看门狗。后端另有"ticks 算出来是 0 就退化成 1 tick"的兜底。
3. **栈深口径**：IDF 的 `xTaskCreate*` 收的是**字节**（不是 vanilla FreeRTOS 的字），
   而 `uxTaskGetStackHighWaterMark()` 返回的是**字** —— 框架里的 `*_stack_words`
   在创建时要乘 `sizeof(StackType_t)`，报水位时要再乘回去。
4. **中间件视图要在 `project()` 之前建**：`project/CMakeLists.txt` 先
   `embark_create_middleware_view(<build>/include/middleware <仓库根>)`（第二个参数
   必须显式传仓库根：IDF 工程的 `PROJECT_SOURCE_DIR` 是 `platform/esp32/project`），
   否则组件的 `<middleware/elog/elog.hpp>` 找不到。
5. **`esp_lcd` 的颜色发送是异步的**：`esp_lcd_panel_draw_bitmap` 只是入队，
   `on_color_trans_done` 只在最后一块 chunk 上回调一次 —— 所以 `flush()` 的同步语义
   就是"等一个二值信号量"，不必自己拆包计数。
6. 组件需要自己补宿主由 CMake 提供的东西：`-include config/embark_config.h`、
   `third_party/etl/include`（ETL 是 header-only，真机没有 `etl::etl` target）、
   `EMBARK_PLATFORM_NAME` / `EMBARK_VERSION_STRING` 宏。
7. **那条 `-include` 必须留在组件 `PRIVATE`**：组件的 `PUBLIC` 编译选项会传播到最终
   链接目标 `embark_esp32.elf`，而它要编一个自动生成的 `project_elf_src_esp32s3.c`
   （**纯 C**）—— C++ 专用的 `-include` 一旦写成 `PUBLIC`，C 文件看见 ETL 头就是一片
   `error: unknown type name 'namespace'`。依赖方（`project/main/`）自己带一份同款
   `-include`（`get_filename_component(... ../../../..)` 上跳 4 级取仓库根）。

构建命令（本机 IDF 5.4，`build-esp32/` 已被 `.gitignore` 的 `build-*/` 覆盖）：

```powershell
idf.py -C platform/esp32/project -B build-esp32 set-target esp32s3
idf.py -C platform/esp32/project -B build-esp32 build
idf.py -C platform/esp32/project -B build-esp32 -p COM5 flash monitor
```

上板之后"真机侧"还有两件只能人工确认的事（spec §14 的验收项 ③⑤）：屏幕方向/颜色
与触摸方向/触点位置 —— 开关就在 `src/esp32_board.h`，改完重编即可。

**省力顺序建议**：先写假后端（tests/fakes）把 App 测起来，再写宿主后端跑
demo，最后落真机——三个阶段共用同一套 App 代码，任何一步改的都是后端，
不需要碰 `app/`。

## 为什么没有"后端注册表"

后端在链接期决定：宿主可执行文件链宿主后端、测试链假后端、将来真机链
ESP32 后端。内核不查表、不动态选择——这保证零堆与"换后端不动 app/"
两条验收都能成立。相关决策见 `docs/adr/0002-hal-capability-granularity.md`。