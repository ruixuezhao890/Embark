/**
 * @file esp32_font_probe.cpp
 * @brief 字体运行时栅格化：真机侧自检与数字（ADR 0017 之后）
 *
 * 探针量的就是**产品路径**，而不是另起一套实验字体：
 *   1. 卡上那份 TTF 的顺序读吞吐（32 KB 缓冲滚着读，不额外汇 PSRAM）；
 *   2. 桥给每个字号建出来的 tiny_ttf 实例：冷栅格 / 热命中各多少 µs per 字；
 *   3. 每个字号一屏 N 个真汉字的首帧 / 热帧毫秒；
 *   4. 内置 montserrat 14 px 的绘制本底（对照组）。
 *
 * 为什么要复用桥的实例：旧版自己 mmap flash 分区、自己建字体，结果是"探针好看、产品另说"；
 * 而且它要第二份 3.85 MB TTF —— 8 MB PSRAM 上桥已经常驻一份，再读一份就装不下了。
 * 复用产品实例的代价是会让各字号的 LRU 缓存先被灌一遍，这对测量无害（量完就结束）。
 *
 * 第一版在这里翻过车，值得留个记号：字形位图与 stb 的临时边表都走 lv_mem_alloc，而 LVGL 的
 * 默认分配器是 48 KB 静态池（config/lv_conf.h 的 LV_MEM_SIZE）。板子一启动，界面控件已经把
 * 那个池吃掉大半，于是第一张字形就在 `stbtt__new_active` 里 malloc 失败 —— assert 直接复位。
 * 现在字形位图落在字库区（PSRAM 定容池），这条路径由桥的 scoped_glyph_bitmap 保证。
 *
 * 样本码点要先探一遍再收集：拿 U+4E00 起连续 120 个码点当样例会有一半缺字（GB2312 全集
 * 6,763 字，不是整个 CJK 基本区），µs/字 会被摊薄。
 *
 * 全程在 UI 任务里跑（framework.boot() 之后），所以 LVGL 不需要加锁。
 * 整个文件在 EMBARK_FONT_PROBE 未定义时是空的 —— 默认构建里不存在这段代码。
 */
#if defined(EMBARK_FONT_PROBE)

#include "esp32_font_probe.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <lvgl.h>

#include <middleware/elog/elog.hpp>

#include "aidogui_res_fs.h"
#include "aidogui_ui_bridge.h"
#include "esp32_lvgl_mem.h"
#include "font_alloc_scope.h"

