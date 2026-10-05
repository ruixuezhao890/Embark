/**
 * 启动器几何（issues/16 契约）的单元测试。
 *
 * launcher_geometry.h 刻意不 include LVGL（纯数学），所以本文件也不需要 LVGL——
 * tests 目标本来就不链 LVGL。测试朋友：弹簧收敛、槽位↔角度边界、拖动手势换算、
 * UTF-8 首字兜底，以及"元数据表长一致"契约的工程表达。
 */
#include <cmath>
#include <cstdint>
#include <cstring>

#include <doctest/doctest.h>

#include <embark/app.h>
#include <embark/app_registry.h>
#include <embark/launcher_geometry.h>

using namespace embark::launcher;

// ============================ 弹簧 ============================

TEST_CASE("弹簧收敛：有限步内 |pos-target| < epsilon") {
  const float target = 1.0F;
  float pos = 0.0F;
  int steps = 0;
  for (; steps < 64; ++steps) {
    pos = spring_step(pos, target);
    if (spring_settled(pos, target)) {
      break;
    }
  }
  // 0.85^n < 0.01 ⇒ n ≈ 29；64 步是它的两倍多，够宽裕。
  REQUIRE(steps < 64);
  CHECK(spring_settled(pos, target));
  CHECK(std::fabs(pos - target) < spring_epsilon);
  // 收敛后继续推仍稳定在 target（不动点）。
  CHECK(spring_settled(pos + (target - pos) * spring_factor, target));
}

TEST_CASE("弹簧单步：pos += (target-pos) * 0.15") {
  CHECK(spring_step(0.0F, 1.0F) == doctest::Approx(0.15F));
  CHECK(spring_step(1.0F, 0.0F) == doctest::Approx(0.85F));
  CHECK(spring_step(2.0F, 2.0F) == doctest::Approx(2.0F));
  CHECK(spring_settled(0.0F, 0.0F));
  CHECK_FALSE(spring_settled(0.0F, 1.0F));
}

// ============================ 槽位 ↔ 角度 ============================

TEST_CASE("槽位偏移：0° = 正上方，正 = 顺时针，1 槽 = STEP") {
  CHECK(slot_offset(0, 0.0F) == doctest::Approx(0.0F));
  CHECK(slot_offset(1, 0.0F) == doctest::Approx(step_deg));
  CHECK(slot_offset(2, 1.0F) == doctest::Approx(step_deg));
  CHECK(slot_offset(-1, 0.0F) == doctest::Approx(-step_deg));
  CHECK(slot_offset(0, 1.0F) == doctest::Approx(-step_deg));
}

TEST_CASE("槽位中心：支点 (120,270)、R=82、角度 0° 在正上方") {
  const auto top = slot_center(0.0F);
  CHECK(top.x == doctest::Approx(pivot_x));
  CHECK(top.y == doctest::Approx(pivot_y - radius));  // (120, 188)

  const auto left = slot_center(-90.0F);
  CHECK(left.x == doctest::Approx(pivot_x - radius));  // 38
  CHECK(left.y == doctest::Approx(pivot_y));

  const auto right = slot_center(90.0F);
  CHECK(right.x == doctest::Approx(pivot_x + radius));  // 202
  CHECK(right.y == doctest::Approx(pivot_y));

  // 选中槽就是"自己停在正上方"：slot_center_for(selected, selected) = 正上方。
  const auto sel = slot_center_for(1, 1);
  CHECK(sel.x == doctest::Approx(pivot_x));
  CHECK(sel.y == doctest::Approx(pivot_y - radius));
  // 相邻槽相差 26°：sin/cos 直接对上几何定义。
  const auto adj = slot_center_for(2, 1);  // 26° 顺时针
  CHECK(adj.x == doctest::Approx(pivot_x + radius * std::sin(26.0 * 3.14159265358979 / 180.0)));
  CHECK(adj.y == doctest::Approx(pivot_y - radius * std::cos(26.0 * 3.14159265358979 / 180.0)));
}

TEST_CASE("命中判定：|offset| < STEP/2 才算选中槽") {
  CHECK(is_selected(0.0F));
  CHECK(is_selected(12.99F));
  CHECK(is_selected(-12.99F));
  CHECK_FALSE(is_selected(13.0F));   // 恰好半槽：边界外
  CHECK_FALSE(is_selected(-13.0F));
  CHECK_FALSE(is_selected(13.01F));
  CHECK_FALSE(is_selected(40.0F));
}

// ============================ 槽号换算 ============================

TEST_CASE("selected_index：四舍五入 + 环绕 + 空表防御") {
  CHECK(selected_index(0.4F, 5) == 0);
  CHECK(selected_index(0.5F, 5) == 1);   // lround 半步进位
  CHECK(selected_index(1.7F, 5) == 2);
  CHECK(selected_index(-0.4F, 5) == 0);
  CHECK(selected_index(-0.5F, 5) == 4);  // lround(-0.5) = -1 → 环绕到末尾槽
  CHECK(selected_index(5.0F, 5) == 0);   // 整圈回来
  CHECK(selected_index(123.0F, 5) == 3); // 多圈取模
  CHECK(selected_index(0.5F, 0) == 0);   // 空表：防御值
}

TEST_CASE("normalize_index：负数与多圈统一到 [0, count)") {
  CHECK(normalize_index(0, 5) == 0);
  CHECK(normalize_index(4, 5) == 4);
  CHECK(normalize_index(5, 5) == 0);
  CHECK(normalize_index(-1, 5) == 4);
  CHECK(normalize_index(-6, 5) == 4);
  CHECK(normalize_index(12, 5) == 2);
  CHECK(normalize_index(0, 0) == 0);   // 空表：防御值
  CHECK(normalize_index(-3, 0) == 0);
}

