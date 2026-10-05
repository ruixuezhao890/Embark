/**
 * Embark 最简 App 模板（issues/12 的新手最短路径，docs/README.md「改起来」）
 *
 * 只显示一行字的空 App：七钩子全实现、无后台策略（suspend）、无状态、
 * 无消息。作为"加一个 App"的最小范例，任何新 App 都可以从这里起步。
 */
#ifndef EMBARK_APP_HELLO_APP_H
#define EMBARK_APP_HELLO_APP_H

#include <lvgl.h>

#include <embark/app.h>
#include <embark/framework.h>

namespace embark::demo {

class HelloApp final : public App {
 public:
  HelloApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "hello"; }
  // 中文标题（issue 16 元数据）；无图标 → 启动器取"你"字兜底（字库已含）。
  [[nodiscard]] const char* title() const override { return "你好"; }

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onExit() override;

 private:
  Framework* fw_ = nullptr;
  lv_obj_t* screen_ = nullptr;
};

}  // namespace embark::demo

#endif /* EMBARK_APP_HELLO_APP_H */