#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""scaffold_app.py —— 生成一个带 EEZ 界面的 App 薄壳（docs/guides/new-app-guide.md 第 1 节自动化）。

用法：
    python tools/scaffold_app.py my                          # dry-run：只打印蓝图与清单
    python tools/scaffold_app.py my --title "我的" \
        --var visit_count:int --var speed:float             # 声明 Flow 变量骨架
    python tools/scaffold_app.py my --apply                 # 落地：写 h/cpp + eez_vars.txt + 接 CMakeLists + 注册表
    python tools/scaffold_app.py my --apply \
        --vars-check build/include/embark_eez_vars.h        # 落地后核对变量表（构建期产物）

设计：默认 dry-run（不碰任何文件）；--apply 才写盘，且幂等（已存在且一致 → 跳过；
内容不一致 → 报错不覆盖）。EEZ 侧（画屏/绑变量/SetPage）是 Studio 二进制，**不可脚本化**，
脚本只打印对应 checklist（见 docs/guides/new-app-guide.md §2）。剧本只改三处代码：
  - 新建       app/<name>/<name>_app.{h,cpp} + eez_vars.txt（薄壳模板，目录化形态）
  - 追加一行   app/CMakeLists.txt（embark_demo_apps 源列表，锚点源行 common/eez_ui_nav.cpp 之后）
  - 追加一项   platform/host/ui_demo.cpp（EMBARK_APP_TABLE，LauncherApp 之后）
"""
from __future__ import annotations

import argparse
import datetime
import re
import sys
from pathlib import Path

try:
    sys.stdout.reconfigure(encoding="utf-8")  # Windows 控制台中文
    sys.stderr.reconfigure(encoding="utf-8")
except Exception:
    pass

REPO_ROOT = Path(__file__).resolve().parents[1]
APP_DIR = "app"
CMAKELISTS = Path("app") / "CMakeLists.txt"
UI_DEMO = Path("platform") / "host" / "ui_demo.cpp"
VAR_FN = {"int": "set_var_int", "float": "set_var_float", "bool": "set_var_bool", "string": "set_var_string"}
VAR_EXPR = {
    "int": "int(visits_)",
    "float": "float(visits_) * 0.01f",
    "bool": "(visits_ % 2) == 0",
    "string": '(visits_ % 2) ? "on" : "off"',
}
APP_NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")
FIELD_NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")


def die(msg: str, code: int = 2) -> None:
    print(f"[scaffold_app] 错误：{msg}", file=sys.stderr)
    sys.exit(code)


def pascal(name: str) -> str:
    return "".join(p.capitalize() for p in name.split("_"))


def guard_upper(name: str) -> str:
    return "EMBARK_APP_" + name.upper() + "_APP_H"


def parse_var(spec: str) -> tuple[str, str]:
    name, _, typ = spec.partition(":")
    typ = (typ or "int").strip()
    if not FIELD_NAME_RE.match(name):
        die(f"--var 字段名不合法：{name!r}（须 ^[a-z][a-z0-9_]*$）")
    if typ not in VAR_FN:
        die(f"--var 类型不合法：{typ!r}（可选 int/float/bool/string）")
    return name, typ


def build_header(app_name: str, class_name: str, title: str, policy: str | None = None, period_ms: int = 0) -> str:
    today = datetime.date.today().isoformat()
    guard = guard_upper(app_name)
    settings_decl = ""
    if policy == "tick":
        settings_decl = "\n  [[nodiscard]] AppSettings settings() const override;"
    return f"""
 * {app_name} App 壳（{today} 由 tools/scaffold_app.py 生成）—— docs/guides/new-app-guide.md 的薄壳模板。
 *
 * 界面全部交给 EEZ：此壳一行 LVGL 都不写。屏名约定：EEZ 屏名 == App 名（{app_name}；
 * 子页 <app名>_<编号>_sub）。Studio 还没画同名屏 → 进入保持当前屏 + 一条告警（缺屏
 * App 策略 A，见 eez_ui_bridge.h）—— 画好同名屏后自动生效，C++ 零改动。
 * 本文件由脚本生成：改业务逻辑直接编辑；要重新生成请先删除旧文件。
 */
#ifndef {guard}
#define {guard}

