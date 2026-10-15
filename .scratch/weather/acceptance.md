# 天气 App 验收清单（issue：weather）

跑法（宿主模拟器；工作目录 = 构建目录的 `platform/host`）：

```powershell
build\platform\host\embark_host_ui.exe --ui-weather --delay 20     # 期待〔aidogui 天气验收通过〕
build\platform\host\embark_host_ui.exe --ui --frames 40 --delay 20  # 回归：〔aidogui 验收通过〕
build\platform\host\embark_host_ui.exe --ui-nav --delay 20          # 回归：〔aidogui 导航验收通过〕
ctest --test-dir build
```

## 断言（`--ui-weather`，帧号见 platform/host/ui_demo.cpp 的故事线常量）

| # | 帧 | 断言 | 证据行（日志） |
| --- | --- | --- | --- |
| 1 | 12 | 点主屏天气卡 → 切到天气 App（设计器「调用函数」事件驱动） | `AiDoGUI 导航：请求切到 App weather` |
| 2 | 22 | 天气屏已创建、控件数 37；`L_City` / `L_NIcon` / `L_NTemp` / `L_NDesc` 与桩表逐字段一致；5 天 20 个控件逐行一致；**字形覆盖齐全** | `ui-weather 天气屏：已创建 是；控件 37 个；…；字形齐全` |
| 3 | 30→40 | 点「>」换城市：城市下标 +1（`city_switches()==1`），屏上「现在」区与 5 天全部换成新城市的数据，字形仍齐全 | `天气切到城市 …` + `ui-weather 天气屏：… 城市 …` |
| 4 | 48→58 | 点「返回」回主屏；主屏天气卡（`L_WCity` / `L_WTemp` / `L_WIcon` / `L_WDesc`）换成**同一个城市**（卡片与天气屏同源），字形齐全 | `ui-weather 主屏天气卡：…（期望 …）` |
| 5 | 收尾 | 〔aidogui 天气验收通过〕；`--ui` / `--ui-nav` 仍通过 | 三行〔…验收通过〕 |

## 已实测结果（2026-10-15，宿主 MinGW 构建）

```
--ui-weather 第 1 次：onCreate（城市 = 上海）→ 屏上 5 天一致/字形齐全 → 切到 广州 → 卡片 广州 30° 雷雨 闷热 →〔aidogui 天气验收通过〕
--ui-weather 第 2 次：onCreate（城市 = 广州）→ 切到 深圳 → 卡片 深圳 29° 多云 闷热 →〔aidogui 天气验收通过〕
--ui          ：〔aidogui 验收通过〕（屏表 3 屏：clock / launcher / weather；字体 7/7）
--ui-nav      ：〔aidogui 导航验收通过〕（前台 clock，switches=3，导航命中 3 次）
ctest         ：1/1 通过
缺字形警告    ：0 条（lv_draw_letter: glyph dsc. not found）
```

**两次运行起始城市不同（上海 → 广州）本身就是持久化的证据**：`wx.city` 落在
`embark_host_kv.bin`（宿主持久化后端，定长槽格式），第二次启动读回来就是上次选的城市。

## 已知边界

- 城市起始值依赖上一次运行的持久化文件；`--ui-weather` 的城市断言因此**不写死城市名**，
  而是「屏上的字 == 桩表里那座城市的字」。
- 真机（ESP32）未验：BIN 资源进 SD 卡是 ADR 0011 的步 3，尚未开始；`weather` 屏在真机上
  会走「缺屏安全」路径（C++ 壳照常可跑，界面等资源就位）。
