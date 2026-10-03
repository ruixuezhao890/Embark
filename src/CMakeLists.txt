# Embark 内核（spec §9/§10：零堆、无异常、无 RTTI）
#
# 骨架期只有版本信息这一支；UI 任务、App 注册表、消息、定时器按 issues/06、07 补。

add_library(embark_core STATIC
        embark/version.cpp)

add_library(embark::core ALIAS embark_core)

# 自己的公开头文件：include/embark/...（内部一律 <embark/...> 引用）
target_include_directories(embark_core PUBLIC "${PROJECT_SOURCE_DIR}/include")

# 平台名由顶层按目标决定，编进二进制（platform_name()）
target_compile_definitions(embark_core PRIVATE EMBARK_PLATFORM_NAME="${EMBARK_PLATFORM_NAME}")

target_link_libraries(embark_core
        PUBLIC  embark_build_options
        PRIVATE embark_kernel_flags)
