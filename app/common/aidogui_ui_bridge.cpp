/**
 * AiDoGUI UI 薄桥实现（ADR 0011）：见 aidogui_ui_bridge.h 头注释。
 *
 * 表头是构建期生成的：cmake/embark_aidogui_tables.cmake 在配置期解析 app/aidogui_ui/export，
 * 写出构建目录 include/embark_aidogui_ui_tables.h，由 embark::aidogui_ui 的 PUBLIC include 根
 * 带进来。它是自含的：自己 extern 用到的控件/字体符号，不 include 生成代码的 ui.h —— 两个
 * 目标也有一份 ui.h，两边同名必然打架（谁在 include 路径前面谁赢，非常隐蔽）。
 */
#include "aidogui_ui_bridge.h"

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <embark/log.h>
#include <embark/version.h>

#include "aidogui_res_fs.h"
#include "embark_aidogui_ui_tables.h"
#include "font_alloc_scope.h"
#include "font_quota.h"

// 字体方案 = TTF 运行期栅格化（ADR 0017）：没有 LVGL 的 tiny_ttf 就编不出这条路径。
#if !defined(LV_USE_TINY_TTF) || !LV_USE_TINY_TTF
#error "AiDoGUI 桥需要 LV_USE_TINY_TTF=1（config/lv_conf.h；ADR 0017 的 TTF 字体方案）"
#endif

