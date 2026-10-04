/**
 * Embark ESP32-S3 · 日志后端实现（issues/11）
 */
#include "esp32_log_sink.h"

#include <cstdio>

namespace embark::platform::esp32 {

bool Esp32LogSink::init() noexcept {
  return true;  // IDF 已经把 stdout 接到控制台 UART 上了
}

void Esp32LogSink::write(const char* data, std::size_t size) noexcept {
  if (data == nullptr || size == 0U) {
    return;
  }
  // 整行一次交给控制台：先 fwrite 再 fflush —— 致命错误那一条必须落出去，
  // 不能留在 stdio 缓冲里陪葬。
  (void)std::fwrite(data, 1U, size, stdout);
  (void)std::fflush(stdout);
}

void Esp32LogSink::flush() noexcept {
  (void)std::fflush(stdout);
}

}  // namespace embark::platform::esp32
