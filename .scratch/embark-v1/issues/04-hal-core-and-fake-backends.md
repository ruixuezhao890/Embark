# 04 · HAL 接口 + 宿主基础后端 + fake 后端

Status: resolved
Type: task
Blocked by: 03

## 目标

按 spec §8 定义 HAL 纯虚接口，并落地「宿主基础后端」与「测试用 fake 后端」两套实现。

## 范围

- `include/embark/hal/`：显示输出、输入、时间、持久化、日志后端、系统控制、总线（最薄 raw 读写）七个能力的纯虚抽象类。接口里**不得出现任何芯片型号、引脚、总线参数**。
- 宿主后端（`platform/host/`）：时间（chrono/SDL）、持久化（宿主文件）、日志 sink（stdout）、系统控制（空实现 + 日志）、总线（空实现）；显示/输入留到 issue 05（SDL2）。
- fake 后端（`tests/` 侧）：可控时间（手动推进）、可注入输入事件、内存持久化、捕获日志行 —— 让不碰硬件的单元测试能跑。
- 错误路径：可失败操作返回 `etl::expected<..., embark::Error>`（spec §9），`embark::Error` 取值集合在此定稿。

## 验收

- 接口里 grep 不到引脚/寄存器/芯片型号字样。
- 用 fake 后端写出的单元测试可跑通，覆盖每个能力至少一条成功路径 + 一条失败路径。
- 宿主基础后端能被一个最小的 main 调用（不需要 UI）。

## 备注

显示/输入要等 SDL2 集成，因此本 issue 先把接口定死并给 fake，真后端在 05 补。

## Answer

结论：**已完成**。七个能力的纯虚接口、宿主五个基础后端、八个 fake 后端与 7 个能力的测试全部落地，构建零警告、`ctest` 全绿、宿主最小 main 连跑三次自检通过。下面按"交付物 / 定稿契约 / 关键决定 / 验收证据"记录。

### 交付物（43 个新文件 + 8 个文件改动）

| 区域 | 文件 |
| --- | --- |
| `include/embark/` | `error.h`、`diagnostics.h`、`log.h`、`detail/kv_slot.h` |
| `include/embark/hal/` | `types.h`、`time.h`、`persistence.h`、`log_sink.h`、`system.h`、`bus.h`、`display.h`、`input.h`、`context.h` |
| `src/embark/` | `error.cpp`、`log.cpp` |
| `platform/host/` | `host_time`、`host_log_sink`、`host_system`、`host_bus`、`host_persistence`、`host_context`（各 `.h`/`.cpp`）+ `main.cpp` + `CMakeLists.txt` |
| `tests/fakes/` | `fake_time.h`、`fake_display.h`、`fake_input.h`、`fake_persistence.h`、`fake_log_sink.h`、`fake_system.h`、`fake_bus.h`、`fakes.h`、`fakes.cpp` |
| `tests/hal/` | `test_time.cpp`、`test_display.cpp`、`test_input.cpp`、`test_persistence.cpp`、`test_log_sink.cpp`、`test_system.cpp`、`test_bus.cpp` |
| 配置/构建改动 | 顶层 `CMakeLists.txt`（`config/` 进 include 路径）、`config/embark_limits.h`（HAL 容量常量）、`config/embark_config.h`（`EMBARK_LOG_SERIALIZE`）、`src/CMakeLists.txt`、`platform/host/CMakeLists.txt`（INTERFACE → STATIC）、`tests/CMakeLists.txt`（多源 + `tests/` 进 include 路径） |

### 定稿契约

