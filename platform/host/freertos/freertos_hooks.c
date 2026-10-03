/*
 * Embark 宿主 FreeRTOS 钩子（issues/06）
 *
 * v1 是纯静态分配内核（FreeRTOSConfig.h 里 configSUPPORT_DYNAMIC_ALLOCATION 0），
 * 所以内核需要的三块"任务内存"全部由这里供给：
 *   - 空闲任务（configSUPPORT_STATIC_ALLOCATION == 1 时必须提供，否则起不来）；
 *   - 定时器任务（configUSE_TIMERS == 1 时需要；宿主关闭了软件定时器，这块保留作
 *     为防御 —— 万一以后打开，不用回头补钩子）。V10.6.2 用 vApplicationGetTimerTaskMemory
 *     声明函数，V11 改名 vApplicationGetTimerTaskMemoryV2；升级内核时记得改；
 *   - 栈溢出与堆分配失败钩子：栈溢出（configCHECK_FOR_STACK_OVERFLOW == 2）走
 *     embark_freertos_stack_overflow → embark::fatal；malloc 失败钩子在纯静态
 *     配置下永远不会被调，留着以防哪天打开动态分配。
 *
 * 本文件按进程级单例写：空闲/定时器任务内存都是文件级 static（BSS 段，零初始化，
 * 编译期就存在，不占堆、没有初始化顺序问题）。
 */
#include "FreeRTOS.h"
#include "task.h"

#include "embark_freertos_bridge.h"

/*-----------------------------------------------------------*/
/* 空闲任务：任务控制块 + 栈，都是 BSS 里的静态存储。        */
/*-----------------------------------------------------------*/

static StaticTask_t idle_task_tcb;
static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];

void vApplicationGetIdleTaskMemory( StaticTask_t** tcb,
                                    StackType_t** stack,
                                    uint32_t* stack_size ) {
    *tcb = &idle_task_tcb;
    *stack = idle_task_stack;
    *stack_size = ( uint32_t ) configMINIMAL_STACK_SIZE;
}

/*-----------------------------------------------------------*/
/* 定时器任务（configUSE_TIMERS == 1 才被引用；见文件头注释）。*/
/*-----------------------------------------------------------*/

#if ( configUSE_TIMERS == 1 )
static StaticTask_t timer_task_tcb;
static StackType_t timer_task_stack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetTimerTaskMemory( StaticTask_t** tcb,
                                     StackType_t** stack,
                                     uint32_t* stack_size ) {
    *tcb = &timer_task_tcb;
    *stack = timer_task_stack;
    *stack_size = ( uint32_t ) configTIMER_TASK_STACK_DEPTH;
}
#endif /* configUSE_TIMERS == 1 */

/*-----------------------------------------------------------*/
/* 栈溢出（configCHECK_FOR_STACK_OVERFLOW == 2）             */
/*-----------------------------------------------------------*/

#if ( configCHECK_FOR_STACK_OVERFLOW > 0 )
void vApplicationStackOverflowHook( TaskHandle_t task,
                                    char* task_name ) {
    /* 走到这里时内核已经清楚是哪个任务爆栈了；任务名最多 configMAX_TASK_NAME_LEN
     * 个字符且末尾有空字符（prvTaskCheckFreeStackSpace 的调用点保证）。 */
    ( void ) task;

    embark_freertos_stack_overflow( task_name );
}
#endif /* configCHECK_FOR_STACK_OVERFLOW > 0 */

/*-----------------------------------------------------------*/
/* malloc 失败（configSUPPORT_DYNAMIC_ALLOCATION == 1 才可能被调）*/
/*-----------------------------------------------------------*/

#if ( configUSE_MALLOC_FAILED_HOOK == 1 )
void vApplicationMallocFailedHook( void ) {
    embark_freertos_malloc_failed();
}
#endif /* configUSE_MALLOC_FAILED_HOOK == 1 */