/**
 * SD 卡挂载实现：见 esp32_sd.h。
 *
 * 用 IDF 的 sdspi 驱动（不是 SDMMC 主机）：卡座信号线还没查清，而 SPI 对引脚没有额外
 * 要求，任意四个 GPIO 都能接；SDMMC 要求信号走在固定的 IO_MUX 引脚上。fatfs 之上那一层
 * （esp_vfs_fat_sdspi_mount / esp_vfs_fat_sdmmc_mount）两者共用。
 *
 * 板子默认 EMBARK_SD_ENABLED = 0：这段驱动代码照样编译（不随开关烂掉），运行时第一行
 * 判断直接返回 not_ready。
 */
#include "esp32_sd.h"

#include <cstdio>

#include <driver/gpio.h>
#include <driver/sdmmc_host.h>
#include <driver/sdspi_host.h>
#include <driver/spi_master.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

#include <middleware/elog/elog.hpp>

namespace embark::platform::esp32 {
namespace {

/// 打开的文件数上限：与 filesystem_max_open_files 同量级（那才是真正同时在用的数量）。
constexpr int mount_max_files = 5;

}  // namespace

SdCard::~SdCard() noexcept {
  unmount();
}

Error SdCard::mount() noexcept {
  if (mounted_) {
    return Error::none;
  }
  if (!sd_enabled) {
    ELOG_INFO("SD：本板未配置卡座引脚（esp32_board.h 的 EMBARK_SD_ENABLED = 0），跳过挂载");
    return Error::not_ready;
  }

  // 卡检测脚：只在日志里报一行电平。极性随卡座而定，确认之前不拿它做判断 ——
  // 挂载失败同样能得出"没插卡"的结论。
  if (sd_pin_detect >= 0) {
    gpio_config_t detect = {};
    detect.pin_bit_mask = 1ULL << static_cast<unsigned>(sd_pin_detect);
    detect.mode = GPIO_MODE_INPUT;
    detect.pull_up_en = GPIO_PULLUP_ENABLE;
    static_cast<void>(gpio_config(&detect));
    ELOG_INFO("SD：卡检测脚 IO{} 电平 = {}", sd_pin_detect,
              static_cast<int>(gpio_get_level(static_cast<gpio_num_t>(sd_pin_detect))));
  }

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = sd_spi_host;
  host.max_freq_khz = static_cast<int>(sd_spi_hz / 1000U);

  const spi_host_device_t host_id = static_cast<spi_host_device_t>(sd_spi_host);
  spi_bus_config_t bus = {};
  bus.mosi_io_num = sd_pin_mosi;
  bus.miso_io_num = sd_pin_miso;
  bus.sclk_io_num = sd_pin_clk;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 4000;
  esp_err_t err = spi_bus_initialize(host_id, &bus, SDSPI_DEFAULT_DMA);
  if (err != ESP_OK) {
    ELOG_ERROR("SD：SPI{} 总线初始化失败（{}）—— 引脚被别的东西占了？", sd_spi_host,
               esp_err_to_name(err));
    return Error::io_failure;
  }

  sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot.host_id = host_id;
  slot.gpio_cs = static_cast<gpio_num_t>(sd_pin_cs);
  slot.gpio_cd = SDSPI_SLOT_NO_CD;  // 卡检测不交给驱动（极性未确认，见 esp32_board.h）
  slot.gpio_wp = SDSPI_SLOT_NO_WP;

  esp_vfs_fat_mount_config_t mount_config = {};
  mount_config.format_if_mount_failed = false;  // 绝不格式化别人的卡
  mount_config.max_files = mount_max_files;
  mount_config.allocation_unit_size = 0;  // 0 = 驱动自选（FATFS 的分配已偏好 PSRAM）

  sdmmc_card_t* card = nullptr;
  err = esp_vfs_fat_sdspi_mount(sd_mount_point, &host, &slot, &mount_config, &card);
  if (err != ESP_OK) {
    static_cast<void>(spi_bus_free(host_id));
    ELOG_WARN("SD：{} 挂载失败（{}）—— 没插卡 / 卡没格式化 / 引脚不对？", sd_mount_point,
              esp_err_to_name(err));
    return Error::not_ready;
  }

  card_ = card;
  mounted_ = true;
  spi_host_ = sd_spi_host;
  capacity_bytes_ = static_cast<std::size_t>(card->csd.capacity) *
                    static_cast<std::size_t>(card->csd.sector_size);
  // cid.name 是 8 字节、不保证带结束符：按精度 8 打印，别越界读。
  std::snprintf(info_, sizeof(info_), "%.8s %u MB", card->cid.name,
                static_cast<unsigned>(capacity_bytes_ >> 20U));
  ELOG_INFO("SD：已挂载 {}（{}，{} 字节）", sd_mount_point, info_,
            static_cast<unsigned>(capacity_bytes_));
  return Error::none;
}

void SdCard::unmount() noexcept {
  if (!mounted_) {
    return;
  }
  static_cast<void>(esp_vfs_fat_sdcard_unmount(sd_mount_point, static_cast<sdmmc_card_t*>(card_)));
  static_cast<void>(spi_bus_free(static_cast<spi_host_device_t>(spi_host_)));
  mounted_ = false;
  card_ = nullptr;
  capacity_bytes_ = 0;
  info_[0] = '\0';
  ELOG_INFO("SD：已卸载 {}", sd_mount_point);
}

}  // namespace embark::platform::esp32
