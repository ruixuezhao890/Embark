/**
 * @file test_text_wrap.cpp
 * @brief LVGL 8.3.11 的断行行为（issue 07 的记账）：中文能逐字断、标点会起行、长串硬切。
 *
 * 为什么要它：ADR 0017 第 10 条把「断行沿用 LVGL 现状」写成了决定，阅读器将来可能在这里
 * 加行首禁则。但仓库里原本只有一句结论式注释「CJK 逐字断行只认 U+4E00–U+9FA5」
 * （third_party/lvgl/src/misc/lv_txt.h:165-184），没有实测。这里用仓库那份真 TTF 把四种
 * 文本的切行结果钉住（14 px 下：汉字 14 px/字、假名 14、全角标点 14、'A' 11）：
 *
 *   1. 纯中文：逐字断，用满宽度，行宽不超上限，字符不丢；
 *   2. 中文 + 全角标点：**标点会起行**。「，」「。」既不在 U+4E00–U+9FA5，也不在
 *      LV_TXT_BREAK_CHARS（默认 " ,.;:-_"）里 ⇒ 断点只能落在它前面的汉字上，
 *      于是标点被推到下一行行首。全仓没有行首禁则（kinsoku）。
 *   3. 假名 / 拉丁长单词：不在断点区间又没有空格，LVGL 就按宽度**硬切**——不溢出，
 *      也不管词边界（supercalifragilis|ticexpialidocious，没有连字符）。
 *
 * LVGL 升级或有人改断行时这些断言会响 —— 那正是重新评估「阅读器要不要自算断行」的时机
 * （.scratch/font-ttf/issues/07-reader-line-breaking.md）。
 *
 * 这里量宽用的 _lv_txt_get_next_line() 也正是 lv_label 排版时走的那个函数，所以这份用例
 * 同时是给阅读器的方子：排版前用它可以先把整章切成行（要独立量宽度用 lv_txt_get_size()）。
 */
#include <doctest/doctest.h>

#include <lvgl.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

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

void ensure_lv_init() {
  static bool ready = [] {
    lv_init();
    return true;
  }();
  (void)ready;
}

/// 一行切出来的东西：文本本身 + LVGL 报的像素宽。
struct Line {
  std::string text;
  int width;
};

/// 按 LVGL 自己的规则把 text 切成行（max_width 就是 lv_label 的宽度）。
std::vector<Line> lines_of(const std::string& text, const lv_font_t* font, int max_width) {
  std::vector<Line> lines;
  std::uint32_t offset = 0;
  while (offset < text.size() && lines.size() < 64) {
    lv_coord_t used = 0;
    const std::uint32_t len =
        _lv_txt_get_next_line(&text[offset], font, 0, max_width, &used, LV_TEXT_FLAG_NONE);
    if (len == 0) {
      break;
    }
    lines.push_back({text.substr(offset, len), static_cast<int>(used)});
    offset += len;
  }
  return lines;
}

/// 切行不许吃字：把行拼回去必须等于原文。
void check_lossless(const std::string& text, const std::vector<Line>& lines) {
  std::string joined;
  for (const Line& line : lines) {
    joined += line.text;
  }
  CHECK(joined == text);
}

/// 每行都不许超宽（超宽 = 溢出控件边界）。
void check_fits(const std::vector<Line>& lines, int max_width) {
  for (const Line& line : lines) {
    INFO("行：|" << line.text << "| 宽 " << line.width);
    CHECK(line.width <= max_width);
  }
}

/// 14 px 字体整个文件只建一次：真 TTF 3.85 MB 读几遍没意义，lv_tiny_ttf 只存指针不复制。
struct Font14 {
  std::vector<unsigned char> data;
  lv_font_t* font = nullptr;

  Font14() {
    ensure_lv_init();
    data = read_ttf();
    font = lv_tiny_ttf_create_data_ex(data.data(), data.size(), 14, 32U * 1024U);
    REQUIRE(font != nullptr);
  }
  ~Font14() {
    if (font != nullptr) {
      lv_tiny_ttf_destroy(font);
    }
  }
  Font14(const Font14&) = delete;
  Font14& operator=(const Font14&) = delete;
};

}  // namespace

TEST_CASE("text_wrap：中文逐字断行，用满宽度且不丢字") {
  Font14 holder;

  // 24 个汉字、行宽 100 px：14 px/字 ⇒ 每行 7 个字（98 px），剩 3 个字。
  const std::string text = "天地玄黄宇宙洪荒日月盈昃辰宿列张寒来暑往秋收冬藏";
  const std::vector<Line> lines = lines_of(text, holder.font, 100);

  REQUIRE(lines.size() >= 3);
  check_fits(lines, 100);
  check_lossless(text, lines);
  // 非末行都该把宽度吃掉 —— 提前断行说明断点规则变了（LVGL 贪心断）。
  for (std::size_t i = 0; i + 1 < lines.size(); ++i) {
    INFO("行 " << i << "：|" << lines[i].text << "|");
    CHECK(lines[i].width >= 84);
  }
}

TEST_CASE("text_wrap：全角标点会落在行首（没有行首禁则）") {
  Font14 holder;

  // 「，」U+FF0C 与「。」U+3002 都不是断字符 ⇒ 断点只能落在前面的汉字上。
  // 这是 ADR 0017 第 10 条记下的已知缺陷，阅读器要在自算断行时才治得了它。
  const std::string text = "天地玄黄，宇宙洪荒。日月盈昃，辰宿列张。寒来暑往，秋收冬藏。";
  const std::vector<Line> lines = lines_of(text, holder.font, 100);

  REQUIRE(lines.size() >= 3);
  check_fits(lines, 100);
  check_lossless(text, lines);

  int punct_at_line_start = 0;
  for (const Line& line : lines) {
    if (line.text.compare(0, 3, "，") == 0 || line.text.compare(0, 3, "。") == 0) {
      ++punct_at_line_start;
      INFO("标点起行：|" << line.text << "|");
    }
  }
  // 反过来写：哪天上游加了禁则，这条会变红，提醒把 GLOSSARY 的 Line breaking gap 一起改。
  CHECK(punct_at_line_start >= 1);
}

TEST_CASE("text_wrap：没有断点的长串被按宽度硬切，不溢出") {
  Font14 holder;

  // 假名在 U+4E00–U+9FA5 之外、不是断字符：LVGL 找不到断点就按宽度切（lv_txt.c 的
  // lv_txt_get_next_word(force = true) 那条路），所以中文之外的 CJK 不会溢出成一行长条。
  const std::string kana = "あいうえおかきくけこさしすせそたちつてと";
  const std::vector<Line> kana_lines = lines_of(kana, holder.font, 100);
  REQUIRE(kana_lines.size() >= 2);
  check_fits(kana_lines, 100);
  check_lossless(kana, kana_lines);

  // 拉丁长单词同理：切在字中间，没有连字符、也没有词边界保护（LV_TXT_LINE_BREAK_LONG_LEN=0）。
  const std::string word = "supercalifragilisticexpialidocious";
  const std::vector<Line> word_lines = lines_of(word, holder.font, 100);
  REQUIRE(word_lines.size() >= 2);
  check_fits(word_lines, 100);
  check_lossless(word, word_lines);
}
