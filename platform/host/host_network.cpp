#include "host_network.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include <middleware/elog/elog.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace embark::platform::host {
namespace {

#ifdef _WIN32
using socket_handle = SOCKET;
constexpr socket_handle kInvalidSocket = INVALID_SOCKET;
#else
using socket_handle = int;
constexpr socket_handle kInvalidSocket = -1;
#endif

/// 各阶段的超时（连接 / 等响应）。宿主上没人在意多等一秒，但**不能永远卡住**：
/// 验收脚本跑在别人的机器上，DNS 黑洞会把一次点击变成一场挂机。
constexpr int kConnectTimeoutSec = 5;
constexpr int kRecvTimeoutSec = 5;

/// 响应总量上限。正常 API 响应是几 KB；超过这个数说明要么对端疯了，要么我们在下大文件
/// —— 两种情况都不该由「把响应塞进调用方给的缓冲」这条路处理。
constexpr std::size_t kMaxResponseBytes = 64U * 1024U;

void close_socket(socket_handle handle) noexcept {
#ifdef _WIN32
  ::closesocket(handle);
#else
  ::close(handle);
#endif
}

bool would_block() noexcept {
#ifdef _WIN32
  const int error = ::WSAGetLastError();
  return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
  return errno == EINPROGRESS || errno == EWOULDBLOCK;
#endif
}

/// 一份最小 URL 拆分：只看「http://host[:port]/path」。别的 scheme 一律 unsupported。
struct ParsedUrl {
  std::string host;
  std::string port;
  std::string path;
};

bool starts_with_ci(const std::string& text, const char* prefix) {
  const std::size_t length = std::strlen(prefix);
  if (text.size() < length) {
    return false;
  }
  for (std::size_t i = 0; i < length; ++i) {
    const auto a = static_cast<unsigned char>(text[i]);
    const auto b = static_cast<unsigned char>(prefix[i]);
    if (std::tolower(a) != std::tolower(b)) {
      return false;
    }
  }
  return true;
}

etl::expected<ParsedUrl, Error> parse_url(const char* url) {
  if (url == nullptr || *url == '\0') {
    return unexpected(Error::invalid_argument);
  }
  const std::string text(url);
  if (!starts_with_ci(text, "http://")) {
    // https:// 与 ws:// 都走这条：本层只说明文 HTTP（见 include/embark/hal/network.h）。
    return unexpected(Error::unsupported);
  }

  const std::string rest = text.substr(7);
  const std::size_t slash = rest.find('/');
  const std::string authority = (slash == std::string::npos) ? rest : rest.substr(0, slash);
  ParsedUrl parsed;
  parsed.path = (slash == std::string::npos) ? "/" : rest.substr(slash);

  if (authority.empty()) {
    return unexpected(Error::invalid_argument);
  }
  const std::size_t colon = authority.find(':');
  if (colon == std::string::npos) {
    parsed.host = authority;
    parsed.port = "80";
  } else {
    parsed.host = authority.substr(0, colon);
    parsed.port = authority.substr(colon + 1);
    if (parsed.host.empty() || parsed.port.empty()) {
      return unexpected(Error::invalid_argument);
    }
  }
  return parsed;
}

/// 等一个 socket 变成「可读 / 可写」。返回 false = 超时或出错（两者在这里不用区分：
/// 上层把「没等到」一律报 timeout，因为调用方能做的事是一样的）。
bool wait_for(socket_handle handle, bool want_write, int timeout_sec) noexcept {
  fd_set set;
  FD_ZERO(&set);
  FD_SET(handle, &set);
  timeval tv{};
  tv.tv_sec = timeout_sec;
  tv.tv_usec = 0;
  const int ready =
      ::select(static_cast<int>(handle) + 1, want_write ? nullptr : &set, want_write ? &set : nullptr,
               nullptr, &tv);
  return ready > 0;
}

/// 把 chunked 编码解成裸 body（响应头里带了 chunked 才走这里）。不做这个会有个很隐蔽的
/// 后果：真机的 HTTP 客户端替你解，宿主不解 —— 同一份 App 逻辑在两边拿到的字节就不一样了。
etl::expected<std::string, Error> decode_chunked(const std::string& raw) {
  std::string out;
  std::size_t pos = 0;
  while (pos < raw.size()) {
    const std::size_t line_end = raw.find("\r\n", pos);
    if (line_end == std::string::npos) {
      return unexpected(Error::corrupt_data);
    }
    const std::string size_text = raw.substr(pos, line_end - pos);
    std::size_t size = 0;
    for (const char ch : size_text) {
      int digit = -1;
      if (ch >= '0' && ch <= '9') {
        digit = ch - '0';
      } else if (ch >= 'a' && ch <= 'f') {
        digit = ch - 'a' + 10;
      } else if (ch >= 'A' && ch <= 'F') {
        digit = ch - 'A' + 10;
      } else if (ch == ';') {
        break;  // chunk 扩展（很少见），忽略剩余部分
      } else {
        return unexpected(Error::corrupt_data);
      }
      size = size * 16U + static_cast<std::size_t>(digit);
    }
    pos = line_end + 2;
    if (size == 0U) {
      return out;  // 末块
    }
    if (pos + size > raw.size()) {
      return unexpected(Error::corrupt_data);
    }
    out.append(raw, pos, size);
    pos += size + 2;  // 跳过块尾的 CRLF
  }
  return out;
}

/// 把一段原始响应拆成「状态码 + body」。只认我们需要的那个头（chunked）。
etl::expected<std::string, Error> split_response(const std::string& raw, int& status) {
  const std::size_t header_end = raw.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    return unexpected(Error::corrupt_data);
  }

