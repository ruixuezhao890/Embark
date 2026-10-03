# ESP32-S3 后端（占位）
#
# 真机接入以 ESP-IDF 组件形式进行，见 .scratch/embark-v1/issues/11-esp32s3-backend.md。
#
# 这个文件什么时候会被处理：
#   * 宿主构建：默认不进来；只有 -DEMBARK_BUILD_ESP32=ON 时顶层才 add_subdirectory 本目录，
#     而在实现之前，进来就是下面这句 FATAL_ERROR。
#   * IDF 构建（v1 尚未接通）：IDF 把自己组件的 CMakeLists 当组件描述处理，届时本文件要改成
#     idf_component_register(...) —— 现在还没到那一步。
#
# 开工前要先确认的事（spec §15）：
#   * 具体板型与触摸控制器型号
#   * 显示总线与时序参数、LVGL 的显示/输入驱动对接方式
#   * IDF 版本（本机现装 5.4）与 FreeRTOS 配置
message(FATAL_ERROR
        "platform/esp32 还没实现：见 .scratch/embark-v1/issues/11-esp32s3-backend.md\n"
        "开工前要先定：板型与触摸控制器型号、显示总线与时序、LVGL 显示/输入对接、IDF 与 FreeRTOS 配置。")
