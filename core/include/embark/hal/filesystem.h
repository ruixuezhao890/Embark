/**
 * HAL · 文件系统（ADR 0007 / issue 17）
 *
 * 只做一件真需要的事：**只读地把一个文件读出来**（ADR 0017 之前是从盘上取字体 .bin）。
 * 接口是字节流的 open/read/seek/close，没有目录遍历、没有写、没有并发语义承诺；
 * 路径是"实现自己理解的路径"（宿主 = 可执行文件旁的根目录，真机 = SD 挂载点）。
 *
 * 为什么不直接用 LVGL 自带的 FS 驱动：LV_USE_FS_* 的根路径是编译期宏，而宿主（仓库目录）
 * 与真机（SD 挂载点）不同、开发期还要能临时换根。把"根"交给后端，'F' 盘接到这个接口上的
 * 那层（core/common/font_service.cpp）已在 issue 04 随 BIN 链退役；今天的两个消费者是
 * Aidogui 资源盘（'S' 盘，core/common/aidogui_res_fs.cpp）与启动期读整份 TTF 的那次 fopen。
 *
 * 错误码口径：文件不存在 → not_found；路径非法（绝对路径 / 含 ".." / 含盘符）→
 * invalid_argument；后端未就绪 → not_ready；句柄槽用完 → no_space；读到文件尾返回 0，不是错误。
 */
#ifndef EMBARK_HAL_FILESYSTEM_H
#define EMBARK_HAL_FILESYSTEM_H

#include <cstddef>
#include <cstdint>

#include <embark/error.h>
#include <middleware/etl/expected.h>
#include <middleware/etl/span.h>
#include <middleware/etl/string_view.h>

namespace embark::hal {

/// 已打开文件的句柄：0 = 无效；实现自己发号（宿主后端 = 定长槽下标 + 1，不是指针）。
using FileHandle = std::uint16_t;

inline constexpr FileHandle invalid_file_handle = 0;

class IFileSystem {
 public:
  virtual ~IFileSystem() = default;

  /// 只读打开。成功返回非 0 句柄；文件不存在 → not_found；路径非法 → invalid_argument。
  [[nodiscard]] virtual etl::expected<FileHandle, Error> open(etl::string_view path) noexcept = 0;

  /// 从当前位置读，最多 out 个字节；返回实际读到的字节数，0 = 已到文件尾（不是错误）。
  [[nodiscard]] virtual etl::expected<std::size_t, Error> read(
      FileHandle file, etl::span<std::uint8_t> out) noexcept = 0;

  /// 绝对定位到 offset（从文件头算）；超过文件长度 → invalid_argument。
  virtual Error seek(FileHandle file, std::size_t offset) noexcept = 0;

  /// 关闭。幂等：同一个句柄关两次，第二次也返回 none（调用方不必自己记"关过没有"）。
  virtual Error close(FileHandle file) noexcept = 0;

  /// 文件字节数（记账用：运行时字库按文件大小计费）。句柄无效 → not_found。
  [[nodiscard]] virtual etl::expected<std::size_t, Error> size(FileHandle file) const noexcept = 0;
};

}  // namespace embark::hal

#endif /* EMBARK_HAL_FILESYSTEM_H */
