# 25 · 真机 SD 后端：IFileSystem 的 esp32 实现 + 字库缓冲走 PSRAM 显式分配

Status: resolved
Type: feature
Blocked by: 17
来源: issue 17 落地时按契约推迟的部分（ADR 0007「接口先行，SD 后置」）

## 目标

把 issue 17 已经在宿主上跑通的 `hal::IFileSystem` 接到真机：SD 卡上的字库文件经 `lv_font_load`
进 PSRAM 显式分配，记账仍走 `include/embark/font_ledger.h`（字节预算 100 KB + 引用计数，语义不变）。

## 前置门槛（缺一不可，先做完再动代码）

1. **HAL 面**：`platform/esp32/src/esp32_bus.{h,cpp}` 现在明写 `spi_transfer` 如实返回 `unsupported`
   （"等真要挂 SD 卡之类的外设时"再加）。SD 走 SPI 模式要先补这条能力；走 SDMMC 则要另加面。
   按 ADR 0002，HAL 只抽象到芯片能力粒度，别把 SD 协议塞进 IBus。
2. **板子接线确认**：SD 的 CS / MISO / MOSI / CLK（或 SDMMC 的 D0-D3 / CMD / CLK）与卡检测脚，
   要用户从板子手册/原理图确认，再写进 `platform/esp32/src/esp32_board.h`（"换板子只改这一个文件"）。
   现有板级开关表在 `esp32_board.h` 里成节组织，SD 那节照同一形状加。

## 设计草案

- 后端 `platform/esp32/src/esp32_filesystem.{h,cpp}` 实现 `hal::IFileSystem`（`open/read/seek/close/size`，
  句柄语义与宿主后端一致：`FileHandle` 0 = 无效，`not_found` / `invalid_argument` / `not_ready` / `no_space` 复用现有错误码）。
  与宿主后端同形：定长槽（`config/embark_limits.h` 的 `filesystem_max_open_files`）+ 拒绝绝对路径 / `..` / 含 `:`。
  未插卡时 `init()` 返回 `not_ready`，`Context::filesystem` 保持 `nullptr` 或给一个恒 `not_ready` 的实例 —— 决策点写在这里。
- 字库服务：`platform/common/font_service.cpp` 目前是 LVGL 盘符回调 + 记账，真机复用同一份；差别只在
  **缓冲分配**：真机要 `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)` 与 PSRAM 显式记账（LVGL 的 `lv_font_load`
  会自己分配，需确认能否挂钩；挂不上就在 `font_service` 里自己读文件、用 PSRAM 缓冲，必要时给 LVGL 喂 buffer）。
- 挂载：`esp_vfs_fat_sdspi_mount` / `esp_vfs_fat_sdmmc_mount` 与 LVGL 无关（我们的盘符回调自己走 FATFS 的 POSIX 接口）。
- 板级开关：`esp32_board.h` 加一节 SD（引脚、SPI 主机、频率、是否用 SDMMC 模式、挂载点），默认关。

## 验收

- 无卡启动：`init()` 报 `not_ready`，其余功能不受影响（日志一行说清"未插卡"）。
- 有卡：读一个文件逐字节比对；`seek` 中段/文件尾；`close` 幂等；打开超出槽位数报 `no_space`。
- 字库：从 SD 加载一份 `lv_font_conv` 生成的 bin，屏上渲染出字形，`font_ledger` 账目与非 PSRAM 基线一致；
  释放后 `used_bytes()` 归零，PSRAM 余量回到加载前。
- 记账独立性：LVGL 静态池（256 KB）不受字库影响（加载前后 `lvgl_peak_bytes` 不变）。

## 备注

- 本条不阻塞任何现有功能：App 层 v1 不用运行时字库（界面文案全在静态子集 `embark_zh_14` 里）。
- `config/lv_conf.h` 的 LVGL 自带 FS 驱动开关仍可保持关闭（`font_service` 自持盘符驱动）。
## 落地（2026-10-17）

