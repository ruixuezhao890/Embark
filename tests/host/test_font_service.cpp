// 运行时字库端到端（issue 17 验收项）：宿主进程加载真实字库 .bin → 渲染一个字形 →
// 释放后账目归零。
//
// 为什么单独一个可执行文件：这里要 lv_init() + 一块显示缓冲 + 真实的 FS 后端与
// LVGL 'F' 盘，跟 embark_tests（纯逻辑、链 tests/fakes）不是一类；链的是
// embark::platform_host + embark::common_ui。
// 注意：不在这里 DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN —— 同一个可执行文件里只能有一个
// doctest 实现（它在 host/test_host_filesystem.cpp 里）。
#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include <lvgl.h>

#include <embark/font_ledger.h>

#include "font_service.h"
#include "host_filesystem.h"
#include "host_lvgl_mem.h"

namespace {

using embark::Error;
using embark::FontLedger;
using embark::platform::FontService;
using embark::platform::host::HostFileSystem;
// host_lvgl_mem.h 不只是「想看一眼内存」：宿主 LVGL 的 malloc/free 钩子（embark_lvgl_alloc/
// free/realloc 与 assert_failed）就在那份 TU 里，而它在静态归档 embark_platform_host.a 中、
// liblvgl.a 排在这之后 —— 链接器不回头扫。演示程序是靠 ui_demo 里的内存报表把它拉进链接的；
// 本测试若一个符号都不引用，就会以 undefined reference to embark_lvgl_alloc 收场。
using embark::platform::host::lvgl_peak_bytes;

/// 显示分辨率：够放下 14 号字的一个字就行（顺带让整块缓冲只有 4 KiB）。
constexpr int kHor = 64;
constexpr int kVer = 32;

lv_color_t g_draw_buf[kHor * kVer];
std::size_t g_painted = 0;

/// LVGL 的 flush：只数"非黑像素"（本用例把屏刷成黑底、用白字，于是像素数 > 0
/// 就等于"字库文件真的被解析并画出来了"）。
void flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
  std::size_t index = 0;
  for (int32_t y = area->y1; y <= area->y2; ++y) {
    for (int32_t x = area->x1; x <= area->x2; ++x, ++index) {
      if (color_p[index].full != 0) {
        ++g_painted;
      }
    }
  }
  lv_disp_flush_ready(drv);
}

void register_display() {
  static lv_disp_draw_buf_t draw_buf;
  static lv_disp_drv_t disp_drv;
  lv_disp_draw_buf_init(&draw_buf, g_draw_buf, nullptr, kHor * kVer);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = kHor;
  disp_drv.ver_res = kVer;
  disp_drv.flush_cb = flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
}

void set_env(const char* name, const std::string& value) {
#if defined(_WIN32)
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}

/// 每个用例一块干净的根目录（ctest 的工作目录是构建目录，可写）。
std::filesystem::path fresh_root(const char* name) {
  const std::filesystem::path dir = std::filesystem::current_path() / "embark_host_fs_test" / name;
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);
  return dir;
}

