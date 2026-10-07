/**
 * Embark · 最小示例 App 实现（examples/minimal）
 *
 * 全部界面调用都是普通 LVGL，没有 EEZ 生成代码参与 —— 这样"框架怎么跑"和
 * "EEZ 怎么用"就是两件可以分开看的事。想用 EEZ Studio 画界面，见
 * docs/guides/eez-ui-manual.md 与 app/launcher/launcher_app.cpp（薄壳写法）。
 *
 * 三条纪律（都在 docs/concepts/app-lifecycle.md 有出处）：
 *   1. 只有 UI 任务能碰 LVGL —— App 的钩子都在 UI 任务里跑，所以这里随便用 LVGL；
 *      own_task 的后台钩子【不】在 UI 任务里，那里绝不能碰 LVGL。
 *   2. 界面对象归 App 自己所有：框架只发 onPause/onResume，不会替你隐藏/销毁。
 *   3. 屏切换只发请求：App 不能自己调 request_switch 的等价物，EEZ 那边也是
 *      屏观察者把 SetPage 转成框架请求（见 app/common/eez_ui_nav.cpp）。
 */
#include <embark/log.h>

#include "minimal_app.h"

namespace embark::example {
namespace {

/// 标签多久刷一次。60 帧 @ 5 ms/帧 ≈ 0.3 秒 —— 够看出在动，又不至于每帧都写。
constexpr unsigned kRefreshEveryFrames = 60U;

}  // namespace

void MinimalApp::onCreate(Framework& fw) {
  fw_ = &fw;  // 想在钩子里发消息/切 App 就靠它（v1 的 App 都这么存一份）

  // 自己建屏：lv_obj_create(nullptr) 造的就是一块屏（没有父对象）。
  // 这里不马上挂上去 —— onCreate 是所有 App 的装配期，挂屏是 onEnter 的事。
  screen_ = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen_, lv_color_hex(embark::design_tokens::bg), LV_PART_MAIN);

  lv_obj_t* title = lv_label_create(screen_);
  lv_label_set_text(title, "Embark minimal example");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);
  lv_obj_set_style_text_color(title, lv_color_hex(embark::design_tokens::text_primary),
                              LV_PART_MAIN);

  counter_ = lv_label_create(screen_);
  lv_label_set_text(counter_, "frame 0");
  lv_obj_align(counter_, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_color(counter_, lv_color_hex(embark::design_tokens::accent), LV_PART_MAIN);

  lv_obj_t* hint = lv_label_create(screen_);
  lv_label_set_text(hint, "close the window to quit");
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -24);
  lv_obj_set_style_text_color(hint, lv_color_hex(embark::design_tokens::text_secondary),
                              LV_PART_MAIN);

  ELOG_INFO("App {} onCreate：屏与控件已建好", name());
}

void MinimalApp::onEnter() {
  ELOG_INFO("App {} 进入前台", name());
  // 第一次成为前台：把自己的屏挂上显示器。之后每次回来走 onResume。
  lv_scr_load(screen_);
}

void MinimalApp::onPause() {
  // 框架只通知，不碰你的界面：这里【不要】去隐藏/销毁屏，也不要再改标签。
  ELOG_INFO("App {} 退到后台", name());
}

void MinimalApp::onResume() {
  ++resumes_;
  ELOG_INFO("App {} 回到前台（第 {} 次）", name(), resumes_);
  // 回到前台要【再挂一次】：别的 App 可能把屏换走过（本例只有一个 App，但习惯要养成）。
  lv_scr_load(screen_);
}

void MinimalApp::onForegroundTick(std::uint32_t now_ms) {
  (void)now_ms;  // 这个例子不用时间；要显示时钟/倒计时就在这里取 now_ms
  ++foreground_ticks_;
  if (foreground_ticks_ % kRefreshEveryFrames == 0U) {
    refresh_counter();
  }
}

void MinimalApp::onExit() {
  ELOG_INFO("App {} onExit（v1 只有关机路径会走到这里）", name());
}

void MinimalApp::refresh_counter() {
  // LVGL 自带的 lv_label_set_text_fmt 只认 ASCII 格式串；要打中文得先换字体
  // （LVGL 内置字体不含汉字，见 tools/font/gen_font.mjs）。
  lv_label_set_text_fmt(counter_, "frame %u", foreground_ticks_);
}

}  // namespace embark::example
