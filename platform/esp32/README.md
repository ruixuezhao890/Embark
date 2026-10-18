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
**0xc01c0 字节 = 786880 字节**（自检屏那版；issue 17 把静态子集字库接成默认字体后是
**0xbf8d0 字节 = 784592 字节**；issue 25 加上真机 SD 后端 + IFileSystem + 字库 PSRAM 池之后是
**0xc3580 字节 = 800128 字节**，app 分区富余 75%），bootloader 仍是 0x5210（富余 36%）。
**源清单只有一份**：`project/components/embark/CMakeLists.txt` 现在只 `include` 仓库根的
`cmake/embark_sources.cmake`（issue 20），加文件改那份；它的配置期审计会在路径写错、
归口目录里有文件没登记、或 main.cpp 的裸头名找不到 include 目录时直接报错。`build` 结尾会把 esptool 直写命令打出来（不想用 `idf.py flash`
时照抄即可）。

仓库根 CMake 侧只提供一句提示（不接管真机构建）：

```powershell
cmake -S . -B build -DEMBARK_BUILD_ESP32=ON   # 出现 embark_esp32_hint / embark_esp32_build 目标
```

## 4. 与宿主模拟器的对应关系

| 能力 | 宿主（`platform/host/`） | 真机（`platform/esp32/src/`） |
| --- | --- | --- |
| `IDisplay` | SDL2 窗口 + 软件渲染 | ST7789 + `esp_lcd`：`draw_bitmap` 后**等 DMA 完成信号量**（HAL 的同步语义） |
| `IInput` | SDL 事件 | CST328：10 ms 节流轮询、坐标去重、press/move/release 状态机；**两个边沿都过 30 ms 去抖窗口**（按下要 3 拍里见 2 拍有点、抬起要连续 3 拍全无点），见 §5.9 |
| `ITime` | 墙钟（SDL） | `esp_timer` 单调时基 + **板载 PCF85063 RTC**（`I2C_NUM_1`、地址 `0x51`）：开机读一次锚定 UTC epoch，之后用 tick 推算；RTC 不在 / 振荡器停走（OS 位=1）时 `epoch_ms()` 才 `unsupported` |
| `IPersistence` | exe 同目录二进制文件（32 定长槽） | NVS 命名空间 `embark`（键 ≤15、值 ≤64 字节，写入必 `nvs_commit`） |
| `IFileSystem` | 可执行文件旁的 `embark_host_fs/`（或 `EMBARK_HOST_FS_ROOT`），定长 4 槽 | SD 卡挂载点 `/sdcard`（SPI2 + FATFS）；`EMBARK_SD_ENABLED` 默认 0，没配引脚 / 没插卡 → `Context::filesystem` 是 `nullptr` |
| `ILogSink` | stdout | stdout = UART0 控制台（整行 fwrite + fflush） |
| `ISystem` | 进程重启 + 平台水位 | `heap_caps`（只看内部 SRAM）+ `uxTaskGetStackHighWaterMark` + `esp_task_wdt_reset`，`fatal` → `abort`（走 IDF panic handler 打回溯） |
| `IBus` | 一律 `unsupported`（这台机器没有物理总线） | `I2C_NUM_1` 上的 `i2c_master`（按地址懒建设备、不驱逐）；`spi_transfer` 仍是 `unsupported`（屏幕 SPI 由 `esp_lcd` 独占） |
| LVGL 堆 | `malloc` + 记账（256 KB 软预算） | **`.bss` 静态池 48 KB**（`src/esp32_board.h` 的 `lvgl_pool_bytes`） |
| 字库内存（整份 TTF + 字形缓存） | `malloc`（`font_alloc_scope` 是空操作） | **PSRAM 定容池 192 KB**（`src/esp32_board.h` 的 `font_psram_bytes`，懒建；与那 48 KB 分开记账，ADR 0017） |
| 唯一 UI 任务 | `xTaskCreateStatic` + 宿主自起调度器 | `xTaskCreateStaticPinnedToCore(…, core 1)`，调度器已在跑 |
| 后台 own task | 静态任务（不落核） | 静态任务落 core 0 |
| 断言 / `fatal` | `platform/host/host_system.cpp` 唯一定义 | `src/esp32_system.cpp` 唯一定义 |

## 5. 实机 bring-up 清单（上板前无法验证的项，全在这里）

