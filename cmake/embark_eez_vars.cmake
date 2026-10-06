# EEZ 变量表生成 —— 把「Flow 全局变量 = UI 数据接口」落成构建期数据（ADR 0008）
#
# 约定（用户口径，2026-10-06）：
#   * 变量在 EEZ Studio 的 Flow 变量面板里声明（Global 作用域），导出后出现在
#     app/eez_ui/src/ui/vars.h 的 FlowGlobalVariables 枚举里；
#   * 变量名建议 <app名>_<字段>（如 launcher_tap_count）—— Flow 全局变量是全局
#     作用域，多 App 共存时前缀就是归属；
#   * C++ 侧不 include 生成头：按名读写走 app/eez_ui_bridge 的 set_var_*/get_var_*，
#     索引由本模块生成。
#
# 数据来源：vars.h 的 enum FlowGlobalVariables（带 = N 用显式值，否则按顺序递增），
# 跳过 NONE/LAST 哨兵。解析不到变量就写空表（kVarCount = 0），绝不 FATAL_ERROR：
# 用户随时可能在 Studio 里删掉变量，构建不能因此失败。
#
# 产物（OUT_H，落在构建目录 include/ 下，不手改）：
#   namespace embark::demo::eez {
#     struct VarEntry { const char* name; int index; };
#     inline constexpr VarEntry kVars[] = { {"launcher_tap_count", 0} };
#     inline constexpr int kVarCount = 1;
#   }

function(embark_generate_eez_var_table VARS_H OUT_H)
  set(_names "")
  set(_idxs "")
  set(_source "无（未找到 vars.h）")
  set(_native_decls 0)

  if(EXISTS "${VARS_H}")
    file(READ "${VARS_H}" _txt)

    if(_txt MATCHES "enum +FlowGlobalVariables[^}]*")
      set(_block "${CMAKE_MATCH_0}")
      string(REGEX MATCHALL "FLOW_GLOBAL_VARIABLE_[A-Za-z0-9_]+( *= *[0-9]+)?" _entries "${_block}")
      set(_next 0)
      foreach(_e IN LISTS _entries)
        string(REGEX MATCH "FLOW_GLOBAL_VARIABLE_([A-Za-z0-9_]+)" _m "${_e}")
        set(_raw "${CMAKE_MATCH_1}")
        if(_e MATCHES "= *([0-9]+)")
          set(_idx "${CMAKE_MATCH_1}")
        else()
          set(_idx "${_next}")
        endif()
        math(EXPR _next "${_idx} + 1")
        # 哨兵不是真实变量（旧导出形态里 NONE 是枚举第 0 项）。
        if(NOT _raw STREQUAL "NONE" AND NOT _raw STREQUAL "LAST")
          if(NOT _raw STREQUAL "")
            string(TOLOWER "${_raw}" _n)
            list(APPEND _names "${_n}")
            list(APPEND _idxs "${_idx}")
          endif()
        endif()
      endforeach()
      if(_names)
        set(_source "vars.h FlowGlobalVariables")
      endif()
    endif()

    # 原生变量（本仓库接线走路线 A：只做 Flow 全局变量）：检测到就提示，不生成。
    if(_txt MATCHES "// Native global variables([^#]*)")
      set(_native_section "${CMAKE_MATCH_1}")
      string(REGEX MATCHALL ";" _semis "${_native_section}")
      list(LENGTH _semis _native_decls)
    endif()
  endif()

  list(LENGTH _names _n)
  math(EXPR _last "${_n} - 1")
  set(_rows "")
  set(_count 0)
  foreach(_i RANGE 0 ${_last})
    list(GET _names ${_i} _name)
    list(GET _idxs ${_i} _idx)
    string(APPEND _rows "  {\"${_name}\", ${_idx}},\n")
    math(EXPR _count "${_i} + 1")
  endforeach()
  if(_count EQUAL 0)
    set(_rows "  {\"\", -1},  // 空表占位（EEZ 生成代码里暂时没有变量）\n")
  endif()

  get_filename_component(_out_dir "${OUT_H}" DIRECTORY)
  file(MAKE_DIRECTORY "${_out_dir}")
  file(WRITE "${OUT_H}"
"#ifndef EMBARK_EEZ_VARS_H
#define EMBARK_EEZ_VARS_H

// 自动生成，请勿手改：由 cmake/embark_eez_vars.cmake 从 EEZ Studio 生成代码解析。
// 变量名约定：<app名>_<字段>（Flow 全局变量，读写走 eez_ui_bridge）。
// 本次来源：${_source}
namespace embark::demo::eez {

struct VarEntry {
  const char* name;
  int index;  // eez::flow::setGlobalVariable/getGlobalVariable 的索引（= FlowGlobalVariables 枚举值）
};

inline constexpr VarEntry kVars[] = {
${_rows}};
inline constexpr int kVarCount = ${_count};

}  // namespace embark::demo::eez

#endif /* EMBARK_EEZ_VARS_H */
")
  message(STATUS "  EEZ 变量：${_count} 个（来源：${_source}）")
  foreach(_i RANGE 0 ${_last})
    list(GET _names ${_i} _name)
    list(GET _idxs ${_i} _idx)
    message(STATUS "    [${_idx}] ${_name}")
  endforeach()
  if(_native_decls GREATER 0)
    message(STATUS "  EEZ 原生变量：检测到 ${_native_decls} 个声明（当前接线只覆盖 Flow 全局变量，见 ADR 0008）")
  endif()
endfunction()
