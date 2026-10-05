# Spike 18：EEZ Flow 运行时 × Embark LVGL 8.3.11（宿主）

目标：证明 EEZ Flow 生成代码能编进仓库同款 LVGL 8.3.11 并在宿主模拟器跑起来，
拿到编译配对结论与 Flow 运行时的 LVGL 堆增量（对照 256 KB 预算）。

## 版本钉死
- LVGL：仓库 submodule 74d0a81（v8.3.11，与样例 pin 4d96c27 同版本）
- lv_drivers：样例 submodule 8cdabe8d（release/v8.3）
- eez-framework：样例 submodule c3e0ac0
- 生成 UI 代码：vendor/native-interface-lvgl-with-flow/src/ui（官方样例产物，Studio 0.29 生成）

## 构建与运行（Windows / MinGW-w64 / SDL2）
    .\build.ps1
    build\spike18.exe [帧数] [宽] [高]    # 默认 600 帧 800x480

## 与仓库配置的差异（仅限本 spike，不进生产）
- lv_conf.h = 仓库 config/lv_conf.h 副本 + 追加 FONT_MONTSERRAT_18/20/24/32/48（样例 UI 引用）
- hooks.c 只计数不设预算上限：先测真实增量，再与 256 KB 预算比较
- 编译定义 EEZ_FOR_LVGL + EEZ_PLATFORM_SIMULATOR（后者为拿到 osKernelGetTickCount 符号）
