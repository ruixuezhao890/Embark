# ESP32-S3 真机平台（issue 11）

本目录是 Embark 的第一个真机目标：**Waveshare ESP32-S3-Touch-LCD-2.8** 开发板。
HAL 七个能力接口（`include/embark/hal/`）在这里有真实后端，宿主模拟器与它共用同一套内核、
App、LVGL 端口代码（`platform/common/`），差别只在后端实现与任务落核。

## 1. 板子参数

| 项目 | 值 |
| --- | --- |
| 模组 | ESP32-S3R8：16 MB flash（W25Q128JVSI）+ 8 MB 八线（OPI）PSRAM |
| 屏幕 | ST7789，原生 240×320 竖屏 → 框架按 **240×320 竖屏** 使用（不旋转，ROT_NONE），与宿主模拟窗口同向 |
| 触摸 | CST328，I2C 地址 `0x1A`，直连 `i2c_master`（不走 `esp_lcd_touch`） |
| LCD SPI | `SPI3_HOST`，80 MHz，MOSI 45 / SCLK 40 / CS 42 / DC 41 / RST 39 |
| 背光 | LEDC 13 位 / 5 kHz / GPIO 5（duty = `percent × 8191 / 100`） |
| 触摸 I2C | 控制器 `I2C_NUM_0`，SDA 1 / SCL 3 / RST 2 / INT 4，400 kHz |
| 板载 I2C | 控制器 `I2C_NUM_1`，SDA 11 / SCL 10，400 kHz（IMU、RTC 与 `hal::IBus` 共用） |
| 任务落核 | UI 任务 → core 1；后台 own task → core 0 |

引脚与开关**只在一处**定义：`src/esp32_board.h`（纯常量表，不依赖 IDF 头）。
实机方向/颜色不对时，改那张表里的开关即可，不用翻驱动代码。

## 2. 目录结构

```
platform/esp32/
├── CMakeLists.txt          宿主侧门面：只在 EMBARK_BUILD_ESP32=ON 时被顶层 CMake 收进来，
│                           真机构建由 IDF 自己驱动（它只提供一句"怎么构建"的提示）
├── project/                ESP-IDF 工程根（idf.py -C 指到这里）
│   ├── CMakeLists.txt      先建 middleware 视图，再 include IDF 的 project.cmake
│   ├── sdkconfig.defaults  只放"与这块板子/本框架有关"的项（纯 ASCII，见 §6）
│   ├── partitions.csv      NVS 24 KB + phy_init + 3 MB factory（纯 ASCII）
│   ├── main/               入口：HAL 起来 → 交棒给唯一 UI 任务，之后只做心跳观测
│   └── components/
│       ├── lvgl/           上游 LVGL 源码（GLOB_RECURSE）+ 仓库的 config/lv_conf.h
│       └── embark/         内核 + App + platform/common + platform/esp32/src
└── src/                    真机后端：HAL 实现 + UI 任务 + 任务工厂 + LVGL 内存钩子
```

## 3. 构建与烧录

前置：**ESP-IDF v5.4**（`idf.py --version` 核对）。本机 checkout 在 `E:\00_Software\03_Dev_Env\IDF`，
工具链在 `E:\00_Software\03_Dev_Env\Espressif\tools`。

Windows（PowerShell）激活环境。**本机不要用 `activate.py`**：这台机器的 `.py` 关联到了
`node.exe`，`& "$env:IDF_PATH\tools\activate.py" --export` 会被 Node 拿去解析 Python 源码，
报 `SyntaxError: Invalid or unexpected token ... at wrapSafe (node:internal/modules/cjs/loader)`，
环境其实**没激活**（`IDF_PYTHON_ENV_PATH is missing`），随后 CMake 会挑到 `PATH` 里 Rust 的
esp 工具链并报 `Tool doesn't match supported version from list ['esp-14.2.0_20241119']`。
一律显式用 IDF 的 venv python 跑脚本（`& $py 脚本.py`，永远别写 `& 某个.py`）：

```powershell
$env:IDF_PATH = 'E:\00_Software\03_Dev_Env\IDF'
$env:IDF_TOOLS_PATH = 'E:\00_Software\03_Dev_Env\Espressif\tools'
$env:IDF_PYTHON_ENV_PATH = 'E:\00_Software\03_Dev_Env\Espressif\tools\python_env\idf5.4_py3.11_env'
$py = "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe"
$kv = & $py "$env:IDF_PATH\tools\idf_tools.py" export --format key-value
$paths = @()
foreach ($line in $kv) {
  if ($line -match '^PATH=(.+)$') { $paths = @($Matches[1]) + $paths }
  elseif ($line -match '^([A-Za-z_][A-Za-z0-9_]*)=(.+)$') { Set-Item -Path ("env:" + $Matches[1]) -Value $Matches[2] }
}
$env:PATH = (($paths | Select-Object -Unique) -join ';') + ';' + $env:PATH
& $py "$env:IDF_PATH\tools\idf.py" -C platform/esp32/project -B build-esp32 build
```

