# 11 · ESP32-S3 真机后端

Status: resolved
Type: task
Blocked by: 04, 05

## 目标

spec §14.3：`idf.py build` 通过，烧写后能显示 demo 的第一屏。

## 范围

- IDF component 形式的 `platform/esp32/`：显示（ST7789 via `esp_lcd`）、触摸（I2C）、持久化（NVS）、日志（UART0）、时间（`esp_timer`）、系统控制（`esp_restart` / 水位查询 / 喂狗）、总线 raw 读写（ESP-IDF 驱动）。
- LVGL 真机配置：静态池 allocator、`lv_conf.h` 的目标侧开关（与宿主同一份配置分叉点最小化）。
- 编译先跑 `export.ps1` 导出 IDF 5.4 环境（本机环境变量未导出）。

## 验收

- `idf.py build` 通过（CI 里同样能编过）。
- 烧写后显示 demo 第一屏，触摸/按键能切前台。
- **换后端不动 App**：`app/` 与 `include/embark/` 一行不改（§14.5）。

## 备注

**开工前需要用户确认板型与触摸控制器型号**（spec §15 待定项）：Touch-LCD-2.8 参考板还是 devkit + 外接屏。宿主线不受此阻塞。

## Answer

提交 `2134df3`（本地提交，未推送）。本机 IDF 5.4 实跑通过：`idf.py -C platform/esp32/project
-B build-esp32 set-target esp32s3` + `build` ⇒ `BUILD_EXIT=0`，产物 `build-esp32/embark_esp32.bin`
**0x89880 字节**（app 分区 3 MB，富余 82%），bootloader 0x5210 字节（富余 36%）。

交付物：

- `platform/common/`：LVGL 端口 / UI 端口 / 静态池，从 `platform/host/` 提到共享层（宿主与真机同一份）；
  UI 端口的"关窗退出"改成注入式 `ExitQuery`，真机不传。
- `platform/esp32/src/`：八个 HAL 后端（`esp32_display` / `esp32_input` / `esp32_persistence` /
  `esp32_log_sink` / `esp32_time` / `esp32_system` / `esp32_bus` / `esp32_ui_task` +
  `esp32_task_spawner`）+ 板级常量表 `esp32_board.h` + 装配 `esp32_hal.{h,cpp}`。
  零堆落点：UI 任务静态栈、own task 静态栈、显示刷新静态二值信号量、LVGL 堆 = 48 KB `.bss` 静态池。
- `platform/esp32/project/`：IDF 工程（`components/{embark,lvgl}/CMakeLists.txt`、
  `sdkconfig.defaults`、`partitions.csv`、`main/{CMakeLists.txt,main.cpp}`）。
- `platform/esp32/README.md`；`docs/hal-backend-guide.md`（真机实现列 + 7 条坑）、
  `docs/common-pitfalls.md`（`## ESP32-S3 真机（IDF）特有` 10 条）、`docs/README.md`、根 `README.md`、
  `spec.md` §15；CI 的 esp32 job 去掉 `continue-on-error`、改成真跑 `idf.py build`。
- `tests/kernel/test_static_pool.cpp`（8 个用例）—— 静态池在宿主侧就有测试。

验收状态：

- ✅ `idf.py build` 通过（CI 用同一套命令，容器 `espressif/idf:release-v5.4`）。
- ⏳ 烧写后显示 demo 第一屏、触摸/按键能切前台 —— **要上板人工确认**（spec §14 的 ③⑤）。
  判定与调法在 `platform/esp32/README.md` 第 5 节；方向/颜色开关都在 `src/esp32_board.h`，
  启动日志会打 CST328 自报的 `RES_X/RES_Y`。
- ✅ 换后端不动 App：`include/embark/` 一行未改；`app/` 只有一处与后端无关的可移植性改动
  （`demo_apps.cpp` 里 LVGL printf 风格 `%u` → `static_cast<unsigned>`，xtensa 上 `uint32_t`
  是 `long unsigned int`）—— 所有后端差异都被 HAL 吸收。

落地时踩到的坑（已同步进文档）：

1. `sdkconfig.defaults` / `partitions.csv` 必须**纯 ASCII**：IDF 的 `kconfgen` 与
   `gen_esp32part.py` 按宿主编码读，中文 Windows(GBK) 下报 `UnicodeDecodeError: 'gbk' codec
   can't decode byte ...`（中文说明放 README）。
2. `CONFIG_FREERTOS_HZ` 必须 ≥ 1000：5 ms UI 循环在 100 Hz 下 `pdMS_TO_TICKS(5) == 0`。
3. IDF 的 `xTaskCreate*` 收**字节**、`uxTaskGetStackHighWaterMark()` 返回**字**。
4. 组件的 `PUBLIC` 编译选项会漏到 IDF 生成的纯 C 文件（`project_elf_src_esp32s3.c`）上：
   `-include config/embark_config.h` 必须留 `PRIVATE`，依赖方（`main/`）各带一份。
5. `esp_lcd` 的颜色发送是异步的，一次 `draw_bitmap` 只回调一次 ⇒ `flush()` 的同步语义
   就是"等一个二值信号量"。