std::string utf8_of(std::uint32_t codepoint) {
  std::string out;
  if (codepoint < 0x80U) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint < 0x800U) {
    out.push_back(static_cast<char>(0xC0U | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
  } else {
    out.push_back(static_cast<char>(0xE0U | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80U | ((codepoint >> 6) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
  }
  return out;
}

}  // namespace

TEST_CASE("字库服务：加载真实 .bin → 调 lv_font_load → 渲染字形 → 释放归零") {
  const std::filesystem::path font_dir = EMBARK_TEST_FONT_DIR;
  const std::filesystem::path font_file = font_dir / "ui_font_ui_heiti_14.bin";
  if (!std::filesystem::exists(font_file)) {
    MESSAGE("跳过：找不到 fixture 字库 " << font_file.string());
    return;
  }

  set_env("EMBARK_HOST_FS_ROOT", font_dir.string());
  HostFileSystem filesystem;
  REQUIRE(filesystem.init() == Error::none);

  lv_init();
  register_display();

  FontLedger ledger;
  FontService service(filesystem, ledger);
  REQUIRE(service.init() == Error::none);
  CHECK(service.ready());

  const char* const name = "ui_font_ui_heiti_14.bin";
  const auto loaded = service.load(name);
  REQUIRE(loaded.has_value());
  lv_font_t* font = *loaded;
  REQUIRE(font != nullptr);
  CHECK(ledger.loaded() == 1);
  CHECK(ledger.used_bytes() == std::filesystem::file_size(font_file));
  CHECK(lvgl_peak_bytes() > 0);  // 字库确实是 LVGL 堆里的一份数据（顺带见上面的链接说明）

  // 挑一个这个字库里确实有的字符（导出字库按屏上的文案取子集，不保证有任意汉字）
  const std::uint32_t candidates[] = {'A', 0x542FU /* 启 */, 0x65F6U /* 时 */, 0x8BBEU /* 设 */};
  std::string text;
  for (const std::uint32_t codepoint : candidates) {
    lv_font_glyph_dsc_t dsc = {};
    if (lv_font_get_glyph_dsc(font, &dsc, codepoint, 0)) {
      text = utf8_of(codepoint);
      break;
    }
  }
  REQUIRE_FALSE(text.empty());

  lv_obj_t* const base = lv_scr_act();
  lv_obj_t* const screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(screen);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_t* const label = lv_label_create(screen);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_label_set_text(label, text.c_str());
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
  lv_scr_load(screen);

  g_painted = 0;
  lv_refr_now(nullptr);
  CHECK(g_painted > 0);  // 画出了像素 = 这份字库被真正加载并用上了

  // 先把字体从控件上摘下来（删屏会连带删 label），再 release：免得释放后还有引用
  lv_scr_load(base);
  lv_obj_del(screen);

  // 复用：同一个路径再 load 一次只加引用，仍返回同一个指针
  const auto again = service.load(name);
  REQUIRE(again.has_value());
  CHECK(*again == font);
  CHECK(ledger.loaded() == 1);
  CHECK(service.release(name) == Error::none);  // 引用 2 → 1：字库继续驻留
  CHECK(ledger.loaded() == 1);
  CHECK(ledger.used_bytes() == std::filesystem::file_size(font_file));

  CHECK(service.release(name) == Error::none);  // 归零：lv_font_free + 退额度
  CHECK(ledger.loaded() == 0);
  CHECK(ledger.used_bytes() == 0);
  CHECK(service.release(name) == Error::not_found);  // 多释放一次不静默成功
}

TEST_CASE("字库服务：没 init / 文件缺失 / 路径非法") {
  const std::filesystem::path root_dir = fresh_root("embark_font_service_errors");
  set_env("EMBARK_HOST_FS_ROOT", root_dir.string());
  HostFileSystem filesystem;
  REQUIRE(filesystem.init() == Error::none);

  FontLedger ledger;
  FontService not_inited(filesystem, ledger);
  CHECK(not_inited.load("x.bin").error() == Error::not_ready);  // 没注册 'F' 盘

  FontService service(filesystem, ledger);
  REQUIRE(service.init() == Error::none);
  CHECK(service.load("nope.bin").error() == Error::not_found);
  CHECK(service.load("").error() == Error::invalid_argument);
  CHECK(ledger.loaded() == 0);
  CHECK(ledger.used_bytes() == 0);

  char too_long[embark::font_path_bytes + 2] = {};
  for (std::size_t i = 0; i < embark::font_path_bytes + 1; ++i) {
    too_long[i] = 'x';
  }
  CHECK(service.load(too_long).error() == Error::invalid_argument);
}

TEST_CASE("字库服务：不是字库的文件 → corrupt_data，且不占额度") {
  const std::filesystem::path root_dir = fresh_root("embark_font_service_junk");
  const std::filesystem::path junk = root_dir / "junk.bin";
  {
    std::ofstream out(junk, std::ios::binary);
    const char garbage[64] = {};
    out.write(garbage, sizeof(garbage));
  }
  set_env("EMBARK_HOST_FS_ROOT", root_dir.string());
  HostFileSystem filesystem;
  REQUIRE(filesystem.init() == Error::none);

  FontLedger ledger;
  FontService service(filesystem, ledger);
  REQUIRE(service.init() == Error::none);

  CHECK(service.load("junk.bin").error() == Error::corrupt_data);
  CHECK(ledger.loaded() == 0);  // 加载失败要把刚占的额度还回去
  CHECK(ledger.used_bytes() == 0);
}
