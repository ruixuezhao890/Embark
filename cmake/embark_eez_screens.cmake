# EEZ 屏表生成 —— 把「屏名 == App 名」的约定落成构建期数据（issue 19 续）
#
# 约定（用户口径，2026-10-06）：
#   * EEZ Studio 里的 screen 名 == 对应 App 的 name()（如 clock = ClockApp）；
#   * App 的子页面命名 <app名>_<编号>_sub（编号 = 子页序号）。
# 生成代码每次导出都可增删屏/文件，所以本模块的纪律与 app/eez_ui/CMakeLists.txt
# 的 GLOB 一致：解析不到屏就写「空表」（kScreenCount = 0），绝不 FATAL_ERROR。
#
# 数据来源优先级：
#   1) 生成代码里的 screen_names[] 字符串表（运行时权威，屏序即 1..N）；
#   2) 回退 screens.h 的 ScreensEnum（SCREEN_ID_CLOCK_1_SUB = 2 -> "clock_1_sub"）。
#
# 产物（OUT_H，落在构建目录 include/ 下，不手改）：
#   namespace embark::demo::eez {
#     struct ScreenEntry { const char* name; int id; };
#     inline constexpr ScreenEntry kScreens[] = { {"desk", 1}, {"clock", 2} };
#     inline constexpr int kScreenCount = 2;
#   }

function(embark_generate_eez_screen_table UI_DIR OUT_H)
  set(_names "")
  set(_ids "")
  set(_source "无（未找到 screen_names[] 与 screens.h）")

  # --- 来源 1：生成代码里的 screen_names[] ---------------------------------
  file(GLOB _ui_c "${UI_DIR}/*.c")
  set(_all_c "")
  foreach(_f IN LISTS _ui_c)
    file(READ "${_f}" _one_c)
    string(APPEND _all_c "${_one_c}")
  endforeach()
  if(_all_c)
    if(_all_c MATCHES "screen_names[^=]*=[^;]*")
      string(REGEX MATCHALL "\"[^\"]*\"" _quoted "${CMAKE_MATCH_0}")
      foreach(_q IN LISTS _quoted)
        string(REPLACE "\"" "" _n "${_q}")
        list(APPEND _names "${_n}")
      endforeach()
      if(_names)
        set(_source "screen_names[]")
      endif()
    endif()
  endif()

  # --- 来源 2：回退 screens.h 的 ScreensEnum -------------------------------
  if(NOT _names)
    set(_h "${UI_DIR}/screens.h")
    if(EXISTS "${_h}")
      file(READ "${_h}" _h_txt)
      # 行首锚定并排除 _SCREEN_ID_FIRST/_LAST 哨兵（它们不是真实屏名）。
      string(REGEX MATCHALL "(^|\n)[ \t]*SCREEN_ID_[A-Za-z0-9_]+ *= *[0-9]+" _items
             "${_h_txt}")
      foreach(_it IN LISTS _items)
        string(REGEX MATCH "SCREEN_ID_([A-Za-z0-9_]+) *= *([0-9]+)" _ignored "${_it}")
        if(CMAKE_MATCH_1)
          string(TOLOWER "${CMAKE_MATCH_1}" _n)
          list(APPEND _names "${_n}")
          list(APPEND _ids "${CMAKE_MATCH_2}")
        endif()
      endforeach()
      if(_names)
        set(_source "screens.h ScreensEnum")
      endif()
    endif()
  endif()

  # --- 拼表（空表也生成合法数组：kScreens 恒非空）--------------------------
  list(LENGTH _names _n)
  list(LENGTH _ids _n_ids)
  math(EXPR _last "${_n} - 1")
  set(_rows "")
  set(_count 0)
  foreach(_i RANGE 0 ${_last})
    list(GET _names ${_i} _name)
    if(_n_ids GREATER 0)
      list(GET _ids ${_i} _id)
    else()
      math(EXPR _id "${_i} + 1")
    endif()
    string(APPEND _rows "  {\"${_name}\", ${_id}},\n")
    math(EXPR _count "${_i} + 1")
  endforeach()
  if(_count EQUAL 0)
    set(_rows "  {\"\", -1},  // 空表占位（EEZ 生成代码里暂时没有屏）\n")
  endif()

  get_filename_component(_out_dir "${OUT_H}" DIRECTORY)
  file(MAKE_DIRECTORY "${_out_dir}")
  file(WRITE "${OUT_H}"
"#ifndef EMBARK_EEZ_SCREENS_H
#define EMBARK_EEZ_SCREENS_H

// 自动生成，请勿手改：由 cmake/embark_eez_screens.cmake 从 EEZ Studio 生成代码解析。
// 屏名约定（用户口径）：screen 名 == App 名；App 子页 = <app名>_<编号>_sub。
// 本次来源：${_source}
namespace embark::demo::eez {

struct ScreenEntry {
  const char* name;
  int id;  // EEZ 侧 1 起；-1 = 无效
};

inline constexpr ScreenEntry kScreens[] = {
${_rows}};
inline constexpr int kScreenCount = ${_count};

}  // namespace embark::demo::eez

#endif /* EMBARK_EEZ_SCREENS_H */
")
  message(STATUS "  EEZ 屏表：${_count} 个（来源：${_source}）")
  foreach(_i RANGE 0 ${_last})
    list(GET _names ${_i} _name)
    message(STATUS "    [${_i}] ${_name}")
  endforeach()
endfunction()
