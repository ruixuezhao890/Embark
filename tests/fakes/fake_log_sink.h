// 测试替身：假日志后端。
//
// 把 elog 送来的整条记录抄进定长缓冲，供断言检查内容；写满之后只计 dropped，
// 保持"日志永不阻塞、永不失败"的实际语义（elog 不看 write 的返回值）。
#pragma once

#include <embark/hal/log_sink.h>
#include <embark_limits.h>

#include <cstddef>

#include <middleware/etl/string_view.h>

namespace embark::fakes {

class FakeLogSink final : public hal::ILogSink {
 public:
  FakeLogSink() noexcept = default;

  bool init() noexcept override {
    ++init_calls;
    ready = init_result;
    return init_result;
  }

  void write(const char* data, std::size_t size) noexcept override {
    ++write_calls;
    total_requested += size;
    if (data == nullptr) {
      return;
    }
    for (std::size_t i = 0; i < size; ++i) {
      if (length < buffer.size()) {
        buffer[length] = data[i];
        ++length;
      } else {
        ++dropped;
      }
    }
  }

  void flush() noexcept override { ++flush_calls; }

  [[nodiscard]] etl::string_view text() const noexcept {
    return etl::string_view(buffer.data(), length);
  }

  [[nodiscard]] bool contains(etl::string_view needle) const noexcept {
    return text().find(needle) != etl::string_view::npos;
  }

  void clear() noexcept {
    length = 0;
    dropped = 0;
  }

  // 旋钮与观测点。
  bool init_result = true;
  bool ready = false;
  std::size_t init_calls = 0;
  std::size_t write_calls = 0;
  std::size_t flush_calls = 0;
  std::size_t total_requested = 0;
  std::size_t dropped = 0;
  etl::array<char, fake_log_capture_bytes> buffer{};
  std::size_t length = 0;
};

}  // namespace embark::fakes
