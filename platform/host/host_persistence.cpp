#include "host_persistence.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <embark/detail/kv_slot.h>

#ifdef _WIN32
#include <windows.h>
#endif

namespace embark::platform::host {
namespace {

constexpr const char* kFileName = "embark_host_kv.bin";

/// 定路径：环境变量优先 → exe 同目录 → 当前目录（最后一条只是兜底）。
void resolve_path(char* out, std::size_t capacity) noexcept {
  const char* from_env = std::getenv("EMBARK_HOST_STORAGE");
  if (from_env != nullptr && from_env[0] != '\0') {
    std::snprintf(out, capacity, "%s", from_env);
    return;
  }

#ifdef _WIN32
  char exe_path[MAX_PATH] = {};
  const DWORD written = GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
  if (written > 0 && written < MAX_PATH) {
    char* last_sep = std::strrchr(exe_path, '\\');
    if (last_sep != nullptr) {
      *(last_sep + 1) = '\0';  // 只留目录（含结尾分隔符）
      std::snprintf(out, capacity, "%s%s", exe_path, kFileName);
      return;
    }
  }
#endif

  std::snprintf(out, capacity, "%s", kFileName);
}

}  // namespace

HostPersistence::~HostPersistence() {
  if (file_.is_open()) {
    file_.close();
  }
}

Error HostPersistence::init() noexcept {
  if (ready_) {
    return Error::none;
  }
  if (!file_.is_open()) {
    resolve_path(path_, sizeof(path_));
    file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_.is_open()) {
      // 文件不存在：第一次运行。注意必须带上 in —— 只用 out 打开的流没有读能力，
      // 之后的 seekg/read 会一路失败（踩过）。
      file_.clear();
      file_.open(path_, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
      if (!file_.is_open()) {
        return Error::io_failure;
      }
    }
  }

  file_.clear();
  file_.seekg(0, std::ios::end);
  if (file_.fail()) {
    return Error::io_failure;
  }
  const std::streamoff size = file_.tellg();
  if (size < 0) {
    return Error::io_failure;
  }
  if (size == 0) {
    return write_all_empty();
  }
  if (static_cast<std::size_t>(size) == file_bytes_) {
    ready_ = true;
    return Error::none;
  }

  // 大小对不上：多半是改过 persistence_max_slots / 键值上限。不猜，报损坏；
  // 文件仍开着，erase_all() 可以重建。
  return Error::corrupt_data;
}

Error HostPersistence::read_slot(std::size_t index, std::uint8_t* slot) const noexcept {
  file_.clear();
  file_.seekg(static_cast<std::streamoff>(index * slot_bytes_), std::ios::beg);
  if (file_.fail()) {
    return Error::io_failure;
  }
  file_.read(reinterpret_cast<char*>(slot), static_cast<std::streamsize>(slot_bytes_));
  if (file_.gcount() != static_cast<std::streamsize>(slot_bytes_)) {
    return Error::io_failure;
  }
  return Error::none;
}

Error HostPersistence::write_slot(std::size_t index, const std::uint8_t* slot) noexcept {
  file_.clear();
  file_.seekp(static_cast<std::streamoff>(index * slot_bytes_), std::ios::beg);
  if (file_.fail()) {
    return Error::io_failure;
  }
  file_.write(reinterpret_cast<const char*>(slot), static_cast<std::streamsize>(slot_bytes_));
  file_.flush();
  if (file_.fail()) {
    return Error::io_failure;
  }
  return Error::none;
}

Error HostPersistence::write_all_empty() noexcept {
  std::uint8_t empty_slot[detail::kv_slot_bytes] = {};
  for (std::size_t i = 0; i < slots_; ++i) {
    const Error error = write_slot(i, empty_slot);
    if (error != Error::none) {
      return error;
    }
  }
  ready_ = true;
  return Error::none;
}

Error HostPersistence::find_slot(etl::string_view key, std::size_t& index) const noexcept {
  std::uint8_t raw[detail::kv_slot_bytes] = {};
  for (std::size_t i = 0; i < slots_; ++i) {
    const Error error = read_slot(i, raw);
    if (error != Error::none) {
      return error;
    }
    detail::SlotView view{};
    const Error decoded =
            detail::decode_slot(etl::span<const std::uint8_t>(raw, slot_bytes_), view);
    if (decoded != Error::none) {
      return decoded;  // 有槽坏了就明说，不装作没这条键
    }
    if (view.used && view.key == key) {
      index = i;
      return Error::none;
    }
  }
  return Error::not_found;
}

etl::expected<std::size_t, Error> HostPersistence::read(etl::string_view key,
                                                        etl::span<std::uint8_t> out) const noexcept {
  if (!ready_) {
    return unexpected(Error::not_ready);
  }
  std::size_t index = 0;
  const Error found = find_slot(key, index);
  if (found != Error::none) {
    return unexpected(found);
  }

  std::uint8_t raw[detail::kv_slot_bytes] = {};
  const Error error = read_slot(index, raw);
  if (error != Error::none) {
    return unexpected(error);
  }
  detail::SlotView view{};
  const Error decoded = detail::decode_slot(etl::span<const std::uint8_t>(raw, slot_bytes_), view);
  if (decoded != Error::none) {
    return unexpected(decoded);
  }
  if (out.size() < view.value.size()) {
    return unexpected(Error::no_space);
  }
  for (std::size_t i = 0; i < view.value.size(); ++i) {
    out[i] = view.value[i];
  }
  return view.value.size();
}

Error HostPersistence::write(etl::string_view key, etl::span<const std::uint8_t> value) noexcept {
  if (!ready_) {
    return Error::not_ready;
  }

  std::uint8_t raw[detail::kv_slot_bytes] = {};
  const Error encoded = detail::encode_slot(etl::span<std::uint8_t>(raw, slot_bytes_), key, value);
  if (encoded != Error::none) {
    return encoded;
  }

  std::size_t index = 0;
  const Error existing = find_slot(key, index);
  if (existing == Error::none) {
    return write_slot(index, raw);  // 覆盖已有键
  }
  if (existing != Error::not_found) {
    return existing;
  }

  // 没有这个键：找第一个空槽。
  for (std::size_t i = 0; i < slots_; ++i) {
    std::uint8_t probe[detail::kv_slot_bytes] = {};
    const Error error = read_slot(i, probe);
    if (error != Error::none) {
      return error;
    }
    detail::SlotView view{};
    const Error decoded =
            detail::decode_slot(etl::span<const std::uint8_t>(probe, slot_bytes_), view);
    if (decoded != Error::none) {
      return decoded;
    }
    if (!view.used) {
      return write_slot(i, raw);
    }
  }
  return Error::no_space;  // 槽位用完：调用方该先 erase 一条
}

Error HostPersistence::erase(etl::string_view key) noexcept {
  if (!ready_) {
    return Error::not_ready;
  }
  std::size_t index = 0;
  const Error found = find_slot(key, index);
  if (found != Error::none) {
    return found;
  }

  std::uint8_t empty_slot[detail::kv_slot_bytes] = {};
  const Error cleared = detail::clear_slot(etl::span<std::uint8_t>(empty_slot, slot_bytes_));
  if (cleared != Error::none) {
    return cleared;
  }
  return write_slot(index, empty_slot);
}

Error HostPersistence::erase_all() noexcept {
  if (!file_.is_open()) {
    const Error opened = init();
    if (opened != Error::none) {
      return opened;
    }
  }
  ready_ = false;
  return write_all_empty();
}

std::size_t HostPersistence::capacity_bytes() const noexcept {
  return slots_ * persistence_max_value_bytes;
}

}  // namespace embark::platform::host
