/**
 * Embark · 相册 App 实现：见 album_app.h 头注释。
 *
 * 薄壳纪律与 music_app.cpp 完全一致：onCreate 只记账；onEnter/onResume 只按屏名挂自己的屏
 * （后端由 app/common/ui_backend.h 选）；onForegroundTick 泵一帧。
 * 界面（占位屏）与导航（返回按钮 → embark_ui_open_launcher）都在 AiDoGUI 工程里，
 * C++ 侧不碰任何控件。
 */
#include <cstdint>

#include <embark/framework.h>
#include <embark/log.h>

#include "album_app.h"

#if defined(EMBARK_AIDOGUI_UI)
#include "ui_backend.h"
#endif

namespace embark::demo {

void AlbumApp::onCreate(Framework& fw) {
  fw_ = &fw;
  ELOG_INFO("App {} onCreate（后台策略 = suspend；界面 = AiDoGUI 占位屏 {}）", name(), name());
}

void AlbumApp::onEnter() {
  ++enters_;
  ELOG_INFO("App {} 进入前台（enter {}, resume {}，累计 {} 次）", name(), enters_, resumes_,
            enters_ + resumes_);
#if defined(EMBARK_AIDOGUI_UI)
  ui_backend_load_for_app(name());
#endif
}

void AlbumApp::onPause() {
  ELOG_INFO("App {} 离开前台（enter {}, resume {}）", name(), enters_, resumes_);
}

void AlbumApp::onResume() {
  ++resumes_;
  ELOG_INFO("App {} 回到前台（enter {}, resume {}）", name(), enters_, resumes_);
#if defined(EMBARK_AIDOGUI_UI)
  ui_backend_load_for_app(name());
#endif
}

void AlbumApp::onExit() {
  ELOG_INFO("App {} onExit", name());
}

void AlbumApp::onForegroundTick(std::uint32_t now_ms) {
  (void)now_ms;
#if defined(EMBARK_AIDOGUI_UI)
  ui_backend_tick();
#endif
}

}  // namespace embark::demo
