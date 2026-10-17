# 01 HAL 底座：背光读回 + 墙上钟可写

Status: done
Stage: 底座
Blocked by: —

## 背景

设置页要显示「现在是 70%」，就得问 HAL 现在的值是多少；要能「把钟拨到今天 14:30」，就得能往墙上钟写数。今天这两条都不存在：

- `include/embark/hal/display.h:38` 只有 `virtual Error set_backlight(std::uint8_t percent) noexcept = 0;`，**没有读回**。真机把值记在 `platform/esp32/src/esp32_display.h:76` 的 `backlight_percent_`，宿主记在 `platform/host/host_display.h:77` 的 `backlight_`，但都不对外。
- `include/embark/hal/time.h:29` 的 `epoch_ms()` 是 const 只读。真机的墙钟是「读一次 RTC，之后用 `now_ms()` 差值往前推」，锚在两个私有字段上（`platform/esp32/src/esp32_time.h:71-73` 的 `base_epoch_ms_` / `base_now_ms_`），所以写时间 = 写 RTC + 重设锚点，不是改一个变量那么简单。

## 做什么

### IDisplay 加读回

``cpp
/// 当前背光亮度 0..100（百分比）。不支持背光的装配返回 0，且 set_backlight() 必须报 unsupported
/// —— 两者口径要一致，别让上层读到 0 却以为「已经关屏」。
[[nodiscard]] virtual std::uint8_t backlight() const noexcept = 0;
``

三个实现：`platform/host/host_display.cpp`（返回 `backlight_`）、`platform/esp32/src/esp32_display.cpp`（返回 `backlight_percent_`；背光没起来时返回 0，与 `set_backlight` 报 unsupported 对齐）、`tests/fakes/fake_display.h`。

### ITime 加可写入口

``cpp
/// 把墙上钟设为 epoch 毫秒（UTC）。没有 RTC / 写失败 → unsupported / 具体错误，**不许静默丢**。
/// 成功之后 epoch_ms() 必须立刻反映新值。
virtual Error set_epoch_ms(std::uint64_t epoch_ms) noexcept = 0;
``

三个实现：

- `platform/host/host_time.cpp`：只改内存基准（宿主不需要 RTC）。
- `platform/esp32/src/esp32_time.cpp`：写 PCF85063 再重锚 `base_epoch_ms_` / `base_now_ms_`，成功时把 `wall_ready_` 置真、`wall_note_` 改成「手动设置」。先读 `platform/esp32/src/esp32_rtc.h` 看有没有 BCD 写接口，没有就补（写 6 个时间寄存器 + 清 OS 位；**不要碰 0x02 的 offset 校准寄存器** —— `esp32_rtc.h:13` 已写明这块板没有温度参考、动了只会引入人为偏差）。
- `tests/fakes/fake_time.h`。

## 验收

- `cmake --build build` 通过、`ctest` 全绿。
- `tests/hal/test_display.cpp` 加两条：`set_backlight(70)` 之后 `backlight() == 70`；`set_backlight(101)` 返回 `invalid_argument` **且读数不变**。
- 时间单测：`set_epoch_ms(T)` 之后 `epoch_ms()` 落在 [T, T+2ms] 内（宿主，允许一次 tick 的推进）；FakeTime 上验证「没有 RTC 的装配」如实返回 unsupported。
- 真机侧（idf 环境可用时）：`esp32_time.cpp` / `esp32_display.cpp` 语法过；改完时间拔电重插仍走。

## Comments

- 2026-10-17 落地：`IDisplay::backlight()` + `ITime::set_epoch_ms()` 进接口，三个实现（host / esp32 / fake）全齐。
- `Esp32Time::set_epoch_ms` 写 RTC 后**读回秒数再重锚**，驱动侧直接复用已有的 `Pcf85063::write_epoch_seconds`（`platform/esp32/src/esp32_rtc.cpp:213`）：没新增驱动代码，也没碰 0x02 的 offset 校准寄存器。
- `tests/fakes/fake_time.h` 里原来那个同名值的 `void set_epoch_ms(uint64)` 改成接口覆写（返回 `Error`、计 `epoch_write_calls`），原值旋钮改名 `set_epoch_value()`；全仓没有测试用过旧名字。
- 验证：`cmake --build build` 通过；`ctest` 2/2 绿（`embark_tests` / `embark_host_tests`）。
- 真机侧校验：`idf.py -C platform/esp32/project -B build-esp32 build` 通过，产物 `build-esp32/embark_esp32.bin` = **0xe8280 字节**（app 分区 3 MB，富余 70%）。
  - **踩坑**：`-B` 是相对当前工作目录解析的。写成 `-B build` 会在**宿主**的 `build/` 里跑 idf 的 cmake，报 source 不匹配并把宿主缓存删掉；正确姿势是 `-C platform/esp32/project -B build-esp32`（与 `platform/esp32/README.md:64` 一致）。宿主构建已用 `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug` 重建恢复。
- 未做（需要真机）：拨表后拔电重插仍走 —— 这条要接 COM 口，留到后续和真机联调一起验。
