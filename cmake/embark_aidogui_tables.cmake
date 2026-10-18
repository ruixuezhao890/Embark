# AiDoGUI 表生成 —— 把「AiDoGUI 里屏 id == App 名」的约定落成构建期数据（ADR 0011）
#
# 数据来源：AiDoGUI 的 C 代码导出目录（app/aidogui_ui/export，GUI 导出向导与
# tools/aidogui/export.ps1 都往这里写）
#   * export/screens/ui_<屏id>.h：该屏的控件符号 extern lv_obj_t * ui_<屏id>[_<控件名>];
#     与 ui_<屏id>_screen_init / ui_<屏id>_screen_destroy 两个函数；
#   * export/ui.c：每个字号的槽定义（lv_font_t * ui_font_ui_heiti_<字号> = NULL;）。ADR 0017
#     之后字体由桥注入（卡上的 TTF 运行期栅格化），这里只抓**槽名里的字号**，不碰文件路径；
#   * export/components/ui_comp.h：自定义事件号（LV_EVENT_GET_COMP_CHILD 等）。
#
# 纪律：解析不到就写空表 + WARNING，绝不 FATAL_ERROR。
# 生成的表头是自含的：自己 extern 用到的符号，不 include 生成代码的 ui.h（那份头是给屏内 C
# 代码互相包含用的；两个 include 根同名会打架，谁在前面谁赢，非常隐蔽）。
#
# 产物（OUT_H，落在构建目录 include/ 下，不手改、不入库）：
#   typedef struct { const char* name; lv_obj_t** slot; } aidogui_widget_entry_t;
#   typedef struct { const char* name; lv_obj_t** root; void (*init)(void);
#                    void (*destroy)(void); const aidogui_widget_entry_t* widgets;
#                    int widget_count; } aidogui_screen_entry_t;
#   typedef struct { const char* bin_path; lv_font_t** slot; } aidogui_font_entry_t;
#   kAidoguiWidgets_<屏id>[] / kAidoguiScreens[] / kAidoguiFonts[]
#   AIDOGUI_SCREEN_COUNT / AIDOGUI_FONT_COUNT

