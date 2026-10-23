// HAL 持久化能力：往返、覆盖、擦除、以及每一种错误码。
//
// 这里跑的是 detail/kv_slot.h 那套真编解码（宿主文件后端同款），所以 fake 上验证过的
// 语义对真后端成立。
#include <doctest/doctest.h>

#include "fakes/fakes.h"

#include <cstdio>

#include <embark_limits.h>

using embark::Error;
using embark::persistence_max_slots;
using embark::persistence_max_value_bytes;
using embark::fakes::FakePersistence;

TEST_CASE("IPersistence：未 init 的读写擦都返回 not_ready") {
  FakePersistence storage;
  std::uint8_t buffer[4] = {};

  const auto read = storage.read("k", etl::span<std::uint8_t>(buffer, sizeof(buffer)));
  REQUIRE_FALSE(read.has_value());
  CHECK(read.error() == Error::not_ready);
  CHECK(storage.write("k", etl::span<const std::uint8_t>(buffer, sizeof(buffer))) ==
        Error::not_ready);
  CHECK(storage.erase("k") == Error::not_ready);
  CHECK(storage.erase_all() == Error::not_ready);
}

TEST_CASE("IPersistence：写读往返、覆盖同键不占新槽、擦除后可查") {
  FakePersistence storage;
  REQUIRE(storage.init() == Error::none);
  CHECK(storage.capacity_bytes() == persistence_max_slots * persistence_max_value_bytes);

  const std::uint8_t value[4] = {0xDE, 0xAD, 0xBE, 0xEF};
  CHECK(storage.write("cal", etl::span<const std::uint8_t>(value, sizeof(value))) == Error::none);

  std::uint8_t out[4] = {};
  const auto read = storage.read("cal", etl::span<std::uint8_t>(out, sizeof(out)));
  REQUIRE(read.has_value());
  CHECK(read.value() == 4U);
  CHECK(out[0] == 0xDEU);
  CHECK(out[3] == 0xEFU);

  const std::uint8_t shorter_value[2] = {1, 2};
  CHECK(storage.write("cal", etl::span<const std::uint8_t>(shorter_value, 2)) == Error::none);
  const auto shorter = storage.read("cal", etl::span<std::uint8_t>(out, sizeof(out)));
  REQUIRE(shorter.has_value());
  CHECK(shorter.value() == 2U);
  CHECK(out[0] == 1U);

  CHECK(storage.erase("cal") == Error::none);
  const auto gone = storage.read("cal", etl::span<std::uint8_t>(out, sizeof(out)));
  REQUIRE_FALSE(gone.has_value());
  CHECK(gone.error() == Error::not_found);
  CHECK(storage.erase("cal") == Error::not_found);
}

TEST_CASE("IPersistence：空键/超长键 invalid_argument，超长值 no_space") {
  FakePersistence storage;
  REQUIRE(storage.init() == Error::none);
  const std::uint8_t one[1] = {1};

  CHECK(storage.write(etl::string_view{}, etl::span<const std::uint8_t>(one, 1)) ==
        Error::invalid_argument);
  // 17 字节键（上限 16）
  CHECK(storage.write("0123456789abcdefg", etl::span<const std::uint8_t>(one, 1)) ==
        Error::invalid_argument);
  // 16 字节键正好用满
  CHECK(storage.write("0123456789abcdef", etl::span<const std::uint8_t>(one, 1)) == Error::none);

  etl::array<std::uint8_t, persistence_max_value_bytes + 1> too_big{};
  CHECK(storage.write("big", etl::span<const std::uint8_t>(too_big.data(), too_big.size())) ==
        Error::no_space);

  etl::array<std::uint8_t, persistence_max_value_bytes> full{};
  REQUIRE(storage.write("full", etl::span<const std::uint8_t>(full.data(), full.size())) ==
          Error::none);
}

