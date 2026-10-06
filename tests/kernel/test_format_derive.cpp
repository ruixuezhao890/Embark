// efmt 派生打印（issue 13）：登记过的框架类型整对象打印成什么样。
//
// 这一组用例钉的是"日志里长什么样"：加字段 / 改名字 / 换 efmt 版本导致格式漂移时，
// 它会先叫起来。两条约定写在 docs/common-pitfalls.md 的"派生打印"一节：
//   1) 类型名与成员名来自 E_FMT_DERIVE(_ENUM)，日志里必然带全名（Rust Debug 风格）；
//   2) 1 字节整型成员会被派生打印当字符输出（上游行为），框架的字段因此都是 2 字节起。
#include <doctest/doctest.h>

#include <cstring>
#include <string_view>

#include <embark/app.h>
#include <embark/error.h>
#include <embark/hal/types.h>
#include <embark/message.h>

namespace {

/// 走 efmt 打一份出来（模板参数自动推导，省得每个断言都写一遍 format_to）。
/// format_to 是 snprintf 语义：返回的是"完整长度"，超缓冲时按缓冲截断。
template <typename T, std::size_t N>
std::string_view render(char (&buffer)[N], const T& value) {
  const std::size_t used = e_fmt::format_to(buffer, N, "{}", value);
  const std::size_t visible = used < N ? used : (N - 1U);
  return std::string_view(buffer, visible);
}

}  // namespace

TEST_CASE("派生打印：错误码的名字表就在枚举声明上（没有 to_string）") {
  char buffer[64];

  CHECK(render(buffer, embark::Error::none) == "embark::Error::none");
  CHECK(render(buffer, embark::Error::not_found) == "embark::Error::not_found");
  CHECK(render(buffer, embark::Error::unsupported) == "embark::Error::unsupported");
}

TEST_CASE("派生打印：error_text 给 fprintf/fatal 这类只吃 const char* 的出口") {
  char buffer[64];

  CHECK(std::strcmp(embark::error_text(buffer, embark::Error::io_failure),
                    "embark::Error::io_failure") == 0);
}

TEST_CASE("派生打印：枚举整条打名字") {
  char buffer[96];

  CHECK(render(buffer, embark::BackgroundPolicy::tick) == "embark::BackgroundPolicy::tick");
  CHECK(render(buffer, embark::ArmPolicy::at_boot) == "embark::ArmPolicy::at_boot");
  CHECK(render(buffer, embark::hal::PixelFormat::rgb565) == "embark::hal::PixelFormat::rgb565");
  CHECK(render(buffer, embark::hal::InputEventKind::press) == "embark::hal::InputEventKind::press");
}

TEST_CASE("派生打印：结构体逐成员打（嵌套枚举也带名字）") {
  char buffer[192];

  const embark::hal::Rect rect{1, 2, 3, 4};
  CHECK(render(buffer, rect) == "embark::hal::Rect { x = 1, y = 2, width = 3, height = 4 }");

  const embark::hal::DisplayInfo info{320, 240, embark::hal::PixelFormat::rgb565, 0};
  CHECK(render(buffer, info) ==
        "embark::hal::DisplayInfo { width = 320, height = 240, format = "
        "embark::hal::PixelFormat::rgb565, stride_bytes = 0 }");
}

TEST_CASE("派生打印：数值字段照整数打（1 字节成员会被当字符打，框架已避让）") {
  char buffer[256];

  const embark::hal::InputEvent event{embark::hal::InputEventKind::press, 160, 170, 113, 7};
  CHECK(render(buffer, event) ==
        "embark::hal::InputEvent { kind = embark::hal::InputEventKind::press, x = 160, y = 170, "
        "key = 113, timestamp_ms = 7 }");

  // 4 字段聚合初始化照旧可用（issue 24 的新字段有默认值 on_first_enter）——
  // 日志里 arm 必须打全名，且是后台策略里最容易看错的一个字段，所以钉死。
  const embark::AppSettings settings{embark::BackgroundPolicy::own_task, 50, 256, 4};
  CHECK(render(buffer, settings) ==
        "embark::AppSettings { background = embark::BackgroundPolicy::own_task, period_ms = 50, "
        "task_stack_words = 256, task_priority = 4, arm = embark::ArmPolicy::on_first_enter }");

  const embark::CrossTaskMessage envelope{1, 7};
  CHECK(render(buffer, envelope) == "embark::CrossTaskMessage { from_app = 1, seq = 7 }");
}
