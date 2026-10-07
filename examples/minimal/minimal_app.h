/**
 * Embark · 最小示例 App（examples/minimal）
 *
 * 一个 App 到底要写什么？这个文件是完整答案 —— 不用 EEZ Studio，也不依赖 app/ 里的
 * demo App：
 *
 *   - name()            注册名（唯一即可；用 EEZ 时要求屏名与它同名）
 *   - onCreate          建自己的 LVGL 对象。框架只调钩子，不创建/不销毁/不隐藏 App 的界面
 *   - onEnter           第一次成为前台：把自己的屏挂上去
 *   - onPause           离开前台：框架只通知，别在这里碰 UI
 *   - onResume          回到前台：再挂一次自己的屏（别的 App 可能把屏换走过）
 *   - onForegroundTick  每帧一次、且只在当前台 —— 刷界面就放这里
 *   - onExit            关机路径（v1 只有这一条路径会触发）
 *
 * 后台策略用默认值（suspend）：这个例子不需要后台跑。要后台节拍就覆写 settings()，
 * 见 app/clock/clock_app.h 与 docs/concepts/messages-and-background.md。
 *
 * 界面文字用 ASCII：LVGL 内置字体只含 ASCII，中文要自己生成字体子集
 * （tools/font/gen_font.mjs，见 docs/guides/scaffold-and-tools.md）。
 */
#ifndef EMBARK_EXAMPLE_MINIMAL_APP_H
#define EMBARK_EXAMPLE_MINIMAL_APP_H

#include <cstdint>

#include <lvgl.h>

#include <embark/app.h>
#include <embark/framework.h>

namespace embark::example {

/// 最小 App：自己建一块屏、自己挂上去、每帧自己更新一个标签。
class MinimalApp final : public App {
 public:
  MinimalApp() noexcept = default;

  [[nodiscard]] const char* name() const override { return "minimal"; }
  [[nodiscard]] const char* title() const override { return "最小示例"; }
  [[nodiscard]] const char* icon() const override { return LV_SYMBOL_PLAY; }
  // accent() 用默认的全局强调色（design_tokens::accent）—— 不写魔法颜色。

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onForegroundTick(std::uint32_t now_ms) override;
  void onExit() override;

  /// 观测点：跑完之后打日志用（--frames 模式）。
  [[nodiscard]] unsigned foreground_ticks() const { return foreground_ticks_; }
  [[nodiscard]] unsigned resumes() const { return resumes_; }

 private:
  /// 把帧数写进标签。节流到每 kRefreshEveryFrames 帧一次 —— 不是所有东西都要每帧重画。
  void refresh_counter();

  Framework* fw_ = nullptr;
  lv_obj_t* screen_ = nullptr;   ///< 本 App 自己的屏；框架从不碰它
  lv_obj_t* counter_ = nullptr;  ///< 显示帧数的标签
  unsigned foreground_ticks_ = 0;
  unsigned resumes_ = 0;
};

}  // namespace embark::example

#endif /* EMBARK_EXAMPLE_MINIMAL_APP_H */
