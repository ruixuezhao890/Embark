// 分配记账（include/embark/detail/allocation_counter.h）。
//
// 这份实现同时被宿主 LVGL 内存钩子（platform/host/host_lvgl_mem.cpp）使用，
// 所以这里测的就是线上跑的那份记账 —— spec §10 的「对象总量上限」判据只有一处定义。
#include <doctest/doctest.h>

#include <embark/detail/allocation_counter.h>

using embark::detail::AllocationCounter;

TEST_CASE("记账：分配累计未回收字节，峰值随之抬升") {
  AllocationCounter counter;

  CHECK(counter.outstanding_bytes() == 0U);
  CHECK(counter.peak_bytes() == 0U);

  counter.on_alloc(100U);
  counter.on_alloc(40U);

  CHECK(counter.outstanding_bytes() == 140U);
  CHECK(counter.peak_bytes() == 140U);
  CHECK(counter.allocations() == 2U);
  CHECK(counter.frees() == 0U);
}

TEST_CASE("记账：释放减少未回收字节，峰值保留历史最高") {
  AllocationCounter counter;

  counter.on_alloc(200U);
  counter.on_free(50U);

  CHECK(counter.outstanding_bytes() == 150U);
  CHECK(counter.peak_bytes() == 200U);
  CHECK(counter.frees() == 1U);
}

TEST_CASE("记账：realloc 按差值调整，不计入 alloc/free 次数") {
  AllocationCounter counter;
  counter.on_alloc(64U);

  counter.on_realloc(64U, 128U);  // 变大
  CHECK(counter.outstanding_bytes() == 128U);
  CHECK(counter.peak_bytes() == 128U);

  counter.on_realloc(128U, 32U);  // 变小
  CHECK(counter.outstanding_bytes() == 32U);
  CHECK(counter.peak_bytes() == 128U);

  CHECK(counter.reallocations() == 2U);
  CHECK(counter.allocations() == 1U);
  CHECK(counter.frees() == 0U);
}

TEST_CASE("记账：释放对不上时夹到 0，不回绕成巨大的数") {
  AllocationCounter counter;
  counter.on_alloc(16U);

  counter.on_free(4096U);

  CHECK(counter.outstanding_bytes() == 0U);
  CHECK(counter.peak_bytes() == 16U);
}

TEST_CASE("记账：over_budget 严格大于预算（等于算没超）") {
  AllocationCounter counter;
  counter.on_alloc(1024U);

  CHECK_FALSE(counter.over_budget(1024U));
  CHECK(counter.over_budget(1023U));
  CHECK_FALSE(counter.over_budget(4096U));
}

TEST_CASE("记账：reset 清空全部计数，含峰值") {
  AllocationCounter counter;
  counter.on_alloc(512U);
  counter.on_realloc(512U, 1024U);
  counter.on_free(256U);

  counter.reset();

  CHECK(counter.outstanding_bytes() == 0U);
  CHECK(counter.peak_bytes() == 0U);
  CHECK(counter.allocations() == 0U);
  CHECK(counter.frees() == 0U);
  CHECK(counter.reallocations() == 0U);
}
