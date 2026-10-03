/**
 * 断言与致命错误（spec §9）
 *
 * 两条必须记住的约定：
 *   1. EMBARK_ASSERT 在 release 下【依然生效】。release 的行为是"尽力写最后一条日志 → halt"，
 *      不是静默继续，所以这里没有 NDEBUG 分支（spec §9 的原话：不静默继续）。
 *   2. assert_failed() / fatal() 由【平台 target】定义，链接期唯一：
 *      宿主在 platform/host/host_system.cpp，测试在 tests/fakes/fakes.cpp。
 *      这是"后端编译期选定、运行期不查表"的一部分（spec §8）。
 */
#ifndef EMBARK_DIAGNOSTICS_H
#define EMBARK_DIAGNOSTICS_H

namespace embark {

/// 编程错误（按契约不可能发生的情况）。实现必须：尽力写一条日志 → halt，不返回。
[[noreturn]] void assert_failed(const char* file, int line, const char* expression) noexcept;

/// 致命错误：写日志后由平台后端决定重启还是进安全态（spec §9）。实现不返回。
[[noreturn]] void fatal(const char* reason) noexcept;

}  // namespace embark

/// 断言：表达式为假就落到平台的 assert_failed。
#define EMBARK_ASSERT(expression)                                                     \
  ((expression) ? static_cast<void>(0)                                                \
                : ::embark::assert_failed(__FILE__, __LINE__, #expression))

#endif /* EMBARK_DIAGNOSTICS_H */