1. **屏幕方向**：现在**默认屏就是 AiDoGUI 生成的真界面**（`aidogui_ui_bridge`，ADR 0011；
   `src/esp32_board.h` 的 `bringup_self_test` 已置 `false`），方向/颜色可以直接拿模拟器截图比对。
   需要回到"纯色带 + 触摸坐标"的自检屏时把那开关改回 `true`（代码在 `src/esp32_bringup.cpp`：
   四角色标 TL/TR/BL/BR + R/G/B/W 四条色带 + 中央实时触摸坐标）。
   判读：TL 不在左上角（跑出角外 / 色带变成竖条）→ 翻 `lcd_swap_xy`；左右镜像 → 翻 `lcd_mirror_x`；
   上下镜像 → 翻 `lcd_mirror_y`（默认 `false/true/false` = 厂商 ROT_NONE 配方，即 240×320 竖屏，与面板原生方向一致）。
2. **颜色**：红蓝互换（第 1 条色带显蓝、第 3 条显红）→ 翻 `lcd_rgb_element_order_bgr`；画面"发花/像 16 位错位"
   → 第一个怀疑 `lcd_data_little_endian`（厂商初始化 `0xB0=0xE8` 的 bit3 要求小端）；像底片（白带发黑）
   → 翻 `lcd_invert_color`。
3. **触摸方向**：点自检屏四角，每按一下串口打一行 `BRINGUP 触摸按下：x, y`（屏中央同时实时显示坐标）：
   点左上应 ≈(0,0)、点右下应 ≈(239,319)。x/y 互换 → `touch_swap_xy`；左右反 → `touch_mirror_x`；上下反 →
   `touch_mirror_y`。启动日志还会打 CST328 自报的 `RES_X / RES_Y`（例：`CST328 就绪：地址 0x1A，自报分辨率 X=240 / Y=320`），
   先用它确认轴方向（面板竖屏 ⇒ x 是短轴 0..239、y 是长轴 0..319）。
   核对完把 `bringup_self_test` 置回 `false`（现在默认就是 false）；`src/esp32_bringup.{h,cpp}`
   与 `main.cpp` 里的色带自检调用随时可以整个删（aidogui 已接管 App 层，这块只是面板对不对的一次性刻度）。
4. **触摸复位时序**：`touch_reset_low_ms = 20` / `touch_reset_high_ms = 120`（先低后高，厂商口径）。
5. **背光**：`backlight_pwm_hz = 5000` / `backlight_pwm_bits = 13` / `backlight_default_percent = 70`。
6. **FreeRTOS 计时**：`CONFIG_FREERTOS_HZ=1000` 是硬要求（默认 100 Hz 下 `pdMS_TO_TICKS(5) == 0`，
   UI 循环会忙等）。改过 `sdkconfig.defaults` 之后要 `idf.py fullclean` 再构建。
7. **v1 有意不做**（将来要就得先加能力面）：RTC **写时间**（没有可信时间源，也别让固件瞎改芯片）、
   RTC 秒中断、温度补偿校准、BLE、`spi_transfer()`、低功耗与深睡。
   音频与 WiFi 已从这条清单里移出：[ADR 0019](../../docs/adr/0019-service-layer-and-public-hardware.md)
   把它们立项成服务层能力（`IAudio` / `INetwork`）—— 两个都已落地：音频见下面第 12 条（issue 09），
   网络见第 13 条（issue 10）。
