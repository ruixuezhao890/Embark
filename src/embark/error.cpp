#include <embark/error.h>

namespace embark {

const char* to_string(Error error) noexcept {
  switch (error) {
    case Error::none:
      return "none";
    case Error::not_ready:
      return "not_ready";
    case Error::invalid_argument:
      return "invalid_argument";
    case Error::not_found:
      return "not_found";
    case Error::no_space:
      return "no_space";
    case Error::io_failure:
      return "io_failure";
    case Error::timeout:
      return "timeout";
    case Error::unsupported:
      return "unsupported";
    case Error::corrupt_data:
      return "corrupt_data";
    case Error::busy:
      return "busy";
  }

  // 走到这里说明 Error 加了新取值却没同步本函数 —— 上面没有 default，GCC 会
  // -Wswitch 提醒；这里兜底返回 "unknown" 而不是断言，免得错误处理本身再炸一次。
  return "unknown";
}

}  // namespace embark
