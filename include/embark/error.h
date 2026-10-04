/**
 * Embark 统一错误码（spec §9）
 *
 * 约定：可失败操作返回 etl::expected<T, Error>（没有返回值时直接返回 Error）；
 * 不抛异常（内核 -fno-exceptions）、不借用 errno、不靠"返回 -1"这种重载手法。
 *
 * 名字只有一份：枚举用 efmt 的 E_FMT_DERIVE_ENUM 声明，取值名在编译期从这份声明
 * 文本里推出来，所以日志里直接写占位符就行：
 *
 *   ELOG_WARN("显示刷新失败：{}", error);   // 打出 Error::io_failure
 *
 * 本头文件因此不再提供 to_string(Error)：那条老路要手抄一份名字表，加取值时必然会漏。
 * 只有 fprintf / fatal 这种"只吃 const char*"的出口才需要字符串，用 efmt 的 format_to
 * 写到栈上缓冲即可（范例见 platform/host/ui_demo.cpp 的 HAL 初始化失败分支）：
 *
 *   char text[24];
 *   e_fmt::format_to(text, sizeof(text), "{}", error);
 *
 * 想让日志里不带类型名（省 ~1.3 KB Flash）编真机时加 -DEFMT_DERIVE_SHOW_TYPE=0，
 * 打印结果就从 Error::io_failure 变成 io_failure。
 */
#ifndef EMBARK_ERROR_H
#define EMBARK_ERROR_H

#include <cstdint>

#include <middleware/etl/expected.h>
#include <middleware/efmt/core/format.hpp>

namespace embark {

E_FMT_DERIVE_ENUM(enum class Error
                  : std::uint8_t{
                      none = 0,   ///< 成功（expected 里有值时不看它）
                      not_ready,  ///< 后端未初始化，或设备还没准备好
                      invalid_argument,  ///< 参数不合法：坐标越界、键为空/超长、地址非法
                      not_found,   ///< 键不存在；或设备未接
                      no_space,    ///< 放不下：槽位用完、目标缓冲太小、值超上限
                      io_failure,  ///< 读写失败：文件 IO、总线 NACK
                      timeout,     ///< 对端无响应
                      unsupported,  ///< 这个后端不提供该能力（宿主总线就是这种）
                      corrupt_data,  ///< 完整性校验失败：CRC、长度或 magic 不对
                      busy,          ///< 设备/资源忙，稍后重试
                  });

/// 造一个 expected 的失败值。ETL 的 expected 没有 Error 的隐式构造（只认
/// etl::unexpected<E>），所以统一从这里造，省得每处都写一遍模板参数。
[[nodiscard]] inline etl::unexpected<Error> unexpected(Error error) noexcept {
  return etl::unexpected<Error>(error);
}

/// 把错误码写进一块栈上缓冲并返回它 —— 只给 fprintf / fatal 这类"只吃 const char*"
/// 的出口用（名字仍来自枚举声明本身，没有第二份名字表）：
///
///   char text[24];
///   std::fprintf(stderr, "HAL 初始化失败：%s\n", embark::error_text(text, hal_error));
template <std::size_t N>
inline const char* error_text(char (&buffer)[N], Error error) noexcept {
  e_fmt::format_to(buffer, N, "{}", error);
  return buffer;
}

}  // namespace embark

#endif /* EMBARK_ERROR_H */
