# 25 · 真机 SD 后端：IFileSystem 的 esp32 实现 + 字库缓冲走 PSRAM 显式分配

Status: open
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