`idf_tools.py` 会把 `PATH` 里已有的 Rust esp 工具链（`esp-14.2.0_20240906`）、`cmake 4.0.0`、
`ninja 1.13.2` 判成「不支持的系统版本」并跳过 —— 那是无害警告，它自己会用
`E:\00_Software\03_Dev_Env\Espressif\tools\tools\xtensa-esp-elf\esp-14.2.0_20241119`。

构建（构建目录固定用 `build-esp32/`，已被 `.gitignore` 的 `build-*/` 覆盖）：

```powershell
idf.py -C platform/esp32/project -B build-esp32 set-target esp32s3   # 只需一次
idf.py -C platform/esp32/project -B build-esp32 build
idf.py -C platform/esp32/project -B build-esp32 -p COM5 flash monitor
```

本机（2026-10-04，IDF 5.4）已实测跑通：`set-target` + 全量 `build` 通过，产物
`build-esp32/embark_esp32.bin` **0x89880 字节**（app 分区 3 MB，富余 82%），bootloader
0x5210 字节（富余 36%）。App 目录化 + 屏内返回改动之后（2026-10-16）产物是
**0xbf4e0 字节 = 783584 字节**（app 分区富余 75%），bootloader 仍是 0x5210（富余 36%）；
真机侧的源列表在 `project/components/embark/CMakeLists.txt`，加文件务必同步那一份。`build` 结尾会把 esptool 直写命令打出来（不想用 `idf.py flash`
时照抄即可）。

仓库根 CMake 侧只提供一句提示（不接管真机构建）：

```powershell
cmake -S . -B build -DEMBARK_BUILD_ESP32=ON   # 出现 embark_esp32_hint / embark_esp32_build 目标
```

## 4. 与宿主模拟器的对应关系

| 能力 | 宿主（`platform/host/`） | 真机（`platform/esp32/src/`） |
| --- | --- | --- |
| `IDisplay` | SDL2 窗口 + 软件渲染 | ST7789 + `esp_lcd`：`draw_bitmap` 后**等 DMA 完成信号量**（HAL 的同步语义） |
| `IInput` | SDL 事件 | CST328：10 ms 节流轮询、坐标去重、press/move/release 状态机 |
| `ITime` | 墙钟（SDL） | `esp_timer`（`epoch_ms()` → `unsupported`：板载 RTC 未接） |
| `IPersistence` | exe 同目录二进制文件（32 定长槽） | NVS 命名空间 `embark`（键 ≤15、值 ≤64 字节，写入必 `nvs_commit`） |
| `ILogSink` | stdout | stdout = UART0 控制台（整行 fwrite + fflush） |
| `ISystem` | 进程重启 + 平台水位 | `heap_caps`（只看内部 SRAM）+ `uxTaskGetStackHighWaterMark` + `esp_task_wdt_reset`，`fatal` → `abort`（走 IDF panic handler 打回溯） |
| `IBus` | 一律 `unsupported`（这台机器没有物理总线） | `I2C_NUM_1` 上的 `i2c_master`（按地址懒建设备、不驱逐）；`spi_transfer` 仍是 `unsupported`（屏幕 SPI 由 `esp_lcd` 独占） |
| LVGL 堆 | `malloc` + 记账（256 KB 软预算） | **`.bss` 静态池 48 KB**（`src/esp32_board.h` 的 `lvgl_pool_bytes`） |
| 唯一 UI 任务 | `xTaskCreateStatic` + 宿主自起调度器 | `xTaskCreateStaticPinnedToCore(…, core 1)`，调度器已在跑 |
| 后台 own task | 静态任务（不落核） | 静态任务落 core 0 |
| 断言 / `fatal` | `platform/host/host_system.cpp` 唯一定义 | `src/esp32_system.cpp` 唯一定义 |

## 5. 实机 bring-up 清单（上板前无法验证的项，全在这里）

1. **屏幕方向**：烧上去第一屏就是 bring-up 自检屏（`src/esp32_board.h` 的 `bringup_self_test = true`，
   代码在 `src/esp32_bringup.cpp`）：四角色标 TL/TR/BL/BR + R/G/B/W 四条纯色带 + 中央实时触摸坐标。
   为什么需要它：真机**还没有 UI 后端**（`app/common/ui_backend.h` 的 `#else` 分支全是 no-op，
   `kUiBackendName = "none"`），默认屏只是一片主题底 —— 方向/颜色没有任何可比对的画面。
   判读：TL 不在左上角（跑出角外 / 色带变成竖条）→ 翻 `lcd_swap_xy`；左右镜像 → 翻 `lcd_mirror_x`；
   上下镜像 → 翻 `lcd_mirror_y`（默认 `false/true/false` = 厂商 ROT_NONE 配方，即 240×320 竖屏，与面板原生方向一致）。
2. **颜色**：红蓝互换（第 1 条色带显蓝、第 3 条显红）→ 翻 `lcd_rgb_element_order_bgr`；画面"发花/像 16 位错位"
   → 第一个怀疑 `lcd_data_little_endian`（厂商初始化 `0xB0=0xE8` 的 bit3 要求小端）；像底片（白带发黑）
   → 翻 `lcd_invert_color`。