namespace embark::demo {
namespace {

bool g_initialized = false;
const aidogui_screen_entry_t* g_current = nullptr;  // 当前装载的屏（未装载 nullptr）
AidoguiScreenObserver g_observer = nullptr;
void* g_observer_user = nullptr;
AidoguiFontReport g_font_report{0, 0};

/// 整份 TTF（ADR 0017）：一次读进来就常驻（lv_tiny_ttf 只存指针、不复制、不读文件）。
/// 真机在 SD 卡 fonts/ 下，宿主在导出目录旁边的 assets/font/ 下 —— 见 read_ttf()。
std::uint8_t* g_ttf_data = nullptr;
std::size_t g_ttf_size = 0U;
/// tiny_ttf 原始的取图回调（7 个实例共用同一份实现，存一份就够）。
const std::uint8_t* (*g_ttf_bitmap_original)(const lv_font_t*, std::uint32_t) = nullptr;
bool g_ttf_scope_active = false;

/// 空屏：显示器上的兜底当前屏（懒建，桥终身持有，destroy 也不删）。
/// 为什么必须有：LVGL 8 的 lv_scr_load() 假定 lv_scr_act() 非空 —— 删掉当前屏后
/// disp->act_scr 会归 NULL（lv_obj_tree.c:72），再装载时 lv_disp.c 会对 NULL 调
/// lv_obj_set_pos()，直接访问冲突（实测退出码 -1073741819）。所以桥保证
/// 「显示器上永远有一张有效当前屏」：释放真屏之前先把显示器交给这张空屏。
lv_obj_t* g_blank_scr = nullptr;

const aidogui_screen_entry_t* find_screen(const char* screen_name) {
  if (screen_name == nullptr || screen_name[0] == '\0') {
    return nullptr;
  }
  for (int i = 0; i < AIDOGUI_SCREEN_COUNT; ++i) {
    const aidogui_screen_entry_t& entry = kAidoguiScreens[i];
    if (std::strcmp(entry.name, screen_name) == 0) {
      return &entry;
    }
  }
  return nullptr;
}

const aidogui_widget_entry_t* find_widget(const aidogui_screen_entry_t* screen,
                                          const char* widget_name) {
  if (screen == nullptr || widget_name == nullptr || widget_name[0] == '\0') {
    return nullptr;
  }
  for (int i = 0; i < screen->widget_count; ++i) {
    const aidogui_widget_entry_t& entry = screen->widgets[i];
    if (std::strcmp(entry.name, widget_name) == 0) {
      return &entry;
    }
  }
  return nullptr;
}

void notify_observer(const char* screen_name) {
  if (g_observer != nullptr) {
    g_observer(screen_name, g_observer_user);
  }
}

/// 空屏（懒建）：纯色底，颜色取工程底色，切走时不闪白。必须在 lv_scr_act() 非空时调用。
lv_obj_t* blank_screen() {
  if (g_blank_scr != nullptr) {
    return g_blank_scr;
  }
  g_blank_scr = lv_obj_create(nullptr);
  lv_obj_clear_flag(g_blank_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(g_blank_scr, lv_color_make(0x0b, 0x10, 0x20),
                            LV_PART_MAIN | LV_STATE_DEFAULT);
  ELOG_INFO("AiDoGUI 桥：建立空屏（没有 App 屏时的兜底当前屏）");
  return g_blank_scr;
}

/// 装载（已装载则 no-op）。notify=true 时回调观察者，报告"屏刚建好"这个事实：观察者据此把
/// 点击绑定重挂到新控件上。桥自己发起的装载也通知，不会成环 —— 观察者顺手请求切到同名 App
/// 时，框架对"已是当前前台"的请求直接 return（src/embark/framework.cpp:352）。
bool load_entry(const aidogui_screen_entry_t* entry, bool notify) {
  if (entry == nullptr) {
    return false;
  }
  if (lv_scr_act() == nullptr) {
    // 正常路径不会走到：释放真屏前一定先把显示器交给空屏（见 unload_current）。
    // 真到这儿说明有人绕开桥删了当前屏，此时 lv_scr_load() 会在 LVGL 内部解引用
    // NULL（lv_disp.c 的 lv_obj_set_pos(lv_scr_act(), ...)）——拒绝，不崩。
    ELOG_ERROR("AiDoGUI 桥：LVGL 当前屏已被外部删除（act_scr == nullptr），拒绝装载 {}",
               entry->name);
    return false;
  }
  if (g_current == entry) {
    return true;  // 已经是当前屏：不重建（重建会闪，也没必要）
  }

  entry->init();
  if (entry->root == nullptr || *entry->root == nullptr) {
    ELOG_ERROR("AiDoGUI 屏 {} 创建失败（ui_{}_screen_init 没建出根控件）", entry->name,
               entry->name);
    return false;
  }
  lv_scr_load(*entry->root);  // 先切上去，再拆旧屏：显示器上不会出现已删除对象

  const aidogui_screen_entry_t* previous = g_current;
  g_current = entry;
  if (previous != nullptr) {
    previous->destroy();
    ELOG_INFO("AiDoGUI 屏 {} 已释放（切走即释放）", previous->name);
  }
  ELOG_INFO("AiDoGUI 屏 {} 已装载（{} 个控件）", entry->name, entry->widget_count);
  if (notify) {
    notify_observer(entry->name);
  }
  return true;
}

// --- 字体：一份 TTF × 每个字号一个 tiny_ttf 实例（ADR 0017）---------------------
//
// 生成代码里每个不同 textFontSize 一个 lv_font_t* 槽（ui_font_ui_heiti_<字号>），由 cmake 在
// 配置期扫成 kAidoguiFonts[]；桥拿同一份 TTF 为每个字号建一个字体 —— 屏内引用因此零改动。
// 拿不到 TTF（卡里没放 / 读不进来）时全部槽退回 flash 里的静态子集 embark_zh_14。

/// 字源文件名。真机：SD 卡的 fonts/ 下（'S' 盘根 = /sdcard）；宿主：导出目录旁边的
/// app/aidogui_ui/assets/font/（相对导出目录就是 ../assets/font/）。
constexpr const char* kTtfFileName = "ui_heiti.ttf";

/// 每个字号的配额由公式算（app/common/font_quota.h）：quota(px) = max(4 KB, 8 × px²)。
/// 表里只存"收敛后的结果"，不再有手写的 7 档实测值 —— 字号是设计器决定的（每个不同
/// textFontSize 一个槽），不是 cmake 决定的，所以配额也只能是算出来的。
///
/// 硬下限为什么不能省：lv_lru_create 拿 cache_size/(px*px) 当哈希桶数（lv_lru.c:83），
/// lv_lru_hash 取 h % 桶数（:301）⇒ 配额 ≤ px² 是除零 panic，不是变慢。公式的 8× 恒 ≥ 2×px²。
constexpr std::size_t kCacheQuotaCount = AIDOGUI_FONT_COUNT > 0
                                             ? static_cast<std::size_t>(AIDOGUI_FONT_COUNT)
                                             : 1U;
/// 字库区的利用上限：字形在池里实占「16 字节对齐的位图 + 48 字节块头」≈ 记账值的 1.3 倍，
/// 再加每个字号一个 lv_lru 的哈希桶 —— 所以配额总和只按池容量的 3/4 收。
constexpr std::size_t kCachePoolNumerator = 3U;
constexpr std::size_t kCachePoolDenominator = 4U;

/// 运行期配额表：init 时按生成代码里实际出现的字号 + 字库区容量算一次。
font_quota::QuotaEntry g_cache_quotas[kCacheQuotaCount] = {};
std::size_t g_cache_quota_count = 0U;

std::size_t cache_quota_total_bytes() {
  return font_quota::total_bytes(g_cache_quotas, g_cache_quota_count);
}

/// 按字库区容量收敛配额。available_bytes == 0 表示没有独立字库区（宿主：有真实堆），
/// 照公式给、不收 —— 宿主与真机走的是同一个 font_quota::fit()，算法不会分叉。
void fit_cache_quotas(std::size_t available_bytes) {
  g_cache_quota_count = 0U;
  for (int i = 0; i < AIDOGUI_FONT_COUNT; ++i) {
    g_cache_quotas[g_cache_quota_count].px = kAidoguiFonts[i].size;
    g_cache_quotas[g_cache_quota_count].bytes = 0U;
    ++g_cache_quota_count;
  }
  font_quota::fit(g_cache_quotas, g_cache_quota_count, available_bytes);
  const std::size_t total = cache_quota_total_bytes();
  if (available_bytes != 0U && total > available_bytes) {
    // 砍到下限仍然超 = 字号太多或池太小，是配置问题：只报错不 abort（界面照旧能跑）。
    ELOG_ERROR("字体缓存配额已全部压到 {} 字节下限，合计 {} 字节仍超过字库区可用 {} 字节",
               static_cast<unsigned>(font_quota::floor_bytes), static_cast<unsigned>(total),
               static_cast<unsigned>(available_bytes));
  }
}

/// 某个字号实际用的配额。表里查不到（不该发生）就按公式给 —— 绝不静默返回一个魔数，
/// 那正是旧表"表外字号按 16 KB 给、却不计入合计"的毛病来源。
std::size_t cache_quota_bytes(int px) {
  for (std::size_t i = 0; i < g_cache_quota_count; ++i) {
    if (g_cache_quotas[i].px == px) {
      return g_cache_quotas[i].bytes;
    }
  }
  return font_quota::quota_bytes(px);
}

/// 字形缓存在**绘制期**才分配（lv_tiny_ttf.c:158 的 lv_mem_alloc），那时加载作用域早退出了 ——
/// 不包这一层，字形位图会全落 48 KB 的 LVGL 静态池（一屏 300 字 ≈ 59 KB，直接撞穿）。
/// 包一层让它落 PSRAM 字库区；重入用标志挡住，作用域不嵌套才安全。
const std::uint8_t* scoped_glyph_bitmap(const lv_font_t* font, std::uint32_t letter) {
  if (g_ttf_bitmap_original == nullptr) {
    return nullptr;
  }
  if (g_ttf_scope_active) {
    return g_ttf_bitmap_original(font, letter);
  }
  g_ttf_scope_active = true;
  embark::platform::font_alloc_scope_begin();
  const std::uint8_t* bitmap = g_ttf_bitmap_original(font, letter);
  embark::platform::font_alloc_scope_end();
  g_ttf_scope_active = false;
  return bitmap;
}

// --- 启动占位屏（开机自检风格）--------------------------------------------------
//
// 读整份 TTF 要好几秒（真机实测 9,296 ms，.scratch/font-ttf/hw/boot-post.log）：这段时间 UI
// 任务堵在 read_ttf() 里，面板一片黑，看着像死机。占位屏就是为这几秒存在的 —— 做成开机自检
// 那种逐行打勾的样子（用户 2026-10-17 定：要的就是这个效果）。
//
// 节奏：自检行**每秒翻绿一行**，翻的动作全部发生在读 TTF 的这九秒里（read_ttf 每读 256 KB
// 回一次 splash_fonts_progress，顺手把到点的行翻绿）。所以「一项一项打勾」不花启动时间 ——
// 只是把已经成立的事实摊开给人看，不是 sleep 出一段假进度。
//
// 事实从哪来：桥自己知道的三件事（分辨率、字库区容量、卡挂载点）之外，别的硬件（flash /
// PSRAM / 触摸 / 卡型号）的句柄都在平台入口手里，桥拿不到 —— 由 main.cpp 在 framework.boot()
// 之前调 aidogui_ui_bridge_set_boot_checks() 注进来。桥只负责节奏与上色，不编事实。
//
// 「假」只假在形式：每一行都是真事，真出问题就打红而不是假装绿（用户要求）。
//
// 文案为什么全英文：此刻 TTF 还没进内存，能用的只有 LVGL 默认字体，也就是 flash 里那 184 个
// 字形（assets/fonts/embark_zh_14.c）—— 中文只覆盖现有界面的 84 个字，新文案会画成方框。
// 桥自己的文案由 static_assert 钉住；外部注入的 label/detail 在注入时扫一遍非 ASCII 并告警。
constexpr const char* kSplashBrand = "EMBARK";
/// 自检行的标记：绿 [OK] 已过 / 琥珀 [..] 在做 / 红 [!!] 出问题 / 灰 [  ] 还没轮到。
constexpr const char* kMarkOk = "[OK]";
constexpr const char* kMarkBusy = "[..]";
constexpr const char* kMarkBad = "[!!]";
constexpr const char* kMarkWait = "[  ]";

constexpr std::uint32_t kSplashWhite = 0xffffffU;
constexpr std::uint32_t kSplashGray = 0x6b7280U;
constexpr std::uint32_t kSplashGreen = 0x4ade80U;
constexpr std::uint32_t kSplashAmber = 0xfbbf24U;
constexpr std::uint32_t kSplashRed = 0xf87171U;

/// 自检行最多几行：再多就把 320 px 的屏挤爆（一行 20 px + 进度条 + 毫秒行）。
constexpr int kBootCheckMax = 6;
/// 一行一秒：读 TTF 的九秒正好摊得下 6 行 + 字体那一行。
constexpr std::uint32_t kBootCheckCadenceMs = 1000U;
constexpr int kSplashRowTop = 96;    ///< 第一行自检的 y（从屏顶算）
constexpr int kSplashRowPitch = 20;  ///< 行距

constexpr bool ascii_only(const char* text) {
  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    if (static_cast<unsigned char>(*cursor) >= 0x80U) {
      return false;
    }
  }
  return true;
}
static_assert(ascii_only(kSplashBrand), "占位屏只能用 ASCII：这时只有内置子集的 95 个 ASCII 字形");
static_assert(ascii_only(kMarkOk), "占位屏只能用 ASCII（同上）");
static_assert(ascii_only(kMarkBusy), "占位屏只能用 ASCII（同上）");
static_assert(ascii_only(kMarkBad), "占位屏只能用 ASCII（同上）");
static_assert(ascii_only(kMarkWait), "占位屏只能用 ASCII（同上）");

/// 一行自检（指针指向调用方的字符串，必须活到 init 结束 —— 真机是 ui_main 的栈，宿主是字面量）。
struct BootCheck {
  const char* label = nullptr;
  const char* detail = nullptr;
  bool ok = true;
};

BootCheck g_boot_checks[kBootCheckMax];
int g_boot_check_count = 0;
AidoguiBootClock g_boot_clock = nullptr;  // 平台给的单调毫秒（lv_tick 在阻塞期是冻住的）
void* g_boot_clock_user = nullptr;

/// 当前毫秒：有平台时钟就用它，没有就退回 LVGL 的 tick（宿主足够）。
std::uint32_t boot_millis() {
  if (g_boot_clock != nullptr) {
    return g_boot_clock(g_boot_clock_user);
  }
  return static_cast<std::uint32_t>(lv_tick_get());
}

struct Splash {
  lv_obj_t* previous = nullptr;
  lv_obj_t* screen = nullptr;
  lv_obj_t* rows[kBootCheckMax] = {};  ///< 自检行（按下标顺序翻绿）
  BootCheck checks[kBootCheckMax] = {};
  int check_count = 0;
  int revealed = 0;           ///< 已经翻成 [OK] 的行数
  std::uint32_t t0 = 0;       ///< 占位屏上屏时刻（lv_tick），节奏从这里算
  lv_obj_t* fonts = nullptr;  ///< [..] fonts 62%  2380 KB
  lv_obj_t* ui = nullptr;     ///< [  ] ui
  lv_obj_t* bar = nullptr;
  lv_obj_t* clock = nullptr;  ///< 右下角 t=<ms>：真毫秒，不是编的
  int shown = -1;
};

Splash g_splash;

/// 建一行日志（初始灰色）：左对齐、左边距 28 px，top 是从屏顶算的像素（行距 20 px）。
/// 开机自检就是这种「一列左对齐的流水账」，居中反而看不出来。
lv_obj_t* splash_row(int top) {
  lv_obj_t* label = lv_label_create(g_splash.screen);
  lv_obj_set_style_text_color(label, lv_color_hex(kSplashGray), 0);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 28, top);
  return label;
}

/// 改一行日志的文案与整行颜色（一行一色就够看懂，不必给 [OK] 单独上色）。
void splash_row_set(lv_obj_t* label, const char* text, std::uint32_t hex) {
  if (label == nullptr) {
    return;
  }
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_hex(hex), 0);
}

