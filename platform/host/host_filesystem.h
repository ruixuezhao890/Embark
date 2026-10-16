/**
 * 宿主 · 文件系统后端（issue 17）
 *
 * 只读、只开文件（不做目录遍历）：v1 的需求就是"把运行时字库的 .bin 读出来"。
 * 根目录：环境变量 EMBARK_HOST_FS_ROOT 优先，否则 <exe 目录>/embark_host_fs（与持久化
 * 后端同一个模式）。路径按"相对根目录"解释：绝对路径、含 ".."、含盘符一律 invalid_argument
 * —— 后端不该留一条跑出根目录的路。
 *
 * 句柄 = 定长槽下标 + 1（filesystem_max_open_files 个槽，无堆分配）；close 幂等。
 */
#ifndef EMBARK_PLATFORM_HOST_FILESYSTEM_H
#define EMBARK_PLATFORM_HOST_FILESYSTEM_H

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <embark/hal/filesystem.h>
#include <embark_limits.h>

namespace embark::platform::host {

class HostFileSystem final : public hal::IFileSystem {
 public:
  HostFileSystem() noexcept = default;

  HostFileSystem(const HostFileSystem&) = delete;
  HostFileSystem& operator=(const HostFileSystem&) = delete;

  ~HostFileSystem() override;

  /// 定下根目录（环境变量 > <exe 目录>/embark_host_fs）并把它建出来；幂等。
  [[nodiscard]] Error init() noexcept;

  [[nodiscard]] etl::expected<hal::FileHandle, Error> open(etl::string_view path) noexcept override;

  [[nodiscard]] etl::expected<std::size_t, Error> read(
      hal::FileHandle file, etl::span<std::uint8_t> out) noexcept override;

  Error seek(hal::FileHandle file, std::size_t offset) noexcept override;

  Error close(hal::FileHandle file) noexcept override;

  [[nodiscard]] etl::expected<std::size_t, Error> size(
      hal::FileHandle file) const noexcept override;

  /// 实际用的根目录（启动日志里打一行，出问题时知道去哪找）。
  [[nodiscard]] const char* root() const noexcept { return root_; }

 private:
  struct Slot {
    std::FILE* file = nullptr;
    std::size_t bytes = 0;
  };

  /// 把相对路径拼成绝对路径；非法路径 → invalid_argument。
  [[nodiscard]] Error resolve(etl::string_view path, char* out,
                              std::size_t out_bytes) const noexcept;

  /// 句柄 → 槽下标；越界 → invalid_argument。
  [[nodiscard]] static Error slot_of(hal::FileHandle file, std::size_t& index) noexcept;

  Slot slots_[filesystem_max_open_files]{};
  char root_[512] = {};
  bool ready_ = false;
};

}  // namespace embark::platform::host

#endif /* EMBARK_PLATFORM_HOST_FILESYSTEM_H */
