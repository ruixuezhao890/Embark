# 自带示例 App 与前后台策略演示（issues/07、08 —— 骨架期为占位）
#
# App 之间只通过框架消息通信，禁止互相 include（spec §4、§6）。
add_library(embark_apps INTERFACE)
add_library(embark::apps ALIAS embark_apps)

# App 层与内核同规则（spec §10）：无异常、无 RTTI、容器只走 ETL 定容版。
# 编译开关留给"宿主仿真放开"（spec §10 末句），真正需要时再加，不提前留钩子。
target_link_libraries(embark_apps INTERFACE
        embark_build_options
        embark_kernel_flags)
