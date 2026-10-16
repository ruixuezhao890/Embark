/**
 * 真机 · 文件系统后端（issue 25）
 *
 * 根 = SD 卡挂载点（esp32_board.h 的 sd_mount_point，挂在 FAT VFS 上）。宿主那份
 * （platform/host/host_filesystem.{h,cpp}）根是"可执行文件旁的目录"：路径纪律与句柄语义
 * 相同，但根的来历不同（一个是本地目录、一个是 FAT 挂载点），所以是两份后端而不是一份
 * 加参数 —— 换平台换后端（ADR 0002）。
 *
 * 与宿主一致的部分：只读、只开文件；路径按"相对根目录"解释，绝对路径 / 含 ".." / 含
 * 盘符一律 invalid_argument；句柄 = 定长槽下标 + 1（0 = 无效）；close 幂等。
 *
 * 没挂上卡时不 attach_root：HAL 装配点把 Context::filesystem 留 nullptr，上层字库服务
 * 因此直接 not_ready —— 这正是"没卡就跳过"的实现方式（issue 25 验收①）。
 */
#ifndef EMBARK_PLATFORM_ESP32_FILESYSTEM_H
#define EMBARK_PLATFORM_ESP32_FILESYSTEM_H

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <embark/hal/filesystem.h>
#include <embark_limits.h>

namespace embark::platform::esp32 {

class Esp32FileSystem final : public hal::IFileSystem {
 public:
  Esp32FileSystem() noexcept = default;

  Esp32FileSystem(const Esp32FileSystem&) = delete;
  Esp32FileSystem& operator=(const Esp32FileSystem&) = delete;

  ~Esp32FileSystem() override;

  /// 定下根目录（SD 挂载点）；幂等。太长 → no_space，空指针 → invalid_argument。
  [[nodiscard]] Error attach_root(const char* root) noexcept;

  [[nodiscard]] bool ready() const noexcept { return ready_; }

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
  char root_[filesystem_path_bytes] = {};
  bool ready_ = false;
};

}  // namespace embark::platform::esp32

#endif /* EMBARK_PLATFORM_ESP32_FILESYSTEM_H */
