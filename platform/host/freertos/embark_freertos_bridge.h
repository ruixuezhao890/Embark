/**
 * Embark · FreeRTOS → 框架诊断桥的一个 C 兼容头（issues/06）
 *
 * FreeRTOSConfig.h 与 freertos_hooks.c 是 C（FreeRTOS 内核源码也是 C），
 * 而 embark::assert_failed / embark::fatal 是 C++ 符号（namespace + noexcept），
 * 两边不能直接互相调用。这三个函数就是跨语言边界：C 侧只管带好现场参数
 * （文件/行号/表达式/任务名），实现（freertos_bridge.cpp）转交 embark 的
 * 诊断入口 —— spec §9：编程错误 = 最后一条日志 + halt。
 */
#ifndef EMBARK_FREERTOS_BRIDGE_H
#define EMBARK_FREERTOS_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* configASSERT 失败：内核内部不变量被破坏（编程错误，halt）。 */
void embark_freertos_assert_failed(const char* file, int line, const char* expression);

/* 任务栈溢出（configCHECK_FOR_STACK_OVERFLOW == 2 的第三种检测触发）。 */
void embark_freertos_stack_overflow(const char* task_name);

/* FreeRTOS 堆分配失败（configSUPPORT_DYNAMIC_ALLOCATION == 1 时才可能走）；
 * v1 宿主内核是纯静态分配，这条只是防御性保留。 */
void embark_freertos_malloc_failed(void);

#ifdef __cplusplus
}
#endif

#endif /* EMBARK_FREERTOS_BRIDGE_H */