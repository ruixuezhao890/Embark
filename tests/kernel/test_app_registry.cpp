// App 注册表（issues/06）：编译期表、名字查找、越界防御、单 App 也能建。
#include <doctest/doctest.h>

#include <middleware/etl/string_view.h>

#include <embark/app.h>
#include <embark/app_registry.h>

using embark::App;
using embark::AppId;
using embark::AppRegistry;
using embark::app_registry;
using embark::invalid_app_id;

namespace {

// 测试用的两个最小 App（不需要任何行为，只用名字与身份）。
class AlphaApp final : public App {
 public:
  [[nodiscard]] const char* name() const override { return "alpha"; }
  void onCreate(embark::Framework&) override {}
  void onEnter() override {}
  void onPause() override {}
  void onResume() override {}
  void onExit() override {}
};

class BetaApp final : public App {
 public:
  [[nodiscard]] const char* name() const override { return "beta"; }
  void onCreate(embark::Framework&) override {}
  void onEnter() override {}
  void onPause() override {}
  void onResume() override {}
  void onExit() override {}
};

bool same_name(const App* app, const char* expected) {
  return app != nullptr && etl::string_view(app->name()) == expected;
}

}  // namespace

TEST_CASE("AppRegistry：注册顺序即前台优先级（第 0 个是默认前台）") {
  const AppRegistry registry = app_registry<AlphaApp, BetaApp>();

  REQUIRE(registry.size() == 2U);
  CHECK(same_name(registry.at(0), "alpha"));
  CHECK(same_name(registry.at(1), "beta"));
}

TEST_CASE("AppRegistry：名字查找、id_of、有效性检查、越界防御") {
  const AppRegistry registry = app_registry<AlphaApp, BetaApp>();

  CHECK(registry.find("alpha") == 0U);
  CHECK(registry.find("beta") == 1U);
  CHECK(registry.find("nope") == invalid_app_id);
  CHECK(registry.find("") == invalid_app_id);

  CHECK(registry.valid(0U));
  CHECK(registry.valid(1U));
  CHECK_FALSE(registry.valid(2U));
  CHECK_FALSE(registry.valid(invalid_app_id));

  // 越界取 = 空指针，不是未定义行为
  CHECK(registry.at(2) == nullptr);
  CHECK(registry.at(99) == nullptr);

  const App* alpha = registry.at(0);
  REQUIRE(alpha != nullptr);
  CHECK(registry.id_of(*alpha) == 0U);
}

TEST_CASE("AppRegistry：单个 App 也能建表，表长上限受 embark::max_apps 约束") {
  const AppRegistry single = app_registry<AlphaApp>();
  CHECK(single.size() == 1U);
  CHECK(single.valid(0U));
  CHECK_FALSE(single.valid(1U));
  CHECK(same_name(single.at(0), "alpha"));

  const AppRegistry registry = app_registry<AlphaApp, BetaApp>();
  CHECK(registry.size() <= embark::max_apps);
}