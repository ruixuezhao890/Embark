/**
 * Embark 编译期配置（forced include —— 见顶层 CMakeLists.txt 的 -include）
 *
 * 这个头文件是三件事的唯一事实来源（spec §16.2）：
 *   1. ETL 的编译期宏 —— ETL 的宏必须在任何 ETL 头文件之前生效；
 *   2. ETL 版本锁定 —— 版本不符直接编译失败，而不是等运行期才发现 API 变了；
 *   3. 平台相关的最小定义。
 *
 * 为什么用 forced include，而不是在 CMake 里散着写 target_compile_definitions：
 * 那样只能盖住"我们主动写了目标"的地方，盖不住测试、沙盒、以及以后新增的每个目标。
 * 而 ETL 的宏一旦漏掉，后果往往是静默的（比如互斥实现悄悄落到 gcc 内建原子）。
 */
#ifndef EMBARK_CONFIG_H
#define EMBARK_CONFIG_H

/* --- ETL 版本锁定（spec §16.3）------------------------------------------- */
/* 仓库锁的 submodule 是 20.49.0；20.40.0 → 20.49.0 的升级已按 spec §16.3 的 12 条清单
 * 复核完（见 .scratch/embark-v1/issues/01-etl-version-verify.md）。
 * 再换版本必须重跑那 12 条，复核完再改下面的数字。 */
#include <middleware/etl/version.h>

static_assert(ETL_VERSION_MAJOR == 20 && ETL_VERSION_MINOR == 49,
              "ETL 版本变了：先按 spec §16.3 的清单复核，再改 config/embark_config.h");

/* --- 要定义的 ETL 宏 ----------------------------------------------------- */

/* 不用 STL：内核与 app 只允许 ETL 的定容容器（spec §10）。
 * 不定义时 ETL 默认 ETL_USING_STL=1（platform.h:89-95）。 */
#define ETL_NO_STL 1

/* 容器边界检查 —— 20.49.0 把它收拢成了三个开关，全在 error_handler.h:537-565：
 *   ETL_CHECK_PUSH_POP         push/pop 的满/空检查（90 处调用点，覆盖 vector、deque、list、
 *                              forward_list、stack、queue、priority_queue、intrusive 族、indirect_vector）
 *   ETL_CHECK_INDEX_OPERATOR   operator[] 的下标检查（15 处：vector/array/span/string_view/…）
 *   ETL_CHECK_EXTRA            其余前置条件（122 处：front/back 不能空、insert/erase 的迭代器区间合法…）
 * 三个都不定义时，对应的宏展开成空 —— 往满容器里写既不报错也不记录，直接把内存写坏。
 * v1 开前两个（直接对应内存安全）；ETL_CHECK_EXTRA 更严但面更大，暂时留着不定义，
 * 哪天想连 front/back 空访问一起抓，把下面这行打开即可。 */
#define ETL_CHECK_PUSH_POP 1
#define ETL_CHECK_INDEX_OPERATOR 1

/* 目标 OS：ETL 只靠这一个宏挑互斥实现（mutex.h:37）。ETL 自己没有任何地方定义它，
 * 不给就会落到 ETL_COMPILER_GCC → mutex_gcc_sync.h，所以最终必须由我们定义。
 *
 * 但它的影响面比"选互斥实现"大得多 —— 实测（issue 01）：
 *   定义它以后，<etl/callback_timer.h> → <etl/timer.h> → <etl/atomic.h> →
 *   <etl/atomic/atomic_gcc_sync.h> → <etl/mutex.h> → mutex_freertos.h → 需要 FreeRTOS.h。
 * 也就是说：只要平台还没有 FreeRTOS 的头文件，就连 callback_timer 都编不过。
 *
 * 所以这里按平台能力开关，而不是无条件定义：
 *   宿主：FreeRTOS Windows port 接进来（issues/02、06）之前为 0；
 *   真机与 port 落地后的宿主：由顶层 CMake 传 EMBARK_PLATFORM_HAS_FREERTOS=1。
 */
#ifndef EMBARK_PLATFORM_HAS_FREERTOS
#define EMBARK_PLATFORM_HAS_FREERTOS 0
#endif

#if EMBARK_PLATFORM_HAS_FREERTOS
#define ETL_TARGET_OS_FREERTOS 1
#endif

/* 错误处理：ETL_ASSERT 在 ETL_LOG_ERRORS 档下只回调、不中断执行
 * （error_handler.h:339）。所以"最后一条日志 + halt"必须由我们注册的回调实现（spec §9）。 */
#define ETL_LOG_ERRORS 1

/* 定时器族强制二选一，不定义直接 #error（callback_timer.h:54、message_timer.h:54）。
 * 宿主与真机都是多任务环境，选原子锁版本；中断上下文里跑定时器 v1 用不着。 */
#define ETL_CALLBACK_TIMER_USE_ATOMIC_LOCK 1
#define ETL_MESSAGE_TIMER_USE_ATOMIC_LOCK 1

/* --- 明确不要定义的（列出来是为了拦住以后"顺手加上"）--------------------- */
/* ETL_USE_ASSERT_FUNCTION —— 会把 ETL_ASSERT 换成调 assert()，而我们要走自己的回调；
 * ETL_THROW_EXCEPTIONS    —— 内核编不了异常（spec §10），默认就是 0；
 * ETL_*_REPAIR_ENABLE     —— 自动修复会掩盖边界错误，宁可炸得早（spec §16.2）；
 * ETL_MESSAGE_ID_TYPE / ETL_FSM_STATE_ID_TYPE —— 保持默认宽度，v1 消息类型不多。 */

#endif /* EMBARK_CONFIG_H */
