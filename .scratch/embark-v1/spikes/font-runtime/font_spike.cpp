/**
 * 字体运行时栅格化 spike —— 量 ADR 草案要的四组数据（判据见同目录 README.md）
 *
 * 做法：同一块 240×320 的假屏（绘制缓冲 2×240×40，对齐真机），同一句话 / 同一屏字，
 * 分别用两种字体画：
 *   BIN  = app/aidogui_ui/export/font/ui_font_ui_heiti_14.bin（构建期生成的字号位图表）
 *   TTF  = lv_tiny_ttf_create_data_ex()，同一份 ui_heiti.ttf，运行时按需栅格化
 *
 * 不链 platform_host（那个库自带 main 与 SDL 窗口），本文件自己实现 embark_lvgl_* 钩子。
 *
 * 一处必须点明的实现细节：lv_tiny_ttf_create_data_ex() 的第 4 个参数 cache_size 是
 * **字节预算**，不是条目数（lv_lru.c:73 total_memory = cache_size，第二参数只用来算
 * 哈希桶数 = cache_size / (font_size*font_size)）。LVGL 默认传 4096 ⇒ 14px 下只能装
 * 约 20 个字形，一屏 150 字每帧都在重栅格化。下面按 4 KB / 32 KB / 96 KB 三档量。
 */
#include <lvgl.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "embark_lvgl_hooks.h"

