/**
 * HAL · 后端装配（spec §8）
 *
 * 一个 Context 就是"这台机器上这一份 HAL"：真机在 platform/<platform>/ 里造一份，
 * 测试自己就地搭一份 fake 的。—— 没有运行期注册表、没有"哪个后端生效"的判断，
 * 因为有且只有一份，编译期就定死了（spec §8：后端在编译期按 CMake 目标选定）。
 *
 * 引用成员意味着 Context 不能默认构造、不能拷贝 —— 这是故意的：装配点在启动入口一处。
 * display / input 可空：v1 允许"无屏平台"（先跑通逻辑），因此是属性而非错误。
 */
#ifndef EMBARK_HAL_CONTEXT_H
#define EMBARK_HAL_CONTEXT_H

#include <embark/hal/bus.h>
#include <embark/hal/display.h>
#include <embark/hal/input.h>
#include <embark/hal/log_sink.h>
#include <embark/hal/persistence.h>
#include <embark/hal/system.h>
#include <embark/hal/time.h>

namespace embark::hal {

struct Context {
  ITime& time;
  IPersistence& storage;
  ILogSink& log;
  ISystem& system;
  IBus& bus;
  IDisplay* display = nullptr;
  IInput* input = nullptr;
};

}  // namespace embark::hal

#endif /* EMBARK_HAL_CONTEXT_H */