3. **触摸方向**：点自检屏四角，每按一下串口打一行 `BRINGUP 触摸按下：x, y`（屏中央同时实时显示坐标）：
   点左上应 ≈(0,0)、点右下应 ≈(239,319)。x/y 互换 → `touch_swap_xy`；左右反 → `touch_mirror_x`；上下反 →
   `touch_mirror_y`。启动日志还会打 CST328 自报的 `RES_X / RES_Y`（例：`CST328 就绪：地址 0x1A，自报分辨率 X=240 / Y=320`），
   先用它确认轴方向（面板竖屏 ⇒ x 是短轴 0..239、y 是长轴 0..319）。
   **核对完把 `bringup_self_test` 置 false**；接上 aidogui/EEZ UI 后端后 `src/esp32_bringup.{h,cpp}`
   与 `main.cpp` 里的两处 `if constexpr` 调用可以整个删。
4. **触摸复位时序**：`touch_reset_low_ms = 20` / `touch_reset_high_ms = 120`（先低后高，厂商口径）。
5. **背光**：`backlight_pwm_hz = 5000` / `backlight_pwm_bits = 13` / `backlight_default_percent = 70`。
6. **FreeRTOS 计时**：`CONFIG_FREERTOS_HZ=1000` 是硬要求（默认 100 Hz 下 `pdMS_TO_TICKS(5) == 0`，
   UI 循环会忙等）。改过 `sdkconfig.defaults` 之后要 `idf.py fullclean` 再构建。
7. **v1 有意不做**（将来要就得先加 HAL 面）：`epoch_ms()`（PCF85063 RTC）、SD 卡/音频、
   WiFi/BLE、`spi_transfer()`、低功耗与深睡。

## 6. 两条构建纪律

1. **`sdkconfig.defaults` 与 `partitions.csv` 必须保持纯 ASCII**：IDF 的 `kconfgen` / `gen_esp32part`
   按"宿主本地编码"读这两个文件（中文 Windows 上是 GBK），文件里有中文注释会直接
   `UnicodeDecodeError: 'gbk' codec can't decode byte …` 让 CMake 配置失败。中文说明写在本文件里。
2. **`sdkconfig` / `sdkconfig.old` / `dependencies.lock` / `managed_components/` 不进仓库**（已 `.gitignore`）：
   它们是本机 `idf.py` 生成物；默认值那份 `sdkconfig.defaults` 才要提交。

## 7. 零堆纪律在真机上的落点

- UI 任务栈、后台任务栈、显示刷新完成信号量、ETL 的 `etl::mutex` **全部走静态 API**
  （`CONFIG_FREERTOS_SUPPORT_STATIC_ALLOCATION=y`，`heap_4.c` 在 IDF 里不参与我们的路径）。
- LVGL 的每一次分配都落进 `.bss` 静态池：`config/lv_conf.h` 的 `LV_MEM_CUSTOM=1` →
  `config/embark_lvgl_hooks.h` → `src/esp32_lvgl_mem.cpp`（池容量就是预算；用尽时报一次 ERROR，
  随后由开着 `LV_USE_ASSERT_MALLOC` 的 LVGL 断言指出**分配点**的 `文件:行号`）。
- 内核与 App 仍按 `-fno-exceptions -fno-rtti` 编译（与宿主同一套 `src/embark/` 源）。

## 8. 串口能看到什么

```
I (312) embark: Embark 0.1.0 启动（平台 esp32）：显示 240×320，NVS 容量 2048 字节；UI 任务栈 2048 字，周期 5 ms
I (418) embark: CST328 就绪：地址 0x1A，自报分辨率 X=240 / Y=320（屏幕 240x320；方向不对先看这几个数）
I (512) embark: ST7789 就绪：240×320 竖屏，SPI 80 MHz，BGR=1，像素小端=1，反色=1
I (640) embark: 框架就绪：3 个 App，默认前台 launcher；LVGL 8.3.11，绘制缓冲 40 行，LVGL 静态池 49152 字节
I (648) embark: BRINGUP 自检屏就绪：四角 TL/TR/BL/BR，四条色带 R/G/B/W；点四角核对坐标（左上应 ≈0,0，右下应 ≈239, 319）
I (1620) embark: BRINGUP 触摸按下：12, 9
I (10420) embark: 心跳：2000 帧；堆空余 268435456 字节，UI 栈余 6824 字节；LVGL 池未回收 9978 / 峰值 12480 字节；刷新 214 次，触摸按下 3 次
```

心跳每 2000 帧（≈10 s）一行，上板调试主要靠它：堆、UI 栈水位、LVGL 静态池、刷新次数、触摸次数。
致命错误长这样（`abort()` → IDF panic handler 打回溯与寄存器）：

```
[embark] fatal: 显示初始化失败
```

## 9. CI

`.github/workflows/ci.yml` 的 `esp32` job 会真的执行 `set-target esp32s3` + `build`
（不再 `continue-on-error`）：宿主测试与真机固件编译一起守住"同一套源码两个目标"。
