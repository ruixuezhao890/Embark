# Embark

可移植的嵌入式应用框架：**全局只有一个 UI 任务**，多个 App 作为"逻辑模块"共享它；
前台 App 拿到输入焦点、渲染权与事件循环权，后台 App 按各自策略挂起或跑轻量逻辑。
同一份源码同时构建 **宿主（PC 仿真）** 与 **ESP32-S3** 两个目标。

- 语言与约束：C++17；内核零堆分配、`-fno-exceptions -fno-rtti`、容器只用 ETL 定容版
- 硬件抽象：按"芯片功能被抽象出来给上层调用"的粒度，不暴露寄存器与引脚细节
- 日志与格式化：[efmt-elog](https://github.com/ruixuezhao890/efmt-elog)（`<middleware/...>` 形式引用）
- 容器与消息等基础设施：[ETL](https://github.com/ETLCPP/etl)

## 状态

骨架阶段（v0.1.0）：构建系统、依赖、配置注入已就位，框架功能按
`.scratch/embark-v1/issues/` 里的工单逐项落地。规格书见 `.scratch/embark-v1/spec.md`。

## 宿主构建

依赖以 git submodule 引入，先拉齐：

```sh
git submodule update --init --recursive
```

配置、构建、跑测试（Windows 下同样适用，Ninja 由 CMake 自己找）：

```sh
cmake -G Ninja -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

跑宿主骨架可执行文件（打印版本 + 校验依赖链路）：

```sh
./build/platform/host/embark_host              # Windows: .\build\platform\host\embark_host.exe
```

## 目录

| 路径 | 放什么 |
| --- | --- |
| `include/embark/` | 框架公开头文件（上层只依赖这里，见 spec §11） |
| `src/` | 内核实现 |
| `platform/host/` | 宿主后端（SDL 显示/输入、宿主 FreeRTOS port、LVGL 接入） |
| `platform/esp32/` | ESP32-S3 后端（以 ESP-IDF 组件形式接入） |
| `app/` | 自带示例 App |
| `tests/` | 宿主单元测试（doctest） |
| `config/` | 编译期宏与固定容量上限（单一事实来源） |
| `cmake/` | 构建辅助（`middleware/` 视图生成） |
| `third_party/` | 依赖（submodule） |
| `docs/` | 文档（完整文档见 issue 12；ADR 在 `docs/adr/`） |
| `.scratch/` | 规格书与 issue 追踪（随仓库提交） |

## 许可

MIT，见 [LICENSE](LICENSE)。
