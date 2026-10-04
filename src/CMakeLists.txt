# Embark 内核（spec §9/§10：零堆、无异常、无 RTTI）
#
# 现在有：版本信息、日志装配、App 契约 + 编译期注册表、Framework
# （装配/唯一 UI 循环/前台切换/退出路径）（issues/04、06）。
# 消息派发与后台定时器已随 issue 07 落地（bus/framework 内部）。
# 错误码只有一份名字表：枚举声明本身（include/embark/error.h 里 E_FMT_DERIVE_ENUM）；
# issue 13 起没有 error.cpp —— 打印交给 efmt，不再手抄一份 to_string。

add_library(embark_core STATIC
        embark/framework.cpp
        embark/bus.cpp
        embark/log.cpp
        embark/version.cpp)

add_library(embark::core ALIAS embark_core)

# 自己的公开头文件：include/embark/...（内部一律 <embark/...> 引用）
target_include_directories(embark_core PUBLIC "${PROJECT_SOURCE_DIR}/include")

# 平台名由顶层按目标决定、版本号取 project(... VERSION ...)：都编进二进制
# （platform_name() / version_string()）。头文件里不再重复写一遍版本号，避免两处漂移。
target_compile_definitions(embark_core PRIVATE
        EMBARK_PLATFORM_NAME="${EMBARK_PLATFORM_NAME}"
        EMBARK_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(embark_core
        PUBLIC  embark_build_options
        PRIVATE embark_kernel_flags)
