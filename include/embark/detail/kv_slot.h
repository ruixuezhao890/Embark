/**
 * 定长槽编解码（HAL 持久化的共享实现细节）
 *
 * 宿主文件后端（platform/host/host_persistence.cpp）与测试替身
 * （tests/fakes/fake_persistence.h）共用这里的布局，所以两边对
 * not_found / no_space / corrupt_data 的判定逐字一致 —— 换后端不改语义。
 *
 * 槽布局（little-endian，共 kv_slot_bytes = 12 + 键上限 + 值上限 = 92 字节）：
 *   偏移 0  .. 3   魔数 'E' 'K' 'V' '1'
 *   偏移 4         state：0 = 空槽，1 = 已用（其它值视为损坏）
 *   偏移 5         key_len
 *   偏移 6  .. 7   value_len（LE u16）
 *   偏移 8  .. 11  crc32（LE u32，只覆盖 key 与 value 的原始字节）
 *   偏移 12        key[persistence_max_key_bytes]
 *   偏移 28        value[persistence_max_value_bytes]
 *
 * 为什么不用结构体直接 memcpy：槽在文件里就是字节，写一份"读字节 → 逐字段校验"的显式
 * 代码，才好在坏数据（掉电写坏、版本错配）时给出 corrupt_data 而不是读到垃圾。
 */
#ifndef EMBARK_DETAIL_KV_SLOT_H
#define EMBARK_DETAIL_KV_SLOT_H

#include <cstddef>
#include <cstdint>

#include <embark/error.h>
#include <embark_limits.h>
#include <middleware/etl/crc32.h>
#include <middleware/etl/span.h>
#include <middleware/etl/string_view.h>

namespace embark::detail {

inline constexpr std::size_t kv_magic_size = 4;
inline constexpr std::size_t kv_header_size = 12;
inline constexpr std::size_t kv_slot_bytes =
    kv_header_size + persistence_max_key_bytes + persistence_max_value_bytes;

inline constexpr std::uint8_t kv_state_empty = 0;
inline constexpr std::uint8_t kv_state_used = 1;

/// 解码结果：used = false 时 key/value 是空的（空槽，不是错误）。
struct SlotView {
  bool used = false;
  etl::string_view key{};
  etl::span<const std::uint8_t> value{};
};

namespace internal {

inline void write_u16(std::uint8_t* out, std::uint16_t value) noexcept {
  out[0] = static_cast<std::uint8_t>(value & 0xFFU);
  out[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

inline void write_u32(std::uint8_t* out, std::uint32_t value) noexcept {
  out[0] = static_cast<std::uint8_t>(value & 0xFFU);
  out[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  out[2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  out[3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

inline std::uint16_t read_u16(const std::uint8_t* in) noexcept {
  return static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(in[0]) |
      static_cast<std::uint16_t>(static_cast<std::uint16_t>(in[1]) << 8U));
}

inline std::uint32_t read_u32(const std::uint8_t* in) noexcept {
  return static_cast<std::uint32_t>(in[0]) | (static_cast<std::uint32_t>(in[1]) << 8U) |
         (static_cast<std::uint32_t>(in[2]) << 16U) | (static_cast<std::uint32_t>(in[3]) << 24U);
}

/// crc32 覆盖 key 与 value 的原始字节（不含 header，也就不会自我引用）。
inline std::uint32_t slot_crc(etl::string_view key, etl::span<const std::uint8_t> value) noexcept {
  etl::crc32 crc;
  crc.add(key.begin(), key.end());
  crc.add(value.begin(), value.end());
  return crc.value();
}

inline bool has_magic(const std::uint8_t* slot) noexcept {
  return slot[0] == static_cast<std::uint8_t>('E') && slot[1] == static_cast<std::uint8_t>('K') &&
         slot[2] == static_cast<std::uint8_t>('V') && slot[3] == static_cast<std::uint8_t>('1');
}

}  // namespace internal

/// 把整槽清零（= 空槽）。槽太小 → invalid_argument。
inline Error clear_slot(etl::span<std::uint8_t> slot) noexcept {
  if (slot.size() < kv_slot_bytes) {
    return Error::invalid_argument;
  }
  for (std::size_t i = 0; i < kv_slot_bytes; ++i) {
    slot[i] = 0;
  }
  return Error::none;
}

/// 编码一条键值。键为空/超长 → invalid_argument；值超上限或槽太小 → no_space。
inline Error encode_slot(etl::span<std::uint8_t> slot, etl::string_view key,
                         etl::span<const std::uint8_t> value) noexcept {
  if (key.empty() || key.size() > persistence_max_key_bytes) {
    return Error::invalid_argument;
  }
  if (value.size() > persistence_max_value_bytes) {
    return Error::no_space;
  }
  if (slot.size() < kv_slot_bytes) {
    return Error::no_space;
  }

  clear_slot(slot);
  internal::write_u32(slot.data() + 8, internal::slot_crc(key, value));
  slot[4] = kv_state_used;
  slot[5] = static_cast<std::uint8_t>(key.size());
  internal::write_u16(slot.data() + 6, static_cast<std::uint16_t>(value.size()));
  slot[0] = static_cast<std::uint8_t>('E');
  slot[1] = static_cast<std::uint8_t>('K');
  slot[2] = static_cast<std::uint8_t>('V');
  slot[3] = static_cast<std::uint8_t>('1');
  for (std::size_t i = 0; i < key.size(); ++i) {
    slot[kv_header_size + i] = static_cast<std::uint8_t>(key[i]);
  }
  for (std::size_t i = 0; i < value.size(); ++i) {
    slot[kv_header_size + persistence_max_key_bytes + i] = value[i];
  }
  return Error::none;
}

/// 解码一槽。空槽 → none 且 used = false；校验不过 → corrupt_data。
inline Error decode_slot(etl::span<const std::uint8_t> slot, SlotView& out) noexcept {
  out.used = false;
  out.key = etl::string_view{};
  out.value = etl::span<const std::uint8_t>{};

  if (slot.size() < kv_slot_bytes) {
    return Error::invalid_argument;
  }
  if (slot[4] == kv_state_empty) {
    return Error::none;  // 空槽：魔数都没写过，不能按损坏算
  }
  if (slot[4] != kv_state_used || !internal::has_magic(slot.data())) {
    return Error::corrupt_data;
  }

  const std::size_t key_len = slot[5];
  const std::size_t value_len = internal::read_u16(slot.data() + 6);
  if (key_len == 0 || key_len > persistence_max_key_bytes ||
      value_len > persistence_max_value_bytes) {
    return Error::corrupt_data;
  }

  const auto key =
      etl::string_view{reinterpret_cast<const char*>(slot.data() + kv_header_size), key_len};
  const auto value = etl::span<const std::uint8_t>(
      slot.data() + kv_header_size + persistence_max_key_bytes, value_len);
  if (internal::slot_crc(key, value) != internal::read_u32(slot.data() + 8)) {
    return Error::corrupt_data;
  }

  out.used = true;
  out.key = key;
  out.value = value;
  return Error::none;
}

}  // namespace embark::detail

#endif /* EMBARK_DETAIL_KV_SLOT_H */
