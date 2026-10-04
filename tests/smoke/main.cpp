/**
 * 骨架冒烟测试（issue 03 的验收项）
 *
 * 这一份测的不是功能，而是"构建链路是通的"：middleware 视图、ETL 定容容器、
 * efmt/elog、doctest、CTest。真正的框架测试（前后台切换、消息溢出、零分配审计…）
 * 按 issues/09 补。
 *
 * 测试可执行文件不链 embark_kernel_flags（保持异常开启），原因见 tests/CMakeLists.txt。
 */
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <embark/version.h>
#include <middleware/etl/string_view.h>
#include <middleware/etl/vector.h>
#include <middleware/etl/version.h>
#include <middleware/efmt/core/format.hpp>
#include <middleware/elog/elog.hpp>

TEST_CASE("版本字符串由构建系统注入") {
  const etl::string_view version(embark::version_string());

  // 兜底值说明 CMake 的编译定义没传进来（见 src/embark/version.cpp）
  CHECK(version != etl::string_view("0.0.0-unknown"));

  // 形如 0.1.0：只由数字与点组成、恰好两个点 —— 免得断言里再抄一份版本号
  int dots = 0;
  for (const char c : version) {
    if (c == '.') {
      ++dots;
    } else {
      CHECK(c >= '0');
      CHECK(c <= '9');
    }
  }
  CHECK(dots == 2);
}

TEST_CASE("平台名不是兜底值") {
  const etl::string_view platform(embark::platform_name());
  CHECK(platform == etl::string_view("host"));
}

TEST_CASE("ETL 定容容器：容量固定、零堆") {
  etl::vector<int, 4> numbers;
  CHECK(numbers.empty());
  CHECK(numbers.capacity() == 4u);

  numbers.push_back(2);
  numbers.push_back(3);
  REQUIRE(numbers.size() == 2u);
  CHECK(numbers[0] == 2);
  CHECK(numbers.back() == 3);
}

/* 这里原本还有一条「ETL 版本 == 20.40.0」的用例，已删：版本基线由
 * config/embark_config.h 的 static_assert 守着（强制包含进每个 TU），编译期就拦下来，
 * 运行期再抄一遍数字只会在换版本时制造第二处要改的地方（code-review 指出的重复）。 */

TEST_CASE("elog + efmt 在测试目标里也能用") {
  e_log::logger* const logger =
      e_log::create_logger("embark-test", e_log::stdout_sink(), e_log::level::debug);
  REQUIRE(logger != nullptr);

  ELOG_INFO("冒烟：elog 与 efmt 链路正常（Embark {}）", embark::version_string());
}
