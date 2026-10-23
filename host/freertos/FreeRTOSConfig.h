/*
 * Embark 宿主侧的 FreeRTOS 内核配置（issues/06）
 *
 * 内核 = 干净的 upstream V10.6.2（submodule third_party/freertos，不带任何本地补丁），
 * 端口 = portable/MSVC-MingW（Win32 线程 + 模拟中断），配置与钩子由我们自持。
 * 蓝本是 .scratch/embark-v1/spikes/02-host-freertos/ 里跑通的那份，本文件只做了三处
 * 刻意的收紧，其余照抄：
 *
 *   1. configSUPPORT_DYNAMIC_ALLOCATION 0 + configUSE_TIMERS 0
 *      —— v1 内核是"零堆内核"（spec §10）：宿主上唯一会创建的任务（UI 任务）用
 *         xTaskCreateStatic，队列/互斥也全部是 Static 版（ETL 的 mutex_freertos.h
 *         用的就是 xSemaphoreCreateMutexStatic）。动态分配 API（xTaskCreate /
 *         xQueueCreate / xTimerCreate 的动态版）在编译期就不存在，heap_4.c 根本不编，
 *         于是"0 次动态分配"是结构性的而不是靠运行期统计。etl::callback_timer 是
 *         ETL 自己的轮询式定时器（由 UI 循环 tick() 驱动），不需要 FreeRTOS 软件定时器。
 *   2. configASSERT —— 内核不变量一破就停：走 embark_freertos_assert_failed →
 *         embark::assert_failed（spec §9：编程错误 = 日志 + halt）。
 *   3. INCLUDE_* 里砍掉没用到的（xTimerPendFunctionCall / xTaskResumeFromISR），
 *      其余保持 spike 验证过的集合。
 *
 * 真机（ESP32-S3，issue 11）不读这份文件：IDF 用 sdkconfig 生成自己的
 * FreeRTOSConfig.h。宿主这份只属于宿主构建。
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* configASSERT 与溢出钩子的 C 兼容桥（实现见同目录 freertos_bridge.cpp） */
#include "embark_freertos_bridge.h"

#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0
#define configTICK_RATE_HZ                      ( 1000 )
#define configMAX_PRIORITIES                    ( 32 )
#define configMINIMAL_STACK_SIZE                ( ( unsigned short ) 128 )
#define configMAX_TASK_NAME_LEN                 ( 16 )
#define configUSE_TRACE_FACILITY                1
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configQUEUE_REGISTRY_SIZE               10
#define configUSE_QUEUE_SETS                    0
#define configUSE_TIME_SLICING                  1
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     1
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 5
#define configUSE_APPLICATION_TASK_TAG          0

/* Memory allocation: 纯静态（见文件头注释 1）。堆（heap_4.c）不参与编译。 */
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        0

/* Hook function name definitions. */
#define configCHECK_FOR_STACK_OVERFLOW          2   /* Method 2：任务交界处 + 寄存器检查 */
#define configUSE_MALLOC_FAILED_HOOK            0
#define configUSE_TICKLESS_IDLE                 0

/* Software timer definitions: v1 不用软件定时器（见文件头注释 1）。 */
#define configUSE_TIMERS                        0

/* Required for the Windows simulator port. */
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS

/* Co-routine definitions（V10.6 内核已删除协程，留空即可）。 */
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         ( 2 )

/* Set the following definitions to 1 to include the API function, or zero
 * to exclude the API function. */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          0
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          0
#define INCLUDE_xTaskAbortDelay                 0
#define INCLUDE_xTaskGetHandle                  0
#define INCLUDE_xTaskResumeFromISR              0

/* Interrupt nesting behaviour. The Windows simulator uses simulated interrupts
 * so the following two defines apply to simulated interrupts only. */
#define configKERNEL_INTERRUPT_PRIORITY         ( 7 << 5 )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    ( 5 << 5 )

/* The maximum number of simulated interrupt handlers that can be installed
 * using vPortSetInterruptHandler(). Must be at least 1. */
#define configMAX_SIMULATED_INTERRUPTS          8

/* 内核不变量一破就停（见文件头注释 2）。注意这是 C 宏，要能在 C 和 C++ 里都展开。 */
#define configASSERT( x )                                                          \
    do {                                                                           \
        if( ( x ) == 0 ) {                                                         \
            embark_freertos_assert_failed( __FILE__, __LINE__, #x );               \
        }                                                                          \
    } while( 0 )

#endif /* FREERTOS_CONFIG_H */