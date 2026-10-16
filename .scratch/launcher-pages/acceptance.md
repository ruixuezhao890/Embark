# 验收：启动器分页磁贴

> **状态（2026-10-16）**：本剧本里「非主屏上滑返回」相关的段落已过期——该手势已删除，回程改为
> 屏内返回按钮，见 [ADR 0014](../../docs/adr/0014-in-screen-back.md) 与
> [`../launcher-fixes/acceptance.md`](../launcher-fixes/acceptance.md)。分页、磁贴、字形守卫部分仍然有效。

前置：宿主构建（SDL2 + LVGL），`build/`。

## 单元测试

```
ctest --test-dir build -R embark_tests --output-on-failure
```

覆盖 `app/launcher/launcher_pages.h` 的纯函数：磁贴数 = App 数 − 1（0/1/2/3/4/5/11/12 个 App）、
页数（4 个一页，向上取整）、`page_of`/`slot_of` 全展开、`tile_x`、页码点行宽与居中。

## 宿主验收（新开关）

```
build/platform/host/embark_host_ui.exe --ui-launcher-pages --frames 132 --delay 40
```

断言的链路（全部基于真控件几何 + 真手势，不写死坐标）：

1. **磁贴由注册表生成**：launcher 屏上运行时建出 5 个磁贴（6 个 App → 5 个），
   文本 = 对应 App 的 `title()`（时钟/设置/天气/音乐/相册）；磁贴**不设 `lv_obj` 名字**，
   验收通过 `app/launcher/launcher_tiles.h` 的观测 API（`launcher_tiles_count/tile/tile_label`）
   取，几何走 `lv_obj_get_x` 读真坐标。
2. **页数与页码点**：页容器两页（第 2 页容器 x = 240）；`Dots_Row` 里 2 个点，居中（x = 113/121）。
3. **左右滑动翻页**：在第 1 页空白处水平拖动（`lv_indev` 真手势轨迹）→ `Band_Apps` 的
   `scroll_x` 吸附到第 2 页（= 240），且第 2 页的磁贴（相册）可见。
4. **点磁贴进 App**：在第 2 页点「相册」磁贴 → 前台 App = `album`、当前屏 = `album`。
5. **上滑返回**：在 album 屏中部上滑（`LV_DIR_TOP` 真手势）→ 前台 App = `launcher`、
   当前屏 = `launcher`，album 屏已释放（切走即释放）。
6. **缺字守卫**：launcher 屏每个磁贴文本 + 提示行 `L_Hint` 逐字符 `lv_font_get_glyph_dsc`
   （漏字 = 屏幕上出现方框，验收必须变红）。
7. **主屏不响应上滑**：在 launcher 屏上滑后前台仍是 `launcher`（不产生任何回主屏请求）。

## 回归

```
build/platform/host/embark_host_ui.exe --ui               --frames 40
build/platform/host/embark_host_ui.exe --ui-nav           --frames 110 --delay 40
build/platform/host/embark_host_ui.exe --ui-weather       --frames 90  --delay 40
```

> **带合成点击/手势的验收必须给 `--delay 40`**：LVGL 约每 30 ms 读一次输入；帧比它快时
> 一帧里推的"按下 → 移动 → 抬起"会被采样进同一次读取，同一份脚本会一趟过一趟不过。

三条都必须保持原有结论（`--ui` 字体/屏表/切走即释放、`--ui-nav` 双向导航 + 翻月、
`--ui-weather` 天气屏与主屏卡片同源）。

## 证据

- 截图（本轮已跑，`.scratch/launcher-pages/shots/`）：`page1.png`（`--shot-frame 12`，第 1 页 = 时钟/设置/天气/音乐 + 第 1 个点亮）、`page2.png`（`--shot-frame 50`，吸附后第 2 页 = 相册 + 第 2 个点亮）、`album.png`（`--shot-frame 70`，点磁贴后的占位屏）；BMP 原图同目录（SDL 只能存 BMP）；
- 脚本自测：`python tools/scaffold_app_test.py`（注册表锚改成只认表头后仍全通过：dry-run / apply 目录 / 幂等 / vars-check / 非空目录）；
- 日志：`--ui-launcher-pages` 的每条断言都打一行 `ELOG_INFO`，末行是结论：
  `〔主屏分页验收通过〕（磁贴 5 / 页 2 / 点 2；上滑返回命中 1 次；前台 launcher，switches=2）`；
  失败时 exit code = 2。
- 已实测的三条结论行（2026-10-15，宿主 `C:/eb/ac94612f`，`--delay 40`）：
  `主屏横向拖动后：停在第 1 页（scroll_x=240，期望吸附到第 1 页 = 240px）`、
  `相册屏上滑后：当前屏 launcher（launcher 已重建 是，album 已释放 是）；上滑返回命中 1 次`、
  `主屏上滑后：当前屏 launcher（应仍停在 launcher）；上滑返回命中 1 次（应仍为 1：主屏不响应）`。
