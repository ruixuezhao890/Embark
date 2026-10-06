/**
 * EEZ 屏名 → App 名解析的单元测试（屏名约定：screen == App 名，子页 <app名>_<编号>_sub）。
 *
 * 被测头 app/eez_ui_screen_names.h 刻意不 include LVGL/Embark（纯字符串数学），
 * 所以本文件跟 tests 目标一起编，不需要 LVGL —— 与 tests/detail/ 里的纯函数测试同理。
 */
#include <cstring>

#include <doctest/doctest.h>

#include "eez_ui_screen_names.h"

using embark::demo::eez_ui_app_name_for_screen;
using embark::demo::eez_ui_screen_is_subpage;

TEST_CASE("屏名与 App 同名：原样返回") {
  char buffer[embark::demo::eez_app_name_max];
  CHECK(std::strcmp(eez_ui_app_name_for_screen("launcher", buffer, sizeof(buffer)), "launcher") == 0);
  CHECK(std::strcmp(eez_ui_app_name_for_screen("clock", buffer, sizeof(buffer)), "clock") == 0);
  CHECK(std::strcmp(eez_ui_app_name_for_screen("settings", buffer, sizeof(buffer)), "settings") == 0);
  CHECK_FALSE(eez_ui_screen_is_subpage("clock"));
}

TEST_CASE("子页 <app名>_<编号>_sub 归到它的 App") {
  char buffer[embark::demo::eez_app_name_max];
  CHECK(std::strcmp(eez_ui_app_name_for_screen("clock_1_sub", buffer, sizeof(buffer)), "clock") == 0);
  CHECK(std::strcmp(eez_ui_app_name_for_screen("settings_12_sub", buffer, sizeof(buffer)), "settings") == 0);
  // 只剥最后一段：App 名自己带数字/下划线也成立。
  CHECK(std::strcmp(eez_ui_app_name_for_screen("ticker_2_3_sub", buffer, sizeof(buffer)), "ticker_2") == 0);
  CHECK(eez_ui_screen_is_subpage("clock_1_sub"));
  CHECK(eez_ui_screen_is_subpage("ticker_2_3_sub"));
}

TEST_CASE("不是子页形状就原样返回") {
  char buffer[embark::demo::eez_app_name_max];
  CHECK(std::strcmp(eez_ui_app_name_for_screen("clock_1", buffer, sizeof(buffer)), "clock_1") == 0);
  CHECK(std::strcmp(eez_ui_app_name_for_screen("clock_sub", buffer, sizeof(buffer)), "clock_sub") == 0);
  CHECK(std::strcmp(eez_ui_app_name_for_screen("clock_1_sub_x", buffer, sizeof(buffer)), "clock_1_sub_x") == 0);
  CHECK(std::strcmp(eez_ui_app_name_for_screen("1_sub", buffer, sizeof(buffer)), "1_sub") == 0);
  // 基名为空（"_1_sub"）不算子页形状，原样返回。
  CHECK(std::strcmp(eez_ui_app_name_for_screen("_1_sub", buffer, sizeof(buffer)), "_1_sub") == 0);
  CHECK_FALSE(eez_ui_screen_is_subpage("clock_1"));
  CHECK_FALSE(eez_ui_screen_is_subpage("_1_sub"));
}

TEST_CASE("参数与缓冲边界：绝不越界、绝不写坏缓冲") {
  char buffer[embark::demo::eez_app_name_max];
  CHECK(eez_ui_app_name_for_screen(nullptr, buffer, sizeof(buffer)) == nullptr);
  CHECK(eez_ui_app_name_for_screen("clock", nullptr, sizeof(buffer)) == nullptr);
  CHECK(eez_ui_app_name_for_screen("clock", buffer, 0U) == nullptr);
  CHECK(eez_ui_app_name_for_screen("", buffer, sizeof(buffer)) == nullptr);
  // 装不下（含结尾空字符）：返回 nullptr，且不动缓冲。
  std::memset(buffer, 'X', sizeof(buffer));
  CHECK(eez_ui_app_name_for_screen("clock", buffer, 5U) == nullptr);
  CHECK(buffer[0] == 'X');
  // 刚好装下（"clock" + 空字符 = 6）。
  CHECK(std::strcmp(eez_ui_app_name_for_screen("clock", buffer, 6U), "clock") == 0);
}

TEST_CASE("子页基名过长：装不下返回 nullptr") {
  char small[8];
  CHECK(eez_ui_app_name_for_screen("settings_1_sub", small, sizeof(small)) == nullptr);
}
