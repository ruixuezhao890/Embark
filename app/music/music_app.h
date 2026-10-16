/**
 * Embark · 音乐 App（占位壳）。issue launcher-pages：主屏磁贴按注册表生成后，
 * 需要"第 5、6 个 App"来证明**分页真的能翻到后面**——这就是那两个 App。
 *
 * 它只做一件事：把 AiDoGUI 工程里同名的占位屏（screen "music"：返回按钮 + "占位屏 / 界面还没做"）
 * 挂上显示器。界面本身还没做（占位屏），所以这里没有数据、没有消息，也没有业务方法。
 *
 * 薄壳纪律与 settings_app / weather_app 完全一致：
 *   - onCreate 只记账 + 持有框架句柄；
 *   - onEnter/onResume 只按屏名加载自己的屏（缺屏只记日志，不崩）；
 *   - onForegroundTick 泵一帧；
 *   - 回主屏由屏上的"返回"按钮经设计器事件 embark_ui_open_launcher 承担
 *     （2026-10-16 起回程只有这一条路：上滑返回手势已撤除，见 docs/adr/0014-in-screen-back.md）。
 *
 * 后台策略 = 默认 suspend（占位屏没有要刷的数据，回到前台时屏会重建）。
 */
#ifndef EMBARK_APP_MUSIC_APP_H
#define EMBARK_APP_MUSIC_APP_H

#include <cstdint>

#include <lvgl.h>

#include <embark/app.h>
#include <embark/framework.h>

namespace embark::demo {

/// 占位 App：音乐。界面 = AiDoGUI 屏 "music"（占位屏）。
class MusicApp final : public App {
 public:
  MusicApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "music"; }
  [[nodiscard]] const char* title() const override { return "音乐"; }
  // 主屏磁贴只有文字（设计稿口径），图标留着给后续"应用列表"界面用；accent 用默认。
  [[nodiscard]] const char* icon() const override { return LV_SYMBOL_AUDIO; }

  [[nodiscard]] AppSettings settings() const override { return AppSettings{}; }

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onExit() override;
  void onForegroundTick(std::uint32_t now_ms) override;

  // --- 验收观测（全部在 UI 任务里跑）-----------------------------------------
  [[nodiscard]] std::uint32_t enters() const noexcept { return enters_; }
  [[nodiscard]] std::uint32_t resumes() const noexcept { return resumes_; }

 private:
  Framework* fw_ = nullptr;
  std::uint32_t enters_ = 0;
  std::uint32_t resumes_ = 0;
};

}  // namespace embark::demo

#endif /* EMBARK_APP_MUSIC_APP_H */
