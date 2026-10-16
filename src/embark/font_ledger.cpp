/**
 * 运行时字库记账实现：见 include/embark/font_ledger.h 头注释。
 *
 * 纯逻辑：定长表 + 两个计数器，没有堆、没有 IO、没有 LVGL（内核线，-fno-exceptions）。
 */
#include <embark/font_ledger.h>

#include <cstring>

namespace embark {

std::size_t FontLedger::find(etl::string_view path) const noexcept {
  for (std::size_t i = 0; i < count_; ++i) {
    if (etl::string_view(entries_[i].path) == path) {
      return i;
    }
  }
  return font_max_loaded;
}

etl::expected<std::size_t, Error> FontLedger::acquire(etl::string_view path,
                                                      std::size_t bytes) noexcept {
  if (path.empty() || path.size() + 1 > font_path_bytes) {
    return etl::unexpected(Error::invalid_argument);
  }

  const std::size_t existing = find(path);
  if (existing != font_max_loaded) {
    ++entries_[existing].refs;  // 复用：同一个字库驻留，不再计费
    return entries_[existing].refs;
  }

  if (bytes == 0) {
    return etl::unexpected(Error::invalid_argument);
  }
  if (count_ >= font_max_loaded || bytes > font_budget_bytes - used_bytes_) {
    return etl::unexpected(Error::no_space);
  }

  Entry& entry = entries_[count_];
  std::memcpy(entry.path, path.data(), path.size());
  entry.path[path.size()] = '\0';
  entry.bytes = bytes;
  entry.refs = 1;
  ++count_;
  used_bytes_ += bytes;
  return std::size_t{1};
}

Error FontLedger::release(etl::string_view path) noexcept {
  const std::size_t index = find(path);
  if (index == font_max_loaded) {
    return Error::not_found;
  }

  Entry& entry = entries_[index];
  if (entry.refs > 1) {
    --entry.refs;
    return Error::none;
  }

  used_bytes_ -= entry.bytes;
  --count_;
  if (index != count_) {
    entries_[index] = entries_[count_];  // 尾部补位，保持"前 count_ 个有效"
  }
  entries_[count_] = Entry{};
  return Error::none;
}

etl::expected<std::size_t, Error> FontLedger::refs(etl::string_view path) const noexcept {
  const std::size_t index = find(path);
  if (index == font_max_loaded) {
    return etl::unexpected(Error::not_found);
  }
  return entries_[index].refs;
}

}  // namespace embark