- **SD（只做 SPI 模式）**：new `platform/esp32/src/esp32_sd.{h,cpp}` —— `sdspi` + FATFS
  （`spi_bus_initialize` → `esp_vfs_fat_sdspi_mount`），SPI2 主机（SPI3 归屏幕），挂载点 `/sdcard`，
  `format_if_mount_failed = false`。板级开关 `EMBARK_SD_ENABLED` 默认 0、四个信号脚 `-1`，文件末
  `static_assert` 挡"开了却没填引脚"；驱动代码无条件编译，运行时 `if (!sd_enabled) return not_ready;`。
  不做 SDMMC（这块板的卡座接线查不到，SPI 对引脚无要求）。
- **真机 IFileSystem**：new `platform/esp32/src/esp32_filesystem.{h,cpp}`（与宿主后端同形：定长槽、
  拒绝绝对路径 / `..` / 含 `:`、`close` 幂等）。装配在 `esp32_hal.cpp`：**挂载成功才**
  `context_.filesystem = &filesystem_`，否则保持 `nullptr`（不是我原来写的"恒 `not_ready` 的实例"——
  可空语义更贴"这台机器有没有盘"，与 display/input 同形）；挂载失败只记日志、**不算故障**。
- **字库走 PSRAM**：new `platform/common/font_alloc_scope.h`（`begin/end` + 池观测）+ 真机侧
  `platform/esp32/src/esp32_lvgl_mem.cpp` 的 PSRAM 定容池（`heap_caps_malloc(MALLOC_CAP_SPIRAM,`
  `font_psram_bytes)` = 128 KB，懒建；归还按指针归属判断）+ `font_service.cpp` 四处 load/free 包作用域；
  宿主侧空实现。**LVGL 静态池不受影响**（验收第 4 条）。
- **`font_service.cpp` 升级为两侧都编**：`cmake/embark_sources.cmake` 把它从
  `EMBARK_COMMON_UI_HOST_ONLY_SOURCES` 挪进 `EMBARK_COMMON_UI_SOURCES`（前者现在为空）。
- **上板自检**：`esp32_bringup.cpp` 的 `check_sd_font()`（`main.cpp` 在自检屏之后调用）——有卡 → 加载
  `fonts/ui_font_ui_heiti_14.bin`、`lv_txt_get_size` 量一次、同步刷一帧再删 label、打记账 / 引用 / PSRAM
  池读数、释放并核对归零；没卡 → 一行日志跳过。
- **HAL 面偏离**：门槛原文要求先补 `IBus::spi_transfer`，实现里**没走 IBus** —— SD 协议栈按 ADR 0002
  的粒度归 IFileSystem 这条新面，`esp32_bus.cpp` 的 `spi_transfer` 仍 `unsupported`。板子引脚仍待填。
- **验收对照**：①（无卡启动不受影响）由 `check_sd_font()` 的早退路径 + 真机日志保证；②（读写 / seek /
  close 幂等 / 槽位）与真机后端同形，宿主侧由 `tests/host/test_host_filesystem.cpp` 6 个 case 覆盖；
  ④（记账独立性）由 `tests/host/test_font_service.cpp` + 真机自检日志覆盖；③**的真机部分需要插卡上板**
  （用户做——本机无串口设备）。
- **证据**：真机 `idf.py build` exit 0（`build-esp32/embark_esp32.bin` = 800128 B / 0xc3580，app 分区富余 75%；
  map 里 `esp32_sd.cpp` / `esp32_filesystem.cpp` / `font_service.cpp` / `font_ledger.cpp` / `font_alloc_scope`
  都有归属）；宿主 `ctest` 2/2；四条界面验收（`--ui` / `--ui-nav` / `--ui-weather` / `--ui-launcher-pages`）全 exit 0。

## Comments

- 2026-10-17：落地完成，`Status: open → resolved`。两条前置门槛的处理："HAL 面"以 IFileSystem 这条
  新面满足（偏离说明见上）；"板子接线确认"仍待用户 —— 默认关，填法与判读记在 `platform/esp32/README.md`
  §5 第 8 条。顺带记一条构建坑：IDF 收 REQUIRES 时用 `cmake -P` 跑各组件 CMakeLists，脚本模式里
  `file(GLOB ... CONFIGURE_DEPENDS)` 非法 —— 见 `cmake/embark_sources.cmake` 的 `CMAKE_SCRIPT_MODE_FILE` 早退。