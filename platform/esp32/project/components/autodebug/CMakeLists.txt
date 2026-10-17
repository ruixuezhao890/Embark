idf_component_register(
    SRCS "autodebug.c"
    INCLUDE_DIRS "include"
    # IDF 5.3 起把单体 driver 拆成 esp_driver_* 组件，driver/uart.h 归 esp_driver_uart。
    # 不写这一项，IDF 5.4 会直接编译失败（"includes driver/uart.h ... not in the requirements
    # list of autodebug"）。IDF < 5.3 没有这个组件名，那时把本行改成 PRIV_REQUIRES driver。
    # IDF 5.3 起驱动组件化后必须逐个点名：uart = esp_driver_uart，esp_timer.h = esp_timer
    # （esp_log/freertos 属 IDF 的公共依赖，不用写）。漏哪个，IDF 5.4 会直接编译失败并提示补哪个。
    PRIV_REQUIRES esp_driver_uart esp_timer
)