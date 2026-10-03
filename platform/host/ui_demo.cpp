/**
 * 宿主 · LVGL 界面演示（issues/05）
 *
 * 这个可执行文件是"宿主真的有屏"的证据：SDL2 窗口 + RGB565 纹理 + LVGL 8.3.11，
 * 走向 HAL（IDisplay/IInput）而不是直接调 SDL —— 于是它与真机跑的是同一条链路。
 *
 * 无人值守验收用的开关（会自动退出，不需要人去点窗口）：
 *   --frames N        跑 N 帧后退出（默认 0 = 一直跑到关窗）
 *   --click           在第 20 帧合成一次鼠标点击（走 SDL_PushEvent → 真事件队列 → 输入后端）
 *   --click X,Y       指定面板坐标（默认点按钮中心）
 *   --screenshot FILE 最后一帧把窗口内容存成 BMP（用 Python/Pillow 转 PNG 便于查看）
 *   --scale S         窗口放大倍数（默认 2）
 *   --delay MS        每帧间隔（默认 5）
 *   --help            打印用法
 *
 * 界面文案是英文：LVGL 内置字体只有 Montserrat（无中文字形），中文文案要配自定义字体，
 * 那是 issue 12 的事。日志照旧是中文。
 *
 * 退出码：0 = 正常；1 = 后端起不来；2 = 用 --click 点了但按钮没响应（验收判据）。
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL.h>
#include <lvgl.h>

#include <embark/diagnostics.h>
#include <embark/error.h>
#include <embark/hal/context.h>
#include <embark_limits.h>
#include <middleware/elog/elog.hpp>

#include "host_context.h"
#include "host_display.h"
#include "host_input.h"
#include "host_lvgl_mem.h"
#include "lvgl_port.h"

namespace {

// 按钮几何：合成点击的默认目标就是它中心（面板坐标系）。
constexpr int button_x = 110;
constexpr int button_y = 150;
constexpr int button_width = 100;
constexpr int button_height = 40;
constexpr int button_center_x = button_x + button_width / 2;
constexpr int button_center_y = button_y + button_height / 2;

/// 合成点击的帧号：先移动+按下，隔两帧再抬起 ——
/// 让 LVGL 分两个读周期处理，点一下就是完整的一次"按下 → 抬起"。
constexpr int click_move_frame = 20;
constexpr int click_release_frame = click_move_frame + 2;

struct Options {
  int frames = 0;
  int scale = 2;
  int delay_ms = 5;
  const char* screenshot = nullptr;
  bool click = false;
  int click_x = button_center_x;
  int click_y = button_center_y;
  int quit_at = 0;  ///< >0 时在第 N 帧推一个 SDL_QUIT（等价于用户点窗口的关闭按钮）
};

void print_usage() {
  std::printf(
      "用法：embark_host_ui [选项]\n"
      "  --frames N         跑 N 帧后退出（默认 0：一直跑到关窗）\n"
      "  --click [X,Y]      合成一次鼠标点击（默认点按钮中心 %d,%d）\n"
      "  --quit-at N        第 N 帧推一个关窗事件（验收「关窗干净退出」用）\n"
      "  --screenshot FILE  最后一帧存 BMP 截图\n"
      "  --scale S          窗口放大倍数（默认 2）\n"
      "  --delay MS         每帧间隔毫秒（默认 5）\n"
      "  --help             显示本帮助\n",
      button_center_x, button_center_y);
}

/// 解析 "X,Y"（失败就用默认值）。
void parse_point(const char* text, int& x, int& y) {
  int parsed_x = 0;
  int parsed_y = 0;
  if (std::sscanf(text, "%d,%d", &parsed_x, &parsed_y) == 2) {
    x = parsed_x;
    y = parsed_y;
  }
}

Options parse_options(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const char* arg = argv[index];
    if (std::strcmp(arg, "--frames") == 0 && index + 1 < argc) {
      options.frames = std::atoi(argv[++index]);
    } else if (std::strcmp(arg, "--scale") == 0 && index + 1 < argc) {
      options.scale = std::atoi(argv[++index]);
    } else if (std::strcmp(arg, "--delay") == 0 && index + 1 < argc) {
      options.delay_ms = std::atoi(argv[++index]);
    } else if (std::strcmp(arg, "--screenshot") == 0 && index + 1 < argc) {
      options.screenshot = argv[++index];
    } else if (std::strcmp(arg, "--quit-at") == 0 && index + 1 < argc) {
      options.quit_at = std::atoi(argv[++index]);
    } else if (std::strcmp(arg, "--click") == 0) {
      options.click = true;
      if (index + 1 < argc && argv[index + 1][0] != '-') {
        parse_point(argv[++index], options.click_x, options.click_y);
      }
    } else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
      print_usage();
      std::exit(0);
    }
  }
  if (options.scale < 1) {
    options.scale = 1;
  }
  if (options.delay_ms < 0) {
    options.delay_ms = 0;
  }
  return options;
}

lv_obj_t* g_counter_label = nullptr;
int g_clicks = 0;

void on_button_clicked(lv_event_t* event) {
  (void)event;
  ++g_clicks;
  if (g_counter_label != nullptr) {
    lv_label_set_text_fmt(g_counter_label, "Clicks: %d", g_clicks);
  }
  ELOG_INFO("按钮点击：第 {} 次", g_clicks);
}

/// 界面：标题 + 提示 + 计数 + 一个按钮。尺寸都在 config/embark_limits.h 里定死。
void build_ui() {
  lv_obj_t* screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_make(0x12, 0x18, 0x20), LV_PART_MAIN);

  lv_obj_t* title = lv_label_create(screen);
  lv_label_set_text(title, "Embark host UI");
  lv_obj_set_style_text_color(title, lv_color_make(0xf0, 0xf4, 0xf8), LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

  lv_obj_t* hint = lv_label_create(screen);
  lv_label_set_text(hint, "SDL2 + LVGL 8.3.11 via HAL");
  lv_obj_set_style_text_color(hint, lv_color_make(0x8a, 0x9a, 0xaa), LV_PART_MAIN);
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 44);

  g_counter_label = lv_label_create(screen);
  lv_label_set_text(g_counter_label, "Clicks: 0");
  lv_obj_set_style_text_color(g_counter_label, lv_color_make(0x6c, 0xd4, 0xff), LV_PART_MAIN);
  lv_obj_align(g_counter_label, LV_ALIGN_TOP_MID, 0, 80);

  lv_obj_t* button = lv_btn_create(screen);
  lv_obj_set_size(button, button_width, button_height);
  lv_obj_align(button, LV_ALIGN_TOP_LEFT, button_x, button_y);
  lv_obj_add_event_cb(button, on_button_clicked, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* button_label = lv_label_create(button);
  lv_label_set_text(button_label, "Click me");
  lv_obj_center(button_label);
}

// --- 合成输入：走 SDL 自己的事件队列，等于把"人在窗口上点了一下"喂给整条链路 ---

void push_motion_and_press(int scale, int panel_x, int panel_y) {
  SDL_Event motion{};
  motion.type = SDL_MOUSEMOTION;
  motion.motion.x = panel_x * scale;
  motion.motion.y = panel_y * scale;
  SDL_PushEvent(&motion);

  SDL_Event press{};
  press.type = SDL_MOUSEBUTTONDOWN;
  press.button.button = SDL_BUTTON_LEFT;
  press.button.state = SDL_PRESSED;
  press.button.clicks = 1;
  press.button.x = panel_x * scale;
  press.button.y = panel_y * scale;
  SDL_PushEvent(&press);
}

void push_release(int scale, int panel_x, int panel_y) {
  SDL_Event release{};
  release.type = SDL_MOUSEBUTTONUP;
  release.button.button = SDL_BUTTON_LEFT;
  release.button.state = SDL_RELEASED;
  release.button.clicks = 1;
  release.button.x = panel_x * scale;
  release.button.y = panel_y * scale;
  SDL_PushEvent(&release);
}

/// 把窗口内容存成 BMP（SDL_SaveBMP 只认 BMP；要看图就转 PNG）。
/// 取像素这段交给显示后端（HostDisplay::capture_surface）—— 它知道正确的读回顺序。
bool save_screenshot(embark::platform::host::HostDisplay& display, const char* path) {
  SDL_Surface* surface = display.capture_surface();
  if (surface == nullptr) {
    ELOG_ERROR("截图失败：{}", SDL_GetError());
    return false;
  }

  const bool ok = SDL_SaveBMP(surface, path) == 0;
  if (!ok) {
    ELOG_ERROR("截图失败：写文件失败（{}）", SDL_GetError());
  }
  SDL_FreeSurface(surface);
  return ok;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // 重定向到文件时也能看到进度

  const Options options = parse_options(argc, argv);

  embark::platform::host::HostHal& hal = embark::platform::host::HostHal::instance();
  embark::platform::host::HostDisplay display(static_cast<std::uint8_t>(options.scale), "Embark 宿主 UI");
  embark::platform::host::HostInput input(hal.time(), display);
  hal.attach_display(display);
  hal.attach_input(input);

  const embark::Error hal_error = hal.init();
  if (hal_error != embark::Error::none) {
    std::fprintf(stderr, "HAL 初始化失败：%s\n", embark::to_string(hal_error));
    return 1;
  }
  ELOG_INFO("HAL 就绪：显示 {}×{}，输入 {}，持久化 {}",
            static_cast<int>(display.info().width), static_cast<int>(display.info().height),
            input.is_ready() ? "就绪" : "未就绪", hal.storage_path());

  embark::platform::host::LvglPort port(hal.context());
  const embark::Error lvgl_error = port.init();
  if (lvgl_error != embark::Error::none) {
    std::fprintf(stderr, "LVGL 端口初始化失败：%s\n", embark::to_string(lvgl_error));
    return 1;
  }

  build_ui();
  // LVGL_VERSION_* 是整数宏（third_party/lvgl/lvgl.h:16-18），拼字符串要逐个占位。
  ELOG_INFO("界面已创建：LVGL {}.{}.{}，绘制缓冲 {} 行，LVGL 堆预算 {} 字节", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
            LVGL_VERSION_PATCH, static_cast<int>(embark::lvgl_draw_buf_lines), embark::lvgl_alloc_budget_bytes);

  int frames_run = 0;
  bool screenshot_done = (options.screenshot == nullptr);
  bool quit_by_window = false;

  for (;;) {
    port.tick();
    port.pump_input();
    lv_timer_handler();  // issue 05：LVGL 只被这条主循环调用；issue 06 起由唯一的 UI 任务调用
    ++frames_run;

    if (input.quit_requested()) {
      quit_by_window = true;
      ELOG_INFO("收到关窗请求：第 {} 帧，开始收尾", frames_run);
      break;
    }

    if (options.click) {
      if (frames_run == click_move_frame) {
        ELOG_INFO("合成点击：移动 + 按下 ({},{})", options.click_x, options.click_y);
        push_motion_and_press(options.scale, options.click_x, options.click_y);
      } else if (frames_run == click_release_frame) {
        push_release(options.scale, options.click_x, options.click_y);
      }
    }

    if (options.quit_at > 0 && frames_run == options.quit_at) {
      // 等价于用户点窗口右上角的 ×：SDL_QUIT 进事件队列，由输入后端认出来再置 quit_requested_
      // （下一帧的 pump_input 会读走它）—— 验收「关窗干净退出」走的就是这条路径。
      SDL_Event quit{};
      quit.type = SDL_QUIT;
      SDL_PushEvent(&quit);
    }

    if (options.frames > 0 && frames_run >= options.frames) {
      if (!screenshot_done) {
        screenshot_done = true;
        if (save_screenshot(display, options.screenshot)) {
          ELOG_INFO("截图已保存：{}", options.screenshot);
        }
      }
      ELOG_INFO("跑满 {} 帧，正常退出", options.frames);
      break;
    }

    if (options.delay_ms > 0) {
      SDL_Delay(static_cast<Uint32>(options.delay_ms));
    }
  }

  ELOG_INFO("统计：帧 {}，刷新 {} 次（{} 字节），Present {} 次，按钮点击 {} 次，丢输入 {}，忽略按键 {}", frames_run,
            port.refreshes(), port.flush_bytes(), display.presents(), g_clicks, port.dropped_input_events(),
            port.ignored_key_events());
  ELOG_INFO("LVGL 堆：分配 {} 次，峰值 {} 字节，退出前未回收 {} 字节（预算 {}）", embark::platform::host::lvgl_allocations(),
            embark::platform::host::lvgl_peak_bytes(), embark::platform::host::lvgl_outstanding_bytes(),
            embark::lvgl_alloc_budget_bytes);

  if (options.click && g_clicks == 0) {
    std::fprintf(stderr, "合成点击没有触发按钮回调（验收失败）\n");
    return 2;
  }
  (void)quit_by_window;
  return 0;
}
