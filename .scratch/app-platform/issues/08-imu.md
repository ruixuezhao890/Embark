# 08 · 姿态服务 IImu

Status: done（2026-10-18）
Blocked by: 07

## 为什么

Q9=(C) 把 IMU 列进公共硬件资源。今天的实情：

- **硬件在**：QMI8658 六轴，挂板载 I2C_NUM_1（`platform/esp32/src/esp32_board.h:155-164`，SDA=11 / SCL=10，400 kHz），INT1=GPIO13 / INT2=GPIO12
- **总线通**：`hal::IBus` 的 `i2c_write_read` 够用，RTC PCF85063（同一条总线，地址 0x51）的驱动已经在跑
- **驱动没有**：`app/` 下 grep `imu|Imu|IMU|accel|gyro|tilt|shake` 只有 4 处误命中
- **没有 App 想用它**：所以这一票的验收不是"某个 App 变好了"，是"服务本身能读数"

## 接口形状

`include/embark/hal/imu.h`

`struct ImuSample {`
`  float accel_x, accel_y, accel_z;  // 单位 g`
`  float gyro_x, gyro_y, gyro_z;    // 单位 dps（度/秒）`
`  float temperature_c;`
`};`

`class IImu {`
` public:`
`  virtual `IImu() = default;`
`  virtual Error init() noexcept = 0;`
`  virtual Error read(ImuSample& out) noexcept = 0;   // 同步读，不阻塞（I2C 400 kHz 下 < 1 ms）`
`};`

**接口里不许出现芯片型号**（Q10 硬规则）：`IImu` 可以，`IQmi8658` 不行。

## 真机怎么落

`platform/esp32/src/esp32_imu.{h,cpp}`，照 `esp32_rtc.cpp` 的骨架（同一条总线、同样的 `IBus` 用法）：

- 地址：QMI8658 的 SA0 决定 0x6A / 0x6B —— **仓库里没有这个数，上板先用 I2C 扫描确认再写死**（RTC 的 0x51 是图纸确认过的，IMU 没有）
- 初始化序列：`WHO_AM_I` 校验 → 复位 → 量程（±4 g / ±512 dps）→ 低通 → 开加速度计与陀螺 → 设定 ODR
- 换算：原始 LSB → g / dps，系数按选定的量程算

## 宿主怎么模拟

`platform/host/host_imu.{h,cpp}`（不依赖 SDL）：

- 静止：`(0, 0, -1.0) g`（板子平放，Z 轴朝上）
- 摇动：`--imu-shake` 开关下按正弦输出，周期 2 s、幅度 0.5 g —— 给"以后写摇一摇 App"的人一个可复现的输入
- 温度：恒定 25.0 °C

## 验收

- 宿主：`embark_host_ui --ui --delay 40 --imu-log` 每 500 ms 打一行 `ax/ay/az/gx/gy/gz`，静止时 `az ≈ -1.00 ± 0.02`
- 真机：启动日志打 `WHO_AM_I` 回读值；静止时 `az ≈ ±1.0`，绕某轴转 90° 后该轴读数换到重力轴上
- `tests/hal/test_imu.cpp`：用 fake 后端验接口契约（未 init 时 `read` 返回 `not_ready`；init 幂等）
- 不回归四条既有验收命令

## 收尾记录（2026-10-18）

### 落了什么

| 文件 | 内容 |
| --- | --- |
| `include/embark/hal/imu.h`（新，67 行） | `ImuSample`（g / dps / °C）+ `IImu{init, read}`。头注释写清四件事：为什么单位是物理量不是 LSB、坐标系由实现定义、为什么没有就绪回调、失败语义 |
| `platform/host/host_imu.{h,cpp}`（新，53 + 59 行） | 合成实现：静止 `(0,0,-1) g` / 温度 25.0 °C；`set_shake(true)` 换成 X/Y 正弦（周期 2 s、幅度 0.5 g）+ 角速度正弦（周期 1 s、30 dps）。无 SDL、无随机数，纯时间函数 |
| `platform/esp32/src/esp32_imu.{h,cpp}`（新，75 + 201 行） | 板载六轴驱动：探地址 → 软复位 → 配 CTRL1/2/3/7 → 14 字节连读 → 换算 |
| `tests/fakes/fake_imu.h`（新） | `FakeImu`：未 init 返回 `not_ready`、init 幂等（计次）、可注入初值与读/写失败 |
| `tests/hal/test_imu.cpp`（新，6 个 TEST_CASE） | 未 init 的 `not_ready` 且不污染 out / init 幂等 / 值原样交出 / 总线失败原样上抛 / 无芯片时 `not_found` 且之后 `not_ready` / Context 接上服务后 App 在 `onCreate` 读得到真值 |
| `include/embark/hal/context.h` 之外的三处装配 | `platform/host/host_context.{h,cpp}`（`HostImu imu_` + `imu()` + 槽位第 9 项 + `init()` 里调一次）、`platform/esp32/src/esp32_hal.{h,cpp}`（`Esp32Imu imu_` + `imu()` + 槽位 + `attach_bus(&bus_)` / `init()`，**失败只记日志**）、`tests/fakes/fakes.h`（保持 nullptr，见下） |
| `platform/host/ui_demo.cpp` | `--imu-log`（每 500 ms 一行 `ax/ay/az/gx/gy/gz/T`）、`--imu-shake`；两者**不进** `synthetic_input`，不会偷偷把 `--delay` 改成 40 |
| `cmake/embark_sources.cmake` / `platform/host/CMakeLists.txt` / `tests/CMakeLists.txt` | 源与测试登记各一处 |
| `platform/esp32/src/esp32_board.h` | 新增板载 IMU 段（引脚、INT 不用、地址为何不写死） |