  const std::string head = raw.substr(0, header_end);
  const std::size_t line_end = head.find("\r\n");
  const std::string status_line = (line_end == std::string::npos) ? head : head.substr(0, line_end);
  // "HTTP/1.1 200 OK" → 200
  const std::size_t space = status_line.find(' ');
  if (space == std::string::npos) {
    return unexpected(Error::corrupt_data);
  }
  status = std::atoi(status_line.c_str() + space + 1);

  std::string lower = head;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });

  const std::string body = raw.substr(header_end + 4);
  if (lower.find("transfer-encoding: chunked") != std::string::npos) {
    return decode_chunked(body);
  }
  return body;
}

}  // namespace

HostNetwork::~HostNetwork() {
#ifdef _WIN32
  if (stack_ready_) {
    ::WSACleanup();
  }
#endif
}

Error HostNetwork::init() noexcept {
#ifdef _WIN32
  if (!stack_ready_) {
    WSADATA data{};
    if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      ELOG_WARN("网络：WSAStartup 失败 —— 本机没有可用的网络服务");
      return Error::unsupported;
    }
  }
#endif
  stack_ready_ = true;
  return Error::none;
}

HostNetwork::NetState HostNetwork::state() const noexcept { return state_; }

const std::string& HostNetwork::ssid() const noexcept { return ssid_; }

Error HostNetwork::connect(const char* ssid, const char* password) noexcept {
  (void)password;
  if (!stack_ready_) {
    return Error::not_ready;
  }
  if (ssid == nullptr || *ssid == '\0') {
    return Error::invalid_argument;
  }
  // 宿主上「拨号」没有对端：恒成功。故意不探测外网可达性 —— 那会把「本机能不能上网」
  // 这种环境噪音混进状态机，而 App 想验的是「连上之后该怎么走」。
  state_ = NetState::connecting;
  ssid_ = ssid;
  state_ = NetState::connected;
  ELOG_INFO("网络：已连接 {}（宿主直通 socket，无真实拨号）", ssid_.c_str());
  return Error::none;
}

Error HostNetwork::disconnect() noexcept {
  state_ = NetState::off;
  ssid_.clear();
  return Error::none;
}

int HostNetwork::last_http_status() const noexcept { return http_status_; }

Error HostNetwork::sntp_sync(std::uint32_t timeout_ms) noexcept {
  (void)timeout_ms;
  // 宿主有时钟，不需要对时；返回 unsupported 而不是「假装成功」—— 假装成功会让 App 以为
  // 时间是被网络校准过的。
  return Error::unsupported;
}

