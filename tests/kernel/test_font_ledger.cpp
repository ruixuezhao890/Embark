// 运行时字库记账（issue 17）：纯逻辑，不碰 LVGL、不碰文件系统。
#include <doctest/doctest.h>

#include <cstddef>

#include <embark/font_ledger.h>
#include <embark_limits.h>

namespace {

using embark::Error;
using embark::FontLedger;

constexpr std::size_t kBudget = FontLedger::budget_bytes();

}  // namespace

TEST_CASE("字库记账：首次记费，重复加载只加引用") {
  FontLedger ledger;
  CHECK(ledger.loaded() == 0);
  CHECK(ledger.used_bytes() == 0);

  const auto first = ledger.acquire("heiti14.bin", 8000);
  REQUIRE(first.has_value());
  CHECK(*first == 1);
  CHECK(ledger.loaded() == 1);
  CHECK(ledger.used_bytes() == 8000);

  const auto second = ledger.acquire("heiti14.bin", 8000);  // 同一路径：字节数不再计一次
  REQUIRE(second.has_value());
  CHECK(*second == 2);
  CHECK(ledger.loaded() == 1);
  CHECK(ledger.used_bytes() == 8000);

  const auto refs = ledger.refs("heiti14.bin");
  REQUIRE(refs.has_value());
  CHECK(*refs == 2);

  CHECK(ledger.acquire("heiti22.bin", 12000).has_value());  // 不同路径累加
  CHECK(ledger.loaded() == 2);
  CHECK(ledger.used_bytes() == 20000);
}

TEST_CASE("字库记账：释放到 0 才退额度，账外释放 not_found") {
  FontLedger ledger;
  REQUIRE(ledger.acquire("a.bin", 1000).has_value());
  REQUIRE(ledger.acquire("a.bin", 1000).has_value());  // 引用 2

  CHECK(ledger.release("a.bin") == Error::none);  // 2 → 1：字库还在
  CHECK(ledger.loaded() == 1);
  CHECK(ledger.used_bytes() == 1000);

  CHECK(ledger.release("a.bin") == Error::none);  // 1 → 0：退额度
  CHECK(ledger.loaded() == 0);
  CHECK(ledger.used_bytes() == 0);
  CHECK(ledger.refs("a.bin").error() == Error::not_found);
  CHECK(ledger.release("a.bin") == Error::not_found);  // 多释放一次不静默成功

  // 归零后同一个路径可以重新记（清干净的条目不会留下残影）
  const auto again = ledger.acquire("a.bin", 1000);
  REQUIRE(again.has_value());
  CHECK(*again == 1);
}

TEST_CASE("字库记账：预算与表项定容，失败路径不留痕") {
  FontLedger ledger;

  // 预算是"已用 + 本次 ≤ 预算"，恰好用满算成功
  REQUIRE(ledger.acquire("full.bin", kBudget).has_value());
  CHECK(ledger.used_bytes() == kBudget);

  static_cast<void>(ledger.release("full.bin"));
  REQUIRE(ledger.acquire("big.bin", kBudget + 1).error() == Error::no_space);
  CHECK(ledger.loaded() == 0);  // 失败不占条目
  CHECK(ledger.used_bytes() == 0);

  REQUIRE(ledger.acquire("half.bin", kBudget / 2).has_value());
  CHECK(ledger.acquire("rest.bin", kBudget / 2 + 1).error() == Error::no_space);  // 超一格
  CHECK(ledger.loaded() == 1);
  CHECK(ledger.used_bytes() == kBudget / 2);

  // 表项用完：第 font_max_loaded + 1 个不同路径 no_space（额度还剩很多）
  FontLedger many;
  for (std::size_t i = 0; i < embark::font_max_loaded; ++i) {
    const char* const names[] = {"f0.bin", "f1.bin", "f2.bin", "f3.bin"};
    REQUIRE(many.acquire(names[i], 100).has_value());
  }
  CHECK(many.loaded() == embark::font_max_loaded);
  CHECK(many.acquire("overflow.bin", 100).error() == Error::no_space);
  CHECK(many.loaded() == embark::font_max_loaded);
}

TEST_CASE("字库记账：非法输入 invalid_argument") {
  FontLedger ledger;
  CHECK(ledger.acquire("", 100).error() == Error::invalid_argument);
  CHECK(ledger.acquire("x.bin", 0).error() == Error::invalid_argument);  // 0 字节不是字库
  CHECK(ledger.used_bytes() == 0);

  char too_long[embark::font_path_bytes + 2] = {};
  for (std::size_t i = 0; i < embark::font_path_bytes + 1; ++i) {
    too_long[i] = 'x';
  }
  CHECK(ledger.acquire(too_long, 100).error() == Error::invalid_argument);
  CHECK(ledger.loaded() == 0);

  // 恰好 font_path_bytes - 1 个字符（加 '\0' 正好装下）算合法
  char exact[embark::font_path_bytes] = {};
  for (std::size_t i = 0; i + 1 < embark::font_path_bytes; ++i) {
    exact[i] = 'y';
  }
  CHECK(ledger.acquire(exact, 100).has_value());
}

TEST_CASE("字库记账：释放中间条目后尾部补位，没有空洞") {
  FontLedger ledger;
  REQUIRE(ledger.acquire("keep.bin", 500).has_value());
  REQUIRE(ledger.acquire("drop.bin", 700).has_value());
  REQUIRE(ledger.acquire("tail.bin", 900).has_value());

  CHECK(ledger.release("drop.bin") == Error::none);  // 中间那条
  CHECK(ledger.loaded() == 2);
  CHECK(ledger.used_bytes() == 1400);
  CHECK(ledger.refs("drop.bin").error() == Error::not_found);
  CHECK(ledger.refs("keep.bin").has_value());
  CHECK(ledger.refs("tail.bin").has_value());

  // 补位后表还能继续收新条目（上限仍是 font_max_loaded，不是"用过一次就少一格"）
  CHECK(ledger.acquire("new.bin", 100).has_value());
  CHECK(ledger.loaded() == 3);
  CHECK(ledger.used_bytes() == 1500);
}