/// 每改完一行就刷一次面板：一次刷新约 20 ms（真机实测 15 次进度刷新共多花约 0.3 s），
/// 这点开销换「看得见它在动」是值的。右下角毫秒数顺手更新 —— 它是 lv_tick，真时间。
void splash_commit() {
  if (g_splash.clock != nullptr) {
    lv_label_set_text_fmt(g_splash.clock, "t=%u ms",
                          static_cast<unsigned>(boot_millis() - g_splash.t0));
  }
  lv_refr_now(lv_disp_get_default());
}

/// 把到点的自检行翻绿/翻红。all = 读卡已经结束，剩下的行全部立刻落定（不再等节奏）。
void splash_reveal_checks(bool all) {
  if (g_splash.screen == nullptr) {
    return;
  }
  const std::uint32_t elapsed = boot_millis() - g_splash.t0;
  bool touched = false;
  while (g_splash.revealed < g_splash.check_count) {
    const std::uint32_t due = static_cast<std::uint32_t>(g_splash.revealed) * kBootCheckCadenceMs;
    if (!all && elapsed < due) {
      break;
    }
    const BootCheck& check = g_splash.checks[g_splash.revealed];
    char line[64];
    std::snprintf(line, sizeof line, "%s %s %s", check.ok ? kMarkOk : kMarkBad,
                  check.label != nullptr ? check.label : "?",
                  check.detail != nullptr ? check.detail : "");
    splash_row_set(g_splash.rows[g_splash.revealed], line, check.ok ? kSplashGreen : kSplashRed);
    ++g_splash.revealed;
    touched = true;
  }
  if (touched) {
    splash_commit();
  }
}

