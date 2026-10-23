# SDL2 探测（issues/05）
#
# 宿主窗口用 SDL2。本机（MinGW-w64）装在 E:\mingw\...\mingw64 下，自带 CMake 配置
# （<prefix>/cmake/sdl2-config.cmake → <prefix>/x86_64-w64-mingw32/lib/cmake/SDL2/SDL2Config.cmake），
# 所以 find_package(SDL2 CONFIG) 开箱即用；Linux 发行版装了 libsdl2-dev 后同样走 CONFIG 模式。
#
# 只认 CONFIG 模式：只有它给出导入目标 SDL2::SDL2 —— 编译选项、include 路径、链接依赖
# 一次带全，不用我们手拼一堆路径变量（FindSDL2.cmake 那套旧写法做不到这些）。
#
# 找不到 SDL2 不让整个构建失败：跳过宿主 UI 目标（embark_host_ui），无窗口的 embark_host
# 与全部单元测试照常可建可跑 —— 显示/输入后端不是框架内核的依赖。

# 探测结果写进出参：TRUE / FALSE。
#
# 用 macro 而不是 function：find_package 提供的 SDL2_BINDIR / SDL2_INCLUDE_DIRS 这类
# 普通变量必须落在【调用方】的目录作用域里（embark_copy_sdl2_runtime 还要读 SDL2_BINDIR
# 去复制 DLL）。function 会开出自己的作用域，那些变量出门就没了。
macro(embark_find_sdl2 out_found)
  find_package(SDL2 CONFIG QUIET)

  if(SDL2_FOUND AND TARGET SDL2::SDL2)
    set(${out_found} TRUE)
  else()
    set(${out_found} FALSE)
    message(STATUS "  未找到 SDL2（CONFIG 模式）：跳过宿主 UI 目标 embark_host_ui；"
                   "内核、后端与测试不受影响。装好 SDL2 后重新 configure 即可。")
  endif()
endmacro()

# 把 SDL2.dll 复制到可执行文件旁边（MinGW 的 DLL 在 <prefix>/bin，不在系统 PATH 里）。
# 目标不存在 SDL2_BINDIR（例如静态链接的 SDL2）时静默跳过。
function(embark_copy_sdl2_runtime target)
  if(SDL2_BINDIR AND EXISTS "${SDL2_BINDIR}/SDL2.dll")
    add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${SDL2_BINDIR}/SDL2.dll" "$<TARGET_FILE_DIR:${target}>/SDL2.dll"
            COMMENT "复制 SDL2.dll 到 $<TARGET_FILE_DIR:${target}>")
  endif()
endfunction()
