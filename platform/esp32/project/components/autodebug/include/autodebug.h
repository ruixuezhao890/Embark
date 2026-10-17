/* esp32-autodebug 固件后门组件: 串口自报接口。 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化串口(115200 8N1)。uart_num 对应 UART_NUM_0/UART_NUM_1。 */
void autodebug_init(int uart_num);

/** 打印固件启动横幅: [AUTODEBUG_BOOT] version ... */
void autodebug_boot_banner(const char *app_name);

/** 打印通过令牌: [AUTODEBUG_PASS] message */
void autodebug_pass(const char *message);

/** 打印测试用例标记: [TEST_START] name */
void autodebug_test_start(const char *name);

/** 打印心跳标记: [AUTODEBUG_HB] uptime_ms=<...> */
void autodebug_heartbeat(void);

/** 查询当前心跳是否启用(仅当 Kconfig 开启时)。 */
bool autodebug_heartbeat_enabled(void);

/** 启动心跳任务(间隔毫秒, 0 = 禁用)。 */
void autodebug_heartbeat_start(uint32_t interval_ms);

#ifdef __cplusplus
}
#endif