void splash_begin() {
  g_splash.previous = lv_scr_act();
  g_splash.screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(g_splash.screen, lv_color_hex(0x101010), 0);
  lv_obj_set_style_bg_opa(g_splash.screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(g_splash.screen, LV_OBJ_FLAG_SCROLLABLE);

  // 抬头：品牌 + 版本（version_string() 由 CMake 注入，改版本号这里自动跟着变）。
  lv_obj_t* title = lv_label_create(g_splash.screen);
  lv_label_set_text_fmt(title, "%s %s", kSplashBrand, embark::version_string());
  lv_obj_set_style_text_color(title, lv_color_hex(kSplashWhite), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 44);
  char line[64];
  const lv_coord_t hor = lv_disp_get_hor_res(nullptr);
  const lv_coord_t ver = lv_disp_get_ver_res(nullptr);
  std::snprintf(line, sizeof line, "%s %dx%d", embark::platform_name(), hor, ver);
  lv_obj_t* head = lv_label_create(g_splash.screen);
  lv_obj_set_style_text_color(head, lv_color_hex(kSplashGray), 0);
  lv_obj_align(head, LV_ALIGN_TOP_MID, 0, 66);
  lv_label_set_text(head, line);

  // 自检行：优先用平台入口注进来的事实（真机 flash / PSRAM / 显示 / 触摸 / 卡型号 / 字库区）；
  // 没人注（宿主）就退回桥自己知道的三件。两种都是真值，只是一个知道得多、一个知道得少。
  if (g_boot_check_count > 0) {
    g_splash.check_count = g_boot_check_count;
    for (int i = 0; i < g_boot_check_count; ++i) {
      g_splash.checks[i] = g_boot_checks[i];
    }
  } else {
    static char display_text[24];
    static char pool_text[24];
    static char card_text[32];
    std::snprintf(display_text, sizeof display_text, "%dx%d", hor, ver);
    const std::size_t pool = embark::platform::font_alloc_pool_budget_bytes();
    if (pool == 0U) {
      std::snprintf(pool_text, sizeof pool_text, "host heap");
    } else {
      std::snprintf(pool_text, sizeof pool_text, "%u KB", static_cast<unsigned>(pool >> 10U));
    }
    const char* root = embark::platform::aidogui_res_fs_root();
    std::snprintf(card_text, sizeof card_text, "%s", root != nullptr ? root : "none");
    g_splash.check_count = 3;
    g_splash.checks[0] = BootCheck{"display", display_text, true};
    g_splash.checks[1] = BootCheck{"font pool", pool_text, pool != 0U || true};
    g_splash.checks[2] = BootCheck{"sd card", card_text, root != nullptr};
  }
  for (int i = 0; i < g_splash.check_count; ++i) {
    g_splash.rows[i] = splash_row(kSplashRowTop + i * kSplashRowPitch);
    std::snprintf(line, sizeof line, "%s %s %s", kMarkWait,
                  g_splash.checks[i].label != nullptr ? g_splash.checks[i].label : "?",
                  g_splash.checks[i].detail != nullptr ? g_splash.checks[i].detail : "");
    splash_row_set(g_splash.rows[i], line, kSplashGray);
  }

  const int row_after_checks = kSplashRowTop + g_splash.check_count * kSplashRowPitch;
  g_splash.fonts = splash_row(row_after_checks);
  std::snprintf(line, sizeof line, "%s fonts 0%%", kMarkBusy);
  splash_row_set(g_splash.fonts, line, kSplashAmber);

  g_splash.ui = splash_row(row_after_checks + kSplashRowPitch);
  std::snprintf(line, sizeof line, "%s ui", kMarkWait);
  splash_row_set(g_splash.ui, line, kSplashGray);

  const int bar_top = row_after_checks + 2 * kSplashRowPitch + 12;
  g_splash.bar = lv_bar_create(g_splash.screen);
  lv_obj_set_size(g_splash.bar, 184, 6);
  lv_bar_set_range(g_splash.bar, 0, 100);
  lv_bar_set_value(g_splash.bar, 0, LV_ANIM_OFF);
  lv_obj_align(g_splash.bar, LV_ALIGN_TOP_LEFT, 28, bar_top);

  g_splash.clock = lv_label_create(g_splash.screen);
  lv_obj_set_style_text_color(g_splash.clock, lv_color_hex(kSplashGray), 0);
  lv_obj_align(g_splash.clock, LV_ALIGN_TOP_RIGHT, -28, bar_top + 22);

  g_splash.t0 = boot_millis();
  lv_scr_load(g_splash.screen);
  splash_commit();              // 先把占位屏推上面板，再开始阻塞的读
  splash_reveal_checks(false);  // 第一行立刻翻绿（它的 1 秒从 t0 开始算）
}

/// 读字体的进度：节奏也在这里 —— 每读一块就看看有没有自检行到点该翻绿了。
/// 已读 KB 写进字体那一行，「真的在读」有证据。
void splash_fonts_progress(int percent, std::size_t got_bytes) {
  if (g_splash.screen == nullptr) {
    return;
  }
  if (percent != g_splash.shown) {
    g_splash.shown = percent;
    lv_bar_set_value(g_splash.bar, percent, LV_ANIM_OFF);
  }
  char line[64];
  std::snprintf(line, sizeof line, "%s fonts %d%%  %u KB", kMarkBusy, percent,
                static_cast<unsigned>(got_bytes >> 10U));
  splash_row_set(g_splash.fonts, line, kSplashAmber);
  splash_reveal_checks(false);
  splash_commit();
}

/// 读完了：正在建 7 个字体实例（真机约 150 ms，不再推进度条）。剩下没翻的自检行全部落定。
void splash_fonts_building() {
  splash_reveal_checks(true);
  char line[64];
  std::snprintf(line, sizeof line, "%s fonts building", kMarkBusy);
  splash_row_set(g_splash.fonts, line, kSplashAmber);
  splash_commit();
}

void splash_fonts_ready(int loaded, int total) {
  splash_reveal_checks(true);
  char line[64];
  std::snprintf(line, sizeof line, "%s fonts %d/%d ready", kMarkOk, loaded, total);
  splash_row_set(g_splash.fonts, line, kSplashGreen);
  splash_commit();
}

/// 真出问题就红在屏上（用户 2026-10-17 要求）：卡里没有这份 TTF / 一个实例也建不出来。
void splash_fonts_failed(bool read_ok) {
  splash_reveal_checks(true);
  char line[64];
  std::snprintf(line, sizeof line, "%s fonts %s", kMarkBad, read_ok ? "built-in" : "not found");
  splash_row_set(g_splash.fonts, line, kSplashRed);
  splash_commit();
}

void splash_ui_ready() {
  char line[64];
  std::snprintf(line, sizeof line, "%s ui", kMarkOk);
  splash_row_set(g_splash.ui, line, kSplashGreen);
  splash_commit();
}

void splash_end() {
  if (g_splash.screen == nullptr) {
    return;
  }
  if (g_splash.previous != nullptr) {
    lv_scr_load(g_splash.previous);
  }
  lv_obj_del(g_splash.screen);
  g_splash = Splash{};
  lv_refr_now(lv_disp_get_default());
}

/// 把整份 TTF 读进常驻缓冲。候选按「真机卡 → 导出目录 → 仓库 assets」排。
bool read_ttf(const char* root) {
  if (root == nullptr) {
    return false;
  }
  constexpr const char* kCandidates[] = {"/fonts/", "/font/", "/../assets/font/"};
  char path[320];
  int first_errno = 0;  // 首个候选打不开的原因（ENOENT 与 EACCES 的排查方向完全不同）
  for (const char* relative : kCandidates) {
    const int written = std::snprintf(path, sizeof path, "%s%s%s", root, relative, kTtfFileName);
    if (written <= 0 || static_cast<std::size_t>(written) >= sizeof path) {
      continue;
    }
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
      if (first_errno == 0) {
        first_errno = errno;
      }
      continue;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    const std::size_t want = size > 0 ? static_cast<std::size_t>(size) : 0U;
    void* data = want > 0U ? embark::platform::font_buffer_alloc(want) : nullptr;
    // 分块读：一是能推进度，二是让看门狗与串口日志有机会喘气（一次 3.85 MB 的 fread 是 9 s 的单点阻塞）。
    std::size_t got = 0U;
    if (data != nullptr) {
      auto* bytes = static_cast<std::uint8_t*>(data);
      constexpr std::size_t kReadChunk = 256U * 1024U;
      while (got < want) {
        const std::size_t chunk = want - got < kReadChunk ? want - got : kReadChunk;
        const std::size_t read = std::fread(bytes + got, 1, chunk, file);
        if (read == 0U) {
          break;
        }
        got += read;
        splash_fonts_progress(static_cast<int>(got * 100U / want), got);
      }
    }
    std::fclose(file);
    if (data == nullptr) {
      ELOG_ERROR("AiDoGUI 字体源 {}（{} 字节）进不了常驻缓冲：PSRAM 不足", path,
                 static_cast<unsigned>(want));
      return false;
    }
    if (got != want) {
      ELOG_WARN("AiDoGUI 字体源 {} 读不全（{}/{} 字节），换下一个候选路径", path,
                static_cast<unsigned>(got), static_cast<unsigned>(want));
      embark::platform::font_buffer_free(data);
      continue;
    }
    g_ttf_data = static_cast<std::uint8_t*>(data);
    g_ttf_size = got;
    ELOG_INFO("AiDoGUI 字体源：{}（{} 字节，常驻 PSRAM）", path, static_cast<unsigned>(got));
    return true;
  }
  ELOG_WARN("AiDoGUI 字体：{}/fonts/ 与 /font/ 下都找不到 {}（首个 errno {}），退回内置字体", root,
            kTtfFileName, first_errno);
  return false;
}

/// 每个字号建一个 tiny_ttf 字体塞进生成代码的槽；返回成功的个数。
int load_ttf_fonts() {
  int loaded = 0;
  for (int i = 0; i < AIDOGUI_FONT_COUNT; ++i) {
    const aidogui_font_entry_t& font = kAidoguiFonts[i];
    const int px = font.size;
    if (px <= 0) {
      ELOG_WARN("AiDoGUI 字体表项字号是 {}（该槽退回 LVGL 内置字体）", px);
      continue;
    }
    const std::size_t quota = cache_quota_bytes(px);
    embark::platform::font_alloc_scope_begin();
    lv_font_t* created = lv_tiny_ttf_create_data_ex(g_ttf_data, g_ttf_size, px, quota);
    embark::platform::font_alloc_scope_end();
    if (created == nullptr) {
      ELOG_WARN("AiDoGUI 字体 {} px 建不出来（该字号退回 LVGL 内置字体）", px);
      continue;
    }
    if (g_ttf_bitmap_original == nullptr) {
      g_ttf_bitmap_original = created->get_glyph_bitmap;
    }
    created->get_glyph_bitmap = &scoped_glyph_bitmap;
    *font.slot = created;
    ++loaded;
    ELOG_INFO("AiDoGUI 字体 {} px 就绪（字形缓存 {} 字节，行高 {}，基线 {}）", px,
              static_cast<unsigned>(quota), static_cast<int>(created->line_height),
              static_cast<int>(created->base_line));
  }
  return loaded;
}

}  // namespace

