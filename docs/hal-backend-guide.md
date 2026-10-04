# 写一个 HAL 后端（宿主 / 真机骨架）

HAL 是"芯片能力"的抽象（spec §8）：七个纯虚接口 + 一个引用聚合体
`hal::Context`，不暴露寄存器与引脚。**同一份 App 代码不 include 任何
`platform/` 头**（spec §14.5 验收项）——后端在链接期被选进来就行。

接口清单（`include/embark/hal/`）：

| 头文件 | 能力 | 宿主实现 | 测试假实现 |
| --- | --- | --- | --- |
| `display.h` | 显示（区域刷新、表面捕获） | `platform/host/host_display.{h,cpp}` | `tests/fakes/fake_display.h` |
| `input.h` | 输入（指针/按键事件） | `platform/host/host_input.{h,cpp}` | `tests/fakes/fake_input.h` |
| `time.h` | 时间（now_ms、delay_ms） | `platform/host/host_time.{h,cpp}` | `tests/fakes/fake_time.h` |
| `persistence.h` | 持久化（KV 槽） | `platform/host/host_persistence.{h,cpp}` | `tests/fakes/fake_persistence.h` |
| `log_sink.h` | 日志后端（线级输出） | `platform/host/host_log_sink.{h,cpp}` | `tests/fakes/fake_log_sink.h` |
| `system.h` | 系统控制（重启、fatal） | `platform/host/host_system.{h,cpp}` | `tests/fakes/fake_system.h`（fatal 桩在 fakes.cpp） |
| `bus.h` | 系统总线（外设/模块间，与消息总线不同） | `platform/host/host_bus.{h,cpp}` | `tests/fakes/fake_bus.h` |
| `types.h` | 共享类型（坐标、尺寸、颜色等） | — | — |
| `context.h` | `hal::Context` 引用聚合体 | `platform/host/host_context.{h,cpp}` | 测试自搭 |

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

宿主特有的三块（不在七个能力里，但在同一目录）：

- `lvgl_port.{h,cpp}`：把 `IDisplay`/`IInput` 接到 LVGL 驱动（时基、刷新回调、
  输入读取回调、LVGL 日志回调）；不持有任何界面对象。
- `lvgl_ui_port.{h,cpp}`：`IUiPort` 实现——UI 任务循环的固定阶段
  （tick → pump_input → process，`lv_timer_handler` 的唯一调用点）。
- `ui_task.{h,cpp}` + `freertos/`：唯一 UI 任务的静态创建（TCB/栈在 BSS、
  `configSUPPORT_STATIC_ALLOCATION`）、FreeRTOSConfig 与钩子；
  `own_task_spawner.h`：`ITaskSpawner` 的宿主实现。

## 真机后端骨架（ESP32，落地时照此清单做）

`platform/esp32/` 目前只有占位 CMakeLists（issue 11 待板型与触摸型号）。
落地时按这个顺序：

1. `platform/esp32/` 建 ESP-IDF 组件：CMakeLists + `esp32_hal`（七个实现类）
   与 `app_main.cpp`（入口，等价宿主的 `main.cpp`）。
2. 七个能力逐一实现，照宿主骨架的纪律：
   - `display` → 面板驱动（ST7789 等）+ LVGL 刷新回调；
   - `input` → 触摸控制器（型号随 issue 11 提供）→ `IInput` 指针事件；
   - `time` → `esp_timer_get_time() / vTaskDelay`；
   - `persistence` → NVS（或 SPIFFS/LittleFS，按数据量定）；
   - `log_sink` → 串口输出（行级串行化复用宿主同款约定）；
   - `system` → `esp_restart()` / fatal 掉进断言；
   - `bus` → 按实际外设（I2C/SPI 等）封装。
3. `lvgl_port` 接真机刷新与输入；`ui_task` 用 ESP-IDF 任务创建（静态栈或
   用 `xTaskCreateStatic` 与宿主一致）；`own_task_spawner` 换 IDF 版。
4. 构建走 `idf.py set-target esp32s3 && idf.py build`（CI 有 esp32 job 占位，
   issue 11 落地后转必需）。

**省力顺序建议**：先写假后端（tests/fakes）把 App 测起来，再写宿主后端跑
demo，最后落真机——三个阶段共用同一套 App 代码，任何一步改的都是后端，
不需要碰 `app/`。

## 为什么没有"后端注册表"

后端在链接期决定：宿主可执行文件链宿主后端、测试链假后端、将来真机链
ESP32 后端。内核不查表、不动态选择——这保证零堆与"换后端不动 app/"
两条验收都能成立。相关决策见 `docs/adr/0002-hal-capability-granularity.md`。