8. **SD 卡（issue 25）**：**已开**（`src/esp32_board.h` 的 `EMBARK_SD_ENABLED 1`），信号脚已填：
   CS = IO21 / MOSI = IO17 / MISO = IO16 / CLK = IO14（卡检测脚留 -1）。挂载点 `/sdcard`（SPI2，10 MHz）。
   历史上这里是"默认关、四个信号脚是 -1"——那块板的手册不给卡座接线，得照 2D/3D 图纸或万用表量；
   引脚填完把开关置 1，`esp32_board.h` 末尾的 `static_assert` 会在**编译期**拦下"开了却没填引脚"。
   改了引脚/开关之后照旧重烧即可；`sd_spi_hz = 10 MHz` 是杜邦线也能跑稳的值（要提频是独立决定，见 issue 08）。
   验证用卡上的一份 TTF（ADR 0017 之后字体只有这条路）：把 `app/aidogui_ui/assets/font/ui_heiti.ttf`
   （3.85 MB）拷到卡的 `fonts/` 目录（桥按 `fonts/` → `font/` → `../assets/font/` 的顺序找），
   启动日志应出现这几行：

   ```
   I (1624) embark: SD：已挂载 /sdcard（SD32G 30436 MB，31914983424 字节）
   I (1700) embark: AiDoGUI 字体源：/sdcard/fonts/ui_heiti.ttf（3854132 字节，常驻 PSRAM）
   I (1706) embark: AiDoGUI 字体 14 px 就绪（字形缓存 4096 字节，行高 16，基线 3）
   I (1712) embark: AiDoGUI 字体 7/7 已加载（TTF 运行期栅格化；根 /sdcard）
   I (1712) embark: 字体缓存：已用 41224 字节 / 字库区 196608 字节（配额合计 66176 字节）
   I (1713) embark: [AUTODEBUG_PASS] SD_FONT_OK
   ```

   判读：只有 `SD：本板未配置卡座引脚…跳过挂载` 一行 → 开关或接线；`AiDoGUI 字体源 … 读不全` 的 WARN
   → 卡上路径不对（桥会把三个候选试完，最后全部槽指向内置 14 px）；`AiDoGUI 字体 N px 建不出来` →
   该字号的字形缓存拿不到内存；最后一行没有 `SD_FONT_OK` → 有的字号没建出来（loaded != total）。同一个心跳行里的
   **LVGL 池未回收/峰值看不出变化**是对的 —— 字体占的是 `font_psram_bytes` 那块 PSRAM 池。

   **自检没过时会怎样**：默认只打错误、**继续启动**（`src/esp32_board.h` 的 `sd_font_check_fatal = false`）——
   界面字号退回内置 14 px，设备能用、串口能看到原因；宿主工具那边因为没收到通过令牌，最终判
   TIMEOUT（退出码 4）而不是 PANIC_DETECTED。把这个开关置 `true` 会让失败当场 `abort()`（panic 回溯 +
   `addr2line` 回源码行），那只是专门验收这一项时的一次性手段；常开的话，没插卡 / 卡槽接触不良都会
   变成 panic 重启循环 —— 原因看不见，界面也进不去。

9. **触摸判据与跟手（2026-10-17）**：CST328 在**手指静止**时会报"0 个触点"——原来的 poll 读 0xD005 的点数、
   读到 0 就判松开，于是"按一下"被拆成几十次 press/release：真机 10 秒里按下计数从 56 涨到 259，
   天气屏 0.2 秒内连切了 4 个城市（北京→上海→广州→深圳），主屏左右滑动也被当成点击。
   现在判"还有没有手指"用的是 **0xD000 报文第 0 字节**（`src/esp32_input.cpp` 的 `touch_flag_byte`）：
   真机实测按住期间恒为 `0x06`、抬手当拍就变 `0x00`，连"0xD005 读到 0"的那些拍也一直保持着 ——
   所以抬起不必再等空窗，1 拍就够。`src/esp32_board.h` 的三个常量：

   | 常量 | 值 | 含义 |
   |---|---|---|
   | `touch_poll_period_ms` | 5 | 驱动侧轮询周期（每拍一次 29 字节 I²C 读，约 0.8 ms） |
   | `touch_press_confirm_hits` | 2 | 连续 2 拍"有手指"才承认按下（10 ms） |
   | `touch_release_confirm_hits` | 2 | 连续 2 拍"没手指"才承认抬起（10 ms） |

   配合 `config/lv_conf.h` 的 `LV_INDEV_DEF_READ_PERIOD = 10` / `LV_DISP_DEF_REFR_PERIOD = 20`：
   按下到看见反应 ≈ 10（确认）+ 10（LVGL 取输入）+ 20（刷新）≈ **40 ms**，松手到 UI 收到 release ≈ 10~20 ms
   （改之前按下 ≈65 ms、抬起 ≈75~105 ms）。还没确认的那一两拍被**静默吞掉**（不抬、不重按），
   计入心跳里的"吞掉空拍"。**被证伪的路子：INT 脚不可用** —— IO4 在无上下拉/下拉/上拉三种配置下恒读 1，
   一个边沿都读不到。心跳那一行现在长这样：

   ```
   触摸 按下 3 / 抬起 3 / 滑动 12 / 吞掉空拍 47 / 读失败 0；帧耗时 均值 5123 us / 峰值 41220 us；墙钟 已对时
   ```

   判读：按下与抬起应该**基本相等、且与你手点/滑的次数一致**；"吞掉空拍"通常只有个位数（每次按压 1~2 拍）；
   这两个数再次爆到几十倍 → 手指标志也不是可靠信号了，得回去看 `touch_flag_byte` 的实测样本；
   "读失败"非 0 → I²C 接触/上拉问题。
   "帧耗时均值"才是"卡不卡"的判据 —— LVGL 自己的 FPS 浮层在 30 ms 刷新周期下报出过 123 FPS，明显偏乐观。

