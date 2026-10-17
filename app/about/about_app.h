/**
 * Embark · 关于 App（"设备信息"页）：issue 26「新 App 端到端流程」的试点 App。
 *
 * 它是本仓库第一个**从零按流程走完**的 App —— AiDoGUI 屏 "about"（12 个控件）+ 这一层薄壳，
 * 用来把"新 App 到底要动哪几处"这条链路一次性压平、并固化成文档（docs/guides/new-app-guide.md）。
 *
 * 选它做试点的理由（三条，都跟它的内容有关）：
 *   1. 字符表门禁：七个中文标签字字都是新的（平台/版本/可用堆/字库/栈水位/存盘/对时），
 *      任何一个字没进字库，真机上就是方框 —— 一次性把 gen_fonts_bin.mjs 的门禁逼出来。
 *   2. 动态数据重推：七行全是运行期值（堆/栈/字库/SD/RTC 每条都可能量不了），
 *      进屏必重推 —— 一次性演示 ADR 0015 的 repaint 通路。
 *   3. 两套后端同源：同一份 C++ 在宿主上报 "host"，在真机上报 "esp32"，
 *      宿主界面上的数字与真机的数字不一样但代码一样 —— 这是"宿主验收 ≠ 真机没问题"的活体证据。
 *
 * 薄壳纪律（与 settings_app / weather_app / music_app 一致）：
 *   - onCreate 只记账 + 持句柄 + 挂 repaint 回调；
 *   - onEnter/onResume 只按屏名加载自己的屏（缺屏只记日志，不崩）；
 *   - onForegroundTick 泵一帧；
 *   - 回主屏由屏上 Btn_Back 经设计器事件 embark_ui_open_launcher 承担（ADR 0014）。
 * 纯读：不发消息、不写存储、不碰任何外设，App 侧没有一行新增 HAL。
 *
 * 后台策略 = 默认 suspend：回到前台时屏会重建，repaint 回调把七行重新推一遍。
 */
#ifndef EMBARK_APP_ABOUT_APP_H
#define EMBARK_APP_ABOUT_APP_H

#include <cstdint>

#include <lvgl.h>

#include <embark/app.h>
#include <embark/framework.h>

namespace embark::demo {

/// 关于 App：界面 = AiDoGUI 屏 "about"，内容 = 平台/版本/堆/栈/字库/SD/RTC 七行只读信息。
class AboutApp final : public App {
 public:
  AboutApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "about"; }
  [[nodiscard]] const char* title() const override { return "关于"; }
  [[nodiscard]] const char* icon() const override { return LV_SYMBOL_LIST; }

  [[nodiscard]] AppSettings settings() const override { return AppSettings{}; }

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onExit() override;
  void onForegroundTick(std::uint32_t now_ms) override;

  /// repaint 回调的落点：nav 桥每次装载完 about 屏就调它（裸函数指针，故转调单例）。
  static void on_screen_reloaded();

  /// 把七行重新推成当前值。进屏时由上面那个回调触发；也可在验收程序里直接调。
  void refresh_display();

  // --- 验收观测（全部在 UI 任务里跑）-----------------------------------------
  [[nodiscard]] std::uint32_t enters() const noexcept { return enters_; }
  [[nodiscard]] std::uint32_t resumes() const noexcept { return resumes_; }
  [[nodiscard]] std::uint32_t refreshes() const noexcept { return refreshes_; }

 private:
  void set_label(const char* widget_name, const char* text);
  void set_row(const char* widget_name, const char* key, const char* value);

  static AboutApp* g_for_ui;  ///< 进程内唯一的实例（宿主与真机都是单例，不存在多开）

  Framework* fw_ = nullptr;
  std::uint32_t enters_ = 0;
  std::uint32_t resumes_ = 0;
  std::uint32_t refreshes_ = 0;
};

}  // namespace embark::demo

#endif /* EMBARK_APP_ABOUT_APP_H */
