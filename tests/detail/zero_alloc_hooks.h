// 测试侧全局分配钩子（零分配审计用，spec §14.4 / issue 09）。
//
// 仅统计、不拦截：重载全局 operator new/delete（含数组与 C++17 对齐版），
// 计数放在 BSS（进程启动时归零）。审计用例在自己范围内
// reset → 跑路径 → 取数；其他用例 / doctest 自身的分配不影响审计
// （审计用例只认自己 reset 之后的计数）。
//
// 注意：
// - 钩子定义在 zero_alloc_hooks.cpp，全程序链接生效（任何 TU 的 new 表达式
//   都解析到本定义，与是否 include 本头无关）；头文件只暴露观测接口。
// - malloc 失败会抛 std::bad_alloc —— 审计可执行文件编译时【不带】
//   -fno-exceptions；内核 TU 的零分配正是本审计保证的内容，不会走到失败分支。
#pragma once

#include <cstdint>

namespace embark::test {

/// 计数清零（审计用例开头调用）。
void reset_global_allocation_counters() noexcept;

/// 自 reset 以来的 operator new/new[] 调用次数。
[[nodiscard]] std::uint32_t global_allocation_count() noexcept;

}  // namespace embark::test