function(embark_generate_aidogui_tables EXPORT_DIR OUT_H)
  set(_screens_dir "${EXPORT_DIR}/screens")
  file(GLOB _headers CONFIGURE_DEPENDS "${_screens_dir}/ui_*.h")
  if(NOT _headers)
    message(WARNING "AiDoGUI 表：${_screens_dir} 下没有 ui_*.h —— 先用 GUI 导出一次，"
                    "或跑 pwsh -NoProfile -File tools/aidogui/export.ps1。本轮写空表。")
  endif()

  set(_decls_all "")
  set(_widget_arrays "")
  set(_screen_table "")
  set(_screen_count 0)
  set(_screen_names "")

  foreach(_h IN LISTS _headers)
    get_filename_component(_stem "${_h}" NAME_WE)     # ui_launcher
    string(REGEX REPLACE "^ui_" "" _sid "${_stem}")   # launcher
    file(READ "${_h}" _txt)
    # 生成代码改了就得重跑配置（GLOB 只管新增/删除，管不了内容变化）。
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_h}")

    if(NOT _txt MATCHES "ui_${_sid}_screen_init *")
      message(WARNING "AiDoGUI 表：${_h} 里找不到 ui_${_sid}_screen_init()，跳过该屏")
      continue()
    endif()

    # 控件符号：extern lv_obj_t * ui_<sid>[_<控件名>];
    string(REGEX MATCHALL "extern lv_obj_t [*] *ui_[A-Za-z0-9_]+" _decls "${_txt}")
    set(_widget_rows "")
    set(_widget_count 0)
    foreach(_d IN LISTS _decls)
      string(REGEX MATCH "[A-Za-z0-9_]+$" _sym "${_d}")
      if(_sym STREQUAL "ui_${_sid}")
        string(APPEND _decls_all "extern lv_obj_t* ${_sym};  // 屏根\n")
      else()
        string(REPLACE "ui_${_sid}_" "" _wname "${_sym}")
        string(APPEND _decls_all "extern lv_obj_t* ${_sym};\n")
        string(APPEND _widget_rows "  {\"${_wname}\", &${_sym}},\n")
        math(EXPR _widget_count "${_widget_count} + 1")
      endif()
    endforeach()
    if(_widget_count EQUAL 0)
      set(_widget_rows "  {nullptr, nullptr},  // 空表占位（该屏暂无控件）\n")
    endif()

    # 屏的两个入口：生成代码的 screens/ui_<id>.h 里就有，表要把它们的地址写进屏表。
    string(APPEND _decls_all "void ui_${_sid}_screen_init(void);\n")
    string(APPEND _decls_all "void ui_${_sid}_screen_destroy(void);\n")

    string(APPEND _widget_arrays
           "inline constexpr aidogui_widget_entry_t kAidoguiWidgets_${_sid}[] = {\n${_widget_rows}};\n\n")
    string(APPEND _screen_table
           "  {\"${_sid}\", &ui_${_sid}, ui_${_sid}_screen_init, ui_${_sid}_screen_destroy, kAidoguiWidgets_${_sid}, ${_widget_count}},\n")
    math(EXPR _screen_count "${_screen_count} + 1")
    list(APPEND _screen_names "${_sid}（${_widget_count} 控件）")
  endforeach()

  # 字体：字号槽的符号在 ui.c 里定义（lv_font_t * ui_font_ui_heiti_11 = NULL;），字号取符号名
  # 的数字后缀。ADR 0017 之后字体不再有"文件路径"这回事（整份 TTF 常驻 + 每字号一份字形缓存），
  # 所以表里只剩 {px, slot}：换加载源（BIN → TTF）就只是换槽里塞的指针，生成代码零改动。
  set(_font_rows "")
  set(_font_count 0)
  set(_font_names "")
  set(_ui_c "${EXPORT_DIR}/ui.c")
  if(EXISTS "${_ui_c}")
    file(READ "${_ui_c}" _ui_c_txt)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_ui_c}")
    string(REGEX MATCHALL "lv_font_t[ \t]*\\*[ \t]*ui_font_[A-Za-z0-9_]+[ \t]*=[ \t]*NULL" _fsyms
           "${_ui_c_txt}")
    if(_fsyms)
      list(REMOVE_DUPLICATES _fsyms)
    endif()
    foreach(_p IN LISTS _fsyms)
      string(REGEX MATCH "ui_font_[A-Za-z0-9_]+" _fsym "${_p}")
      if(NOT _fsym MATCHES "_([0-9]+)$")
        message(WARNING "AiDoGUI 表：字体符号 ${_fsym} 认不出字号，跳过")
        continue()
      endif()
      set(_size "${CMAKE_MATCH_1}")
      string(APPEND _decls_all "extern lv_font_t* ${_fsym};\n")
      string(APPEND _font_rows "  {${_size}, &${_fsym}},\n")
      math(EXPR _font_count "${_font_count} + 1")
      list(APPEND _font_names "${_size} px")
    endforeach()
  else()
    message(WARNING "AiDoGUI 表：找不到 ${_ui_c}，字号槽表为空（界面会退回 LVGL 内置字体）")
  endif()
  if(_font_count EQUAL 0)
    set(_font_rows "  {0, nullptr},  // 空表占位（没解析到字号槽）\n")
  endif()

  # 自定义事件号：ui_comp.c 定义、components/ui_comp.h 声明。生成代码的 aidogui_init 本来会注册
  # LV_EVENT_GET_COMP_CHILD，本桥自己注册，所以表头要把这些声明带过来（解析不到就兜底写死名字）。
  set(_comp_event_decls "")
  set(_comp_h "${EXPORT_DIR}/components/ui_comp.h")
  if(EXISTS "${_comp_h}")
    file(READ "${_comp_h}" _comp_h_txt)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_comp_h}")
    string(REGEX MATCHALL "extern [A-Za-z_][A-Za-z0-9_]* LV_EVENT_[A-Za-z0-9_]+" _ev_decls "${_comp_h_txt}")
    foreach(_ev IN LISTS _ev_decls)
      string(APPEND _comp_event_decls "${_ev};\n")
    endforeach()
  endif()
  if(NOT _comp_event_decls)
    set(_comp_event_decls "extern uint32_t LV_EVENT_GET_COMP_CHILD;  // 兜底（ui_comp.h 里没解析到）\n")
  endif()

  if(_screen_count EQUAL 0)
    set(_screen_table "  {\"\", nullptr, nullptr, nullptr, nullptr, 0},  // 空表占位\n")
  endif()

  get_filename_component(_out_dir "${OUT_H}" DIRECTORY)
  file(MAKE_DIRECTORY "${_out_dir}")
  file(WRITE "${OUT_H}"
    "#ifndef EMBARK_AIDOGUI_UI_TABLES_H\n#define EMBARK_AIDOGUI_UI_TABLES_H\n\n"
    "// 自动生成，请勿手改：由 cmake/embark_aidogui_tables.cmake 在配置期解析 AiDoGUI 导出目录。\n"
    "// 屏名约定（ADR 0011）：AiDoGUI 里 screen 的 id == App 名；子页 id = <app名>_<编号>_sub。\n"
    "// 本次来源：${EXPORT_DIR}\n\n#include <lvgl.h>\n\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n"
    "${_decls_all}\n"
    "// 生成代码声明的自定义事件号（aidogui_init 本来会注册它，本桥自己注册）。\n"
    "${_comp_event_decls}\n")
  file(APPEND "${OUT_H}"
    "typedef struct {\n  const char* name;\n  lv_obj_t** slot;\n} aidogui_widget_entry_t;\n\n"
    "typedef struct {\n  const char* name;\n  lv_obj_t** root;\n  void (*init)(void);\n"
    "  void (*destroy)(void);\n  const aidogui_widget_entry_t* widgets;\n  int widget_count;\n} aidogui_screen_entry_t;\n\n"
    "typedef struct {\n  int size;\n  lv_font_t** slot;\n} aidogui_font_entry_t;\n\n"
    "${_widget_arrays}"
    "inline constexpr aidogui_screen_entry_t kAidoguiScreens[] = {\n${_screen_table}};\n"
    "inline constexpr int AIDOGUI_SCREEN_COUNT = ${_screen_count};\n\n"
    "inline constexpr aidogui_font_entry_t kAidoguiFonts[] = {\n${_font_rows}};\n"
    "inline constexpr int AIDOGUI_FONT_COUNT = ${_font_count};\n\n"
    "#ifdef __cplusplus\n}\n#endif\n\n#endif /* EMBARK_AIDOGUI_UI_TABLES_H */\n")

  message(STATUS "  AiDoGUI 表：${_screen_count} 屏 / ${_font_count} 个字号槽（源：${EXPORT_DIR}）")
  foreach(_n IN LISTS _screen_names)
    message(STATUS "    屏 ${_n}")
  endforeach()
  foreach(_n IN LISTS _font_names)
    message(STATUS "    字体 ${_n}")
  endforeach()
endfunction()
