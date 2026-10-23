// 测试替身：假持久化后端。
//
// 槽位布局与判定完全复用 detail/kv_slot.h —— 和宿主文件后端同一套编解码，
// 所以 fake 上验证过的 not_found / no_space / corrupt_data 语义对真后端同样成立。
#pragma once

#include <embark/detail/kv_slot.h>
#include <embark/hal/persistence.h>
#include <embark_limits.h>

#include <cstddef>
#include <cstdint>

namespace embark::fakes {

class FakePersistence final : public hal::IPersistence {
 public:
  FakePersistence() noexcept = default;

  Error init() noexcept override {
    ++init_calls;
    if (init_error != Error::none) {
      return init_error;
    }
    ready = true;
    return Error::none;
  }

  [[nodiscard]] etl::expected<std::size_t, Error> read(
      etl::string_view key, etl::span<std::uint8_t> out) const noexcept override {
    if (!ready) {
      return unexpected(Error::not_ready);
    }
    ++read_calls;
    if (read_error != Error::none) {
      return unexpected(read_error);
    }
    detail::SlotView view{};
    const Error found = find(key, view);
    if (found != Error::none) {
      return unexpected(found);
    }
    if (!view.used) {
      return unexpected(Error::not_found);
    }
    if (out.size() < view.value.size()) {
      return unexpected(Error::no_space);
    }
    for (std::size_t i = 0; i < view.value.size(); ++i) {
      out[i] = view.value[i];
    }
    return view.value.size();
  }

  Error write(etl::string_view key, etl::span<const std::uint8_t> value) noexcept override {
    if (!ready) {
      return Error::not_ready;
    }
    ++write_calls;
    if (write_error != Error::none) {
      return write_error;
    }
    std::size_t target = slots.size();
    std::size_t first_free = slots.size();
    for (std::size_t i = 0; i < slots.size(); ++i) {
      detail::SlotView view{};
      const Error decoded = detail::decode_slot(const_slot_span(i), view);
      if (decoded == Error::corrupt_data) {
        return Error::corrupt_data;  // 槽坏了就先别写，交给上层决定要不要 erase_all()
      }
      if (!view.used) {
        if (first_free == slots.size()) {
          first_free = i;
        }
        continue;
      }
      if (view.key == key) {
        target = i;
        break;
      }
    }
    if (target == slots.size()) {
      target = first_free;
    }
    if (target == slots.size()) {
      return Error::no_space;  // 槽位用完
    }
    return detail::encode_slot(slot_span(target), key, value);
  }

  Error erase(etl::string_view key) noexcept override {
    if (!ready) {
      return Error::not_ready;
    }
    ++erase_calls;
    if (erase_error != Error::none) {
      return erase_error;
    }
    detail::SlotView view{};
    const Error found = find(key, view);
    if (found != Error::none) {
      return found;
    }
    if (!view.used) {
      return Error::not_found;
    }
    for (std::size_t i = 0; i < slots.size(); ++i) {
      detail::SlotView candidate{};
      if (detail::decode_slot(const_slot_span(i), candidate) != Error::none) {
        continue;
      }
      if (candidate.used && candidate.key == key) {
        return detail::clear_slot(slot_span(i));
      }
    }
    return Error::not_found;
  }

  Error erase_all() noexcept override {
    if (!ready) {
      return Error::not_ready;
    }
    ++erase_all_calls;
    for (std::size_t i = 0; i < slots.size(); ++i) {
      const Error cleared = detail::clear_slot(slot_span(i));
      if (cleared != Error::none) {
        return cleared;
      }
    }
    return Error::none;
  }

  [[nodiscard]] std::size_t capacity_bytes() const noexcept override {
    return slots.size() * persistence_max_value_bytes;
  }

  /// 破坏某个槽的一个字节（落在 CRC 覆盖范围内），用来验证 corrupt_data 分支。
  void corrupt_slot(std::size_t index) noexcept {
    if (index < slots.size()) {
      slots[index][detail::kv_header_size] ^= 0xFFU;
    }
  }

  [[nodiscard]] std::size_t slot_count() const noexcept { return slots.size(); }

  // 旋钮与观测点。
  Error init_error = Error::none;
  Error read_error = Error::none;
  Error write_error = Error::none;
  Error erase_error = Error::none;
  bool ready = false;
  std::size_t init_calls = 0;
  mutable std::size_t read_calls = 0;
  std::size_t write_calls = 0;
  std::size_t erase_calls = 0;
  std::size_t erase_all_calls = 0;
  etl::array<etl::array<std::uint8_t, detail::kv_slot_bytes>, persistence_max_slots> slots{};

 private:
  [[nodiscard]] etl::span<std::uint8_t> slot_span(std::size_t index) noexcept {
    return etl::span<std::uint8_t>(slots[index].data(), slots[index].size());
  }

  [[nodiscard]] etl::span<const std::uint8_t> const_slot_span(std::size_t index) const noexcept {
    return etl::span<const std::uint8_t>(slots[index].data(), slots[index].size());
  }

  [[nodiscard]] Error find(etl::string_view key, detail::SlotView& out) const noexcept {
    for (std::size_t i = 0; i < slots.size(); ++i) {
      detail::SlotView view{};
      const Error decoded = detail::decode_slot(const_slot_span(i), view);
      if (decoded == Error::corrupt_data) {
        return Error::corrupt_data;
      }
      if (view.used && view.key == key) {
        out = view;
        return Error::none;
      }
    }
    out.used = false;
    return Error::none;
  }
};

}  // namespace embark::fakes
