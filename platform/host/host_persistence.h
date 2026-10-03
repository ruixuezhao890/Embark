/**
 * 宿主 · 持久化后端（issues/04）
 *
 * 一个文件就是一整片定长槽（config/embark_limits.h 的 persistence_max_slots ×
 * detail/kv_slot.h 的 kv_slot_bytes）。读写都是"定位到槽 → 整槽读/整槽写"，没有堆分配，
 * 也因此天然是掉电安全的：一槽坏掉只毁一条。
 *
 * 文件路径：环境变量 EMBARK_HOST_STORAGE 优先，否则用 <exe 目录>/embark_host_kv.bin。
 * 大小既不是 0 也不是"正好一片槽"时返回 corrupt_data（多半是改了槽数或键值上限），
 * 此时文件保持打开，调用方可以 erase_all() 重建。
 */
#ifndef EMBARK_PLATFORM_HOST_PERSISTENCE_H
#define EMBARK_PLATFORM_HOST_PERSISTENCE_H

#include <cstddef>
#include <cstdint>
#include <fstream>

#include <embark/detail/kv_slot.h>
#include <embark/hal/persistence.h>
#include <embark_limits.h>

namespace embark::platform::host {

class HostPersistence final : public hal::IPersistence {
 public:
  HostPersistence() noexcept = default;

  HostPersistence(const HostPersistence&) = delete;
  HostPersistence& operator=(const HostPersistence&) = delete;

  ~HostPersistence() override;

  Error init() noexcept override;

  [[nodiscard]] etl::expected<std::size_t, Error> read(
          etl::string_view key,
          etl::span<std::uint8_t> out) const noexcept override;

  Error write(etl::string_view key, etl::span<const std::uint8_t> value) noexcept override;

  Error erase(etl::string_view key) noexcept override;

  Error erase_all() noexcept override;

  [[nodiscard]] std::size_t capacity_bytes() const noexcept override;

  /// 实际用的文件路径（启动日志里打一行，出问题时知道去哪找）。
  [[nodiscard]] const char* path() const noexcept { return path_; }

 private:
  static constexpr std::size_t slots_ = persistence_max_slots;
  static constexpr std::size_t slot_bytes_ = detail::kv_slot_bytes;
  static constexpr std::size_t file_bytes_ = slots_ * slot_bytes_;

  /// 找到键所在的槽号；不存在 → not_found。
  [[nodiscard]] Error find_slot(etl::string_view key, std::size_t& index) const noexcept;

  [[nodiscard]] Error read_slot(std::size_t index, std::uint8_t* slot) const noexcept;

  [[nodiscard]] Error write_slot(std::size_t index, const std::uint8_t* slot) noexcept;

  /// 建一片空存储（首次运行或 erase_all）。
  [[nodiscard]] Error write_all_empty() noexcept;

  mutable std::fstream file_;
  char path_[512] = {};
  bool ready_ = false;
};

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_PERSISTENCE_H */
