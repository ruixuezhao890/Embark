/**
 * AiDoGUI UI 薄桥实现（ADR 0011）：见 aidogui_ui_bridge.h 头注释。
 *
 * 表头是构建期生成的：cmake/embark_aidogui_tables.cmake 在配置期解析 app/aidogui_ui/export，
 * 写出构建目录 include/embark_aidogui_ui_tables.h，由 embark::aidogui_ui 的 PUBLIC include 根
 * 带进来。它是自含的：自己 extern 用到的控件/字体符号，不 include 生成代码的 ui.h —— EEZ 老
 * 目标也有一份 ui.h，两边同名必然打架（谁在 include 路径前面谁赢，非常隐蔽）。
 */
#include "aidogui_ui_bridge.h"

#include <cstdio>
#include <cstring>

#include <embark/log.h>

#include "aidogui_res_fs.h"
#include "embark_aidogui_ui_tables.h"

namespace embark::demo {
namespace {

bool g_initialized = false;
const aidogui_screen_entry_t* g_current = nullptr;  // 当前装载的屏（未装载 nullptr）
AidoguiScreenObserver g_observer = nullptr;
void* g_observer_user = nullptr;
AidoguiFontReport g_font_report{0, 0};

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

}  // namespace

bool aidogui_ui_bridge_init() {
  if (g_initialized) {
    return true;
  }
  if (lv_disp_get_default() == nullptr) {
    ELOG_ERROR("AiDoGUI 桥：还没有 LVGL 显示器，先建显示再 init");
    return false;
  }

  // 资源盘：字体 BIN 在 S:/fonts/，图片 BIN 在 S:/images/（生成代码写死的路径）。
  const char* res_root = embark::platform::aidogui_res_fs_default_root();
  if (embark::platform::aidogui_res_fs_mount()) {
    ELOG_INFO("AiDoGUI 资源盘 S: -> {}", embark::platform::aidogui_res_fs_root());
  } else {
    ELOG_WARN("AiDoGUI 资源盘挂不上（未设 EMBARK_AIDOGUI_RES / 编译期根为空）：字体将退回内置字体");
  }
  (void)res_root;

  // 生成代码的 ui_comp_get_child() 依赖这个事件号，本来由 aidogui_init() 注册。
  LV_EVENT_GET_COMP_CHILD = lv_event_register_id();

  int loaded = 0;
  for (int i = 0; i < AIDOGUI_FONT_COUNT; ++i) {
    const aidogui_font_entry_t& font = kAidoguiFonts[i];
    *font.slot = lv_font_load(font.bin_path);
    if (*font.slot != nullptr) {
      ++loaded;
    } else {
      ELOG_WARN("AiDoGUI 字体加载失败：{}（该字号退回 LVGL 内置字体）", font.bin_path);
    }
  }
  g_font_report.total = AIDOGUI_FONT_COUNT;
  g_font_report.loaded = loaded;
  ELOG_INFO("AiDoGUI 字体 {}/{} 已加载（根 {}）", loaded, AIDOGUI_FONT_COUNT,
            embark::platform::aidogui_res_fs_root() != nullptr
                ? embark::platform::aidogui_res_fs_root()
                : "<未挂载>");

  g_initialized = true;
  return true;
}

void aidogui_ui_bridge_destroy() {
  // unload 之后显示器上是空屏；空屏本身**不删**：删了 act_scr 会归 NULL，
  // 之后再 init + load 必崩。它是桥终身持有的显示级兜底对象。
  aidogui_ui_bridge_unload_current();
  for (int i = 0; i < AIDOGUI_FONT_COUNT; ++i) {
    const aidogui_font_entry_t& font = kAidoguiFonts[i];
    if (*font.slot != nullptr) {
      lv_font_free(*font.slot);
      *font.slot = nullptr;
    }
  }
  g_font_report = AidoguiFontReport{0, 0};
  g_initialized = false;
}

lv_font_t* aidogui_ui_bridge_font(int size) {
  if (size <= 0) {
    return nullptr;
  }
  // 表项只有 bin_path（构建期从生成代码里正则抓的），所以按"路径后缀"认字号：
  // "_14.bin" 不会误配 "_114.bin"（后缀长度不同），字号一多也不会串。
  char suffix[16];
  std::snprintf(suffix, sizeof suffix, "_%d.bin", size);
  const std::size_t want = std::strlen(suffix);
  for (int i = 0; i < AIDOGUI_FONT_COUNT; ++i) {
    const aidogui_font_entry_t& font = kAidoguiFonts[i];
    const std::size_t len = std::strlen(font.bin_path);
    if (len >= want && std::strcmp(font.bin_path + (len - want), suffix) == 0) {
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
  // AiDoGUI 生成代码没有「每帧把变量刷进控件」的层（EEZ 的 ui_tick 没有对应物）：
  // 控件值由 App 通过 aidogui_ui_bridge_widget() 直接写。
}

}  // namespace embark::demo