### 与票面的偏差（都是有意的，理由在此）

1. **地址不写死，改成运行期探**。票面写「上板先用 I2C 扫描确认再写死」。仓库里拿不到这块板 SA0 的确切接法，写死就是赌；而探地址本身让「芯片到底挂在哪」变成日志里的一手证据（`0x6A 在线，WHO_AM_I = 0x05`）。确认后把 `kAddressCandidates` 收成一项即可，逻辑不用改。
2. **初始化序列里的「低通」没做**（CTRL5）。两组公开驱动的位图互相矛盾（一说高/低半字节各管一轴，一说 `bit6:5` 加一个使能位），没有硬件可验。低通默认关闭只让读数更噪、不会错；猜一个位图反而可能把量程/数据率写坏。驱动注释里写明这条与补法。
3. **`IImu` 保持无参 `init()`，总线由 `attach_bus()` 注入**。票面的接口形状没提总线从哪来；Esp32Imu 是 Esp32Hal 的成员对象，构造期总线还没 `init`（与 `Esp32Time::init(&bus_)` 同一个理由），所以拆成 `attach_bus(&bus_)` + `init()`，既满足纯虚契约又保住「先起总线、再起设备」的顺序。
4. **`ImuSample` 字段带默认值 `= 0.0F`**。票面没写；加它是为了让「read 失败不许写 out」这条契约能用哨兵值测出来。
5. **错误码按 `error.h` 现有取值分档**：没应答 `not_found`、没 init / 没探到 `not_ready`、总线读失败 `io_failure`。票面只写了 `not_ready`；多出的两档是为了让上层分得清「没有这块硬件」与「这一帧读坏了」。
6. **`FakeImu` 不塞进 `FakeHal::context()`**。塞进去会让 `tests/hal/test_context.cpp` 的「三个服务槽恒 nullptr」断言变味 —— 那条防的是「有人以为加了服务就能随便接线」。要测契约的用例自己持有 `FakeImu`，需要时自己拼 Context（测试里已经这么演了一遍）。
7. **`--imu-log` 用 `SDL_GetTicks()` 打点**，不用 `std::chrono::steady_clock`：这个程序的帧节拍本来就由 SDL 的时钟驱动，混两个时钟会让「每 500 ms」在慢机器上与帧号对不上。

### 实测

- `cmake --build build` → exit 0；`ctest --test-dir build --output-on-failure` → 2/2 通过。
- `embark_tests` → **132 cases / 1393 assertions** 全过（08 之前 126 / 1368）。
- `embark_host_ui --ui --delay 40 --imu-log --frames 60` → 每约 7 帧一行，`az=-1.00`、`T=25.0`、角速度全 0；同一条命令加 `--imu-shake` 后 ax/ay/gx/gz 逐行变化（幅度 ±0.5 g / ±30 dps 以内），az 恒 -1.00。
- 五条既有故事全 EXIT=0：`--ui --frames 40`、`--click --delay 40`、`--ui-nav --delay 40`（前台 clock，switches=3）、`--ui-weather --delay 40`（switches=2）、`--ui-launcher-pages --delay 40`（磁贴 5 / 页 2、亮度 50%→60%、switches=4）；`embark_host_tour --delay 40` 17 项全 `[通过]`。
- 真机侧未验（手上无板）：`WHO_AM_I` 回读、量程换算、转 90° 换轴这三条仍需上板确认 —— 驱动与日志已按「先取证再改」写好。

### 文档同步

`docs/reference/hooks-and-api.md`（Context 段改成三行表：imu 已接上、audio/network 仍空）、`docs/guides/hal-backend-guide.md`（context.h 表行 + 装配段落 + 指向 `tests/fakes/fake_imu.h`）、`docs/adr/0019-service-layer-and-public-hardware.md`（横幅补 08 已落地 IImu）、`platform/esp32/README.md`（bring-up 第 11 条：地址探针、量程/数据率、CTRL5 为何不配、失败不是致命错误、启动日志两种结局；§8 串口样例加两行）、`GLOSSARY.md`（新增 **ImuSample（姿态采样）** 词条 + HAL Context 条目补服务槽落地进度）。
