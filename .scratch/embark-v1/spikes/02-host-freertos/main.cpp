// Embark issue 02 探针：FreeRTOS Windows(MSVC-MingW) 端口 × MinGW-w64 GCC 15.1
//
// 想证明的四件事：
//   1. 归档的 FreeRTOS V10.6.2 kernel + MSVC-MingW port 能用 GCC 15.1 编过；
//   2. 「一个 UI 任务 + 若干逻辑任务」在宿主上真能跑（每任务一个 Win32 线程）；
//   3. 跨任务队列（框架消息路径的底座）可用，并且满队列不能阻塞发送方；
//   4. 1000 tick 后怎么收尾：本端口 vTaskStartScheduler() 永不返回，退出只能靠 exit()。
//      编译期开关 SPIKE_END_SCHEDULER 用来对照演示「靠 vTaskEndScheduler() 收尾会挂住」。
//
// 这是一次性取证代码，不进仓库。

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#ifndef SPIKE_RUN_TICKS
#define SPIKE_RUN_TICKS 1000
#endif

namespace {

constexpr TickType_t kRunTicks = SPIKE_RUN_TICKS;  // 标称 1 tick = 1 ms
constexpr UBaseType_t kProducerPeriodTicks = 5;   // 逻辑任务：每 5 ms 发一条
constexpr UBaseType_t kUiPeriodTicks = 2;         // UI 任务：每 2 ms 泵一次
constexpr UBaseType_t kStackUiWords = 4096;
constexpr UBaseType_t kStackLogicWords = 2048;

QueueHandle_t g_queue = nullptr;
volatile uint32_t g_produced = 0;
volatile uint32_t g_consumed = 0;
volatile uint32_t g_queue_full = 0;
volatile uint32_t g_ui_loops = 0;

std::chrono::steady_clock::time_point g_start;

void finish_and_exit(TickType_t elapsed_ticks) {
    const auto wall_ms = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - g_start)
            .count());
    std::printf(
        "[spike] ticks=%lu ui_loops=%lu produced=%lu consumed=%lu queue_full=%lu "
        "heap_free=%u\n",
        static_cast<unsigned long>(elapsed_ticks),
        static_cast<unsigned long>(g_ui_loops),
        static_cast<unsigned long>(g_produced),
        static_cast<unsigned long>(g_consumed),
        static_cast<unsigned long>(g_queue_full),
        static_cast<unsigned>(xPortGetFreeHeapSize()));
    std::printf("[spike] wall clock for %lu ticks = %ld ms\n",
                static_cast<unsigned long>(kRunTicks), wall_ms);

    // 宿主仿真不是实时环境：Sleep() 粒度让 1 tick 实测约 2 ms（见 issues/02 的 Answer），
    // 所以判据放宽到「不早于标称、不超过标称的 3 倍」。
    const bool ok = (g_produced > 0) && (g_consumed > 0) && (g_ui_loops > 0) &&
                    (wall_ms >= static_cast<long>(kRunTicks) - 50) &&
                    (wall_ms <= static_cast<long>(kRunTicks) * 3 + 300);
    std::printf("[spike] tick/wall ratio = %.2f ms per tick | RESULT: %s\n",
                static_cast<double>(wall_ms) / static_cast<double>(kRunTicks),
                ok ? "PASS" : "FAIL");

#if defined(SPIKE_END_SCHEDULER)
    // 对照组：按「教科书写法」收尾。先 flush 保证结论已落盘，随后进程会挂住
    //（原因见 issues/02 的 Answer：main 线程的模拟中断循环是 for(;;)，且
    // xPortRunning 变 0 之后 prvProcessTickInterrupt 的 configASSERT 会触发死循环）。
    std::fflush(stdout);
    vTaskEndScheduler();
    vTaskDelete(nullptr);
#else
    std::fflush(nullptr);
    std::_Exit(ok ? 0 : 1);
#endif
}

void producer_task(void* /*argument*/) {
    uint32_t value = 0;
    for (;;) {
        ++value;
        // 零等待：队列满就立刻失败 —— 框架选择「丢最旧 + 计数 + 一条 WARN」，绝不阻塞发送方
        if (xQueueSend(g_queue, &value, 0) == pdPASS) {
            ++g_produced;
        } else {
            ++g_queue_full;
        }
        vTaskDelay(kProducerPeriodTicks);
    }
}

void ui_task(void* /*argument*/) {
    const TickType_t start_tick = xTaskGetTickCount();
    for (;;) {
        uint32_t value = 0;
        while (xQueueReceive(g_queue, &value, 0) == pdPASS) {
            ++g_consumed;
        }
        ++g_ui_loops;
        vTaskDelay(kUiPeriodTicks);

        const TickType_t elapsed_ticks = xTaskGetTickCount() - start_tick;
        if (elapsed_ticks >= kRunTicks) {
            finish_and_exit(elapsed_ticks);
        }
    }
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // 重定向到文件时也要实时可见

    std::printf("[spike] FreeRTOS %s | heap_4 | tick %u Hz | shutdown=%s\n",
                tskKERNEL_VERSION_NUMBER, static_cast<unsigned>(configTICK_RATE_HZ),
#if defined(SPIKE_END_SCHEDULER)
                "vTaskEndScheduler (对照组)"
#else
                "exit() from task"
#endif
    );

    g_queue = xQueueCreate(8, sizeof(uint32_t));
    if (g_queue == nullptr) {
        std::printf("[spike] FAIL: xQueueCreate returned NULL\n");
        return 1;
    }

    const BaseType_t ui_ok = xTaskCreate(ui_task, "ui", kStackUiWords, nullptr, 3, nullptr);
    const BaseType_t logic_ok =
        xTaskCreate(producer_task, "logic", kStackLogicWords, nullptr, 2, nullptr);
    if (ui_ok != pdPASS || logic_ok != pdPASS) {
        std::printf("[spike] FAIL: xTaskCreate ui=%d logic=%d\n", static_cast<int>(ui_ok),
                    static_cast<int>(logic_ok));
        return 1;
    }

    std::printf("[spike] heap before scheduler: %u bytes free\n",
                static_cast<unsigned>(xPortGetFreeHeapSize()));

    g_start = std::chrono::steady_clock::now();
    vTaskStartScheduler();  // 本端口永远不会返回：main 线程会停在模拟中断循环里

    std::printf("[spike] UNEXPECTED: vTaskStartScheduler() returned\n");
    return 2;
}
