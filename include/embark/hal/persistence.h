/**
 * HAL · 持久化（spec §8）
 *
 * 只放"小对象"：校准值、上次停留的页面、开关位。键值都有编译期上限
 * （config/embark_limits.h），宿主后端与测试替身共用 detail/kv_slot.h 的定长槽，
 * 因此两边的成功/失败语义一致（not_found / no_space / corrupt_data）。
 */
#ifndef EMBARK_HAL_PERSISTENCE_H
#define EMBARK_HAL_PERSISTENCE_H

#include <cstddef>

#include <embark/error.h>
#include <middleware/etl/expected.h>
#include <middleware/etl/span.h>
#include <middleware/etl/string_view.h>

namespace embark::hal {

class IPersistence {
 public:
  virtual ~IPersistence() = default;

  /// 打开工作区；首次使用会建好存储。存储本身损坏 → Error::corrupt_data，此时
  /// 调用方可以选择 erase_all() 重来（这是"宁可炸得早"与"能恢复出厂"之间的分界）。
  virtual Error init() noexcept = 0;

  /// 读一条：返回写进 out 的字节数。键不存在 → not_found；out 太小 → no_space。
  [[nodiscard]] virtual etl::expected<std::size_t, Error> read(
      etl::string_view key, etl::span<std::uint8_t> out) const noexcept = 0;

  /// 写一条（键已存在就覆盖）。键为空或超长 → invalid_argument；值超上限 → no_space；
  /// 槽位用完 → no_space。
  virtual Error write(etl::string_view key, etl::span<const std::uint8_t> value) noexcept = 0;

  /// 删一条。键不存在 → not_found（不做"静默成功"，否则调用方分不清拼错键和真没有）。
  virtual Error erase(etl::string_view key) noexcept = 0;

  /// 清空全部（恢复出厂）。
  virtual Error erase_all() noexcept = 0;

  /// 可用值的总字节数（= 槽数 × 单条值上限）。
  [[nodiscard]] virtual std::size_t capacity_bytes() const noexcept = 0;
};

}  // namespace embark::hal

#endif /* EMBARK_HAL_PERSISTENCE_H */
