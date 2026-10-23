/**
 * 网络服务状态机单测（issue 10）：宿主后端的「不联网那一半」
 *
 * 刻意**只在离线路径上断言**：状态迁移、参数校验、错误码。真发包的行为由 `--net-test`
 * 验（那是有网机器上的一趟真请求），CI 不该因为「这台机器没有出网」而红。
 *
 * 宿主 `connect()` 恒成功（没有对端可拨），所以这里能纯离线地把整条状态机走完 ——
 * 这正是它值得单测的原因：App 里 `state() == connected` 这类判断两边同形。
 */
#include <cstddef>

#include <doctest/doctest.h>

#include <embark/error.h>
#include <embark/hal/network.h>

#include "host_network.h"

namespace {

using embark::Error;
using embark::hal::NetState;
using embark::platform::host::HostNetwork;

}  // namespace

TEST_CASE("网络：初始状态是 off，且从没发过请求") {
  HostNetwork network;
  REQUIRE(network.init() == Error::none);
  CHECK(network.state() == NetState::off);
  CHECK(network.last_http_status() == 0);
  CHECK(network.ssid().empty());
}

TEST_CASE("网络：init 幂等 —— 再调一次不改状态") {
  HostNetwork network;
  REQUIRE(network.init() == Error::none);
  REQUIRE(network.connect("embark-test", "pw") == Error::none);
  CHECK(network.init() == Error::none);
  CHECK(network.state() == NetState::connected);
}

TEST_CASE("网络：空 ssid / null 被拒，且状态原地不动") {
  HostNetwork network;
  REQUIRE(network.init() == Error::none);

  CHECK(network.connect(nullptr, nullptr) == Error::invalid_argument);
  CHECK(network.connect("", "pw") == Error::invalid_argument);
  CHECK(network.state() == NetState::off);
  CHECK(network.ssid().empty());
}

TEST_CASE("网络：拨号成功进 connected，SSID 记下来；断开回 off") {
  HostNetwork network;
  REQUIRE(network.init() == Error::none);

  REQUIRE(network.connect("embark-test", "pw") == Error::none);
  CHECK(network.state() == NetState::connected);
  CHECK(network.ssid() == "embark-test");

  CHECK(network.disconnect() == Error::none);
  CHECK(network.state() == NetState::off);
  CHECK(network.ssid().empty());

  // 断开是幂等的：连点两次不该报错（App 退出时不需要先问「现在连没连」）。
  CHECK(network.disconnect() == Error::none);
}

TEST_CASE("网络：没连上就 GET 返回 not_ready（不会替你偷偷拨号）") {
  HostNetwork network;
  REQUIRE(network.init() == Error::none);

  char body[32];
  std::size_t length = 99;
  CHECK(network.http_get("http://example.com/", {body, sizeof(body)}, length) == Error::not_ready);
  CHECK(length == 0U);  // 失败时不写长度：调用方按它读内存，写个假值就是越界
}

TEST_CASE("网络：URL 校验在发任何包之前就做完") {
  HostNetwork network;
  REQUIRE(network.init() == Error::none);
  REQUIRE(network.connect("embark-test", "pw") == Error::none);

  char body[32];
  std::size_t length = 0;

  // 非 http 一律 unsupported：本层不做 TLS（见 core/include/embark/hal/network.h）。
  CHECK(network.http_get("https://example.com/", {body, sizeof(body)}, length) ==
        Error::unsupported);
  CHECK(network.http_get("ws://example.com/", {body, sizeof(body)}, length) ==
        Error::unsupported);

  // 畸形到连主机名都没有 → invalid_argument（不是 DNS 错误：还没到那一步）。
  CHECK(network.http_get(nullptr, {body, sizeof(body)}, length) == Error::invalid_argument);
  CHECK(network.http_get("http://", {body, sizeof(body)}, length) == Error::invalid_argument);
  CHECK(network.http_get("http://:80/x", {body, sizeof(body)}, length) == Error::invalid_argument);

  // 全都没发出请求，所以状态码仍是 0。
  CHECK(network.last_http_status() == 0);
}

TEST_CASE("网络：sntp_sync 在宿主上明确报 unsupported，不假装成功") {
  HostNetwork network;
  REQUIRE(network.init() == Error::none);
  // 假装成功会让 App 以为时间是被网络校准过的 —— 那是错的，宿主有自己的真实时钟。
  CHECK(network.sntp_sync(1000) == Error::unsupported);
  CHECK(network.sntp_sync(0) == Error::unsupported);
}