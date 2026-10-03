#include "host_log_sink.h"

namespace embark::platform::host {

bool HostLogSink::init() noexcept {
  return true;  // stderr 总是可用
}

void HostLogSink::write(const char* data, std::size_t size) noexcept {
  if (data == nullptr || size == 0) {
    return;
  }
  std::fwrite(data, 1, size, stderr);
}

void HostLogSink::flush() noexcept {
  std::fflush(stderr);
}

}  // namespace embark::platform::host
