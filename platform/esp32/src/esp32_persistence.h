/**
 * Embark ESP32-S3 · 持久化后端（NVS）
 *
 * 键 → NVS 的 blob。三条实情写在这里，省得以后靠猜：
 *
 *   - **键长**：NVS 自己的上限是 15 字符（键名缓冲 16 字节含结尾 '\0'），比框架声明的
 *     persistence_max_key_bytes（16）更严。超了如实返回 invalid_argument，**不截断**
 *     （截断会让两个不同的键撞在一起，那种 bug 比报错难查十倍）。
 *   - **值长**：框架的值上限是 persistence_max_value_bytes（64），超了返回 no_space；
 *     NVS 分区本身是 24 KB（partitions.csv 的 nvs 项），够 32 槽 × 64 字节很多轮。
 *   - **容量口径**：capacity_bytes() 报的是框架自己的口径（槽数 × 单值上限 = 2 KB），
 *     不是 NVS 分区的物理大小 —— 这个数是给"上层估算还能放多少"用的，两边后端一致。
 *
 * 初始化失败时不做静默擦除：NVS 报 NO_FREE_PAGES / NEW_VERSION_FOUND 说明分区需要
 * 重建，那是**用户的决定**（erase_all() 或 idf.py erase-flash），不是后端的。
 */
#ifndef EMBARK_PLATFORM_ESP32_PERSISTENCE_H
#define EMBARK_PLATFORM_ESP32_PERSISTENCE_H

#include <cstddef>

#include <embark/hal/persistence.h>

#include <nvs.h>

namespace embark::platform::esp32 {

class Esp32Persistence final : public hal::IPersistence {
 public:
  Esp32Persistence() noexcept = default;
  Esp32Persistence(const Esp32Persistence&) = delete;
  Esp32Persistence& operator=(const Esp32Persistence&) = delete;
  ~Esp32Persistence() override;

  /// nvs_flash_init + nvs_open。分区损坏/版本不符 → corrupt_data（不自动擦除）。
  [[nodiscard]] Error init() noexcept override;

  [[nodiscard]] etl::expected<std::size_t, Error> read(
      etl::string_view key, etl::span<std::uint8_t> out) const noexcept override;

  Error write(etl::string_view key, etl::span<const std::uint8_t> value) noexcept override;
  Error erase(etl::string_view key) noexcept override;
  Error erase_all() noexcept override;

  /// 框架口径：persistence_max_slots × persistence_max_value_bytes。
  [[nodiscard]] std::size_t capacity_bytes() const noexcept override;

 private:
  nvs_handle_t handle_ = 0;
  bool opened_ = false;
};

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_PERSISTENCE_H */
