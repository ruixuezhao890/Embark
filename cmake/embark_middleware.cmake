# middleware/ 视图（spec §11）
#
# 上游三方库（efmt/elog，以及 elog 内部用到的 ETL）在头文件里写死了 <middleware/...>
# 形式的 include 路径。所以我们在【构建目录】里造一个 middleware/ 视图，把三方头文件映射进来，
# 并把视图的父目录加进 include 路径 —— 这样 <middleware/etl/vector.h> 才解析得到：
#
#   <build>/include/middleware/etl/...    -> third_party/etl/include/etl/...
#   <build>/include/middleware/efmt/...   -> third_party/efmt-elog/efmt/...
#   <build>/include/middleware/elog/...   -> third_party/efmt-elog/elog/...
#
# 为什么不直接把 third_party/etl/include 加进 include 路径：
#   1. 上游头文件内部按 <middleware/...> 互相引用，没有视图就找不到；
#   2. 把 third_party/etl/include/etl 当 include 根会踩 ETL 自己的坑
#      （algorithm.h 里 11 处 #include "etl/private/diagnostic_*_push.h" 全部失败）；
#   3. 把 third_party/etl/include 当根会让 <etl/...> 与 <middleware/etl/...> 两套写法同时存在，
#      而我们必须只走后者。
#
# 视图是映射不是拷贝：Windows 用目录 junction（普通权限即可），其它平台用符号链接，
# 只在配置期建一次。
#
# 为什么 LVGL 不在这个视图里：视图存在的唯一理由是「上游头文件自己写死了
# <middleware/...> 形式的互相引用」。LVGL 不是这样 —— 它按 lvgl.h 引入
# （LV_LVGL_H_INCLUDE_SIMPLE=ON，include 根是 third_party/lvgl 仓库根），
# 给它也造一条 junction 只会多出第二套写法。见 third_party/CMakeLists.txt。

# 建视图。参数：视图根目录（例如 ${CMAKE_BINARY_DIR}/include/middleware）
function(embark_create_middleware_view view_dir)
  set(link_etl  "${view_dir}/etl")
  set(link_efmt "${view_dir}/efmt")
  set(link_elog "${view_dir}/elog")

  if(EXISTS "${link_etl}" AND EXISTS "${link_efmt}" AND EXISTS "${link_elog}")
    return()  # 已经建好（重复配置时不要报错）
  endif()

  file(MAKE_DIRECTORY "${view_dir}")

  embark_middleware_link("${link_etl}"  "${PROJECT_SOURCE_DIR}/third_party/etl/include/etl")
  embark_middleware_link("${link_efmt}" "${PROJECT_SOURCE_DIR}/third_party/efmt-elog/efmt")
  embark_middleware_link("${link_elog}" "${PROJECT_SOURCE_DIR}/third_party/efmt-elog/elog")
endfunction()

# 造一条链接。参数：链接路径、目标目录
function(embark_middleware_link link_path target_dir)
  if(EXISTS "${link_path}")
    return()
  endif()

  if(NOT IS_DIRECTORY "${target_dir}")
    message(FATAL_ERROR
            "找不到依赖源码目录：${target_dir}\n"
            "依赖以 git submodule 引入，请先执行：git submodule update --init --recursive")
  endif()

  if(WIN32)
    # mklink 是 cmd 内建命令；/J 建目录 junction，不需要管理员权限
    file(TO_NATIVE_PATH "${link_path}" link_native)
    file(TO_NATIVE_PATH "${target_dir}" target_native)
    execute_process(COMMAND cmd /c mklink /J "${link_native}" "${target_native}"
                    RESULT_VARIABLE link_result
                    OUTPUT_VARIABLE link_output
                    ERROR_VARIABLE  link_error)
  else()
    file(CREATE_LINK "${target_dir}" "${link_path}" SYMBOLIC RESULT link_result)
    set(link_output "")
    set(link_error "")
  endif()

  if(NOT link_result EQUAL 0)
    message(FATAL_ERROR "创建 middleware 视图失败：${link_path} -> ${target_dir}\n"
                        "${link_output}${link_error}")
  endif()
endfunction()