10. **板载 RTC PCF85063（2026-10-17）**：走**板载 I²C**（`I2C_NUM_1`，SDA=IO11 / SCL=IO10 / INT=IO9，
   地址 `0x51`，与 IMU 共用这条总线）。驱动在 `src/esp32_rtc.{h,cpp}`，只做三件事：读（0x04..0x0A
    七个 BCD 寄存器）、判 OS 位、换算 UTC epoch 秒 —— **故意不写时间**（没有可信时间源，也不让固件瞎改芯片）。
    `Esp32Time` 开机读一次把墙钟锚在本刻，之后用 `esp_timer` 推算（`epoch_ms()` 返回 **UTC 毫秒**，
    时区由 App 自己加，`local_clock.h` 的 `kLocalOffsetSeconds = 8*3600`）。
    **HAL 初始化顺序因此改成 `log → bus → time → system → …`**：时间后端要借总线，先起总线才能把
    "总线没起来"和"RTC 读不到"分开报（同一块板只该有一个 `I2C_NUM_1` 的主人）。
    启动日志三种结局：

    ```
    I (…) embark: 板载 RTC：0x51 原始 7 字节 = 45 32 09 17 17 10 26
    I (…) embark: 墙钟：RTC 已对时，UTC epoch 1792195200 秒（1792195200000 ms），之后用 tick 推算
    E (…) embark: 板载 RTC：0x51 读不到（…）—— PCF85063 没焊 / SDA IO11 SCL IO10 接反 / 上拉缺失
    W (…) embark: 板载 RTC：时钟停过（OS 位=1，多半是没有纽扣电池）—— 墙钟不启用
    ```

    OS 位=1（芯片停过）时，板级开关 `rtc_seed_from_build_time = true` 会**用固件编译时刻播种一次**并清 OS 位：
    误差 = 编译到上电那几分钟，没有纽扣电池时每次断电都会重播（有电池后播一次就不再写）。
    播种被关掉或失败时 `epoch_ms()` 返回 `unsupported`，时钟屏显示「等待对时 / ----年--月--日」占位符
    （不是驱动坏了 —— 芯片**真的不知道自己几点**）。心跳行末尾的"墙钟 …"就是当前状态。

11. **板载 IMU QMI8658（2026-10-18，issue 08）**：同样挂板载 I²C（`I2C_NUM_1`，SDA=IO11 / SCL=IO10），
    驱动在 `src/esp32_imu.{h,cpp}`，实现 `hal::IImu`（单位 g / dps，见 `include/embark/hal/imu.h`）。
    三条要知道的事：

    - **7 位地址是探出来的**：SA0 脚决定 `0x6A` 还是 `0x6B`，仓库里没有这块板的确切接法，
      所以 `init()` 依次探两个地址、用 `WHO_AM_I`（`0x00`，期望 `0x05`）判据。
      探到哪个会打进日志；确认接法后把 `kAddressCandidates` 收成一项即可。
    - **量程与数据率**：`CTRL2=0x15`（±4 g / 250 Hz）、`CTRL3=0x55`（±512 dps / 250 Hz）、
      `CTRL1=0x40`（读自动递增，**不加这一位 14 字节连读会一直读到同一个寄存器**）、
      `CTRL7=0x03`（开两轴）。换算：`8192 LSB/g`、`64 LSB/dps`、温度 `raw/256`。
      数据从 `0x33` 起 14 字节一次连读，全部小端。改量程必须同步改这两个系数。
    - **`CTRL5`（低通）故意不配**：两组公开驱动的位图互相矛盾（一说是高半字节陀螺、低半字节加速度，
      一说是 `bit6:5` 加一个使能位），而手上没有硬件可验。低通默认关闭只让读数更噪、不会错；
      猜一个位图反而可能把量程/数据率写坏。要加按数据手册 Rev D 的 `CTRL5` 章补。
    - 初始化失败**不是致命错误**（与 SD 卡同款语义）：芯片没焊、地址不对、上拉缺失都只记日志，
      `Context::imu` 照旧指向驱动实例，`read()` 返回 `not_ready` —— App 判空、判错、降级即可。

    启动日志两种结局：

    ```
    I (…) embark: 板载 IMU：0x6A 在线，WHO_AM_I = 0x05
    I (…) embark: 板载 IMU：已就绪（±4 g / ±512 dps / 250 Hz，地址 0x6A）
    W (…) embark: 板载 IMU：0x6A/0x6B 都没应答 —— 芯片没焊 / SA0 接法不同 / SDA IO11 SCL IO10 上拉缺失
    W (…) embark: 板载 IMU：初始化失败（错误码 …），姿态服务不可用
    ```

    验「读数对不对」不用烧固件：宿主那份是同一套接口的合成实现 ——
    `embark_host_ui --ui --delay 40 --imu-log` 每 500 ms 打一行，静止时 `az = -1.00`；
    加 `--imu-shake` 换成周期 2 s / 幅度 0.5 g 的正弦。真机上对应的是把板子平放
    （某一轴 `≈ ±1.0`）再绕轴转 90°（那一轴读数换到重力轴上）。

