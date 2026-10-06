#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""scaffold_app.py 的自测：在临时 fake repo 上跑 dry-run / apply / 幂等 / vars-check。

运行：python tools/scaffold_app_test.py；退出码 0 = 全过。
"""
from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent / "scaffold_app.py"


def run(args: list[str], repo: Path, expect_code: int = 0) -> subprocess.CompletedProcess:
    p = subprocess.run(
        [sys.executable, str(SCRIPT), *args, "--repo", str(repo)],
        capture_output=True, text=True, encoding="utf-8", errors="replace",
    )
    if p.returncode != expect_code:
        print(f"FAIL: exit {p.returncode} != {expect_code}; args={args}")
        print("--- stdout ---"); print(p.stdout)
        print("--- stderr ---"); print(p.stderr)
        sys.exit(1)
    return p


def make_fake_repo() -> Path:
    tmp = Path(tempfile.mkdtemp(prefix="scaffold_test_"))
    (tmp / "app").mkdir()
    (tmp / "platform" / "host").mkdir(parents=True)
    (tmp / "app" / "CMakeLists.txt").write_text(
        "add_library(embark_demo_apps STATIC\n"
        "        launcher/launcher_app.cpp\n"
        "        clock/clock_app.cpp\n"
        "        settings/settings_app.cpp\n"
        "        common/eez_ui_bridge.cpp\n"
        "        common/eez_ui_nav.cpp)\n",
        encoding="utf-8")
    (tmp / "platform" / "host" / "ui_demo.cpp").write_text(
        "EMBARK_APP_TABLE(embark::demo::LauncherApp, embark::demo::ClockApp, embark::demo::SettingsApp)\n",
        encoding="utf-8")
    return tmp


def sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main() -> None:
    repo = make_fake_repo()
    args = ["demo_timer", "--title", "我的计时", "--var", "tick:int", "--var", "speed:float"]

    # 1) dry-run：输出蓝图与 checklist，且不写盘
    p = run(args, repo)
    for needle in ("demo_timer", "DemoTimerApp", "demo_timer_tick", "demo_timer_speed",
                   "EEZ Studio checklist", "SetPage", "落地命令", "eez_vars.txt",
                   "app/demo_timer/demo_timer_app.h"):
        if needle not in p.stdout:
            print(f"FAIL: dry-run 输出缺 {needle!r}"); sys.exit(1)
    if (repo / "app" / "demo_timer").exists():
        print("FAIL: dry-run 不应写盘"); sys.exit(1)

    # 2) apply：生成 h/cpp + eez_vars.txt（app/<name>/ 目录），改两处，插在 SettingsApp 之后
    run(args + ["--apply"], repo)
    d = repo / "app" / "demo_timer"
    hp = d / "demo_timer_app.h"
    cp = d / "demo_timer_app.cpp"
    vf = d / "eez_vars.txt"
    for f in (hp, cp, vf):
        if not f.exists():
            print(f"FAIL: {f} 未生成"); sys.exit(1)
    cm = (repo / "app" / "CMakeLists.txt").read_text(encoding="utf-8")
    if "demo_timer/demo_timer_app.cpp" not in cm:
        print("FAIL: CMakeLists 未插入（缺目录化路径）"); sys.exit(1)
    ui = (repo / "platform" / "host" / "ui_demo.cpp").read_text(encoding="utf-8")
    if "embark::demo::DemoTimerApp" not in ui:
        print("FAIL: 注册表未插入"); sys.exit(1)
    if ui.index("embark::demo::DemoTimerApp") < ui.index("embark::demo::SettingsApp"):
        print("FAIL: 插入位置应在 SettingsApp 之后"); sys.exit(1)
    cpp = cp.read_text(encoding="utf-8")
    for needle in ('eez_ui_bridge_set_var_int("demo_timer_tick"'
                   'eez_ui_bridge_set_var_float("demo_timer_speed"'
                   "eez_ui_bridge_tick();"):
        if needle not in cpp:
            print(f"FAIL: cpp 缺变量骨架 {needle!r}"); sys.exit(1)
    vt = vf.read_text(encoding="utf-8")
    for needle in ("demo_timer_tick    # int", "demo_timer_speed    # float", "Flow 全局变量"):
        if needle not in vt:
            print(f"FAIL: eez_vars.txt 缺 {needle!r}"); sys.exit(1)

    # 3) 幂等：再 apply，哈希不变
    files = (cp, hp, vf, repo / "app" / "CMakeLists.txt", repo / "platform" / "host" / "ui_demo.cpp")
    before = tuple(sha(f) for f in files)
    p2 = run(args + ["--apply"], repo)
    after = tuple(sha(f) for f in files)
    if before != after:
        print("FAIL: 幂等 apply 改了文件"); sys.exit(1)
    if "幂等" not in p2.stdout:
        print("FAIL: 幂等输出缺提示"); sys.exit(1)

    # 4) vars-check：全在 → 0；缺 → 1
    vh = repo / "build" / "include"
    vh.mkdir(parents=True)
    vars_h = vh / "embark_eez_vars.h"
    vars_h.write_text('namespace demo { struct VarEntry { const char* name; int index; };\n'
                      'inline constexpr VarEntry kVars[] = {\n'
                      '  {"demo_timer_tick", 0}, {"demo_timer_speed", 1}}; }\n', encoding="utf-8")
    run(args + ["--apply", "--vars-check", "build/include/embark_eez_vars.h"], repo)
    vars_h.write_text('{"demo_timer_tick", 0} }}\n', encoding="utf-8")
    run(args + ["--apply", "--vars-check", "build/include/embark_eez_vars.h"], repo, expect_code=1)

    # 5) 非法输入
    run(["bad_sub", "--apply"], repo, expect_code=2)
    run(["--var", "x:double"], repo, expect_code=2)

    print("scaffold_app_test: 全部通过（dry-run / apply 目录化 / 幂等 / vars-check / 非法输入）")


if __name__ == "__main__":
    main()