TEST_CASE("IPersistence：读缓冲太小 → no_space（不是截断）") {
  FakePersistence storage;
  REQUIRE(storage.init() == Error::none);

  etl::array<std::uint8_t, persistence_max_value_bytes> full{};
  full[0] = 0x42;
  REQUIRE(storage.write("full", etl::span<const std::uint8_t>(full.data(), full.size())) ==
          Error::none);

  std::uint8_t tiny[1] = {};
  const auto read = storage.read("full", etl::span<std::uint8_t>(tiny, sizeof(tiny)));
  REQUIRE_FALSE(read.has_value());
  CHECK(read.error() == Error::no_space);
  CHECK(tiny[0] == 0U);  // 失败时不动调用方缓冲

  const auto exact = storage.read("full", etl::span<std::uint8_t>(full.data(), full.size()));
  REQUIRE(exact.has_value());
  CHECK(exact.value() == persistence_max_value_bytes);
}

TEST_CASE("IPersistence：槽位用完 → no_space，erase_all 后可恢复") {
  FakePersistence storage;
  REQUIRE(storage.init() == Error::none);
  const std::uint8_t one[1] = {1};
  char key[8] = {};

  for (std::size_t i = 0; i < persistence_max_slots; ++i) {
    std::snprintf(key, sizeof(key), "k%zu", i);
    CHECK(storage.write(key, etl::span<const std::uint8_t>(one, 1)) == Error::none);
  }

  CHECK(storage.write("overflow", etl::span<const std::uint8_t>(one, 1)) == Error::no_space);

  CHECK(storage.erase_all() == Error::none);
  CHECK(storage.erase_all_calls == 1U);
  CHECK(storage.write("after", etl::span<const std::uint8_t>(one, 1)) == Error::none);
}

TEST_CASE("IPersistence：槽内 CRC 坏了 → corrupt_data，erase_all 能恢复出厂") {
  FakePersistence storage;
  REQUIRE(storage.init() == Error::none);
  const std::uint8_t one[1] = {7};
  REQUIRE(storage.write("key", etl::span<const std::uint8_t>(one, 1)) == Error::none);

  storage.corrupt_slot(0);

  std::uint8_t out[4] = {};
  const auto read = storage.read("key", etl::span<std::uint8_t>(out, sizeof(out)));
  REQUIRE_FALSE(read.has_value());
  CHECK(read.error() == Error::corrupt_data);

  REQUIRE(storage.erase_all() == Error::none);
  const auto after = storage.read("key", etl::span<std::uint8_t>(out, sizeof(out)));
  REQUIRE_FALSE(after.has_value());
  CHECK(after.error() == Error::not_found);
}

TEST_CASE("IPersistence：后端 IO 故障原样上报") {
  FakePersistence storage;
  REQUIRE(storage.init() == Error::none);
  const std::uint8_t one[1] = {1};
  std::uint8_t out[1] = {};

  storage.write_error = Error::io_failure;
  CHECK(storage.write("k", etl::span<const std::uint8_t>(one, 1)) == Error::io_failure);

  storage.write_error = Error::none;
  REQUIRE(storage.write("k", etl::span<const std::uint8_t>(one, 1)) == Error::none);

  storage.read_error = Error::timeout;
  const auto read = storage.read("k", etl::span<std::uint8_t>(out, sizeof(out)));
  REQUIRE_FALSE(read.has_value());
  CHECK(read.error() == Error::timeout);

  storage.erase_error = Error::io_failure;
  CHECK(storage.erase("k") == Error::io_failure);
}

TEST_CASE("IPersistence：init 故障透传，之后仍是 not_ready") {
  FakePersistence storage;
  storage.init_error = Error::corrupt_data;

  CHECK(storage.init() == Error::corrupt_data);

  std::uint8_t out[1] = {};
  const auto read = storage.read("k", etl::span<std::uint8_t>(out, sizeof(out)));
  REQUIRE_FALSE(read.has_value());
  CHECK(read.error() == Error::not_ready);
}
