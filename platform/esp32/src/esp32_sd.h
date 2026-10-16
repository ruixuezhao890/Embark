/**
 * 真机 · SD 卡挂载（issue 25）
 *
 * 只做一件事：把卡挂到 ESP-IDF 的 VFS 上（esp32_board.h 的 sd_mount_point），文件语义
 * 交给 esp32_filesystem.cpp 的 hal::IFileSystem 后端。挂载本身与 LVGL 无关 —— LVGL 侧只看
 * IFileSystem（platform/common/font_service.cpp 的 'F' 盘）。
 *
 * 引脚在 esp32_board.h 的 SD 一节，默认关（EMBARK_SD_ENABLED = 0）：这块板子有卡座，但
 * 官方页面不给卡座接线，填不出引脚。"没配置 / 没插卡 / 挂载失败"一律 not_ready ——
 * 这不是致命错误，没有卡的机器照样要能跑（issue 25 验收①）。
 */
#ifndef EMBARK_PLATFORM_ESP32_SD_H
#define EMBARK_PLATFORM_ESP32_SD_H

#include <cstddef>

#include <embark/error.h>

#include "esp32_board.h"

namespace embark::platform::esp32 {

class SdCard {
 public:
  SdCard() noexcept = default;

  SdCard(const SdCard&) = delete;
  SdCard& operator=(const SdCard&) = delete;

  ~SdCard() noexcept;

  /// 挂载（SPI 模式）；幂等。未配置 / 没插卡 / 挂载失败 → not_ready。
  [[nodiscard]] Error mount() noexcept;

  /// 卸载并释放 SPI 总线；没挂载时是空操作（析构也会调它）。
  void unmount() noexcept;

  [[nodiscard]] bool mounted() const noexcept { return mounted_; }

  [[nodiscard]] const char* mount_point() const noexcept { return sd_mount_point; }

  /// 卡信息一行（型号 + 容量）：挂载成功后才有内容，没卡时是空串。
  [[nodiscard]] const char* info() const noexcept { return info_; }

  [[nodiscard]] std::size_t capacity_bytes() const noexcept { return capacity_bytes_; }

 private:
  bool mounted_ = false;
  int spi_host_ = 0;      // 挂载成功时占用的 SPI 主机（卸载时要 spi_bus_free）
  void* card_ = nullptr;  // sdmmc_card_t*（IDF 类型不进这个头文件）
  char info_[64] = {};
  std::size_t capacity_bytes_ = 0;
};

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_SD_H */