void aidogui_ui_bridge_set_boot_checks(const AidoguiBootCheck* checks, int count,
                                       AidoguiBootClock clock, void* clock_user) {
  g_boot_check_count = 0;
  g_boot_clock = clock;
  g_boot_clock_user = clock_user;
  if (checks == nullptr || count <= 0) {
    return;
  }
  if (count > kBootCheckMax) {
    ELOG_WARN("启动自检：注入了 {} 行，占位屏只放得下 {} 行，多的丢掉", count, kBootCheckMax);
    count = kBootCheckMax;
  }
  for (int i = 0; i < count; ++i) {
    if (!ascii_only(checks[i].label != nullptr ? checks[i].label : "") ||
        !ascii_only(checks[i].detail != nullptr ? checks[i].detail : "")) {
      ELOG_WARN(
          "启动自检：第 {} 行的文案不是 ASCII，屏上会画成方框（这时只有内置子集的 95 个字形）", i);
    }
    g_boot_checks[i] = BootCheck{checks[i].label, checks[i].detail, checks[i].ok};
  }
  g_boot_check_count = count;
  ELOG_INFO("启动自检：注入 {} 行（占位屏一秒翻一行）", count);
}

bool aidogui_ui_bridge_init() {
  if (g_initialized) {
    return true;
  }
  if (lv_disp_get_default() == nullptr) {
    ELOG_ERROR("AiDoGUI 桥：还没有 LVGL 显示器，先建显示再 init");
    return false;
  }

  // 资源盘：字体与图片都在 'S' 盘（生成代码写死的路径 S:/fonts/ 与 S:/images/）。
  if (embark::platform::aidogui_res_fs_mount()) {
    ELOG_INFO("AiDoGUI 资源盘 S: -> {}", embark::platform::aidogui_res_fs_root());
  } else {
    ELOG_WARN("AiDoGUI 资源盘挂不上（未设 EMBARK_AIDOGUI_RES / 编译期根为空）：字体将退回内置字体");
  }

  // 生成代码的 ui_comp_get_child() 依赖这个事件号，本来由 aidogui_init() 注册。
  LV_EVENT_GET_COMP_CHILD = lv_event_register_id();

  // 字形缓存配额：先按字库区的计划容量收一次，绝不出现"配额之和 > 可用池"
  // （破了就是每帧重栅格化，ADR 0016 记过一次 716 ms/帧的教训）。
  const std::size_t pool_budget = embark::platform::font_alloc_pool_budget_bytes();
  const std::size_t cache_available =
      pool_budget == 0U ? 0U : pool_budget * kCachePoolNumerator / kCachePoolDenominator;
  fit_cache_quotas(cache_available);
  ELOG_INFO("字体缓存配额合计 {} 字节（字库区计划 {} 字节，可用 {} 字节）",
            static_cast<unsigned>(cache_quota_total_bytes()), static_cast<unsigned>(pool_budget),
            static_cast<unsigned>(cache_available));

  // 字体：SD 卡上的整份 TTF（ADR 0017：任意字符、任意字号都能栅格化）。BIN 那条路已经删了
  // （issue 03）。拿不到 TTF（未插卡 / 文件不在 / 一个字号也建不出来）时槽位退回 LVGL 默认
  // 字体 —— 也就是 flash 里的静态子集 embark_zh_14（无卡兜底，ADR 0017 第 9 条）。
  const char* res_root = embark::platform::aidogui_res_fs_root();
  splash_begin();
  const std::chrono::steady_clock::time_point font_begin = std::chrono::steady_clock::now();
  int loaded = 0;
  const bool read_ok = read_ttf(res_root);
  if (read_ok) {
    splash_fonts_building();  // 读完了：接下来按字号建字体实例（真机约 150 ms）
    loaded = load_ttf_fonts();
    if (loaded == 0) {
      embark::platform::font_buffer_free(g_ttf_data);
      g_ttf_data = nullptr;
      g_ttf_size = 0U;
      g_ttf_bitmap_original = nullptr;
    }
  }
  if (loaded == 0) {
    // 兜底必须把槽填上，不能留 NULL：生成代码是 lv_obj_set_style_text_font(obj, ui_font_…, …)
    // 直接塞指针，塞 NULL 进去绘制期会解引用空指针（实测宿主段错误），并不是「退回默认字体」。
    for (int i = 0; i < AIDOGUI_FONT_COUNT; ++i) {
      *kAidoguiFonts[i].slot = const_cast<lv_font_t*>(LV_FONT_DEFAULT);  // 内置字体是 const 的
    }
    splash_fonts_failed(read_ok);
    ELOG_WARN("AiDoGUI 字体：一个字号也没建出来（源 {}），全部槽指向内置字体", kTtfFileName);
  } else {
    splash_fonts_ready(loaded, AIDOGUI_FONT_COUNT);
  }
  splash_ui_ready();
  splash_end();
  const double font_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - font_begin)
          .count();
  ELOG_INFO("AiDoGUI 字体加载用时 {:.0f} ms（这段时间面板上是启动占位屏）", font_ms);
  g_font_report.total = AIDOGUI_FONT_COUNT;
  g_font_report.loaded = loaded;
  ELOG_INFO("AiDoGUI 字体 {}/{} 已加载（TTF 运行期栅格化；根 {}）", loaded, AIDOGUI_FONT_COUNT,
            res_root != nullptr ? res_root : "<未挂载>");
  // 可观测口：字库区「已用 / 容量」—— 逼近容量就说明某个字号的配额给少了（帧时间会先抖）。
  // 命中率拿不到：lv_tiny_ttf 的 LRU 藏在私有 dsc 里，读它要么复制它的结构布局、要么改
  // third_party，两条路都比它的价值贵；抖动改看帧时间与这里的「已用」。
  const std::size_t pool_bytes = embark::platform::font_alloc_pool_bytes();
  if (pool_bytes == 0U && pool_budget == 0U) {
    ELOG_INFO("字体缓存：宿主无独立字库区（配额合计 {} 字节）",
              static_cast<unsigned>(cache_quota_total_bytes()));
  } else if (pool_bytes == 0U) {
    // 真机池是懒建的：一个字体实例都没建出来时它还没分配过，别误报成「宿主」。
    ELOG_INFO("字体缓存：字库区尚未分配（计划 {} 字节；没有字体实例时不会占 PSRAM）",
              static_cast<unsigned>(pool_budget));
  } else {
    ELOG_INFO("字体缓存：已用 {} 字节 / 字库区 {} 字节（配额合计 {} 字节）",
              static_cast<unsigned>(embark::platform::font_alloc_outstanding_bytes()),
              static_cast<unsigned>(pool_bytes), static_cast<unsigned>(cache_quota_total_bytes()));
  }

  g_initialized = true;
  return true;
}

