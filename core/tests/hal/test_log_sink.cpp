// 日志链路：elog 记录 → LogSinkBinder → hal::ILogSink（外加名字非法/重名的失败路径）。
//
// 关键契约：elog 一条记录会分几次调用 sink（带颜色时 write_with_style 三次 + 换行一次），
// LogSinkBinder 负责攒成整行再一次交付，所以这里既数"整行条数"也数后端被打扰的次数。
#include <doctest/doctest.h>

#include "fakes/fakes.h"

#include <embark/log.h>

using embark::install_logger;
using embark::LogSinkBinder;
using embark::fakes::FakeLogSink;

namespace {

std::size_t record_lines(const FakeLogSink& sink) noexcept {
  std::size_t lines = 0;
  const etl::string_view text = sink.text();
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n') {
      ++lines;
    }
  }
  return lines;
}

}  // namespace

TEST_CASE("LogSinkBinder：一条 ELOG 记录攒成整行，一次交付、一次落盘") {
  FakeLogSink sink;
  CHECK(sink.init());
  CHECK(sink.ready);

  LogSinkBinder binder(sink);
  e_log::logger* logger = install_logger("embark-sink-test", binder, e_log::level::debug);
  REQUIRE(logger != nullptr);
  CHECK(etl::string_view(logger->name()) == etl::string_view("embark-sink-test"));
  CHECK(logger->current_level() == e_log::level::debug);

  ELOG_LOGGER_INFO(*logger, "hello {}", 42);

  CHECK(binder.lines_written() == 1U);
  CHECK(binder.pending_bytes() == 0U);
  CHECK(binder.bytes_written() == sink.length);
  CHECK(sink.write_calls == 1U);
  CHECK(sink.flush_calls == 1U);
  CHECK(record_lines(sink) == 1U);
  CHECK(sink.contains("hello 42"));
}

TEST_CASE("LogSinkBinder：低于当前级别的记录不打扰后端") {
  FakeLogSink sink;
  LogSinkBinder binder(sink);
  e_log::logger* logger = install_logger("embark-sink-level", binder, e_log::level::warn);
  REQUIRE(logger != nullptr);

  ELOG_LOGGER_DEBUG(*logger, "debug-not-expected");
  ELOG_LOGGER_INFO(*logger, "info-not-expected");
  CHECK(sink.write_calls == 0U);
  CHECK(binder.lines_written() == 0U);

  ELOG_LOGGER_ERROR(*logger, "error-expected {}", 7);
  CHECK(sink.write_calls == 1U);
  CHECK(binder.lines_written() == 1U);
  CHECK(sink.contains("error-expected 7"));
  CHECK_FALSE(sink.contains("debug-not-expected"));
}

TEST_CASE("LogSinkBinder：字符缓冲写满只计数丢弃，不炸也不阻塞") {
  FakeLogSink sink;
  LogSinkBinder binder(sink);
  e_log::logger* logger = install_logger("embark-sink-full", binder, e_log::level::debug);
  REQUIRE(logger != nullptr);

  // 冲爆 fake 的采集缓冲（elog 单条记录上限 384 字节，够跑很多条）。
  constexpr std::size_t record_count = 80U;
  for (std::size_t i = 0; i < record_count; ++i) {
    ELOG_LOGGER_INFO(*logger, "filler-record-{} abcdefghijklmnopqrstuvwxyz0123456789", i);
  }

  // 框架这边一条都不能少；后端装不下的部分由后端自己计数（fake 满仓后只累计 dropped，
  // 所以捕获文本是前缀，行数必然少于总条数）。
  CHECK(binder.lines_written() == record_count);
  CHECK(binder.pending_bytes() == 0U);
  CHECK(record_lines(sink) > 0U);
  CHECK(record_lines(sink) < record_count);
  CHECK(sink.length <= sink.buffer.size());
  CHECK(sink.dropped > 0U);
}

TEST_CASE("install_logger：名字非法或重名时返回 nullptr，注册表里仍查得到原 logger") {
  FakeLogSink sink;
  LogSinkBinder binder(sink);

  CHECK(install_logger(nullptr, binder) == nullptr);
  CHECK(install_logger("", binder) == nullptr);

  e_log::logger* first = install_logger("embark-sink-dup", binder);
  REQUIRE(first != nullptr);
  CHECK(install_logger("embark-sink-dup", binder) == nullptr);
  CHECK(e_log::get("embark-sink-dup") == first);
}
