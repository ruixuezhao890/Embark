/**
 * HAL · 后端装配（spec §8）
 *
 * 一个 Context 就是"这台机器上这一份 HAL"：真机在 device/ 里造一份，
 * 测试自己就地搭一份 fake 的。—— 没有运行期注册表、没有"哪个后端生效"的判断，
 * 因为有且只有一份，编译期就定死了（spec §8：后端在编译期按 CMake 目标选定）。
 *
 * 引用成员意味着 Context 不能默认构造 —— 这是故意的：装配点在启动入口一处。
 * 指针成员可空：允许"无屏平台"（先跑通逻辑）、允许"没有盘"（真机的 SD 卡没插）
 * ⇒ 那是属性而非错误，用到的模块自己判空并返回 not_ready。
 *
 * 服务层（ADR 0019）：Context 同时是服务层的唯一注入面，所以 imu / audio / network 三个
 * 服务槽也在这里，语义与 display / input 同款（没有这块硬件就是 nullptr）。它们的完整
 * 定义在各自的头里（hal/imu.h、hal/audio.h、hal/network.h），这一层只放指针，
 * 因此只做前置声明 —— 不 include 服务头，免得 Context 变成所有服务的公共依赖。
 */
#ifndef EMBARK_HAL_CONTEXT_H
#define EMBARK_HAL_CONTEXT_H

#include <embark/hal/bus.h>
#include <embark/hal/display.h>
#include <embark/hal/filesystem.h>
#include <embark/hal/input.h>
#include <embark/hal/log_sink.h>
#include <embark/hal/persistence.h>
#include <embark/hal/system.h>
#include <embark/hal/time.h>

namespace embark::hal {

class IImu;      // hal/imu.h（姿态服务，issue 08）
class IAudio;    // hal/audio.h（音频服务，issue 09）
class INetwork;  // hal/network.h（网络服务，issue 10）

struct Context {
  ITime& time;
  IPersistence& storage;
  ILogSink& log;
  ISystem& system;
  IBus& bus;
  IDisplay* display = nullptr;
  IInput* input = nullptr;
  IFileSystem* filesystem = nullptr;
  // --- 服务层（ADR 0019）。三个槽今天恒为 nullptr：服务实现还没落地（08/09/10）。
  // 名字按"能力"取（IImu / IAudio / INetwork），不按芯片取 —— 型号名不许进 hal/*.h，
  // 见 AGENTS.md「本仓硬规则」。
  IImu* imu = nullptr;
  IAudio* audio = nullptr;
  INetwork* network = nullptr;
};

}  // namespace embark::hal

#endif /* EMBARK_HAL_CONTEXT_H */