TEST_CASE("clamp_target：拖出边界夹回 [0, count-1]") {
  CHECK(clamp_target(0.0F, 5) == 0.0F);
  CHECK(clamp_target(4.0F, 5) == 4.0F);
  CHECK(clamp_target(-1.0F, 5) == 0.0F);
  CHECK(clamp_target(7.0F, 5) == 4.0F);
  CHECK(clamp_target(-1.0F, 0) == 0.0F);
  CHECK(clamp_target(7.0F, 0) == 0.0F);
}

TEST_CASE("drag_target：1px = 0.1 槽，向上拖 = 槽号增加") {
  CHECK(drag_target(0.0F, 10.0F, 5) == doctest::Approx(0.0F));   // 向下拖出下界 → 夹 0
  CHECK(drag_target(0.0F, -10.0F, 5) == doctest::Approx(1.0F));  // 向上 10px = +1 槽
  CHECK(drag_target(0.0F, -40.0F, 5) == doctest::Approx(4.0F));  // 向上 40px = +4 槽
  CHECK(drag_target(0.0F, -50.0F, 5) == doctest::Approx(4.0F));  // 再拖也夹在末尾
  CHECK(drag_target(2.0F, 5.0F, 5) == doctest::Approx(1.5F));    // 从中间回拖
}

// ============================ UTF-8 首字兜底 ============================

TEST_CASE("first_utf8_char：取首码点的 1..4 字节") {
  char out[8] = {0};
  CHECK(first_utf8_char("启动器", out, sizeof(out)));
  CHECK(std::strcmp(out, "启") == 0);  // 3 字节

  CHECK(first_utf8_char("A", out, sizeof(out)));
  CHECK(std::strcmp(out, "A") == 0);  // 1 字节

  // 容量不足（3 字节字需要 3+NUL = 4）：
  CHECK_FALSE(first_utf8_char("启", out, 3));
  // 恰好够：4 字节容量装 3 字节字 + NUL；
  char tight[4] = {0};
  CHECK(first_utf8_char("启", tight, sizeof(tight)));
  CHECK(std::strcmp(tight, "启") == 0);

  CHECK_FALSE(first_utf8_char("", out, sizeof(out)));            // 空串
  CHECK_FALSE(first_utf8_char(nullptr, out, sizeof(out)));       // 空指针
  CHECK_FALSE(first_utf8_char("A", out, 0));                     // 零容量
  CHECK_FALSE(first_utf8_char("\x80" "ab", out, sizeof(out)));     // 非法首字节（续字节开头）
  CHECK_FALSE(first_utf8_char("\xE5\x90", out, sizeof(out)));  // 码点被截断、缺续字节
}

// ============================ 元数据表长契约 ============================

namespace {

/// 契约假 App：模拟 EMBARK_APP_TABLE 的 5 条注册（launcher 首位）。
/// title() 元数据随 App 类型编译期固定，启动器画槽时逐 App 查询——没有第二张表，
/// 所以"表长一致"降到两个静态断言：表长 ≤ max_apps（app_registry 已保证）且
/// max_slots 放得下注册表（launcher 侧再保证一次）。
struct LauncherMetaApp : embark::App {
  explicit LauncherMetaApp(const char* title_value) : title_value_(title_value) {}
  const char* name() const override { return "meta"; }
  const char* title() const override { return title_value_; }
  // 其余七个钩子全空实现（本测试只关心元数据，不需要生命周期行为）
  void onCreate(embark::Framework&) override {}
  void onEnter() override {}
  void onPause() override {}
  void onResume() override {}
  void onBackgroundTick(std::uint32_t) override {}
  void onMessage(const etl::imessage&) override {}
  void onExit() override {}
  const char* title_value_;
};

struct IconedMetaApp : LauncherMetaApp {
  explicit IconedMetaApp(const char* title_value) : LauncherMetaApp(title_value) {}
  const char* icon() const override { return "x"; }  // 有图标：首字兜底不参与
};

}  // namespace

static_assert(embark::launcher::max_slots == embark::max_apps,
              "启动器槽位数必须与 App 表上限一致（spec/几何契约）");
static_assert(embark::launcher::max_slots >= 5U,
              "注册表至少 5 条（launcher + 4 demo App），启动器必须放得下");

TEST_CASE("元数据契约：5 条注册、中文标题可渲染（首字兜底）") {
  LauncherMetaApp launcher("启动器");
  IconedMetaApp clock("时钟");
  LauncherMetaApp settings("设置");
  LauncherMetaApp ticker("心跳");
  LauncherMetaApp job("任务");

  // 直接模拟 app_registry 的语义：表 = 指针数组，size 已知（静态断言保证 ≤ max_slots）。
  embark::App* table[] = {&launcher, &clock, &settings, &ticker, &job};
  constexpr std::size_t kSize = 5;

  for (std::size_t i = 0; i < kSize; ++i) {
    const embark::App* app = table[i];
    REQUIRE(app->title() != nullptr);
    REQUIRE(std::strlen(app->title()) > 0);
    // 启动器的可渲染性：
    if (app->icon() == nullptr) {
      char first[8] = {0};
      // 无图标 → 标题首字兜底，必须能取到（字库缺字是 CI 审计的事，这里是几何层契约）。
      CHECK(first_utf8_char(app->title(), first, sizeof(first)));
      CHECK(std::strlen(first) > 0);
    }
    // 每个槽的几何坐标存在（选中态与扇形内）：
    const auto center = slot_center_for(static_cast<int>(i), 0);
    CHECK(std::isfinite(center.x));
    CHECK(std::isfinite(center.y));
  }
}