void aidogui_ui_bridge_destroy() {
  // unload 之后显示器上是空屏；空屏本身**不删**：删了 act_scr 会归 NULL，
  // 之后再 init + load 必崩。它是桥终身持有的显示级兜底对象。
  aidogui_ui_bridge_unload_current();
  for (int i = 0; i < AIDOGUI_FONT_COUNT; ++i) {
    const aidogui_font_entry_t& font = kAidoguiFonts[i];
    if (*font.slot == nullptr) {
      continue;
    }
    if (*font.slot == LV_FONT_DEFAULT) {
      *font.slot = nullptr;  // 无卡兜底塞进去的内置字体，不归桥释放
      continue;
    }
    // tiny_ttf 字体连带它的字形缓存（LRU 里逐项 free）一起还给字库区；
    // 池按地址判归属，所以这里不需要作用域。
    lv_tiny_ttf_destroy(*font.slot);
    *font.slot = nullptr;
  }
  if (g_ttf_data != nullptr) {
    embark::platform::font_buffer_free(g_ttf_data);
    g_ttf_data = nullptr;
    g_ttf_size = 0U;
  }
  g_ttf_bitmap_original = nullptr;
  g_font_report = AidoguiFontReport{0, 0};
  g_initialized = false;
}

int aidogui_ui_bridge_font_size_count() {
  return AIDOGUI_FONT_COUNT;
}