12. **板载音频 PCM5101（2026-10-18，issue 09）**：I²S 走 `I2S_NUM_0`，STD / Philips / 48 kHz /
    16 bit 立体声；引脚在 `src/esp32_board.h` 的「板载音频」段（`audio_pin_bclk = 48`、
    `audio_pin_lrck = 38`、`audio_pin_dout = 47`，任一项填 -1 就是关音频）。驱动在
    `src/esp32_audio.{h,cpp}`，实现 `hal::IAudio`。四条要知道的事：

    - **没有 MCLK**：板子只接了 BCK/LRCK/DIN 三根线，`I2S_STD_CLK_DEFAULT_CONFIG` 的
      `mclk = I2S_GPIO_UNUSED` 是对的，别去凑 256 fs（没那根线）。
    - **音量是软件衰减，不是硬件音量**：PCM5101 的控制接口是 HW，没有 I2C/SPI 寄存器；
      板上那个旋钮接的是功放级的模拟增益，**MCU 看不到它**。所以 `set_volume()` 只能在采样上
      乘系数 —— 每 6 dB 掉 1 bit 有效精度，`0` 只是采样全零、底噪还在。
    - **只认 PCM WAV**：读头 → 直接把采样喂 I²S，零解码器。MP3/AAC 要引解码器（约 0.5 MB
      代码体积），是一次独立的依赖决策，`play()` 对它们返回 `unsupported`。
    - **喂数据在后台任务里**：512 帧一块（约 10.7 ms），`xTaskCreatePinnedToCore` 落**核 0**
      （UI 落核 1，两边不抢）；保护播放器状态用的是 FreeRTOS 互斥量 —— 真机上
      `std::atomic_flag` 会因为 xtensa 默认库缺 `__atomic_test_and_set` 而链接失败。

    初始化失败**不是致命错误**（与 SD 卡 / IMU 同款语义）：`Context::audio` 照旧指向驱动实例，
    `play()` 返回 `not_ready`。启动日志两种结局：

    ```
    I (…) embark: 音频：I2S 就绪（48000 Hz / 16 bit / 立体声，BCK=48 WS=38 DOUT=47）
    W (…) embark: 音频：i2s_new_channel 失败 —— 音频输出关闭
    W (…) embark: 板载音频：初始化失败（错误码 …），音频服务不可用
    ```

    验「出声与状态机对不对」不用烧固件：`embark_host_ui --ui --delay 40 --audio-test`
    会走一遍 播 → 暂停 → 继续 → 跳转 → 停止 → 音量边界，逐项打 `✔ / ✘`。