#include <embark/app.h>
#include <embark/framework.h>

namespace embark::demo {{

class {class_name} final : public App {{
 public:
  {class_name}() noexcept = default;

  [[nodiscard]] const char* name() const override {{ return "{app_name}"; }}
  [[nodiscard]] const char* title() const override {{ return "{title}"; }}{settings_decl}

  void onCreate(Framework& fw) override;
  void onEnter() override;
  void onPause() override;
  void onResume() override;
  void onExit() override;
  void onForegroundTick(std::uint32_t now_ms) override;

 private:
  std::uint32_t visits_ = 0;  // 变量骨架的示例数据源：换成真实业务状态
}};

}}  // namespace embark::demo

#endif /* {guard} */
"""


def build_source(app_name: str, class_name: str, vars_: list[tuple[str, str]], policy: str | None = None, period_ms: int = 0) -> str:
    today = datetime.date.today().isoformat()
    L: list[str] = [
        "/**",
        f" * {app_name} App 实现（{today} 由 tools/scaffold_app.py 生成）：薄壳纪律见 app/clock/clock_app.cpp。",
        " *",
        " *   - onCreate 不建屏（无手绘 = 没有 screen_）；",
        " *   - onEnter/onResume 按屏名约定加载自己的 EEZ 屏",
        " *       （eez_ui_bridge_enter_app_screen：还没画同名屏 → 保持当前屏 + 告警）；",
        " *   - onForegroundTick 泵 eez_ui_bridge_tick（LVGL 一帧）并可推 Flow 变量给 UI；",
        " *   - 想离开前台时 request_switch() 请求，框架在循环边界执行切换。",
        " *",
        " * 编译开关 EMBARK_EEZ_UI_BRIDGE：未定义的平台（esp32 真机）把桥调用编译成 no-op。",
        " */",
        "#include <embark/log.h>",
        "",
        f'#include "{app_name}_app.h"',
        "",
        "#if defined(EMBARK_EEZ_UI_BRIDGE)",
        '#include "eez_ui_bridge.h"',
        "#endif",
        "",
        "namespace embark::demo {",
        "",
        f"void {class_name}::onCreate(Framework& fw) {{",
        "  (void)fw;  // 薄壳不持有框架句柄（suspend：无后台、无消息、不切换）",
        f'  ELOG_INFO("App {{}} onCreate（薄壳：界面 = EEZ 屏 {app_name}，Studio 画好即生效）", name());',
        "}",
        "",
        f"void {class_name}::onEnter() {{",
        f'  ELOG_INFO("App {{}} 进入前台", name());',
        "#if defined(EMBARK_EEZ_UI_BRIDGE)",
        "  eez_ui_bridge_enter_app_screen(name());",
        "#endif",
        "}",
        "",
        f"void {class_name}::onPause() {{",
        f'  ELOG_INFO("App {{}} 离开前台", name());',
        "}",
        "",
        f"void {class_name}::onResume() {{",
        f'  ELOG_INFO("App {{}} 回到前台", name());',
        "#if defined(EMBARK_EEZ_UI_BRIDGE)",
        "  eez_ui_bridge_enter_app_screen(name());",
        "#endif",
        "}",
        "",
        f"void {class_name}::onExit() {{",
        f'  ELOG_INFO("App {{}} onExit", name());',
        "}",
        "",
        f"void {class_name}::onForegroundTick(std::uint32_t now_ms) {{",
        "  (void)now_ms;",
        "  ++visits_;  // 示例计数器：换成真实业务",
        "#if defined(EMBARK_EEZ_UI_BRIDGE)",
    ]
    for field, typ in vars_:
        full = f"{app_name}_{field}"
        expr = VAR_EXPR[typ]
        L.append(f'  eez_ui_bridge_{VAR_FN[typ]}("{full}", {expr});  // TODO: 换成真实业务值')
    L.extend([
        "  eez_ui_bridge_tick();",
        "#endif",
        "}",
        "",
        "}  // namespace embark::demo",
        "",
    ])
    if policy == "tick":
        L.insert(L.index(f"void {class_name}::onCreate(Framework& fw) {{"),
                 f"AppSettings {class_name}::settings() const {{\n"
                 f"  // 后台策略：tick（前台每 {period_ms}ms 周期刷新变量；suspend 收不到前台循环）\n"
                 f"  return AppSettings{{BackgroundPolicy::tick, {period_ms}U, 0U, 0U}};\n"
                 "}\n\n")
    if policy == "tick":
        L.insert(L.index(f"void {class_name}::onCreate(Framework& fw) {{"),
                 f"AppSettings {class_name}::settings() const {{\n"
                 f"  // 后台策略：tick（前台每 {period_ms}ms 周期刷新变量；suspend 收不到前台循环）\n"
                 f"  return AppSettings{{BackgroundPolicy::tick, {period_ms}U, 0U, 0U}};\n"
                 "}\n\n")
    return "\n".join(L)


def build_vars_txt(app_name: str, vars_: list[tuple[str, str]]) -> str:
    lines = [
        "# EEZ Studio 粘贴清单（tools/scaffold_app.py 生成）——把下面每行声明为 Flow 全局变量并",
        "# 绑定到控件（docs/guides/new-app-guide.md §2.4-2.5）。声明后重新 cmake -B build：构建日志会",
        "# 打印『EEZ 变量：N 个』，或构建后用 --vars-check build/include/embark_eez_vars.h 核对。",
        "# 建屏时同步设好生命周期（工程已勾 Settings→Build『Screens lifetime support』）：",
        "# 新页 General 里 createAtStart 关、Delete on unload 开；启动屏 launcher 保持常驻。",
    ]
    if vars_:
        lines += [f"{app_name}_{f}    # {t}" for f, t in vars_]
    else:
        lines += ["# （未声明 --var 变量；需要上屏数据时用 --var <字段>:<类型> 重新生成声明）"]
    return "\n".join(lines) + "\n"

def cmake_new_line(app_name: str) -> str:
    return f"{app_name}/{app_name}_app.cpp"


def app_table_entry(app_name: str, class_name: str) -> str:
    return f"embark::demo::{class_name}"


def dry_run(app_name: str, class_name: str, title: str, vars_: list[tuple[str, str]], repo: Path, policy: str | None = None, period_ms: int = 0) -> None:
    print(f"== App 蓝图：{app_name}（{class_name}，『{title}』）==")
    print(f"--- 新建 app/{app_name}/{app_name}_app.h / app/{app_name}/{app_name}_app.cpp + eez_vars.txt（薄壳 + {len(vars_)} 个变量骨架）---")
    print(build_header(app_name, class_name, title, policy, period_ms))
    if vars_:
        print(f"--- cpp 变量骨架（onForegroundTick 内）---")
        for field, typ in vars_:
            full = f"{app_name}_{field}"
            print(f'    eez_ui_bridge_{VAR_FN[typ]}("{full}", {VAR_EXPR[typ]});  // TODO: 真实业务值')
    print("--- eez_vars.txt（EEZ Studio 粘贴清单）---")
    print(build_vars_txt(app_name, vars_))
    print("--- 将修改的两处 ---")
    cm = repo / CMAKELISTS
    text = cm.read_text(encoding="utf-8")
    if f"{app_name}_app.cpp" in text:
        print(f"  = {CMAKELISTS}：已含 {app_name}/{app_name}_app.cpp（幂等）")
    else:
        print(f"  + {CMAKELISTS}：源列表追加 {app_name}/{app_name}_app.cpp")
    ui = repo / UI_DEMO
    utext = ui.read_text(encoding="utf-8")
    entry = app_table_entry(app_name, class_name)
    if entry in utext:
        print(f"  = {UI_DEMO}：注册表已含 {class_name}（幂等）")
    else:
        print(f"  + {UI_DEMO}：EMBARK_APP_TABLE 追加 {entry}")
    print()
    print("== EEZ Studio checklist（不可脚本化，见 docs/guides/new-app-guide.md §2）==")
    print(f"--- 后台策略：{policy if policy else 'suspend（默认）'}" + (f"，tick {period_ms}ms" if policy == "tick" else "") + " ---")
    steps = [
        "打开源工程 .eez-project（不在仓库；向维护者获取）",
        f"新建页面：屏名 == '{app_name}'（子页 <app名>_<编号>_sub）",
        "控件文案用 ASCII（默认字体 Montserrat 无中文）",
    ]
    steps += ([f"Flow 全局变量：{app_name}_{f}（{t}）" for f, t in vars_]
              or ["Flow 全局变量（可选）：本 App 未声明 --var 变量"])
    steps += [
        "控件属性 → 绑变量（值显示控件绑上面的变量）",
        "按钮跳转：SetPage → launcher（别用『回上一屏』——屏栈恒空）",
        "导出到 app/eez_ui/src/ui/ + cmake -S . -B build 重配（日志打印『EEZ 变量：N 个』）",
    ]
    for i, s in enumerate(steps, 1):
        print(f"  {i}. {s}")
    print()
    print(f"落地命令：python tools/scaffold_app.py {app_name} --apply")
    if vars_:
        print("  核对变量：--vars-check build/include/embark_eez_vars.h（构建后）")


def apply(app_name: str, class_name: str, title: str, vars_: list[tuple[str, str]], repo: Path, policy: str | None = None, period_ms: int = 0) -> None:
    hp = repo / APP_DIR / app_name / f"{app_name}_app.h"
    cp = repo / APP_DIR / app_name / f"{app_name}_app.cpp"
    vf = repo / APP_DIR / app_name / "eez_vars.txt"
    hdr = build_header(app_name, class_name, title, policy, period_ms)
    src = build_source(app_name, class_name, vars_, policy, period_ms)
    (repo / APP_DIR / app_name).mkdir(parents=True, exist_ok=True)
    for p, content in ((hp, hdr), (cp, src)):
        if p.exists():
            cur = p.read_text(encoding="utf-8")
            if cur != content:
                die(f"{p.relative_to(repo)} 已存在且内容与模板不同——不覆盖；确认后删除旧文件再重跑")
            print(f"  = {p.relative_to(repo)} 已是最新（幂等跳过）")
        else:
            p.write_text(content, encoding="utf-8")
            print(f"  + {p.relative_to(repo)} 已生成")
    vt = build_vars_txt(app_name, vars_)
    if vf.exists():
        if vf.read_text(encoding="utf-8") != vt:
            die(f"{vf.relative_to(repo)} 与模板不一致——不覆盖")
        print(f"  = {vf.relative_to(repo)} 已是最新（幂等跳过）")
    else:
        vf.write_text(vt, encoding="utf-8")
        print(f"  + {vf.relative_to(repo)} 已生成（EEZ Studio 粘贴清单）")

    cm = repo / CMAKELISTS
    ctext = cm.read_text(encoding="utf-8")
    if f"{app_name}/{app_name}_app.cpp" in ctext:
        print(f"  = {CMAKELISTS} 已含新源（幂等跳过）")
    else:
        anchor = "common/eez_ui_nav.cpp)"
        if anchor not in ctext:
            die(f"{CMAKELISTS} 找不到锚 '{anchor}'（文件结构变了，手工处理）")
        cm.write_text(ctext.replace(anchor, anchor + "\n" +
                    "          " + cmake_new_line(app_name), 1), encoding="utf-8")
        print(f"  + {CMAKELISTS} 源列表已加 {app_name}/{app_name}_app.cpp")
    ui = repo / UI_DEMO
    utext = ui.read_text(encoding="utf-8")
    entry = app_table_entry(app_name, class_name)
    if entry in utext:
        print(f"  = {UI_DEMO} 注册表已含 {class_name}（幂等跳过）")
    else:
        anchor = "EMBARK_APP_TABLE(embark::demo::LauncherApp, embark::demo::ClockApp, embark::demo::SettingsApp)"
        if anchor not in utext:
            die(f"{UI_DEMO} 找不到注册表锚（文件结构变了，手工处理）")
        indent = " " * 17
        utext = utext.replace(anchor, anchor + ",\n" + indent + entry, 1)
        ui.write_text(utext, encoding="utf-8")
        print(f"  + {UI_DEMO} 注册表已加 {class_name}")


def vars_check(vars_file: Path, app_name: str, vars_: list[tuple[str, str]]) -> None:
    if not vars_file.exists():
        die(f"找不到变量表 {vars_file}（先构建：cmake -S . -B build；或检查路径）", 1)
    text = vars_file.read_text(encoding="utf-8")
    missing = [f"{app_name}_{f}" for f, _ in vars_ if f'"{app_name}_{f}"' not in text]
    if missing:
        print(f"[scaffold_app] 变量表缺少 {len(missing)} 个变量：{', '.join(missing)}")
        print("  提示：变量表是构建期从生成代码解析的——检查 Studio 里是否已声明、是否重新 cmake 配置")
        sys.exit(1)
    print(f"[scaffold_app] 变量表核对通过：{app_name}_* 共 {len(vars_)} 个变量全部在列")
    print(f"  （{vars_file}）")




def interactive(repo: Path) -> None:
    """无参数运行：菜单式交互向导（python tools/scaffold_app.py）。

    问答收集 App 名/标题/后台策略/Flow 变量，先预览蓝图（dry-run）再确认落地（apply）。
    与 CLI 参数模式共用 build_header/build_source/dry_run/apply 同一套生成逻辑。
    """
    W = 60
    print("=" * W)
    print("Embark · App 生成器（tools/scaffold_app.py）")
    print("生成 app/<名字>/ 薄壳 + 变量骨架 + eez_vars.txt（EEZ Studio 粘贴清单），")
    print("并自动注册 app/CMakeLists.txt 源列表与 platform/host/ui_demo.cpp 启动表。")
    print("全程菜单问答，不需要命令行参数；参考 docs/guides/new-app-guide.md。")
    print("=" * W)

    print()
    print("请选择操作：")
    print("  1) 新建 App")
    print("  2) 给已有 App 追加变量")
    while True:
        m = input("选择 [1/2]，回车默认 1：").strip()
        if m in ("", "1"):
            mode = "new"
            break
        if m == "2":
            mode = "add"
            break
        print("  ✗ 请输入 1 或 2")

    if mode == "add":
        while True:
            app_name = input("要加变量的已有 App 名（如 demo_timer）：").strip()
            if not APP_NAME_RE.match(app_name):
                print("  ✗ 不合法：须 ^[a-z][a-z0-9_]*$")
                continue
            if not (repo / APP_DIR / app_name / f"{app_name}_app.cpp").exists():
                print(f"  ✗ app/{app_name}/ 不存在——请先新建此 App")
                continue
            break
        vars_ = ask_vars_fields()
        if not vars_:
            print("没有要追加的变量，结束。")
            return
        preview_add_vars(app_name, vars_, repo)
        while True:
            ans = input("确认追加落地？[y/N]：").strip().lower()
            if ans in ("y", "yes"):
                add_vars(app_name, vars_, repo)
                print()
                print("下一步：把 app/<名字>/eez_vars.txt 里新增的行也在 EEZ Studio 声明")
                print("为 Flow 全局变量并绑定控件 → 重新 cmake -S . -B build（日志『EEZ 变量』）")
                print("（屏的生命周期设置见 docs/guides/new-app-guide.md §2.2 旁注 / docs/guides/eez-studio-guide.md）")
                return
            if ans in ("", "n", "no"):
                print("已取消——未写盘。")
                return
            print("  ✗ 请输入 y / N")

    reserved = {"launcher", "clock", "settings", "common"}
    while True:
        raw = input("App 名（英文小写/数字/下划线，如 demo_timer）：").strip()
        if not APP_NAME_RE.match(raw):
            print("  ✗ 不合法：须 ^[a-z][a-z0-9_]*$（小写开头）")
            continue
        if raw in reserved:
            print(f"  ✗ 保留名：{sorted(reserved)} 已被占用")
            continue
        if (repo / APP_DIR / raw / f"{raw}_app.cpp").exists():
            print(f"  ✗ app/{raw}/ 已存在——换一个名字，或先手动删除旧目录")
            continue
        app_name = raw
        break

    title = input("中文标题（菜单/元数据显示）：").strip() or "未命名"

    print()
    print("后台策略（决定切走后怎么活；1 = 最省心）：")
    print("  1) suspend —— 默认：切走即挂起，界面类 App 用它就够")
    print("  2) tick 100ms —— 前台周期性刷新变量（时钟走秒类）")
    print("  3) tick 500ms —— 同上，周期更省")
    print("  （own_task 高实时属高级场景，菜单未收录：手改 settings() 或咨询维护者）")
    policy = None
    period_ms = 0
    while True:
        p = input("选择 [1-3]，回车默认 1：").strip()
        if p in ("", "1"):
            break
        if p in ("2", "3"):
            policy, period_ms = "tick", 100 if p == "2" else 500
            break
        print("  ✗ 请输入 1 / 2 / 3")

    vars_ = ask_vars_fields()
    class_name = pascal(app_name) + "App"
    print()
    dry_run(app_name, class_name, title, vars_, repo, policy, period_ms)
    while True:
        ans = input("确认落地生成？[y/N]：").strip().lower()
        if ans in ("y", "yes"):
            apply(app_name, class_name, title, vars_, repo, policy, period_ms)
            print()
            print("下一步：把 app/<名字>/eez_vars.txt 粘贴到 EEZ Studio 声明变量 →")
            print("新建同名屏（屏名 == App 名）→ 控件绑变量 → 导出 → 重新 cmake -S . -B build")
            print("（构建日志应打印『EEZ 屏表：N 个』『EEZ 变量：N 个』）")
            print("新屏别忘了生命周期：General 里 createAtStart 关 + Delete on unload 开（工程已勾")
            print("Settings→Build『Screens lifetime support』；启动屏 launcher 保持常驻）——见手册 §7")
            return
        if ans in ("", "n", "no"):
            print("已取消——只在屏幕上预览，未写盘。可重跑向导，或按蓝图手动操作。")
            return
        print("  ✗ 请输入 y / N")

def ask_vars_fields() -> list[tuple[str, str]]:
    """菜单式录入 Flow 全局变量字段（每项一个字段名 + 类型），回车结束，至多 12 个。"""
    print("Flow 全局变量（界面控件上显示的数据）")
    print("字段名英文（如 tick / speed），回车 = 结束添加")
    TYPE_MENU = {"1": "int", "2": "float", "3": "bool", "4": "string"}
    vars_: list[tuple[str, str]] = []
    while True:
        if len(vars_) >= 12:
            print("  （已达 12 个上限）")
            break
        field = input(f"  字段名（第 {len(vars_) + 1} 个，回车结束）：").strip()
        if not field:
            break
        if not re.match(r"^[a-z][a-z0-9_]*$", field):
            print("  ✗ 字段名须 [a-z][a-z0-9_]*")
            continue
        t = input("  类型 [1=int 2=float 3=bool 4=string]：").strip()
        typ = TYPE_MENU.get(t)
        if typ is None:
            print("  ✗ 请输入 1 / 2 / 3 / 4")
            continue
        vars_.append((field, typ))
    return vars_


def preview_add_vars(app_name: str, vars_: list[tuple[str, str]], repo: Path) -> None:
    cp = repo / APP_DIR / app_name / f"{app_name}_app.cpp"
    if not cp.exists():
        die(f"app/{app_name}/ 不存在——请先用向导或 CLI 新建此 App，再增量加变量")
    print(f"== 为已有 App {app_name} 追加 {len(vars_)} 个变量（加 --apply 才落地）==")
    for field, typ in vars_:
        full = f"{app_name}_{field}"
        print(f'  + app/{app_name}/{app_name}_app.cpp:  eez_ui_bridge_{VAR_FN[typ]}("{full}", {VAR_EXPR[typ]});')
        print(f"  + app/{app_name}/eez_vars.txt:  {full}    # {typ}")


def add_vars(app_name: str, vars_: list[tuple[str, str]], repo: Path) -> None:
    """把变量骨架增量追加进已有 App 的 cpp 与 eez_vars.txt（幂等：已存在的跳过）。"""
    d = repo / APP_DIR / app_name
    cp = d / f"{app_name}_app.cpp"
    vf = d / "eez_vars.txt"
    if not cp.exists():
        die(f"app/{app_name}/ 不存在——请先用向导或 CLI 新建此 App，再增量加变量")
    lines = cp.read_text(encoding="utf-8").split("\n")
    added: list[str] = []
    for field, typ in vars_:
        full = f"{app_name}_{field}"
        head = f'  eez_ui_bridge_{VAR_FN[typ]}("{full}"'
        if any(head in ln for ln in lines):
            print(f"  = {full} 已在 cpp（幂等跳过）")
            continue
        row = head + f", {VAR_EXPR[typ]});  // TODO: 换成真实业务值"
        idxs = [i for i, ln in enumerate(lines) if "eez_ui_bridge_set_var_" in ln]
        if idxs:
            lines.insert(idxs[-1] + 1, row)
        else:
            ti = next((i for i, ln in enumerate(lines) if "eez_ui_bridge_tick();" in ln), None)
            lines.insert(ti if ti is not None else len(lines) - 3, row)
        added.append(full)
    if added:
        cp.write_text("\n".join(lines), encoding="utf-8")
        print(f"  + {cp.relative_to(repo)} 已追加 {len(added)} 行变量骨架")
    else:
        print(f"  = {cp.relative_to(repo)} 无需改动")
    if not vf.exists():
        vf.write_text(build_vars_txt(app_name, vars_), encoding="utf-8")
        print(f"  + {vf.relative_to(repo)} 已生成（EEZ Studio 粘贴清单）")
        return
    rows = vf.read_text(encoding="utf-8").split("\n")
    added_txt = 0
    for field, typ in vars_:
        r = f"{app_name}_{field}    # {typ}"
        if r in rows:
            print(f"  = {r} 已在 eez_vars.txt（幂等跳过）")
            continue
        rows.insert(len(rows) - 1, r)
        added_txt += 1
    if added_txt:
        vf.write_text("\n".join(rows), encoding="utf-8")
        print(f"  + {vf.relative_to(repo)} 已追加 {added_txt} 行")
    else:
        print(f"  = {vf.relative_to(repo)} 无需改动")



def main() -> None:
    if len(sys.argv) == 1:
        interactive(REPO_ROOT)
        return

    ap = argparse.ArgumentParser(description="生成带 EEZ 界面的 App 薄壳（docs/guides/new-app-guide.md 路线）")
    ap.add_argument("app_name", help="App 名（小写标识符，须与 EEZ 屏名一致）")
    ap.add_argument("--title", default="未命名", help="中文标题（元数据用）")
    ap.add_argument("--var", action="append", default=[], metavar="字段:类型",
                    help="Flow 全局变量骨架（<app名>_<字段>，类型 int|float|bool|string），可多次")
    ap.add_argument("--add-var", action="append", default=[], metavar="字段:类型",
                    help="给已有 App 增量追加变量骨架（默认预览，--apply 落地），可多次")
    ap.add_argument("--apply", action="store_true", help="落地写盘（默认 dry-run 只打印蓝图与清单）")
    ap.add_argument("--repo", type=Path, default=REPO_ROOT, help="仓库根（默认脚本所在仓库；自测用）")
    ap.add_argument("--vars-check", type=Path, default=None, metavar="FILE",
                    help="核对构建期变量表（build/include/embark_eez_vars.h）里的变量")
    args = ap.parse_args()

    app_name = args.app_name
    if not APP_NAME_RE.match(app_name):
        die(f"App 名不合法：{app_name!r}（须 ^[a-z][a-z0-9_]*$）")
    if app_name.endswith("_sub"):
        die("App 名不能以 _sub 结尾（那是 EEZ 子页的保留后缀）")
    vars_ = [parse_var(v) for v in args.var]
    class_name = pascal(app_name) + "App"
    repo = args.repo.resolve()
    for rel in (CMAKELISTS, UI_DEMO):
        if not (repo / rel).exists():
            die(f"--repo {repo} 下缺 {rel}（请指向仓库根，或用默认值）")

    if args.add_var:
        av = [parse_var(v) for v in args.add_var]
        if args.apply:
            add_vars(app_name, av, repo)
        else:
            preview_add_vars(app_name, av, repo)
        return

    if args.vars_check:
        vp = args.vars_check if args.vars_check.is_absolute() else repo / args.vars_check
        vars_check(vp, app_name, vars_)
        return
    if args.apply:
        apply(app_name, class_name, args.title, vars_, repo)
    else:
        dry_run(app_name, class_name, args.title, vars_, repo)


if __name__ == "__main__":
    main()