int aidogui_ui_bridge_font_size(int index) {
  if (index < 0 || index >= AIDOGUI_FONT_COUNT) {
    return 0;
  }
  return kAidoguiFonts[index].size;
}

lv_font_t* aidogui_ui_bridge_font(int size) {
  if (size <= 0) {
    return nullptr;
  }
  for (int i = 0; i < AIDOGUI_FONT_COUNT; ++i) {
    const aidogui_font_entry_t& font = kAidoguiFonts[i];
    if (font.size == size) {
      return *font.slot;
    }
  }
  return nullptr;
}

bool aidogui_ui_bridge_ready() {
  return g_initialized;
}

AidoguiFontReport aidogui_ui_bridge_font_report() {
  return g_font_report;
}

int aidogui_ui_bridge_screen_count() {
  return AIDOGUI_SCREEN_COUNT;
}

const char* aidogui_ui_bridge_screen_name(int index) {
  if (index < 0 || index >= AIDOGUI_SCREEN_COUNT) {
    return nullptr;
  }
  return kAidoguiScreens[index].name;
}

bool aidogui_ui_bridge_has_screen(const char* screen_name) {
  return find_screen(screen_name) != nullptr;
}

bool aidogui_ui_bridge_load_screen(const char* screen_name) {
  if (!g_initialized) {
    ELOG_WARN("AiDoGUI 桥：load_screen({}) 在 init 之前被调用",
              screen_name != nullptr ? screen_name : "<null>");
    return false;
  }
  const aidogui_screen_entry_t* entry = find_screen(screen_name);
  if (entry == nullptr) {
    ELOG_WARN("AiDoGUI 工程里没有屏 {}（屏名约定：screen == App 名）",
              screen_name != nullptr ? screen_name : "<null>");
    ELOG_WARN("下一步：在 AiDoGUI 设计器里建一张名为 {} 的屏并导出，然后重新配置构建",
              screen_name != nullptr ? screen_name : "<null>");
    return false;
  }
  return load_entry(entry, /*notify=*/true);
}

