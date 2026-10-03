# ESP32-S3 后端（占位）
#
# v1 的宿主构建不包含本目录：真机接入以 ESP-IDF 组件形式进行，见
# .scratch/embark-v1/issues/11-esp32s3-backend.md。
#
# 开工前需要先确认的事（spec §15）：
#   * 具体板型与触摸控制器型号
#   * 显示总线与时序参数、LVGL 的显示/输入驱动对接方式
#   * IDF 版本（本机现装 5.4）与 FreeRTOS 配置
message(STATUS "platform/esp32：占位目录，尚未接入构建（见 issues/11）")
