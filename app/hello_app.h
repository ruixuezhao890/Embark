/**
 * Embark 最简 App 模板（issues/12 的新手最短路径，docs/README.md「改起来」）
 *
 * 手绘 UI 退役后它是最薄的薄壳：无后台策略（suspend）、无状态、无消息。
 * 界面 = EEZ 屏 “hello”（屏名约定：screen 名 == App 名）；Studio 还没画时进入
 * 保持当前屏 + 一条告警（缺屏 App 策略 A，见 eez_ui_bridge.h）—— 画好同名屏后
 * 自动生效，C++ 零改动。作为“加一个 App”的最小范例，任何新 App 都可以从这里起步。
 */
#ifndef EMBARK_APP_HELLO_APP_H
#define EMBARK_APP_HELLO_APP_H

#include <embark/app.h>
#include <embark/framework.h>

namespace embark::demo {

class HelloApp final : public App {
 public:
  HelloApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "hello"; }
  // 中文标题（issue 16 元数据）；无图标 → 启动器取“你”字兜底（字库已含）。
  [[nodiscard]] const char* title() const override { return "你好"; }

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onExit() override;
  void onForegroundTick(std::uint32_t now_ms) override;
};

}  // namespace embark::demo

#endif /* EMBARK_APP_HELLO_APP_H */
