# 验收：launcher 回程与两个界面 bug（2026-10-16）

Status: 全绿（2026-10-16）。规格见 [spec.md](spec.md)。

## 环境：必须用短路径构建目录

仓库 worktree 的路径本来就长，Windows 上直接用它会撞 260 字符上限（报
`fatal error: opening dependency file third_party/.../*.obj.d: No such file or directory`）。
本次用 `subst S: <worktree>` 后以 `S:/build-short` 为构建目录（撤映射用 `subst S: /D`）。

```sh
cmake -S S:/ -B S:/build-short -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_COMPILER=<mingw>/gcc.exe -DCMAKE_CXX_COMPILER=<mingw>/g++.exe \
      -DCMAKE_MAKE_PROGRAM=<ninja>/ninja.exe
cmake --build S:/build-short
```

配置日志会打印 6 屏 / 7 字体：`album 7`、`clock 27`、`launcher 14`、`music 7`、`settings 8`、`weather 37`
（settings 是本次新增的第 6 张屏）。产物：`S:/build-short/platform/host/embark_host_ui.exe`。

## 四条宿主验收 + ctest（cwd 在 S: 盘，否则找不到字体/资源）

```sh
ctest --test-dir S:/build-short --output-on-failure
S:/build-short/platform/host/embark_host_ui.exe --ui                  --frames 40
S:/build-short/platform/host/embark_host_ui.exe --ui-nav             --delay 40
S:/build-short/platform/host/embark_host_ui.exe --ui-weather         --delay 40
S:/build-short/platform/host/embark_host_ui.exe --ui-launcher-pages  --delay 40
```

| 命令 | 退出码 | 实测口径行 |
| --- | --- | --- |
| `ctest` | 0 | `1/1 Test #1: embark_tests ... Passed`（0.15s，含分页纯函数单测） |
| `--ui` | 0 | `〔aidogui 验收通过〕` |
| `--ui-nav` | 0 | `〔aidogui 导航验收通过〕（前台 clock，switches=3，导航命中 3 次，月历基准 2026-10）` |
| `--ui-weather` | 0 | `〔aidogui 天气验收通过〕（… 切城 1 次，城市 广州）` |
| `--ui-launcher-pages` | 0 | `〔主屏分页验收通过〕（磁贴 5/页 2/点 2；设置屏 已进 亮度档 0→1；前台 launcher，switches=4）` |

## 逐条对应

- Q1 时钟：`--ui-nav` 里额外打印「点返回后 launcher/L_Time = "07:38"」——launcher 屏点返回后
  时间来自墙钟，不再是设计稿死值 `11:31`。（`--ui` 裸重载后仍回设计值 `11:31` 属预期：那条路径
  不走 onEnter/onResume。）
- Q2 回程：`--ui-nav` 走「点磁贴 → 进屏 → 点屏内 `Btn_Back` → 回 launcher」；`--ui-launcher-pages`
  第 2 页进 album 后同样点返回；两条里都不再有上滑帧脚本，提示行文案断言为「左右滑动翻页」。
- Q3 天气卡：`--ui-weather` 新增一行「主屏天气卡图标盒：宽 46，最长今日天气字 "雷雨" 量得 44px（装得下：是）」，
  并把「盒子宽 ≥ 最宽文本」并入该验收的通过条件。
- Q4 设置屏：`--ui-launcher-pages` 三段——进设置屏（前台 = settings、`settings.enters` 增加）→ 点
  `Btn_Bright`（亮度档 0→1 且 ClockApp 收到 `BrightnessMessage`）→ 点 `Btn_Back` 回主屏。

## 已知噪声（非本次引入，不影响判定）

- `--ui-nav` 会打印若干 `lv_draw_sw_letter: glyph dsc. not found for U+4E94`（`五`）——既有现象。
- 带合成点击/手势的验收必须给 `--delay 40`（LVGL 约每 30 ms 读一次输入，否则同一份脚本一趟过一趟不过）。