Error HostNetwork::http_get(const char* url, etl::span<char> body_out,
                            std::size_t& body_len) noexcept {
  body_len = 0;
  http_status_ = 0;

  if (state_ != NetState::connected) {
    return Error::not_ready;
  }
  const auto parsed_result = parse_url(url);
  if (!parsed_result.has_value()) {
    return parsed_result.error();
  }
  const ParsedUrl& parsed = parsed_result.value();

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* resolved = nullptr;
  if (::getaddrinfo(parsed.host.c_str(), parsed.port.c_str(), &hints, &resolved) != 0 ||
      resolved == nullptr) {
    ELOG_WARN("网络：DNS 解析失败 {}", parsed.host.c_str());
    return Error::not_found;
  }

  socket_handle sock = kInvalidSocket;
  Error connect_result = Error::io_failure;
  for (addrinfo* it = resolved; it != nullptr; it = it->ai_next) {
    sock = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (sock == kInvalidSocket) {
      continue;
    }
    // 非阻塞连 + select：阻塞 connect 在某些网络下会卡到 TCP 自己的超时（75 s 起）。
#ifdef _WIN32
    u_long non_blocking = 1;
    ::ioctlsocket(sock, FIONBIO, &non_blocking);
#else
    const int flags = ::fcntl(sock, F_GETFL, 0);
    ::fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
    const int rc = ::connect(sock, it->ai_addr, static_cast<int>(it->ai_addrlen));
    if (rc == 0) {
      connect_result = Error::none;
    } else if (would_block() && wait_for(sock, true, kConnectTimeoutSec)) {
      // select 说可写：还要问一次 SO_ERROR 才知道是连上还是连失败。
      int socket_error = 0;
      socklen_t length = sizeof(socket_error);
      ::getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socket_error), &length);
      connect_result = (socket_error == 0) ? Error::none : Error::io_failure;
    } else {
      connect_result = Error::timeout;
    }

    if (connect_result == Error::none) {
      // 连上之后恢复阻塞模式：后面按「有数据就读」的简单循环来。
#ifdef _WIN32
      u_long blocking = 0;
      ::ioctlsocket(sock, FIONBIO, &blocking);
#else
      const int flags2 = ::fcntl(sock, F_GETFL, 0);
      ::fcntl(sock, F_SETFL, flags2 & ~O_NONBLOCK);
#endif
      break;
    }
    close_socket(sock);
    sock = kInvalidSocket;
  }
  ::freeaddrinfo(resolved);

  if (connect_result != Error::none) {
    ELOG_WARN("网络：连不上 {}:{} —— 本机没网 / 被防火墙拦 / 对端不在", parsed.host.c_str(),
              parsed.port.c_str());
    return connect_result;
  }

  const std::string request = "GET " + parsed.path + " HTTP/1.1\r\nHost: " + parsed.host +
                              "\r\nUser-Agent: embark-host\r\nAccept: */*\r\nConnection: close\r\n\r\n";
  std::size_t sent = 0;
  while (sent < request.size()) {
    const int written =
        ::send(sock, request.data() + sent, static_cast<int>(request.size() - sent), 0);
    if (written <= 0) {
      close_socket(sock);
      return Error::io_failure;
    }
    sent += static_cast<std::size_t>(written);
  }

  std::string raw;
  char chunk[1024];
  Error read_result = Error::none;
  while (true) {
    if (!wait_for(sock, false, kRecvTimeoutSec)) {
      read_result = Error::timeout;
      break;
    }
    const int got = ::recv(sock, chunk, static_cast<int>(sizeof(chunk)), 0);
    if (got == 0) {
      break;  // 对端关了连接：Connection: close 下的正常结束
    }
    if (got < 0) {
      read_result = Error::io_failure;
      break;
    }
    raw.append(chunk, static_cast<std::size_t>(got));
    if (raw.size() > kMaxResponseBytes) {
      read_result = Error::io_failure;
      break;
    }
  }
  close_socket(sock);

  if (read_result != Error::none) {
    ELOG_WARN("网络：读响应失败 / 超时（已收 {} 字节）", raw.size());
    return read_result;
  }

  const auto body_result = split_response(raw, http_status_);
  if (!body_result.has_value()) {
    return body_result.error();
  }
  const std::string& body = body_result.value();
  if (body.size() > body_out.size()) {
    // 一个字节都不写：调用方拿到的 body_len 必须是「能安全读的长度」。
    ELOG_WARN("网络：响应体 {} 字节超过调用方缓冲 {} 字节（no_space）", body.size(), body_out.size());
    return Error::no_space;
  }
  if (!body.empty()) {
    std::memcpy(body_out.data(), body.data(), body.size());
  }
  body_len = body.size();
  return Error::none;
}

}  // namespace embark::platform::host