namespace {

// ===== LVGL 内存钩子（宿主实现的最小版：块头记账，够看峰值即可）================
struct alignas(std::max_align_t) BlockHeader {
  std::size_t size;
};

std::size_t g_outstanding = 0;
std::size_t g_peak = 0;

BlockHeader* header_of(void* pointer) noexcept
{
  return static_cast<BlockHeader*>(pointer) - 1;
}

// ===== 假屏 ===================================================================
constexpr int kScreenW = 240;
constexpr int kScreenH = 320;
constexpr int kBufferLines = 40;  // 真机同款：platform/esp32 的绘制缓冲是 40 行

std::uint16_t g_framebuffer[kScreenW * kScreenH];
lv_color_t g_draw_buffer_1[kScreenW * kBufferLines];
lv_color_t g_draw_buffer_2[kScreenW * kBufferLines];
lv_disp_draw_buf_t g_draw_buf;
lv_disp_drv_t g_disp_drv;
lv_disp_t* g_display = nullptr;

// ===== 'S' 盘（stdio，指向启动目录）：只为 lv_font_load 能读 BIN ===============
lv_fs_drv_t g_fs_drv;
std::string g_fs_root = ".";

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point start)
{
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

void print_rule(const char* title)
{
  std::printf("\n================ %s ================\n", title);
}

double median_of(std::vector<double> values)
{
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

const char* describe(std::uint32_t codepoint, char* scratch, std::size_t bytes)
{
  if (codepoint < 0x80U) {
    std::snprintf(scratch, bytes, "U+%04X '%c'", static_cast<unsigned>(codepoint),
                  static_cast<char>(codepoint));
  } else {
    std::snprintf(scratch, bytes, "U+%04X", static_cast<unsigned>(codepoint));
  }
  return scratch;
}

}  // namespace

extern "C" {

void* embark_lvgl_alloc(std::size_t size)
{
  auto* raw = static_cast<BlockHeader*>(std::malloc(sizeof(BlockHeader) + size));
  if (raw == nullptr) {
    return nullptr;
  }
  raw->size = size;
  g_outstanding += size;
  g_peak = std::max(g_peak, g_outstanding);
  return raw + 1;
}

void embark_lvgl_free(void* pointer)
{
  if (pointer == nullptr) {
    return;
  }
  BlockHeader* header = header_of(pointer);
  g_outstanding -= header->size;
  std::free(header);
}

void* embark_lvgl_realloc(void* pointer, std::size_t size)
{
  if (pointer == nullptr) {
    return embark_lvgl_alloc(size);
  }
  BlockHeader* header = header_of(pointer);
  const std::size_t previous = header->size;
  g_outstanding -= previous;
  auto* raw = static_cast<BlockHeader*>(std::realloc(header, sizeof(BlockHeader) + size));
  if (raw == nullptr) {
    g_outstanding += previous;
    return nullptr;
  }
  raw->size = size;
  g_outstanding += size;
  g_peak = std::max(g_peak, g_outstanding);
  return raw + 1;
}

void embark_lvgl_assert_failed(void)
{
  std::fprintf(stderr, "\n[spike] LVGL 断言失败，终止\n");
  std::fflush(stderr);
  std::abort();
}

std::size_t embark_lvgl_outstanding_bytes(void)
{
  return g_outstanding;
}

std::size_t embark_lvgl_peak_bytes(void)
{
  return g_peak;
}

std::size_t embark_lvgl_budget_bytes(void)
{
  return 64U * 1024U * 1024U;
}

}  // extern "C"

namespace {

// ===== 显示与文件系统驱动 =====================================================
void flush_cb(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* color_p)
{
  const int width = area->x2 - area->x1 + 1;
  for (int y = area->y1; y <= area->y2; ++y) {
    std::memcpy(&g_framebuffer[y * kScreenW + area->x1], color_p,
                static_cast<std::size_t>(width) * sizeof(std::uint16_t));
    color_p += width;
  }
  lv_disp_flush_ready(driver);
}

void* fs_open(lv_fs_drv_t*, const char* path, lv_fs_mode_t mode)
{
  if ((mode & LV_FS_MODE_RD) == 0) {
    return nullptr;
  }
  const std::string full = g_fs_root + "/" + path;
  return static_cast<void*>(std::fopen(full.c_str(), "rb"));
}

lv_fs_res_t fs_close(lv_fs_drv_t*, void* file_p)
{
  return std::fclose(static_cast<FILE*>(file_p)) == 0 ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

lv_fs_res_t fs_read(lv_fs_drv_t*, void* file_p, void* buf, std::uint32_t bytes_to_read,
                    std::uint32_t* bytes_read)
{
  const std::size_t got = std::fread(buf, 1, bytes_to_read, static_cast<FILE*>(file_p));
  if (bytes_read != nullptr) {
    *bytes_read = static_cast<std::uint32_t>(got);
  }
  return LV_FS_RES_OK;
}

lv_fs_res_t fs_seek(lv_fs_drv_t*, void* file_p, std::uint32_t pos, lv_fs_whence_t whence)
{
  int origin = SEEK_SET;
  if (whence == LV_FS_SEEK_CUR) {
    origin = SEEK_CUR;
  } else if (whence == LV_FS_SEEK_END) {
    origin = SEEK_END;
  }
  return std::fseek(static_cast<FILE*>(file_p), static_cast<long>(pos), origin) == 0
             ? LV_FS_RES_OK
             : LV_FS_RES_FS_ERR;
}

lv_fs_res_t fs_tell(lv_fs_drv_t*, void* file_p, std::uint32_t* pos_p)
{
  *pos_p = static_cast<std::uint32_t>(std::ftell(static_cast<FILE*>(file_p)));
  return LV_FS_RES_OK;
}

void register_stdio_drive(char letter, const char* root)
{
  g_fs_root = root;
  lv_fs_drv_init(&g_fs_drv);
  g_fs_drv.letter = letter;
  g_fs_drv.cache_size = 0;
  g_fs_drv.open_cb = fs_open;
  g_fs_drv.close_cb = fs_close;
  g_fs_drv.read_cb = fs_read;
  g_fs_drv.seek_cb = fs_seek;
  g_fs_drv.tell_cb = fs_tell;
  lv_fs_drv_register(&g_fs_drv);
}

void bring_up_display()
{
  lv_disp_draw_buf_init(&g_draw_buf, g_draw_buffer_1, g_draw_buffer_2, kScreenW * kBufferLines);
  lv_disp_drv_init(&g_disp_drv);
  g_disp_drv.hor_res = kScreenW;
  g_disp_drv.ver_res = kScreenH;
  g_disp_drv.draw_buf = &g_draw_buf;
  g_disp_drv.flush_cb = flush_cb;
  g_display = lv_disp_drv_register(&g_disp_drv);
}

// ===== 测量原语 ===============================================================
/// 整屏重画一次：先让全屏失效，再同步刷新（含排版 + 栅格化 + 绘制 + flush）。
double refresh_whole_screen()
{
  lv_obj_invalidate(lv_disp_get_scr_act(g_display));
  const Clock::time_point start = Clock::now();
  lv_refr_now(g_display);
  return elapsed_ms(start);
}

/// 在屏上放一个定宽 label，然后连续整屏刷新 frames 次。
/// 返回每帧耗时；第 0 帧 = 该字体对象的"首帧"（TTF 缓存空 ⇒ 冷），其后为热。
std::vector<double> run_label_frames(lv_font_t* font, const std::string& text, int frames)
{
  lv_obj_t* label = lv_label_create(lv_disp_get_scr_act(g_display));
  lv_obj_set_width(label, kScreenW - 8);
  lv_obj_set_pos(label, 4, 4);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(label, text.c_str());

  std::vector<double> frames_out;
  frames_out.reserve(static_cast<std::size_t>(frames));
  for (int i = 0; i < frames; ++i) {
    frames_out.push_back(refresh_whole_screen());
  }
  lv_obj_del(label);
  return frames_out;
}

void print_frames(const char* name, const std::vector<double>& frames)
{
  std::printf("%-28s 首帧 %7.2f ms | 热帧中位 %7.2f ms  (", name, frames.front(),
              median_of(std::vector<double>(frames.begin() + 1, frames.end())));
  for (std::size_t i = 0; i < frames.size(); ++i) {
    std::printf("%s%.2f", i == 0 ? "" : " ", frames[i]);
  }
  std::printf(")\n");
}

/// BIN 字体覆盖了哪些码点：候选集里能查到真实字形（非占位）的。
std::vector<std::uint32_t> covered_codepoints(lv_font_t* font,
                                             const std::vector<std::uint32_t>& candidates)
{
  std::vector<std::uint32_t> covered;
  lv_font_glyph_dsc_t dsc{};
  for (std::uint32_t codepoint : candidates) {
    if (lv_font_get_glyph_dsc(font, &dsc, codepoint, 0) && !dsc.is_placeholder) {
      covered.push_back(codepoint);
    }
  }
  return covered;
}

std::vector<std::uint32_t> codepoint_candidates()
{
  std::vector<std::uint32_t> all;
  for (std::uint32_t cp = 0x20; cp <= 0x7E; ++cp) {
    all.push_back(cp);
  }
  for (std::uint32_t cp = 0x4E00; cp <= 0x9FFF; ++cp) {
    all.push_back(cp);
  }
  for (std::uint32_t cp = 0x3000; cp <= 0x303F; ++cp) {
    all.push_back(cp);
  }
  for (std::uint32_t cp = 0xFF01; cp <= 0xFF5E; ++cp) {
    all.push_back(cp);
  }
  return all;
}

std::string utf8_of(const std::vector<std::uint32_t>& codepoints, std::size_t count)
{
  std::string out;
  for (std::size_t i = 0; i < count && i < codepoints.size(); ++i) {
    const std::uint32_t cp = codepoints[i];
    if (cp < 0x80U) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800U) {
      out.push_back(static_cast<char>(0xC0U | (cp >> 6)));
      out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else {
      out.push_back(static_cast<char>(0xE0U | (cp >> 12)));
      out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    }
  }
  return out;
}

// ===== 判据 3：逐字度量偏差 ===================================================
void report_metrics(lv_font_t* bin_font, lv_font_t* ttf_font,
                    const std::vector<std::uint32_t>& covered)
{
  struct Field {
    const char* name;
    long max_delta;
    double sum_delta;
    int over_one;
  };
  Field fields[5] = {{"adv_w", 0, 0.0, 0},
                     {"box_w", 0, 0.0, 0},
                     {"box_h", 0, 0.0, 0},
                     {"ofs_x", 0, 0.0, 0},
                     {"ofs_y", 0, 0.0, 0}};

  struct Worst {
    long delta;
    std::uint32_t codepoint;
    const char* field;
    long bin_value;
    long ttf_value;
  };
  Worst worst[5] = {{-1, 0, "", 0, 0}, {-1, 0, "", 0, 0}, {-1, 0, "", 0, 0}, {-1, 0, "", 0, 0},
                    {-1, 0, "", 0, 0}};

  int compared = 0;
  lv_font_glyph_dsc_t bin_dsc{};
  lv_font_glyph_dsc_t ttf_dsc{};
  for (std::uint32_t cp : covered) {
    if (!lv_font_get_glyph_dsc(bin_font, &bin_dsc, cp, 0)) {
      continue;
    }
    if (!lv_font_get_glyph_dsc(ttf_font, &ttf_dsc, cp, 0)) {
      continue;
    }
    ++compared;
    const long bin_values[5] = {static_cast<long>(bin_dsc.adv_w), static_cast<long>(bin_dsc.box_w),
                                static_cast<long>(bin_dsc.box_h), static_cast<long>(bin_dsc.ofs_x),
                                static_cast<long>(bin_dsc.ofs_y)};
    const long ttf_values[5] = {static_cast<long>(ttf_dsc.adv_w), static_cast<long>(ttf_dsc.box_w),
                                static_cast<long>(ttf_dsc.box_h), static_cast<long>(ttf_dsc.ofs_x),
                                static_cast<long>(ttf_dsc.ofs_y)};
    for (int i = 0; i < 5; ++i) {
      const long delta = ttf_values[i] - bin_values[i];
      const long absolute = delta < 0 ? -delta : delta;
      fields[i].max_delta = std::max(fields[i].max_delta, absolute);
      fields[i].sum_delta += static_cast<double>(absolute);
      if (absolute > 1) {
        ++fields[i].over_one;
      }
      // 只把"偏差最大的 5 例"记下来（同一码点各字段分别参与比较）
      for (int slot = 0; slot < 5; ++slot) {
        if (absolute > worst[slot].delta) {
          for (int move = 4; move > slot; --move) {
            worst[move] = worst[move - 1];
          }
          worst[slot] = Worst{absolute, cp, fields[i].name, bin_values[i], ttf_values[i]};
          break;
        }
      }
    }
  }

  std::printf("参与比较的码点：%d 个（BIN 里有的）\n", compared);
  std::printf("线高 / 基线：BIN line_height=%d base_line=%d | TTF line_height=%d base_line=%d\n",
              static_cast<int>(bin_font->line_height), static_cast<int>(bin_font->base_line),
              static_cast<int>(ttf_font->line_height), static_cast<int>(ttf_font->base_line));
  for (const Field& field : fields) {
    std::printf("  %-6s 最大偏差 %2ld px | 平均 %.2f px | 偏差 > 1 px 的码点 %d 个\n", field.name,
                field.max_delta, compared > 0 ? field.sum_delta / compared : 0.0, field.over_one);
  }
  std::printf("偏差最大的例子：\n");
  char scratch[32];
  for (const Worst& w : worst) {
    if (w.delta < 0) {
      continue;
    }
    std::printf("  %-6s %s   BIN=%ld  TTF=%ld   Δ=%ld\n", w.field, describe(w.codepoint, scratch,
                                                                          sizeof(scratch)),
                w.bin_value, w.ttf_value, w.delta);
  }
}

// ===== 像素级对照：同一字形两种字体画出来差多少 ===============================
constexpr int kTile = 48;

void set_screen_background(std::uint32_t rgb)
{
  lv_obj_t* scr = lv_disp_get_scr_act(g_display);
  lv_obj_set_style_bg_color(scr, lv_color_hex(rgb), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
}

void capture_tile(std::uint16_t* out, int x, int y, int side)
{
  for (int row = 0; row < side; ++row) {
    for (int col = 0; col < side; ++col) {
      out[row * side + col] = g_framebuffer[(y + row) * kScreenW + (x + col)];
    }
  }
}

/// 屏上只留一个字形，整屏刷新后抓 48×48。
void render_single_glyph(std::uint32_t codepoint, lv_font_t* font, std::uint16_t* out)
{
  lv_obj_t* scr = lv_disp_get_scr_act(g_display);
  lv_obj_clean(scr);
  const std::string text = utf8_of(std::vector<std::uint32_t>{codepoint}, 1);
  lv_obj_t* label = lv_label_create(scr);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), 0);
  lv_obj_set_pos(label, 4, 4);
  lv_label_set_text(label, text.c_str());
  refresh_whole_screen();
  capture_tile(out, 0, 0, kTile);
  lv_obj_clean(scr);
}

void rgb_of(std::uint16_t pixel, int& r, int& g, int& b)
{
  r = ((pixel >> 11) & 0x1FU) * 255 / 31;
  g = ((pixel >> 5) & 0x3FU) * 255 / 63;
  b = (pixel & 0x1FU) * 255 / 31;
}

void write_bmp_24(const char* path, const std::uint16_t* canvas, int width, int height)
{
  std::FILE* out = std::fopen(path, "wb");
  if (out == nullptr) {
    return;
  }
  const int row_bytes = width * 3;
  const int padding = (4 - (row_bytes % 4)) % 4;
  const int image_bytes = (row_bytes + padding) * height;
  const int file_bytes = 54 + image_bytes;
  unsigned char header[54] = {0};
  header[0] = 'B';
  header[1] = 'M';
  header[2] = static_cast<unsigned char>(file_bytes & 0xFF);
  header[3] = static_cast<unsigned char>((file_bytes >> 8) & 0xFF);
  header[4] = static_cast<unsigned char>((file_bytes >> 16) & 0xFF);
  header[5] = static_cast<unsigned char>((file_bytes >> 24) & 0xFF);
  header[10] = 54;
  header[14] = 40;
  header[18] = static_cast<unsigned char>(width & 0xFF);
  header[19] = static_cast<unsigned char>((width >> 8) & 0xFF);
  header[22] = static_cast<unsigned char>(height & 0xFF);
  header[23] = static_cast<unsigned char>((height >> 8) & 0xFF);
  header[26] = 1;
  header[28] = 24;
  header[34] = static_cast<unsigned char>(image_bytes & 0xFF);
  header[35] = static_cast<unsigned char>((image_bytes >> 8) & 0xFF);
  header[36] = static_cast<unsigned char>((image_bytes >> 16) & 0xFF);
  header[37] = static_cast<unsigned char>((image_bytes >> 24) & 0xFF);
  std::fwrite(header, 1, sizeof(header), out);
  const unsigned char zero[3] = {0, 0, 0};
  for (int y = height - 1; y >= 0; --y) {  // BMP 自下而上
    for (int x = 0; x < width; ++x) {
      int r = 0;
      int g = 0;
      int b = 0;
      rgb_of(canvas[y * width + x], r, g, b);
      const unsigned char bgr[3] = {static_cast<unsigned char>(b), static_cast<unsigned char>(g),
                                    static_cast<unsigned char>(r)};
      std::fwrite(bgr, 1, 3, out);
    }
    if (padding > 0) {
      std::fwrite(zero, 1, static_cast<std::size_t>(padding), out);
    }
  }
  std::fclose(out);
}

void compare_pixels(lv_font_t* bin_font, const std::uint8_t* ttf_data, std::size_t ttf_bytes,
                    const std::vector<std::uint32_t>& codepoints)
{
  const std::size_t count = codepoints.size();
  const int canvas_width = static_cast<int>(count) * kTile;
  std::vector<std::uint16_t> canvas(static_cast<std::size_t>(canvas_width) * kTile * 3, 0);
  std::vector<std::uint16_t> bin_tile(kTile * kTile, 0);
  std::vector<std::uint16_t> ttf_tile(kTile * kTile, 0);

  lv_font_t* ttf = lv_tiny_ttf_create_data_ex(ttf_data, ttf_bytes, 14, 96 * 1024);
  set_screen_background(0x101010);

  std::printf("%-10s %-18s %-18s %8s %8s %8s\n", "码点", "BIN box/ofs", "TTF box/ofs", "异像素",
              "占比", "最大通道差");
  int worst_delta = 0;
  double worst_ratio = 0.0;
  for (std::size_t i = 0; i < count; ++i) {
    render_single_glyph(codepoints[i], bin_font, bin_tile.data());
    render_single_glyph(codepoints[i], ttf, ttf_tile.data());

    int differing = 0;
    int max_delta = 0;
    for (int p = 0; p < kTile * kTile; ++p) {
      int r1 = 0;
      int g1 = 0;
      int b1 = 0;
      int r2 = 0;
      int g2 = 0;
      int b2 = 0;
      rgb_of(bin_tile[static_cast<std::size_t>(p)], r1, g1, b1);
      rgb_of(ttf_tile[static_cast<std::size_t>(p)], r2, g2, b2);
      const int delta = std::max(std::max(std::abs(r2 - r1), std::abs(g2 - g1)),
                                 std::abs(b2 - b1));
      if (delta > 8) {
        ++differing;
      }
      max_delta = std::max(max_delta, delta);
    }
    const double ratio = 100.0 * static_cast<double>(differing) / (kTile * kTile);
    worst_delta = std::max(worst_delta, max_delta);
    worst_ratio = std::max(worst_ratio, ratio);

    lv_font_glyph_dsc_t bin_dsc{};
    lv_font_glyph_dsc_t ttf_dsc{};
    lv_font_get_glyph_dsc(bin_font, &bin_dsc, codepoints[i], 0);
    lv_font_get_glyph_dsc(ttf, &ttf_dsc, codepoints[i], 0);
    char scratch[32];
    char bin_text[32];
    char ttf_text[32];
    std::snprintf(bin_text, sizeof(bin_text), "%dx%d ofs %d,%d", static_cast<int>(bin_dsc.box_w),
                  static_cast<int>(bin_dsc.box_h), static_cast<int>(bin_dsc.ofs_x),
                  static_cast<int>(bin_dsc.ofs_y));
    std::snprintf(ttf_text, sizeof(ttf_text), "%dx%d ofs %d,%d", static_cast<int>(ttf_dsc.box_w),
                  static_cast<int>(ttf_dsc.box_h), static_cast<int>(ttf_dsc.ofs_x),
                  static_cast<int>(ttf_dsc.ofs_y));
    std::printf("%-10s %-18s %-18s %8d %7.1f%% %8d\n",
                describe(codepoints[i], scratch, sizeof(scratch)), bin_text, ttf_text, differing,
                ratio, max_delta);

    for (int row = 0; row < kTile; ++row) {
      for (int col = 0; col < kTile; ++col) {
        const std::size_t dst = static_cast<std::size_t>(row) * canvas_width + i * kTile + col;
        canvas[dst] = bin_tile[static_cast<std::size_t>(row) * kTile + col];
        canvas[static_cast<std::size_t>(kTile + row) * canvas_width + i * kTile + col] =
            ttf_tile[static_cast<std::size_t>(row) * kTile + col];
        // 差异图：每个通道差 ×4 提亮，肉眼看"哪里不一样"
        int r1 = 0;
        int g1 = 0;
        int b1 = 0;
        int r2 = 0;
        int g2 = 0;
        int b2 = 0;
        rgb_of(bin_tile[static_cast<std::size_t>(row) * kTile + col], r1, g1, b1);
        rgb_of(ttf_tile[static_cast<std::size_t>(row) * kTile + col], r2, g2, b2);
        const int dr = std::min(255, std::abs(r2 - r1) * 4);
        const int dg = std::min(255, std::abs(g2 - g1) * 4);
        const int db = std::min(255, std::abs(b2 - b1) * 4);
        const std::uint16_t pixel = static_cast<std::uint16_t>(
            ((dr >> 3) << 11) | ((dg >> 2) << 5) | (db >> 3));
        canvas[static_cast<std::size_t>(2 * kTile + row) * canvas_width + i * kTile + col] = pixel;
      }
    }
  }
  write_bmp_24(".scratch/fonttest/glyph_compare.bmp", canvas.data(), canvas_width, kTile * 3);
  std::printf("最差：异像素占比 %.1f%%，最大通道差 %d/255；对照图 .scratch/fonttest/glyph_compare.bmp"
              "（上=BIN 14px，中=运行时 14px，下=差异×4）\n",
              worst_ratio, worst_delta);
  set_screen_background(0xffffff);
  lv_tiny_ttf_destroy(ttf);
}

// ===== 逐字栅格化裸耗时 =======================================================
double rasterize_all(lv_font_t* font, const std::vector<std::uint32_t>& codepoints,
                     std::size_t* bitmap_bytes)
{
  std::size_t total_bytes = 0;
  const Clock::time_point start = Clock::now();
  for (std::uint32_t cp : codepoints) {
    lv_font_glyph_dsc_t dsc{};
    if (!lv_font_get_glyph_dsc(font, &dsc, cp, 0)) {
      continue;
    }
    const std::uint8_t* bitmap = lv_font_get_glyph_bitmap(font, cp);
    if (bitmap != nullptr) {
      total_bytes += static_cast<std::size_t>(dsc.box_w) * static_cast<std::size_t>(dsc.box_h);
    }
  }
  if (bitmap_bytes != nullptr) {
    *bitmap_bytes = total_bytes;
  }
  return elapsed_ms(start);
}

}  // namespace

int main(int argc, char** argv)
{
  const char* ttf_path = argc > 1 ? argv[1] : "app/aidogui_ui/assets/font/ui_heiti.ttf";
  const char* bin_path =
      argc > 2 ? argv[2] : "app/aidogui_ui/export/font/ui_font_ui_heiti_14.bin";

  // ---- 读 TTF（判据 4 的宿主侧输入：整份 TTF 进 RAM 要多久） ----
  const Clock::time_point read_start = Clock::now();
  std::FILE* ttf_file = std::fopen(ttf_path, "rb");
  if (ttf_file == nullptr) {
    std::fprintf(stderr, "打不开 TTF：%s\n", ttf_path);
    return 1;
  }
  std::fseek(ttf_file, 0, SEEK_END);
  const long ttf_bytes = std::ftell(ttf_file);
  std::fseek(ttf_file, 0, SEEK_SET);
  auto* ttf_data = static_cast<std::uint8_t*>(std::malloc(static_cast<std::size_t>(ttf_bytes)));
  const std::size_t read_bytes = std::fread(ttf_data, 1, static_cast<std::size_t>(ttf_bytes), ttf_file);
  std::fclose(ttf_file);
  const double read_ms = elapsed_ms(read_start);

  lv_init();
  bring_up_display();
  register_stdio_drive('S', ".");

  lv_font_t* bin_font = lv_font_load(std::string("S:/").append(bin_path).c_str());
  if (bin_font == nullptr) {
    std::fprintf(stderr, "lv_font_load 失败：%s\n", bin_path);
    return 1;
  }

  std::printf("==== 字体运行时栅格化 spike ====\n");
  std::printf("LVGL %d.%d.%d | 屏 %dx%d RGB565 | 绘制缓冲 2 x %dx%d\n", LVGL_VERSION_MAJOR,
              LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, kScreenW, kScreenH, kScreenW, kBufferLines);
  std::printf("TTF  %s  %ld 字节（读入 RAM %.1f ms）\n", ttf_path, ttf_bytes, read_ms);
  std::printf("BIN  %s  line_height=%d base_line=%d\n", bin_path,
              static_cast<int>(bin_font->line_height), static_cast<int>(bin_font->base_line));

  // ---- 空屏基线 ----
  print_rule("基线：空屏整屏刷新");
  std::vector<double> baseline;
  for (int i = 0; i < 5; ++i) {
    baseline.push_back(refresh_whole_screen());
  }
  std::printf("空屏（无文字）中位 %.2f ms —— 下面各行都要减掉它才是字形代价\n", median_of(baseline));

  // ---- 覆盖集 ----
  const std::vector<std::uint32_t> candidates = codepoint_candidates();
  const std::vector<std::uint32_t> covered = covered_codepoints(bin_font, candidates);
  std::size_t cjk_covered = 0;
  for (std::uint32_t cp : covered) {
    if (cp >= 0x4E00U && cp <= 0x9FFFU) {
      ++cjk_covered;
    }
  }
  std::printf("\n14px BIN 覆盖 %zu 个码点（其中 CJK %zu 个）\n", covered.size(), cjk_covered);

  // ---- 判据 3：度量偏差 ----
  print_rule("判据 3：逐字度量偏差（BIN 14px vs 运行时 14px）");
  {
    const std::size_t outstanding_before = embark_lvgl_outstanding_bytes();
    lv_font_t* ttf_14 = lv_tiny_ttf_create_data_ex(ttf_data, read_bytes, 14, 96 * 1024);
    report_metrics(bin_font, ttf_14, covered);
    std::printf("TTF 字体对象 %zu B（含 96 KB 缓存预算，实际占用见内存账单）\n",
                embark_lvgl_outstanding_bytes() - outstanding_before);
    lv_tiny_ttf_destroy(ttf_14);
  }

  // ---- 像素级对照 ----
  print_rule("像素级对照：同一字形 BIN vs 运行时（14px，48x48 逐字抓图）");
  {
    std::vector<std::uint32_t> sample;
    for (std::uint32_t cp : covered) {
      if (cp >= 0x4E00U && cp <= 0x9FFFU && sample.size() < 10) {
        sample.push_back(cp);
      }
    }
    sample.push_back(0x4E2DU);  // 中
    sample.push_back(0x56FEU);  // 图
    compare_pixels(bin_font, ttf_data, read_bytes, sample);
  }

  // ---- 判据 1：一句话 20 字 ----
  print_rule("判据 1：一句话（20 个中文字）@14px");
  std::vector<std::uint32_t> first_cjk;
  for (std::uint32_t cp : covered) {
    if (cp >= 0x4E00U && cp <= 0x9FFFU) {
      first_cjk.push_back(cp);
      if (first_cjk.size() >= 20) {
        break;
      }
    }
  }
  const std::string sentence = utf8_of(first_cjk, first_cjk.size());
  std::printf("文本（BIN 全覆盖的 20 个中文字）：%s\n", sentence.c_str());
  {
    std::printf("BIN :\n");
    print_frames("  BIN 整屏刷新", run_label_frames(bin_font, sentence, 5));

    lv_font_t* ttf_14 = lv_tiny_ttf_create_data_ex(ttf_data, read_bytes, 14, 96 * 1024);
    print_frames("  TTF 96KB 缓存", run_label_frames(ttf_14, sentence, 5));
    lv_tiny_ttf_destroy(ttf_14);

    lv_font_t* ttf_small = lv_tiny_ttf_create_data_ex(ttf_data, read_bytes, 14, 4096);
    print_frames("  TTF 默认 4KB 缓存", run_label_frames(ttf_small, sentence, 5));
    lv_tiny_ttf_destroy(ttf_small);
  }

  // ---- 判据 2：一屏 150 个不同字形 ----
  print_rule("判据 2：一屏 150 个不同字形 @14px");
  std::vector<std::uint32_t> screen_chars = first_cjk;
  {
    std::size_t want = 150;
    screen_chars.clear();
    for (std::uint32_t cp : covered) {
      if (cp >= 0x4E00U && cp <= 0x9FFFU) {
        screen_chars.push_back(cp);
        if (screen_chars.size() >= want) {
          break;
        }
      }
    }
    want = screen_chars.size();
    std::printf("取 BIN 覆盖集里的前 %zu 个 CJK 字\n", want);
  }
  const std::string screen_text = utf8_of(screen_chars, screen_chars.size());
  {
    print_frames("BIN 整屏刷新", run_label_frames(bin_font, screen_text, 5));

    lv_font_t* ttf_big = lv_tiny_ttf_create_data_ex(ttf_data, read_bytes, 14, 96 * 1024);
    print_frames("TTF 96KB 缓存", run_label_frames(ttf_big, screen_text, 5));
    lv_tiny_ttf_destroy(ttf_big);

    lv_font_t* ttf_default = lv_tiny_ttf_create_data_ex(ttf_data, read_bytes, 14, 4096);
    print_frames("TTF 默认 4KB 缓存", run_label_frames(ttf_default, screen_text, 5));
    lv_tiny_ttf_destroy(ttf_default);
  }

  // ---- 逐字栅格化裸耗时 ----
  print_rule("逐字栅格化裸耗时（绕过排版，150 次 get_glyph_bitmap）");
  {
    lv_font_t* ttf_14 = lv_tiny_ttf_create_data_ex(ttf_data, read_bytes, 14, 96 * 1024);
    std::size_t bitmap_bytes = 0;
    const double cold_ms = rasterize_all(ttf_14, screen_chars, &bitmap_bytes);
    const double warm_ms = rasterize_all(ttf_14, screen_chars, nullptr);
    std::printf("TTF 冷 %.2f ms（%.1f us/字）| 热 %.3f ms | 位图共 %zu 字节\n", cold_ms,
                cold_ms * 1000.0 / static_cast<double>(screen_chars.size()), warm_ms, bitmap_bytes);
    lv_tiny_ttf_destroy(ttf_14);

    std::size_t bin_bytes = 0;
    const double bin_ms = rasterize_all(bin_font, screen_chars, &bin_bytes);
    std::printf("BIN 取位图 %.3f ms（本来就躺在文件里，位图共 %zu 字节）\n", bin_ms, bin_bytes);
  }

  // ---- 内存账单 ----
  print_rule("内存账单：字形缓存预算 vs 实际占用（150 字，14px）");
  for (std::size_t budget : {static_cast<std::size_t>(4096), static_cast<std::size_t>(32 * 1024),
                             static_cast<std::size_t>(96 * 1024)}) {
    const std::size_t before = embark_lvgl_outstanding_bytes();
    lv_font_t* ttf = lv_tiny_ttf_create_data_ex(ttf_data, read_bytes, 14, budget);
    const std::size_t after_create = embark_lvgl_outstanding_bytes();
    (void)rasterize_all(ttf, screen_chars, nullptr);
    const std::size_t after_fill = embark_lvgl_outstanding_bytes();
    std::printf("缓存预算 %6zu B（%3zu 桶）：对象 %zu B，灌满 150 字后 LVGL 占用 %zu B\n", budget,
                budget / (14 * 14), after_create - before, after_fill - before);
    lv_tiny_ttf_destroy(ttf);
  }
  std::printf("LVGL 未回收 %zu B / 峰值 %zu B（含 BIN 字体对象与绘制缓冲）\n",
              embark_lvgl_outstanding_bytes(), embark_lvgl_peak_bytes());

  // ---- 换字号 ----
  print_rule("换字号：lv_tiny_ttf_set_size() 之后的行为（阅读器刚需）");
  {
    lv_font_t* ttf = lv_tiny_ttf_create_data_ex(ttf_data, read_bytes, 14, 64 * 1024);
    print_frames("14px 首帧/热帧", run_label_frames(ttf, screen_text, 3));
    const std::size_t before = embark_lvgl_outstanding_bytes();
    lv_tiny_ttf_set_size(ttf, 22);
    std::printf("set_size(22)：line_height=%d base_line=%d，LVGL 占用 %zu → %zu B（旧缓存不回收）\n",
                static_cast<int>(ttf->line_height), static_cast<int>(ttf->base_line), before,
                embark_lvgl_outstanding_bytes());
    print_frames("22px 首帧/热帧", run_label_frames(ttf, screen_text, 3));
    std::printf("→ 结论：同一对象换字号会留下按旧 line_height 键控的残留条目，且缓存预算不变；"
                "每字号一个字体对象更省心（TTF 数据可共用同一份指针）\n");
    lv_tiny_ttf_destroy(ttf);
  }

  // ---- 收尾 ----
  print_rule("结论用的原始数字");
  std::printf("TTF 整份读入 RAM：%.1f ms / %ld 字节（真机若从 SD 卡 10MHz SPI 读，约 %.1f s —— "
              "这是把 TTF 放 flash 分区 + esp_partition_mmap 的理由）\n",
              read_ms, ttf_bytes,
              static_cast<double>(ttf_bytes) / (1024.0 * 1024.0) * 1.25 /* 10MHz SPI ≈ 1.25 MB/s 实测档 */);

  lv_font_free(bin_font);
  std::free(ttf_data);
  return 0;
}