# App 留存与刷新策略 —— 验收剧本

Status: passed（2026-10-16；源码状态 = 本轮改动全部落盘后重编）

## 构建（Windows 原生；仓库路径过长，必须用短路径）

    subst S: <worktree>
    cmake -S S:/ -B S:/build-short -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_COMPILER=<mingw>\gcc.exe -DCMAKE_CXX_COMPILER=<mingw>\g++.exe -DCMAKE_MAKE_PROGRAM=<ninja>
    cmake --build S:/build-short            # cwd = S:\，incremental 7 步、0 error

配置期应打印 **6 屏 / 7 字体**（album 7、clock 27、launcher 14、music 7、settings 8、weather 37 控件）。

## 五条命令与口径行（cwd = S:\；带合成点击 / 手势的必须 --delay 40）

| 命令 | 口径行 |
| --- | --- |
| `ctest --test-dir S:/build-short --output-on-failure` | `100% tests passed, 0 failed out of 1` |
| `embark_host_ui.exe --ui --frames 40` | 〔aidogui 验收通过〕 |
| `--ui-nav --delay 40` | 〔aidogui 导航验收通过〕（前台 clock，switches=3，导航命中 3 次，月历基准 2026-10） |
| `--ui-weather --delay 40` | 〔aidogui 天气验收通过〕（前台 launcher，switches=2，切城 1 次，导航命中 2 次，城市 广州） |
| `--ui-launcher-pages --delay 40` | 〔主屏分页验收通过〕（磁贴 5 / 页 2 / 点 2；设置屏 已进 亮度档 0→1；前台 launcher，switches=4；**无后台体 = 是，值重推 = 对**） |

本轮新增的断言（`platform/host/ui_demo.cpp`）：

- **视图留存**：翻到第 2 页 → 进 clock → 回主屏 →「仍在第 2 页（scroll_x=240）」（`:781`）。
- **反向拖动**：反向拖 release 在帧 104、检查在帧 118（留 14 帧等吸附）→「停在第 1 页（scroll_x=0）」（`:795`）。
- **没有后台体**：遍历注册表，每个 App 的 `AppSettings::policy == suspend`（`:1324-1334`）。
- **值重推**：`aidogui_ui_nav_repaint_bindings() == 3`（launcher / clock / weather）且 `_calls() >= 3`（`:1336-1342`）。
- **时钟缓存（上一轮）**：点返回后 launcher/L_Time 不再是设计稿死值 `11:31`（`:571`）。
- **天气字形（上一轮）**：主屏天气卡图标盒宽 46 ≥ 最长今日天气字「雷雨」44px（`:894`）。

## 已知噪声（非本轮引入）

- `--ui-nav` 打印若干 `lv_draw_sw_letter: glyph dsc. not found for U+4E94`（既有现象）。
- `embark_host_tour.exe` 是 **EEZ 老链路**的走查程序（`platform/host/CMakeLists.txt:95-103`，无 `EMBARK_AIDOGUI_UI` 宏）：
  在 HEAD 上就红 8 项，且**不稳定**——同一份 HEAD 二进制两次运行数值不同（switches 2 vs 0、settings 进入 1 vs 0）。
  结论：它的红项与本轮改动无关；`docs/guides/quickstart.md:26` 与 ADR 0010 里「全过 / 唯一红项」的说法已更正。
- `third_party/doctest`、`third_party/etl` 在 git status 里显示 dirty（行尾 / 长路径）；`third_party/freertos` 是未初始化子模块
  （内容按 gitlink `18ed8886fe3eed9dbe433a82cdc62ce5dc0315eb` 拷入，gitlink 未变）。
