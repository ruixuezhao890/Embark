/* esp32-autodebug 固件后门组件: UART 串口自报实现。 */
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "autodebug.h"

#ifndef CONFIG_AUTODEBUG_BACKDOOR
#define CONFIG_AUTODEBUG_BACKDOOR 1
#endif
#ifndef CONFIG_AUTODEBUG_UART_NUM
#define CONFIG_AUTODEBUG_UART_NUM 0
#endif
#ifndef CONFIG_AUTODEBUG_HEARTBEAT_MS
#define CONFIG_AUTODEBUG_HEARTBEAT_MS 1000
#endif

static const char *TAG = "autodebug";
static int s_uart = -1;
static bool s_heartbeat = false;

static void autodebug_write(const char *s)
{
#if CONFIG_AUTODEBUG_BACKDOOR
    if (s_uart >= 0) {
        uart_write_bytes((uart_port_t)s_uart, s, (int)strlen(s));
    }
#endif
}

void autodebug_init(int uart_num)
{
#if CONFIG_AUTODEBUG_BACKDOOR
    const uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install((uart_port_t)uart_num, 2048, 0, 0, NULL, 0) != ESP_OK) {
        ESP_LOGW(TAG, "uart_driver_install failed on UART%d", uart_num);
        return;
    }
    uart_param_config((uart_port_t)uart_num, &cfg);
    uart_set_pin((uart_port_t)uart_num, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    s_uart = uart_num;
    ESP_LOGI(TAG, "backdoor ready on UART%d", uart_num);
#endif
}

void autodebug_boot_banner(const char *app_name)
{
    char line[96];
    snprintf(line, sizeof(line), "[AUTODEBUG_BOOT] %s\n", app_name ? app_name : "esp32s3-app");
    autodebug_write(line);
}

void autodebug_pass(const char *message)
{
    char line[128];
    snprintf(line, sizeof(line), "[AUTODEBUG_PASS] %s\n", message ? message : "OK");
    autodebug_write(line);
}

void autodebug_test_start(const char *name)
{
    char line[128];
    snprintf(line, sizeof(line), "[TEST_START] %s\n", name ? name : "test");
    autodebug_write(line);
}

void autodebug_heartbeat(void)
{
    char line[64];
    int64_t up = esp_timer_get_time() / 1000;
    snprintf(line, sizeof(line), "[AUTODEBUG_HB] uptime_ms=%lld\n", (long long)up);
    autodebug_write(line);
}

bool autodebug_heartbeat_enabled(void)
{
#if CONFIG_AUTODEBUG_BACKDOOR
    return s_uart >= 0 && s_heartbeat;
#else
    return false;
#endif
}

static void autodebug_hb_task(void *arg)
{
    uint32_t interval_ms = (uint32_t)(uintptr_t)arg;
    while (true) {
        autodebug_heartbeat();
        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }
}

void autodebug_heartbeat_start(uint32_t interval_ms)
{
#if CONFIG_AUTODEBUG_BACKDOOR
    if (interval_ms == 0 || s_heartbeat) {
        return;
    }
    s_heartbeat = true;
    xTaskCreate(autodebug_hb_task, "adbg_hb", 2048, (void *)(uintptr_t)interval_ms, 5, NULL);
#endif
}