13. **板载 WiFi + HTTP（2026-10-18，issue 10）**：`src/esp32_network.{h,cpp}` 实现 `hal::INetwork`，
    底层是 IDF 的 esp_wifi（STA）+ esp_http_client + SNTP。五条要知道的事：

    - **只做明文 HTTP**：`http_get()` 只认 `http://`，其它 scheme（`https://` / `ws://`）返回
      `unsupported`。上 HTTPS 要引 TLS（代码体积与证书校验都是独立决定），另开一票。
      对时走 `sntp_sync()`（pool.ntp.org），它自己用 UDP，不受这条限制。
    - **凭据只存内存，不落 NVS**：`esp_wifi_set_storage(WIFI_STORAGE_RAM)`。要"记住 WiFi"由 App
      自己经 `IPersistence` 存两个槽（SSID 一槽、口令一槽；单条上限 64 字节，见
      `config/embark_limits.h`），服务层不替 App 决定要不要记住。
    - **`connect()` 阻塞到出结果**（拿到 IP / 失败 / 15 s 超时）：成功进 `connected`、失败进
      `failed`。拨号前先 `esp_wifi_disconnect()` 再置状态是必须的 —— 否则自己触发的 DISCONNECTED
      事件会抢先把它翻成 `failed`。
    - **`http_get()` 是阻塞的**：按 256 字节块 `read()` 到 EOF 或缓冲写满；写满返回 `no_space`
      且 `body_len = 0`。HTTP 4xx/5xx **不是** `Error`（返回 `none`，状态码由
      `last_http_status()` 如实报），只有连不上 / 超时 / 协议错才算错误。别在 UI 任务里调它。
    - **初始化失败不是致命错误**（与 SD 卡 / IMU / 音频同款语义）：`Context::network` 照旧指向驱动
      实例，`connect()` 返回 `not_ready`。App 判空、判错、降级即可。

    启动日志两种结局：

    ```
    I (…) embark: 网络：WiFi 就绪（STA 模式；凭据只存内存，不落 NVS）
    I (…) embark: 网络：已连接 MyAP（IP 192.168.1.42）
    W (…) embark: 网络：esp_wifi_init 失败（…）
    W (…) embark: 网络：初始化失败（错误码 …），网络服务不可用
    W (…) embark: 网络：拨号 MyAP 失败或超时（密码错 / AP 不在 / 信号差）
    ```

    验「通不通」不用烧固件：`embark_host_ui --net-test` 会 GET `http://example.com/` 并逐项打
    `✔ / ✘`（状态机、body 缓冲 `no_space`、断连后 `not_ready`）。宿主后端是直通 socket、
    `connect()` 恒成功，所以它验的是**接口契约**，不是真无线；真机要联网得由 App 调
    `connect(ssid, password)`。

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
I (660) embark: 板载 IMU：0x6A 在线，WHO_AM_I = 0x05
I (668) embark: 板载 IMU：已就绪（±4 g / ±512 dps / 250 Hz，地址 0x6A）
I (1620) embark: BRINGUP 触摸按下：12, 9
I (1624) embark: SD：已挂载 /sdcard（SD32G 30436 MB，31914983424 字节）
I (1700) embark: AiDoGUI 字体源：/sdcard/fonts/ui_heiti.ttf（3854132 字节，常驻 PSRAM）
I (1706) embark: AiDoGUI 字体 14 px 就绪（字形缓存 4096 字节，行高 16，基线 3）
I (1712) embark: AiDoGUI 字体 7/7 已加载（TTF 运行期栅格化；根 /sdcard）
I (1712) embark: 字体缓存：已用 41224 字节 / 字库区 196608 字节（配额合计 66176 字节）
I (1713) embark: [AUTODEBUG_PASS] SD_FONT_OK
I (10420) embark: 心跳：2000 帧；堆空余 268435456 字节，UI 栈余 6824 字节；LVGL 池未回收 9978 / 峰值 12480 字节；刷新 214 次，触摸按下 3 次
```

心跳每 2000 帧（≈10 s）一行，上板调试主要靠它：堆、UI 栈水位、LVGL 静态池、刷新次数、触摸次数。
字库那几行只在插了卡（且填了引脚）时出现；默认关 / 没插卡时是 `SD：本板未配置卡座引脚（esp32_board.h 的
EMBARK_SD_ENABLED = 0），跳过挂载` 一行，随后桥读不到 TTF ⇒ 7 个槽全指内置 14 px，串口打
`AiDoGUI 字体：一个字号也没建出来（源 ui_heiti.ttf），7 个槽全指向内置字体`。
致命错误长这样（`abort()` → IDF panic handler 打回溯与寄存器）：

```
[embark] fatal: 显示初始化失败
```

## 9. CI

`.github/workflows/ci.yml` 的 `esp32` job 会真的执行 `set-target esp32s3` + `build`
（不再 `continue-on-error`）：宿主测试与真机固件编译一起守住"同一套源码两个目标"。
