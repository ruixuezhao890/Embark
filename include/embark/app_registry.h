/**
 * Embark · App 编译期注册表（issues/06）
 *
 * spec §5：多个 App 是逻辑模块，编译期静态注册、零堆、注册顺序即默认前台。
 *
 * 用在哪里（在 main / 平台装配点写一次）：
 *
 *     #include <embark/app_registry.h>
 *     EMBARK_APP_TABLE(CounterApp, SwitchApp)   // 整个可执行文件只许出现一次；
 *                                               // 展开为 inline 函数 embark_apps()
 *
 * 然后 Framework 用注册表装配：embark::Framework fw(hal, embark_apps(), ui_port)。
 *
 * 实现是"指针表 + 计数"：AppRegistry 只是 const 视图（const App* const*，不拥有、
 * 不搬运），实例由 detail::app_instance<App>() 的静态存储持有 —— 每个 App 类型
 * 恰好一个实例，跨编译单元唯一（local static + inline 函数），无初始化顺序问题。
 * 表长在编译期由参数个数决定，超过 embark::max_apps 会在编译期报错。
 */
#ifndef EMBARK_APP_REGISTRY_H
#define EMBARK_APP_REGISTRY_H

#include <cstddef>
#include <cstring>

#include <embark/app.h>
#include <embark_limits.h>

namespace embark {

/// 注册表的只读视图：指针数组 + 个数（都不拥有）。数组生命周期 = 进程。
class AppRegistry {
 public:
  constexpr AppRegistry() noexcept = default;

  constexpr AppRegistry(App* const* apps, std::size_t count) noexcept
      : apps_(apps), count_(count) {}

  [[nodiscard]] constexpr std::size_t size() const noexcept { return count_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return count_ == 0U; }

  /// 越界返回 nullptr（不要拿 [] 越界：没有检查）。
  [[nodiscard]] App* at(std::size_t index) const noexcept {
    return index < count_ ? apps_[index] : nullptr;
  }

  [[nodiscard]] App* operator[](std::size_t index) const noexcept {
    return apps_[index];
  }

  /// 下标是不是合法 AppId。
  [[nodiscard]] constexpr bool valid(AppId id) const noexcept {
    return id < count_;
  }

  /// 按名字查（线性扫，App 数 ≤ max_apps，无所谓）；找不到返回 invalid_app_id。
  [[nodiscard]] AppId find(const char* name) const noexcept {
    if (name == nullptr) {
      return invalid_app_id;
    }
    for (std::size_t index = 0; index < count_; ++index) {
      if (apps_[index] != nullptr &&
          std::strcmp(apps_[index]->name(), name) == 0) {
        return static_cast<AppId>(index);
      }
    }
    return invalid_app_id;
  }

  /// 反查：某个 App 实例的编号（不在注册表里返回 invalid_app_id）。
  [[nodiscard]] AppId id_of(const App& app) const noexcept {
    for (std::size_t index = 0; index < count_; ++index) {
      if (apps_[index] == &app) {
        return static_cast<AppId>(index);
      }
    }
    return invalid_app_id;
  }

 private:
  App* const* apps_ = nullptr;
  std::size_t count_ = 0;
};

namespace detail {

/// 每个 App 类型恰好一个进程级实例（local static + inline：跨 TU 唯一）。
template <typename T>
[[nodiscard]] T& app_instance() noexcept {
  static T instance;
  return instance;
}

}  // namespace detail

/// 从类型表造注册表。App 数编译期固定；超过 max_apps 编译失败。
template <typename... Apps>
[[nodiscard]] AppRegistry app_registry() noexcept {
  static_assert(sizeof...(Apps) <= embark::max_apps,
                "App 太多了：超过 config/embark_limits.h 的 max_apps，先想清楚要不要堆上放");
  // 0 个 App 时零长数组不合法，用 1 元素占位（count 仍然为 0）。
  static App* const table[sizeof...(Apps) == 0U ? 1U : sizeof...(Apps)] = {
      static_cast<App*>(&detail::app_instance<Apps>())...};
  return AppRegistry(table, sizeof...(Apps));
}

/// 单处声明 App 表：展开成 inline 函数 embark_apps()。
/// 整个可执行文件里只能出现一次（否则重定义）。参数顺序 = 注册顺序 = 默认前台顺序。
#define EMBARK_APP_TABLE(...)                                                        \
    [[nodiscard]] inline ::embark::AppRegistry embark_apps() noexcept {              \
        return ::embark::app_registry<__VA_ARGS__>();                                \
    }

}  // namespace embark

#endif /* EMBARK_APP_REGISTRY_H */