bool aidogui_ui_bridge_load_screen_for_app(const char* app_name) {
  return aidogui_ui_bridge_load_screen(app_name);
}

void aidogui_ui_bridge_unload_current() {
  if (g_current == nullptr) {
    return;
  }
  const aidogui_screen_entry_t* entry = g_current;
  g_current = nullptr;
  // 先切空屏、再删真屏：顺序反了就会留下 act_scr == nullptr 的显示器（见空屏注释）。
  lv_scr_load(blank_screen());
  entry->destroy();
  ELOG_INFO("AiDoGUI 屏 {} 已释放（显示器交给空屏）", entry->name);
}

const char* aidogui_ui_bridge_current_screen() {
  return g_current != nullptr ? g_current->name : nullptr;
}

bool aidogui_ui_bridge_screen_created(const char* screen_name) {
  const aidogui_screen_entry_t* entry = find_screen(screen_name);
  if (entry == nullptr || entry->root == nullptr) {
    return false;
  }
  return *entry->root != nullptr;
}

int aidogui_ui_bridge_widget_count(const char* screen_name) {
  const aidogui_screen_entry_t* entry = find_screen(screen_name);
  return entry != nullptr ? entry->widget_count : 0;
}

const char* aidogui_ui_bridge_widget_name(const char* screen_name, int index) {
  const aidogui_screen_entry_t* entry = find_screen(screen_name);
  if (entry == nullptr || index < 0 || index >= entry->widget_count) {
    return nullptr;
  }
  return entry->widgets[index].name;
}

lv_obj_t* aidogui_ui_bridge_widget(const char* screen_name, const char* widget_name) {
  const aidogui_screen_entry_t* entry = find_screen(screen_name);
  const aidogui_widget_entry_t* widget = find_widget(entry, widget_name);
  if (widget == nullptr || widget->slot == nullptr) {
    return nullptr;
  }
  return *widget->slot;
}

void aidogui_ui_bridge_set_screen_observer(AidoguiScreenObserver observer, void* user) {
  g_observer = observer;
  g_observer_user = user;
}

void aidogui_ui_bridge_tick() {
  // AiDoGUI 生成代码没有「每帧把变量刷进控件」的层：
  // 控件值由 App 通过 aidogui_ui_bridge_widget() 直接写。
}

}  // namespace embark::demo
