/**
 * 真机文件系统后端实现：见 esp32_filesystem.h 头注释。
 *
 * 用 stdio（fopen/fread/fseek）而不是 IDF 的 open/read：fatfs 通过 VFS 把 stdio 接好了，
 * 而且这样与宿主后端是同一套读写语义，行为不会因为平台不同而分叉。
 */
#include "esp32_filesystem.h"

#include <cstring>

#include <middleware/elog/elog.hpp>

namespace embark::platform::esp32 {

Esp32FileSystem::~Esp32FileSystem() {
  for (Slot& slot : slots_) {
    if (slot.file != nullptr) {
      std::fclose(slot.file);
      slot.file = nullptr;
    }
  }
}

Error Esp32FileSystem::attach_root(const char* root) noexcept {
  if (root == nullptr || root[0] == '\0') {
    return Error::invalid_argument;
  }
  if (ready_ && std::strcmp(root_, root) == 0) {
    return Error::none;
  }
  const std::size_t len = std::strlen(root);
  if (len + 1 > sizeof(root_)) {
    return Error::no_space;
  }
  std::memcpy(root_, root, len + 1);
  ready_ = true;
  ELOG_INFO("真机文件系统根目录：{}", root_);
  return Error::none;
}

Error Esp32FileSystem::resolve(etl::string_view path, char* out,
                               std::size_t out_bytes) const noexcept {
  if (!ready_) {
    return Error::not_ready;
  }
  if (path.empty()) {
    return Error::invalid_argument;
  }
  if (path[0] == '/' || path[0] == '\\' || path[0] == '.') {
    return Error::invalid_argument;  // 绝对路径，或 ".." / "." 开头
  }
  for (std::size_t i = 0; i + 1 < path.size(); ++i) {
    if (path[i] == '.' && path[i + 1] == '.') {
      return Error::invalid_argument;
    }
  }
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (path[i] == ':') {
      return Error::invalid_argument;  // 盘符：根由后端定，不接受调用方指定
    }
  }
  const std::size_t root_len = std::strlen(root_);
  if (root_len + 1 + path.size() + 1 > out_bytes) {
    return Error::no_space;
  }
  std::memcpy(out, root_, root_len);
  out[root_len] = '/';
  std::memcpy(out + root_len + 1, path.data(), path.size());
  out[root_len + 1 + path.size()] = '\0';
  return Error::none;
}

Error Esp32FileSystem::slot_of(hal::FileHandle file, std::size_t& index) noexcept {
  if (file == hal::invalid_file_handle || file > filesystem_max_open_files) {
    return Error::invalid_argument;
  }
  index = static_cast<std::size_t>(file) - 1;
  return Error::none;
}

etl::expected<hal::FileHandle, Error> Esp32FileSystem::open(etl::string_view path) noexcept {
  char full[filesystem_path_bytes + 128] = {};
  const Error resolved = resolve(path, full, sizeof(full));
  if (resolved != Error::none) {
    return etl::unexpected(resolved);
  }

  std::FILE* file = std::fopen(full, "rb");
  if (file == nullptr) {
    return etl::unexpected(Error::not_found);
  }

  if (std::fseek(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    return etl::unexpected(Error::io_failure);
  }
  const long bytes = std::ftell(file);
  if (bytes < 0 || std::fseek(file, 0, SEEK_SET) != 0) {
    std::fclose(file);
    return etl::unexpected(Error::io_failure);
  }

  for (std::size_t i = 0; i < filesystem_max_open_files; ++i) {
    if (slots_[i].file != nullptr) {
      continue;
    }
    slots_[i].file = file;
    slots_[i].bytes = static_cast<std::size_t>(bytes);
    return static_cast<hal::FileHandle>(i + 1);
  }
  std::fclose(file);
  return etl::unexpected(Error::no_space);  // 槽位用完：调用方没关文件
}

etl::expected<std::size_t, Error> Esp32FileSystem::read(hal::FileHandle file,
                                                        etl::span<std::uint8_t> out) noexcept {
  std::size_t index = 0;
  const Error invalid = slot_of(file, index);
  if (invalid != Error::none) {
    return etl::unexpected(invalid);
  }
  if (slots_[index].file == nullptr) {
    return etl::unexpected(Error::not_found);
  }
  if (out.empty()) {
    return std::size_t{0};
  }
  const std::size_t got = std::fread(out.data(), 1, out.size(), slots_[index].file);
  if (got < out.size() && std::ferror(slots_[index].file) != 0) {
    return etl::unexpected(Error::io_failure);
  }
  return got;  // 0 = 文件尾
}

Error Esp32FileSystem::seek(hal::FileHandle file, std::size_t offset) noexcept {
  std::size_t index = 0;
  const Error invalid = slot_of(file, index);
  if (invalid != Error::none) {
    return invalid;
  }
  if (slots_[index].file == nullptr) {
    return Error::not_found;
  }
  if (offset > slots_[index].bytes) {
    return Error::invalid_argument;
  }
  return (std::fseek(slots_[index].file, static_cast<long>(offset), SEEK_SET) == 0)
             ? Error::none
             : Error::io_failure;
}

Error Esp32FileSystem::close(hal::FileHandle file) noexcept {
  std::size_t index = 0;
  const Error invalid = slot_of(file, index);
  if (invalid != Error::none) {
    return invalid;
  }
  if (slots_[index].file == nullptr) {
    return Error::none;  // 幂等：已经关过了（或从来没开过这个槽）
  }
  const bool ok = (std::fclose(slots_[index].file) == 0);
  slots_[index].file = nullptr;
  slots_[index].bytes = 0;
  return ok ? Error::none : Error::io_failure;
}

etl::expected<std::size_t, Error> Esp32FileSystem::size(hal::FileHandle file) const noexcept {
  std::size_t index = 0;
  const Error invalid = slot_of(file, index);
  if (invalid != Error::none) {
    return etl::unexpected(invalid);
  }
  if (slots_[index].file == nullptr) {
    return etl::unexpected(Error::not_found);
  }
  return slots_[index].bytes;
}

}  // namespace embark::platform::esp32
