# 贡献指南（CONTRIBUTING）

欢迎 —— 无论你是想加一个 App、修一个 bug，还是补一段文档。这个仓库对新手友好，
但有几条**自己的纪律**：读完这一页大约 5 分钟，能省掉一轮 review 往返。

---

## 1. 先跑起来

```sh
git clone --recursive https://github.com/ruixuezhao890/Embark.git
cd Embark
cmake -G Ninja -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

前置工具（Git / CMake ≥ 3.24 / Ninja / C++17 编译器 / SDL2）与 Windows 上的装法见
[docs/guides/quickstart.md](docs/guides/quickstart.md)。只改内核与测试的话不需要 SDL2 ——
CMake 找不到 SDL2 时会**跳过**带窗口的目标，内核与单测照常构建。

**提交之前，本地至少跑这三条**：

```sh
cmake --build build
ctest --test-dir build --output-on-failure
./build/platform/host/embark_host_tour        # 17 项系统自检（带窗口）
```

> CI 的三个 job 是：宿主构建 + ctest、ESP32-S3 固件构建（只编不烧）、clang-format 检查。
> 见 [.github/workflows/ci.yml](.github/workflows/ci.yml)。

---

## 2. 这个仓库的四条硬纪律

违反这四条的东西基本不会被合 —— 它们不是风格偏好，是**结构性保证**。

| 纪律 | 含义 | 为什么 |
| --- | --- | --- |
| **零堆分配** | 内核与 App 不用 `new` / `malloc`，容器只用 ETL 定容版 | 宿主 FreeRTOS 干脆不编译 `heap_4.c`，`pvPortMalloc` 在符号层面不存在；「内存够不够」是链接期问题 |
| **无异常、无 RTTI** | 内核目标带 `-fno-exceptions -fno-rtti` | 嵌入式工具链常关掉它们；错误用 `Error` 枚举 + `etl::expected` 表达 |
| **App 不碰平台头** | `app/` 与 `examples/` 下**不 include 任何 `platform/` 头** | 换后端不动 App 代码（spec §14.5 的验收项） |
| **App 不碰生成代码** | App 只调薄桥（`app/common/eez_ui_bridge.h`），不 include `ui.h` / `eez-flow.h` | 重新导出 UI 不该改业务代码 |

配套的还有两条操作层面的：

- **不要用 PowerShell 的 `Set-Content` / `Out-File` / `>` 改源码** ——
  PowerShell 5.1 会按 GBK 解码 UTF-8 再写出带 BOM 的文件，中文全变乱码。
  用编辑器、Python（`encoding="utf-8"`）或 Node（`fs.writeFileSync(p, s, 'utf8')`）。
- **行尾统一 LF**（`.gitattributes` 已定死 `* text=auto eol=lf`），不要手动改行尾。

---

## 3. 代码风格

- 格式以仓库根的 [.clang-format](.clang-format) 为准（Google 基础、100 列、2 空格缩进、指针靠左）。
  **v1 阶段本机不强制格式化**，CI 的 `clang-format --dry-run --Werror` 只作提示/卡漂移；
  提交前顺手跑一下最好：

  ```sh
  clang-format -i <改过的文件>
  ```

  不参与格式检查的文件：`third_party/**`、`.scratch/embark-v1/spikes/**`、
  `config/lv_conf.h`、`platform/host/freertos/FreeRTOSConfig.h`、`app/eez_ui/**`（Studio 生成，重导出即覆盖）。

- 命名：类型 `UpperCamelCase`、函数与变量 `snake_case`、成员变量带尾部下划线（`foreground_`）。
- 注释写**为什么**，不写**是什么** —— 代码已经说了是什么。复杂取舍在注释里带 issue/ADR 编号
  （例如 `// 见 issue 23 / ADR 0009`），方便以后考古。
- 所有代码注释、文档、issue 都是**中文**（术语用 [GLOSSARY.md](GLOSSARY.md) 里的词，别造同义词）。

---

## 4. 提交信息

沿用 [Conventional Commits](https://www.conventionalcommits.org/) 前缀，**正文写清"为什么"**：

```text
<type>(<scope>): <一句话说清改了什么>

- 要点一：改了什么 + 为什么这么改
- 要点二：验收怎么过的（命令 / 退出码 / 用例名）
```

常用 `type`：`feat` / `fix` / `refactor` / `docs` / `chore` / `test`。
`scope` 用模块名（`framework` / `host` / `esp32` / `app` / `ui` / `tools` / `docs`）。

> **Windows 用户注意**：`git commit -m "多行内容"` 在 PowerShell 里可能把换行写成字面
> `\n`。多行提交信息请写进文件再 `git commit -F <文件>`。

---

## 5. 加一个 App

有两条路，看你要什么：

| 你想要 | 怎么做 |
| --- | --- |
| 一个**不依赖 EEZ** 的最小 App（纯 LVGL 手写界面） | 照 [examples/minimal/](examples/minimal/) 抄 —— 一个 `*_app.h/cpp` + 一个 `main.cpp`，约 250 行含注释 |
| 一个**带 EEZ 界面**的 App（推荐路径） | 跑 `python tools/scaffold_app.py`，然后照 [docs/guides/new-app-guide.md](docs/guides/new-app-guide.md) 在 Studio 里画同名屏 |

加完 App 要动的地方、屏名/变量名约定、八个钩子各写什么，都在
[docs/guides/new-app-guide.md](docs/guides/new-app-guide.md) 与
[docs/reference/hooks-and-api.md](docs/reference/hooks-and-api.md)。

---

## 6. 改框架 / 内核

- **先看 ADR**：`docs/adr/` 里每条决策都写了当时的取舍与被否决的方案。
  如果你的改法推翻了某条 ADR，**在 PR 描述里显式说明并给出理由**，不要默默改掉。
- **契约测试是唯一真相**：`tests/kernel/` 里的用例定义了"正确用法"。
  改内核行为时，先改测试再改实现，或在同一个提交里一起改 —— 文档与测试冲突时以测试为准。
- **新增/修改公开 API** 要同步 [docs/reference/hooks-and-api.md](docs/reference/hooks-and-api.md)；
  **新增容量常量**要同步 [docs/reference/limits.md](docs/reference/limits.md)；
  **踩过的坑**记进 [docs/reference/pitfalls.md](docs/reference/pitfalls.md)。
- 文档有三层（concepts 为什么 / guides 怎么做 / reference 具体值），
  总索引 [docs/index.md](docs/index.md) —— 别把三种内容混进一个文件。

---

## 7. Issue 与工单

本仓库的 issue 追踪**就在仓库里**（`.scratch/embark-v1/`）：

- 规格书：`.scratch/embark-v1/spec.md`
- 工单：`.scratch/embark-v1/issues/NN-<slug>.md`，一个工单一个文件，编号从 `01` 起
- 工单顶部有 `Status:` 行记录状态；讨论追加在文件底部的 `## Comments` 下

约定细节见 [docs/agents/issue-tracker.md](docs/agents/issue-tracker.md)。
**提交工单文件与提交代码一样重要** —— 它们是设计决策的一手记录，也是这个仓库最值得读的部分之一。

---

## 8. 许可

贡献即表示你同意以 [MIT](LICENSE) 许可发布你的贡献。
