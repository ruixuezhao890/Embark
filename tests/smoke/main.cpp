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

#include <elog/elog.hpp>
#include <embark/version.h>
#include <middleware/efmt/core/format.hpp>
#include <middleware/etl/string_view.h>
#include <middleware/etl/vector.h>
#include <middleware/etl/version.h>

TEST_CASE("版本字符串由构建系统给出") {
  const etl::string_view version(embark::version_string());
  CHECK_FALSE(version.empty());
  CHECK(version == etl::string_view(EMBARK_VERSION_STRING));
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

TEST_CASE("ETL 版本符合 spec §16 的核实基线") {
  CHECK(ETL_VERSION_MAJOR == 20);
  CHECK(ETL_VERSION_MINOR == 40);
}

TEST_CASE("elog + efmt 在测试目标里也能用") {
  e_log::logger* const logger =
          e_log::create_logger("embark-test", e_log::stdout_sink(), e_log::level::debug);
  REQUIRE(logger != nullptr);

  ELOG_INFO("冒烟：elog 与 efmt 链路正常（Embark {}）", embark::version_string());
}
