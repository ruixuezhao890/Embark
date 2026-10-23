/**
 * 宿主文件系统后端实现：见 host_filesystem.h 头注释。
 *
 * 平台线允许用宿主标准库（fstream / filesystem / cstdio），-fno-exceptions 只约束
 * 内核与 app 那条线（host/CMakeLists.txt 头注释）。
 */
#include "host_filesystem.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <windows.h>  // GetModuleFileNameA（<exe 目录> 的默认根）
#else
#include <unistd.h>  // readlink(/proc/self/exe)
#endif

#include <middleware/elog/elog.hpp>

namespace embark::platform::host {
namespace {

/// <exe 目录>/embark_host_fs —— 与持久化后端"可执行文件旁"同一个口径。
std::filesystem::path default_root() {
  char exe[1024] = {};
#if defined(_WIN32)
  const DWORD written = GetModuleFileNameA(nullptr, exe, static_cast<DWORD>(sizeof(exe)));
  if (written == 0 || written >= sizeof(exe)) {
    return std::filesystem::path("embark_host_fs");
  }
#else
  const ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
  if (n <= 0) {
    return std::filesystem::path("embark_host_fs");
  }
#endif
  return std::filesystem::path(exe).parent_path() / "embark_host_fs";
}

}  // namespace

HostFileSystem::~HostFileSystem() {
  for (Slot& slot : slots_) {
    if (slot.file != nullptr) {
      std::fclose(slot.file);
      slot.file = nullptr;
    }
  }
}

Error HostFileSystem::init() noexcept {
  if (ready_) {
    return Error::none;
  }
  const char* from_env = std::getenv("EMBARK_HOST_FS_ROOT");
  const std::filesystem::path root = (from_env != nullptr && from_env[0] != '\0')
                                         ? std::filesystem::path(from_env)
                                         : default_root();

  std::error_code ec;
  std::filesystem::create_directories(root, ec);  // 已存在不算错：ec 只在真失败时置位
  if (ec) {
    ELOG_ERROR("宿主文件系统：建不出根目录 {}（{}）", root.string(), ec.message());
    return Error::io_failure;
  }
  const std::string full = root.string();
  if (full.size() + 1 > sizeof(root_)) {
    return Error::no_space;
  }
  std::memcpy(root_, full.c_str(), full.size() + 1);
  ready_ = true;
  ELOG_INFO("宿主文件系统根目录：{}", root_);
  return Error::none;
}

Error HostFileSystem::resolve(etl::string_view path, char* out,
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

Error HostFileSystem::slot_of(hal::FileHandle file, std::size_t& index) noexcept {
  if (file == hal::invalid_file_handle || file > filesystem_max_open_files) {
    return Error::invalid_argument;
  }
  index = static_cast<std::size_t>(file) - 1;
  return Error::none;
}

etl::expected<hal::FileHandle, Error> HostFileSystem::open(etl::string_view path) noexcept {
  char full[512] = {};
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

etl::expected<std::size_t, Error> HostFileSystem::read(hal::FileHandle file,
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

Error HostFileSystem::seek(hal::FileHandle file, std::size_t offset) noexcept {
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

Error HostFileSystem::close(hal::FileHandle file) noexcept {
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

etl::expected<std::size_t, Error> HostFileSystem::size(hal::FileHandle file) const noexcept {
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

}  // namespace embark::platform::host
