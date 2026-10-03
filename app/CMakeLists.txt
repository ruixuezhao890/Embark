# 自带示例 App 与前后台策略演示（issues/07、08 —— 骨架期为占位）
#
# App 之间只通过框架消息通信，禁止互相 include（spec §4、§6）。
add_library(embark_apps INTERFACE)
add_library(embark::apps ALIAS embark_apps)
target_link_libraries(embark_apps INTERFACE embark_build_options)