- `hal::Context` = `ITime&` / `IPersistence&` / `ILogSink&` / `ISystem&` / `IBus&` + 可空 `IDisplay*` / `IInput*`；**每平台一份实例**：宿主 `HostHal::instance()`（函数内静态），测试自建 `FakeHal`。内核不持全局单例。
- 接口签名：`ITime{now_ms, delay_ms, epoch_ms→expected<uint64,Error>}`；`IPersistence{init→Error, read→expected<size,Error>, write/erase/erase_all→Error, capacity_bytes}`；`ILogSink{init→bool, write/flush→void}`；`ISystem{restart, free_heap_bytes, min_free_heap_bytes, stack_high_water_bytes, feed_watchdog, fatal}`（`restart`/`fatal` 虚拟函数**故意不加 `[[noreturn]]`**，fake 要能记录后返回）；`IBus{i2c_write, i2c_write_read, spi_transfer→Error}`；`IDisplay{init, is_ready, info, flush, set_backlight}`；`IInput{init, poll→expected<bool,Error>}`。
- `embark::Error` 10 个取值定稿（`none`/`not_ready`/`invalid_argument`/`not_found`/`no_space`/`io_failure`/`timeout`/`unsupported`/`corrupt_data`/`busy`）+ `to_string`。**ETL 的 `expected` 不接受裸 `Error`** → `error.h` 提供 `embark::unexpected(Error)` 助手。映射：参数非法 `invalid_argument`；没 init `not_ready`；不存在 `not_found`；容量/缓冲不够 `no_space`；IO `io_failure`；magic/长度/CRC 坏 `corrupt_data`；后端没这功能 `unsupported`。`timeout`/`busy` 暂无人使用，留给 05/11 的总线与显示后端（若那时确认用不到就删）。
- 持久化槽格式（`detail/kv_slot.h`，宿主与 fake **共用同一套编解码**）：magic `EKV1`(4B) / state(1B) / key_len(1B) / value_len(2B LE) / CRC32(4B LE，覆盖 key+value) / key[16] / value[64] = **92 字节**；空槽 state=0。
- 宿主持久化文件：默认 `<exe 目录>/embark_host_kv.bin`，`EMBARK_HOST_STORAGE` 可覆盖；文件 0 字节 → 按 32 槽 × 92 字节建满空槽；大小恰好不符 → `corrupt_data`（可用 `erase_all` 重建）；`capacity_bytes() = 32 × 64 = 2048`。

### 关键决定

1. **测试不链 `embark_platform_host`**：只链 `embark::core` + `doctest::doctest`。否则宿主的 `platform_context()` 与 `embark::assert_failed`/`embark::fatal` 会和测试版定义撞车（C++ 侧没有条件编译隔开两个静态库的强符号）。
2. **日志串行化按"整行"**：elog 的一条记录会分 3 次 `sink.write`（带颜色时的颜色前缀/正文/复位，`elog.hpp:222`）+ 1 次换行写（`elog.hpp:224`）。逐次加锁挡不住两个任务把两行切成四段，所以 `LogSinkBinder` 先把字节攒进 `etl::array<char, max_record_size + 16>`，遇 `'\n'` 才加锁并一次 `write` + 一次 `flush`；暴露 `lines_written()` / `bytes_written()` / `pending_bytes()` 供测试与运行期观测。（这个缺陷是首轮 `ctest` 三个失败用例暴露出来的：期望"一条记录 = 一次 sink 调用"实测 4 —— 记下来，免得后来者又按"一次记录一次调用"设计。）
3. **fake 复用真编解码**：`FakePersistence` 直接调 `detail/kv_slot.h` 的 `encode_slot`/`decode_slot`，并用 `corrupt_slot(index)` 翻 CRC 造坏数据；宿主后端格式一改，fake 侧测试立刻红。
4. **系统类能力的失败上报**：`ISystem` 的重启 / 喂狗 / 水位查询没有 `Error` 语义，失败路径统一由 `embark::fatal` 覆盖（平台版写 stderr + `abort()`，测试版记录 + `abort()`），不假装成功。宿主后端 `restart()` 打印后 `std::exit(70)`，三处水位返回 0（0 = 后端量不了）。
5. **`platform/host` 不链 `embark_kernel_flags`**：平台后端要用 `<fstream>` / `<thread>` / `<chrono>`（宿主侧本来就允许），只有 `embark_host` 可执行文件保留 `-fno-exceptions -fno-rtti`。

### 验收证据

- **接口洁净**：`grep -riE "pin|gpio|esp32|st7789|i2c_addr|ili9341|rmt|ledc" include/embark/hal/` → 0 命中；全 `include/embark/` 只有 `version.h:20` 的注释提到平台名 `"esp32"`（构建系统传进来的字符串，非芯片细节）。
- **测试**：`ctest --test-dir build --output-on-failure` → `1/1 Passed`；测试二进制 `39 test cases | 39 passed`、`259 assertions | 259 passed`。七个能力均含成功 + 失败路径（未 init → `not_ready`、非法参数 → `invalid_argument`、容量不够 → `no_space`、坏数据 → `corrupt_data`、后端无此功能 → `unsupported`、注入 IO 故障透传）。
- **宿主最小 main**：`build/platform/host/embark_host.exe`（`EMBARK_HOST_STORAGE` 指向临时目录）连跑三次 exit 0，`boot_count` 1 → 2 → 3，`epoch_ms 可用`、`free_heap=0`、`i2c/spi 一律 unsupported`，落盘文件 2944 字节（= 32 × 92）。
- **构建**：`cmake --build build` 零警告（`-Wall -Wextra -Wpedantic`）。
