/**
 * @file test_ttf_font.cpp
 * @brief 运行期字体（ADR 0017：整份 TTF + stb 栅格化）的宿主用例。
 *
 * 为什么要它：字体链今天只有 embark_host_ui 的五条验收故事覆盖，而 CI 只跑 ctest ——
 * 字体整体坏掉（LV_USE_TINY_TTF 被关、stb 换了一份、字号槽全空）时 CI 会全绿。这里用
 * 仓库里那份真 TTF 走一遍最短路：建字号 → 取字形描述 → 栅格出位图 → 极小缓存不崩 → 释放。
 *
 * 真 TTF 路径由 CMake 传进来（EMBARK_TEST_TTF_FILE），缺失时配置期就报错。
 */
#include <doctest/doctest.h>

#include <lvgl.h>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

/// 读仓库里那份真 TTF。lv_tiny_ttf 只存指针不复制数据，所以缓冲要活过字体寿命。
std::vector<unsigned char> read_ttf() {
  std::vector<unsigned char> data;
  std::FILE* file = std::fopen(EMBARK_TEST_TTF_FILE, "rb");
  REQUIRE(file != nullptr);
  std::fseek(file, 0, SEEK_END);
  const long size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);
  REQUIRE(size > 0);
  data.resize(static_cast<std::size_t>(size));
  REQUIRE(std::fread(data.data(), 1, data.size(), file) == data.size());
  std::fclose(file);
  return data;
}

/// 「启」= U+542F，界面文案里的第一个字，真 TTF 里一定有它。
constexpr std::uint32_t kLetterQi = 0x542FU;

/// lv_init() 只需要内存/日志模块，不要显示器；整个可执行文件做一次就够。
void ensure_lv_init() {
  static bool ready = [] {
    lv_init();
    return true;
  }();
  (void)ready;
}

}  // namespace

TEST_CASE("tiny_ttf：从仓库 TTF 建 14 px 字号、取字形、栅格出位图") {
  ensure_lv_init();

  const std::vector<unsigned char> ttf = read_ttf();
  lv_font_t* font = lv_tiny_ttf_create_data_ex(ttf.data(), ttf.size(), 14, 16U * 1024U);
  REQUIRE(font != nullptr);
  CHECK(font->line_height > 0);

  lv_font_glyph_dsc_t dsc{};
  REQUIRE(lv_font_get_glyph_dsc(font, &dsc, kLetterQi, 0U));
  CHECK(dsc.box_w > 0);
  CHECK(dsc.box_h > 0);
  CHECK(dsc.adv_w > 0);

  // 位图是绘制期才栅格出来的（lv_tiny_ttf 的 get_glyph_bitmap 里 lv_mem_alloc）：
  // 非空 = stb 真把像素算出来了，BIN 时代这一步是 lv_font_load 解析压缩位图，两条路不同。
  CHECK(lv_font_get_glyph_bitmap(font, kLetterQi) != nullptr);

  lv_tiny_ttf_destroy(font);
}

TEST_CASE("tiny_ttf：缓存压到刚够一个字形也只是重栅格，不崩") {
  ensure_lv_init();

  // 1 KB 缓存装 28 px 的一个字形都紧（28×28 = 784 B + 描述），于是每次取字都可能淘汰重栅格 ——
  // 这正是 issue 02 里那套配额的失败模式（缓存不够 ≠ 崩溃，只是每帧白烧栅格时间）。
  const std::vector<unsigned char> ttf = read_ttf();
  lv_font_t* font = lv_tiny_ttf_create_data_ex(ttf.data(), ttf.size(), 28, 1U * 1024U);
  REQUIRE(font != nullptr);

  const std::uint32_t letters[] = {0x542FU, 0x52A8U, 0x4E2DU, 0x6587U, 0x5B57U};
  for (int round = 0; round < 3; ++round) {
    for (const std::uint32_t letter : letters) {
      CHECK(lv_font_get_glyph_bitmap(font, letter) != nullptr);
    }
  }

  lv_tiny_ttf_destroy(font);
}
