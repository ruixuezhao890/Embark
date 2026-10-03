/**
 * Embark 统一错误码（spec §9）
 *
 * 约定：可失败操作返回 etl::expected<T, Error>（没有返回值时直接返回 Error）；
 * 不抛异常（内核 -fno-exceptions）、不借用 errno、不靠"返回 -1"这种重载手法。
 * 取值集合在 issue 04 定稿，之后新增取值要同时更新 to_string()。
 */
#ifndef EMBARK_ERROR_H
#define EMBARK_ERROR_H

#include <cstdint>

#include <middleware/etl/expected.h>

namespace embark {

enum class Error : std::uint8_t {
  none = 0,          ///< 成功（expected 里有值时不看它）
  not_ready,         ///< 后端未初始化，或设备还没准备好
  invalid_argument,  ///< 参数不合法：坐标越界、键为空/超长、地址非法
  not_found,         ///< 键不存在；或设备未接
  no_space,          ///< 放不下：槽位用完、目标缓冲太小、值超上限
  io_failure,        ///< 读写失败：文件 IO、总线 NACK
  timeout,           ///< 对端无响应
  unsupported,       ///< 这个后端不提供该能力（宿主总线就是这种）
  corrupt_data,      ///< 完整性校验失败：CRC、长度或 magic 不对
  busy,              ///< 设备/资源忙，稍后重试
};

/// 稳定短名（日志与测试断言用；不是给用户看的文案）。
const char* to_string(Error error) noexcept;

/// 造一个 expected 的失败值。ETL 的 expected 没有 Error 的隐式构造（只认
/// etl::unexpected<E>），所以统一从这里造，省得每处都写一遍模板参数。
[[nodiscard]] inline etl::unexpected<Error> unexpected(Error error) noexcept {
  return etl::unexpected<Error>(error);
}

}  // namespace embark

#endif /* EMBARK_ERROR_H */