namespace embark::platform::esp32::font_probe {

namespace {

/// 一屏大约有多少个**不同**字形：中文内容里重复字不少，120 是偏重的估计
constexpr std::size_t kGlyphCount = 120U;
/// 大字号一屏放不下这么多字（48/56 px 的界面只有数字与标题），样本少一些
constexpr std::size_t kGlyphCountLarge = 24U;
/// 大字号的门槛：超过它就用 kGlyphCountLarge
constexpr int kLargePx = 28;
/// 探针码点从 U+4E00 开始往上扫
constexpr std::uint32_t kFirstCodepoint = 0x4E00U;
/// 扫到哪儿停（够数就提前结束）
constexpr std::uint32_t kLastCodepoint = 0x9FFFU;
/// 顺序读卡用的滚动缓冲（不常驻，读完就还）
constexpr std::size_t kReadChunkBytes = 32U * 1024U;

std::int64_t now_us() {
  return esp_timer_get_time();
}

double elapsed_us(std::int64_t begin_us) {
  return static_cast<double>(now_us() - begin_us);
}

std::size_t psram_free_bytes() {
  return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

std::size_t internal_free_bytes() {
  return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

std::size_t append_utf8(char* out, std::uint32_t codepoint) {
  if (codepoint < 0x80U) {
    out[0] = static_cast<char>(codepoint);
    return 1U;
  }
  if (codepoint < 0x800U) {
    out[0] = static_cast<char>(0xC0U | (codepoint >> 6));
    out[1] = static_cast<char>(0x80U | (codepoint & 0x3FU));
    return 2U;
  }
  out[0] = static_cast<char>(0xE0U | (codepoint >> 12));
  out[1] = static_cast<char>(0x80U | ((codepoint >> 6) & 0x3FU));
  out[2] = static_cast<char>(0x80U | (codepoint & 0x3FU));
  return 3U;
}

/// 逐字走一遍 dsc + bitmap。真的栅格化的字（有位图的）才计数。
double rasterize(const std::uint32_t* codepoints, std::size_t count, const lv_font_t* font,
                 std::size_t& drawn) {
  drawn = 0U;
  const std::int64_t begin = now_us();
  for (std::size_t i = 0; i < count; ++i) {
    lv_font_glyph_dsc_t dsc{};
    if (lv_font_get_glyph_dsc(font, &dsc, codepoints[i], 0)) {
      if (lv_font_get_glyph_bitmap(font, codepoints[i]) != nullptr) {
        ++drawn;
      }
    }
  }
  return elapsed_us(begin);
}

/// 整屏重绘一次：invalidate 屏幕 + lv_refr_now。
double refresh_screen_ms(lv_obj_t* screen) {
  const lv_disp_t* display = lv_disp_get_default();
  lv_obj_invalidate(screen);
  const std::int64_t begin = now_us();
  lv_refr_now(const_cast<lv_disp_t*>(display));
  return elapsed_us(begin) / 1000.0;
}

/// 建一个满屏标签、量首帧与热帧，然后把屏删掉。
void measure_screen(const lv_font_t* font, const char* content, const char* tag, double& first_ms,
                    double& warm_ms) {
  lv_obj_t* previous = lv_scr_act();
  lv_obj_t* screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101010), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_scr_load(screen);
  lv_obj_t* label = lv_label_create(screen);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
  lv_obj_set_pos(label, 2, 2);
  lv_label_set_text(label, content);
  first_ms = refresh_screen_ms(screen);
  warm_ms = refresh_screen_ms(screen);
  lv_scr_load(previous);
  lv_obj_del(screen);
  ELOG_INFO("[FONTPROBE] {:<28} 首帧 {:>8.2f} ms | 热帧 {:>8.2f} ms", tag, first_ms, warm_ms);
}

/// 卡上那份 TTF 的顺序读吞吐：32 KB 缓冲滚着读完，然后就把缓冲还掉。
void measure_sd_read() {
  const char* root = aidogui_res_fs_root();
  if (root == nullptr) {
    ELOG_WARN("[FONTPROBE] 'S' 盘没挂上，跳过卡上读速测量");
    return;
  }
  constexpr const char* kCandidates[] = {"/fonts/", "/font/"};
  char path[320];
  std::FILE* file = nullptr;
  for (const char* relative : kCandidates) {
    const int written = std::snprintf(path, sizeof path, "%s%sui_heiti.ttf", root, relative);
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof path) {
      continue;
    }
    file = std::fopen(path, "rb");
    if (file != nullptr) {
      break;
    }
  }
  if (file == nullptr) {
    ELOG_WARN("[FONTPROBE] 卡上找不到 ui_heiti.ttf（根 {}），跳过读速测量", root);
    return;
  }
  char* chunk = static_cast<char*>(std::malloc(kReadChunkBytes));  // NOLINT：探针专用的一块暂存
  if (chunk == nullptr) {
    std::fclose(file);
    ELOG_WARN("[FONTPROBE] 读速测量用的 32 KB 缓冲拿不到，跳过");
    return;
  }
  std::size_t total = 0U;
  const std::int64_t begin = now_us();
  for (;;) {
    const std::size_t got = std::fread(chunk, 1, kReadChunkBytes, file);
    total += got;
    if (got < kReadChunkBytes) {
      break;
    }
  }
  const double read_us = elapsed_us(begin);
  std::fclose(file);
  std::free(chunk);
  ELOG_INFO("[FONTPROBE] 卡上 TTF：{} 字节，顺序读完 {:.1f} ms（{:.2f} MB/s）",
            static_cast<unsigned>(total), read_us / 1000.0,
            read_us > 0.0 ? static_cast<double>(total) / read_us : 0.0);
}

}  // namespace

void run() {
  ELOG_INFO("[FONTPROBE] ===== 字体运行时栅格化自检（产品路径，ADR 0017）=====");
  ELOG_INFO(
      "[FONTPROBE] 进来时：LVGL 静态池 容量 {} / 未回收 {} / 峰值 {} 字节；"
      "字库区 容量 {} / 未回收 {} 字节；PSRAM 余 {}；内部堆余 {}",
      static_cast<unsigned>(lvgl_pool_capacity_bytes()),
      static_cast<unsigned>(lvgl_outstanding_bytes()), static_cast<unsigned>(lvgl_peak_bytes()),
      static_cast<unsigned>(embark::platform::font_alloc_pool_bytes()),
      static_cast<unsigned>(embark::platform::font_alloc_outstanding_bytes()),
      static_cast<unsigned>(psram_free_bytes()), static_cast<unsigned>(internal_free_bytes()));

  measure_sd_read();

  const embark::demo::AidoguiFontReport report = embark::demo::aidogui_ui_bridge_font_report();
  if (report.total == 0) {
    ELOG_WARN("[FONTPROBE] 桥没初始化（字号槽 0 个），跳过栅格化测量");
    return;
  }
  ELOG_INFO("[FONTPROBE] 桥报告：{}/{} 个字号建出来了", report.loaded, report.total);

  // ---- 1. 先用 14 px 收集"真的覆盖"的码点 --------------------------------
  const lv_font_t* sample_font = embark::demo::aidogui_ui_bridge_font(14);
  if (sample_font == nullptr) {
    ELOG_ERROR("[FONTPROBE] 14 px 槽是空的（没插卡？），一个数字也量不了");
    return;
  }
  std::uint32_t codepoints[kGlyphCount] = {0};
  std::size_t glyph_count = 0U;
  std::size_t scanned = 0U;
  for (std::uint32_t cp = kFirstCodepoint; cp <= kLastCodepoint && glyph_count < kGlyphCount;
       ++cp) {
    ++scanned;
    lv_font_glyph_dsc_t dsc{};
    if (lv_font_get_glyph_dsc(sample_font, &dsc, cp, 0)) {
      codepoints[glyph_count++] = cp;
    }
  }
  ELOG_INFO("[FONTPROBE] 扫 U+4E00 起 {} 个码点，命中字库 {} 个（缺字 {} 个）",
            static_cast<unsigned>(scanned), static_cast<unsigned>(glyph_count),
            static_cast<unsigned>(scanned - glyph_count));
  if (glyph_count == 0U) {
    ELOG_ERROR("[FONTPROBE] 一个码点都没命中，字体文件可能不是 ui_heiti.ttf");
    return;
  }

  // 拼成满屏文字（大字号只用前一段，一屏塞不下 120 个）
  char text[kGlyphCount * 3U + 1U] = {0};
  std::size_t written = 0U;
  for (std::size_t i = 0; i < glyph_count; ++i) {
    written += append_utf8(&text[written], codepoints[i]);
  }

  // ---- 2. 逐个字号量产品实例 --------------------------------------------
  const int size_count = embark::demo::aidogui_ui_bridge_font_size_count();
  for (int i = 0; i < size_count; ++i) {
    const int px = embark::demo::aidogui_ui_bridge_font_size(i);
    const lv_font_t* font = embark::demo::aidogui_ui_bridge_font(px);
    if (font == nullptr) {
      ELOG_WARN("[FONTPROBE] {} px 槽是空的，跳过", px);
      continue;
    }
    const std::size_t count = px > kLargePx ? kGlyphCountLarge : glyph_count;
    const std::size_t timing_count = count < 60U ? count : 60U;

    std::size_t drawn_cold = 0U;
    const double cold_us = rasterize(codepoints, timing_count, font, drawn_cold);
    std::size_t drawn_warm = 0U;
    const double warm_us = rasterize(codepoints, timing_count, font, drawn_warm);
    ELOG_INFO(
        "[FONTPROBE] {} px 冷栅格 {} 字（成 {} 字）：{:.1f} us ⇒ {:.2f} us/字；"
        "热命中 {:.1f} us ⇒ {:.2f} us/字；字库区已用 {} 字节",
        px, static_cast<unsigned>(timing_count), static_cast<unsigned>(drawn_cold), cold_us,
        cold_us / static_cast<double>(drawn_cold == 0U ? 1U : drawn_cold), warm_us,
        warm_us / static_cast<double>(drawn_warm == 0U ? 1U : drawn_warm),
        static_cast<unsigned>(embark::platform::font_alloc_outstanding_bytes()));

    // 屏上文字按字号截断：截在 UTF-8 边界上（样本全是 3 字节汉字，按 3 的倍数取）
    char tag[40];
    std::snprintf(tag, sizeof tag, "产品 %d px（%u 字）", px, static_cast<unsigned>(count));
    text[count * 3U] = '\0';
    double first_ms = 0.0;
    double warm_ms = 0.0;
    measure_screen(font, text, tag, first_ms, warm_ms);
    text[written] = '\0';  // 还原给下一个小字号用
  }

  // ---- 3. 绘制本底：内置 montserrat 14 px 画 120 个 'A' ------------------
  char ascii_text[kGlyphCount + 1U] = {0};
  for (std::size_t i = 0; i < kGlyphCount; ++i) {
    ascii_text[i] = 'A';
  }
  double ascii_first = 0.0;
  double ascii_warm = 0.0;
  measure_screen(&lv_font_montserrat_14, ascii_text, "内置 montserrat 14px（120A）", ascii_first,
                 ascii_warm);

  ELOG_INFO("[FONTPROBE] 收尾：字库区 已用 {} / 容量 {} 字节；PSRAM 余 {}；内部堆余 {}",
            static_cast<unsigned>(embark::platform::font_alloc_outstanding_bytes()),
            static_cast<unsigned>(embark::platform::font_alloc_pool_bytes()),
            static_cast<unsigned>(psram_free_bytes()),
            static_cast<unsigned>(internal_free_bytes()));
  ELOG_INFO("[FONTPROBE] ===== 测量结束 =====");
}

}  // namespace embark::platform::esp32::font_probe

#endif  // EMBARK_FONT_PROBE
