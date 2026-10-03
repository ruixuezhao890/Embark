# 11 · ESP32-S3 真机后端

Status: pending
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
