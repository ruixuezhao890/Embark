#!/usr/bin/env node
/**
 * 生成 docs/reference/ui-widgets.md —— 每屏每个控件的「对照表」：
 * 叫什么、什么类型、在哪个位置多大、设计器里写死什么字、点一下调什么、**代码里谁在用它**。
 *
 * 三份真相 join：
 *   设计真相   app/aidogui_ui/screen.aid 的 screens[].widgets[]
 *   使用真相   app/ 与 platform/host/ 里 aidogui_ui_bridge_widget(<屏>, <控件>) 等调用点
 *   C 符号真相 app/aidogui_ui/export/screens/ui_*.h 的 extern（只用来查「导出目录过期了没」）
 *
 * 用法：
 *   node tools/aidogui/gen_widget_doc.mjs              # 生成 / 覆盖 docs/reference/ui-widgets.md
 *   node tools/aidogui/gen_widget_doc.mjs --check      # 只校验是否最新（过期 exit 1，不改文件）
 *   node tools/aidogui/gen_widget_doc.mjs --help
 * 选项：--aid <file>  --export <dir>  --out <file>  --src <dir>（可重复）
 *
 * 为什么需要它：设计器里控件叫 L_WTemp，代码里要写 aidogui_ui_bridge_widget("weather", "L_WTemp")。
 * 设计器里改个名，代码不会编译失败 —— 它只会静默拿到 nullptr，界面那一格从此不更新。
 * 这份文档就是那张对照表，顺带把「没人用的控件」和「用了但不存在的控件」一起抖出来。
 *
 * 输出**不含时间戳**：没改动时重跑是字节相同的，不会给 docs 制造 git 噪音。
 */

import { existsSync, readFileSync, readdirSync, statSync, writeFileSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = resolve(HERE, "..", "..");

const USAGE = [
  "用法：",
  "  node tools/aidogui/gen_widget_doc.mjs [选项]",
  "",
  "  （无选项）        写 docs/reference/ui-widgets.md",
  "  --check          只比对：文件过期 → exit 1 并打印差多少行，不写文件（CI / 交前自查用）",
  "",
  "选项：",
  "  --aid <文件>     设计器工程（默认 app/aidogui_ui/screen.aid）",
  "  --export <目录>  导出目录，只用来查「.aid 改了但没重新导出」（默认 app/aidogui_ui/export）",
  "  --out <文件>     输出（默认 docs/reference/ui-widgets.md）",
  "  --src <目录>     扫哪些目录找调用点（默认 app 与 platform/host，可重复传）",
  "  --help           这张表",
].join("\n");

function parseArgs(argv) {
  const opts = {
    aid: "app/aidogui_ui/screen.aid",
    exportDir: "app/aidogui_ui/export",
    out: "docs/reference/ui-widgets.md",
    src: ["app", "platform/host"],
    check: false,
  };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    const next = () => {
      i += 1;
      if (i >= argv.length) {
        fail("选项 " + arg + " 后面缺值");
      }
      return argv[i];
    };
    if (arg === "--help" || arg === "-h") {
      console.log(USAGE);
      process.exit(0);
    } else if (arg === "--check") {
      opts.check = true;
    } else if (arg === "--aid") {
      opts.aid = next();
    } else if (arg === "--export") {
      opts.exportDir = next();
    } else if (arg === "--out") {
      opts.out = next();
    } else if (arg === "--src") {
      if (opts.src.length === 2 && opts.src[0] === "app" && opts.src[1] === "platform/host") {
        opts.src = [];
      }
      opts.src.push(next());
    } else {
      fail("认不得的参数：" + arg + "（--help 看用法）");
    }
  }
  return opts;
}

function fail(message) {
  console.error("错误：" + message);
  process.exit(1);
}

function readText(relPath) {
  const full = resolve(REPO_ROOT, relPath);
  if (!existsSync(full)) {
    fail("找不到 " + relPath);
  }
  return readFileSync(full, "utf8");
}

// --- 源码扫描：一把小状态机，把注释抹成空格（保持行号），顺手记下所有字符串字面量 ----
//
// 为什么不直接正则去注释：C++ 里 "http://..." 这种字符串含 //，粗暴替换会把字符串切成两半，
// 后面所有行号和字面量全错。这里逐字符走，只有真在注释里才抹。
function scanSource(text) {
  const code = text.split("");
  const strings = [];
  const blank = (from, to) => {
    for (let i = from; i < to && i < code.length; i += 1) {
      if (code[i] !== "\n") code[i] = " ";
    }
  };
  let i = 0;
  let line = 1;
  while (i < text.length) {
    const ch = text[i];
    if (ch === "\n") {
      line += 1;
      i += 1;
      continue;
    }
    if (ch === "/" && text[i + 1] === "/") {
      const start = i;
      while (i < text.length && text[i] !== "\n") i += 1;
      blank(start, i);
      continue;
    }
    if (ch === "/" && text[i + 1] === "*") {
      const start = i;
      i += 2;
      while (i < text.length && !(text[i] === "*" && text[i + 1] === "/")) {
        if (text[i] === "\n") line += 1;
        i += 1;
      }
      i = Math.min(i + 2, text.length);
      blank(start, i);
      continue;
    }
    if (ch === '"' || ch === "'") {
      const quote = ch;
      const startLine = line;
      let value = "";
      i += 1;
      while (i < text.length && text[i] !== quote) {
        if (text[i] === "\\") {
          value += text[i + 1] === undefined ? "" : text[i + 1];
          i += 2;
          continue;
        }
        if (text[i] === "\n") line += 1;
        value += text[i];
        i += 1;
      }
      i += 1;
      if (quote === '"') strings.push({ value, line: startLine });
      continue;
    }
    i += 1;
  }
  return { code: code.join(""), strings };
}

// --- 设计真相：screen.aid -------------------------------------------------------
function loadAid(relPath) {
  const data = JSON.parse(readText(relPath));
  if (!Array.isArray(data.screens)) fail(relPath + " 里没有 screens[]");
  return data.screens.map((screen) => ({
    id: String(screen.id),
    name: String(screen.name === undefined ? screen.id : screen.name),
    widgets: (screen.widgets || []).map((widget) => ({
      name: String(widget.name),
      type: String(widget.type || "lv_obj"),
      x: Number(widget.x || 0),
      y: Number(widget.y || 0),
      width: Number(widget.width || 0),
      height: Number(widget.height || 0),
      text: typeof widget.text === "string" ? widget.text : "",
      fontSize: readFontSize(widget),
      clickable: Boolean(widget.flags && widget.flags.clickable),
      scrollable: Boolean(widget.flags && widget.flags.scrollable),
      events: (widget.events || []).map(readEvent),
    })),
  }));
}

function readFontSize(widget) {
  const styles = (widget.style && widget.style.styles) || {};
  for (const part of Object.keys(styles)) {
    const block = styles[part] || {};
    if (block.textFontSize !== undefined) return Number(block.textFontSize);
  }
  return 0;
}

function readEvent(event) {
  const params = (event && event.actionParams) || {};
  return {
    type: String((event && event.eventType) || "?"),
    action: String(params.functionName || (event && event.actionType) || "?"),
    enabled: !(event && event.enabled === false),
  };
}

// --- C 符号真相：export/screens/ui_<屏>.h（只用来查导出过期）------------------
function loadExportSymbols(exportDir) {
  const dir = resolve(REPO_ROOT, exportDir, "screens");
  const symbols = new Map();
  if (!existsSync(dir)) return symbols;
  for (const file of readdirSync(dir)) {
    if (!file.endsWith(".h")) continue;
    const text = readFileSync(join(dir, file), "utf8");
    for (const match of text.matchAll(/extern\s+lv_obj_t\s*\*\s*(ui_[A-Za-z0-9_]+)/g)) {
      symbols.set(match[1], file);
    }
  }
  return symbols;
}

// --- 使用真相：扫源码 -----------------------------------------------------------
const WIDGET_APIS = ["aidogui_ui_bridge_widget", "aidogui_ui_bridge_widget_name"];
const BIND_APIS = ["ui_backend_bind_click", "ui_backend_bind_action", "aidogui_ui_nav_bind_click"];
const SCREEN_APIS = [
  "ui_backend_bind_repaint",
  "ui_backend_load_for_app",
  "aidogui_ui_bridge_screen_created",
  "aidogui_ui_bridge_has_screen",
  "aidogui_ui_bridge_load_screen",
];
const ALL_APIS = [...WIDGET_APIS, ...BIND_APIS, ...SCREEN_APIS];

const SKIP_DIRS = new Set(["export", "build", "node_modules", "third_party", ".git"]);

function collectSourceFiles(roots) {
  const out = [];
  const walk = (relDir) => {
    const full = resolve(REPO_ROOT, relDir);
    if (!existsSync(full)) return;
    let entries;
    try {
      entries = readdirSync(full, { withFileTypes: true });
    } catch {
      return;
    }
    for (const entry of entries) {
      const childRel = relDir.replace(/[\\/]+$/, "") + "/" + entry.name;
      if (entry.isDirectory()) {
        if (SKIP_DIRS.has(entry.name)) continue;
        walk(childRel);
      } else if (/\.(c|cc|cpp|h|hpp)$/.test(entry.name)) {
        out.push(childRel);
      }
    }
  };
  for (const root of roots) walk(root);
  out.sort();
  return out;
}

// 名字 → 字面量。两处来源：本文件的 constexpr const char* kX = "...";
// 以及全仓唯一的同名常量（跨文件用，例如 launcher_tiles.cpp 的 kLauncherScreen）。
const CONST_PTR_RE = /(?:constexpr|const|static)\s+(?:const\s+)?char\s*(?:const\s*)?\*\s*(?:const\s+)?([A-Za-z_]\w*)\s*=\s*"((?:[^"\\]|\\.)*)"/g;
const CONST_ARR_RE = /(?:constexpr|const|static)\s+(?:const\s+)?char\s+([A-Za-z_]\w*)\s*\[\s*\]\s*=\s*"((?:[^"\\]|\\.)*)"/g;

function collectConsts(code) {
  const map = new Map();
  for (const re of [CONST_PTR_RE, CONST_ARR_RE]) {
    re.lastIndex = 0;
    for (const match of code.matchAll(re)) {
      map.set(match[1], match[2]);
    }
  }
  return map;
}

function lineAt(code, index) {
  let line = 1;
  for (let i = 0; i < index && i < code.length; i += 1) {
    if (code[i] === "\n") line += 1;
  }
  return line;
}

// 取 name( ... ) 的实参列表。找不到配对括号返回 null。
function readArgs(code, openIndex) {
  let depth = 0;
  let i = openIndex;
  while (i < code.length) {
    const ch = code[i];
    if (ch === '"') {
      i += 1;
      while (i < code.length && code[i] !== '"') {
        if (code[i] === "\\") i += 1;
        i += 1;
      }
    } else if (ch === "(") {
      depth += 1;
    } else if (ch === ")") {
      depth -= 1;
      if (depth === 0) {
        return splitArgs(code.slice(openIndex + 1, i));
      }
    } else if (ch === ";") {
      if (depth === 0) return null;
    }
    i += 1;
  }
  return null;
}

function splitArgs(inner) {
  const args = [];
  let depth = 0;
  let current = "";
  for (let i = 0; i < inner.length; i += 1) {
    const ch = inner[i];
    if (ch === '"') {
      current += ch;
      i += 1;
      while (i < inner.length && inner[i] !== '"') {
        if (inner[i] === "\\") {
          current += inner[i];
          i += 1;
        }
        current += inner[i];
        i += 1;
      }
      current += inner[i] === undefined ? "" : inner[i];
      continue;
    }
    if (ch === "(" || ch === "[" || ch === "{") depth += 1;
    if (ch === ")" || ch === "]" || ch === "}") depth -= 1;
    if (ch === "," && depth === 0) {
      args.push(current.trim());
      current = "";
      continue;
    }
    current += ch;
  }
  if (current.trim() !== "") args.push(current.trim());
  return args;
}

function callSites(code, consts, fileConstants) {
  const sites = [];
  for (const api of ALL_APIS) {
    let from = 0;
    for (;;) {
      const at = code.indexOf(api + "(", from);
      if (at < 0) break;
      from = at + api.length;
      const before = at === 0 ? "" : code[at - 1];
      if (/[A-Za-z0-9_]/.test(before)) continue;  // 别的标识符的后缀
      const args = readArgs(code, at + api.length);
      if (args === null) continue;
      if (args.some((arg) => arg.includes("*") || arg.includes("&"))) continue;  // 声明 / 函数指针
      const resolved = args.map((arg) => resolveToken(arg, consts, fileConstants));
      sites.push({ api, line: lineAt(code, at), args, resolved });
    }
  }
  return sites;
}

function resolveToken(token, consts, fileConstants) {
  const text = token.trim();
  if (text.startsWith('"')) {
    const inner = text.slice(1, text.lastIndexOf('"'));
    return inner.replace(/\\(.)/g, "$1");
  }
  if (/^[A-Za-z_]\w*$/.test(text)) {
    if (fileConstants.has(text)) return fileConstants.get(text);
    if (consts.has(text)) return consts.get(text);
  }
  return null;
}

// 字面量里的格式串（"L_D%u_Dow"）→ 正则，用来认「拼出来的控件名」。
function templateMatcher(value) {
  if (value.indexOf("%") < 0) return null;
  const escaped = value.replace(/[.*+?^$^{}()|[\]\\]/g, "\\$&");
  const pattern = escaped.replace(/%[-+ #0-9.]*[diouxXeEfgGaAcspn]/g, "[0-9]+");
  if (pattern === escaped) return null;
  try {
    return new RegExp("^" + pattern + "$");
  } catch {
    return null;
  }
}

// --- 汇总 -----------------------------------------------------------------------
const KEY_SEP = String.fromCharCode(1);
const NL = String.fromCharCode(10);

function buildModel(opts) {
  const screens = loadAid(opts.aid);
  const exportSymbols = loadExportSymbols(opts.exportDir);
  const files = collectSourceFiles(opts.src);

  const parsed = [];
  const globalConsts = new Map();
  for (const rel of files) {
    const scanned = scanSource(readFileSync(resolve(REPO_ROOT, rel), "utf8"));
    const fileConstants = collectConsts(scanned.code);
    parsed.push({ rel, code: scanned.code, strings: scanned.strings, fileConstants });
    for (const [name, value] of fileConstants) {
      const seen = globalConsts.get(name) || new Set();
      seen.add(value);
      globalConsts.set(name, seen);
    }
  }
  const consts = new Map();
  for (const [name, values] of globalConsts) {
    if (values.size === 1) consts.set(name, [...values][0]);
  }

  const screenIds = new Set(screens.map((screen) => screen.id));
  const widgetKey = (screen, widget) => screen + KEY_SEP + widget;
  const refs = new Map();
  const screenFiles = new Map(screens.map((screen) => [screen.id, new Set()]));
  const dynamic = [];
  const addRef = (screen, widget, ref) => {
    const key = widgetKey(screen, widget);
    const list = refs.get(key) || [];
    if (!list.some((item) => item.file === ref.file && item.line === ref.line)) list.push(ref);
    refs.set(key, list);
  };

  for (const file of parsed) {
    const sites = callSites(file.code, consts, file.fileConstants);
    for (const site of sites) {
      const screen = site.resolved[0];
      if (screen === null || !screenIds.has(screen)) continue;
      screenFiles.get(screen).add(file.rel);
      if (!WIDGET_APIS.includes(site.api) && !BIND_APIS.includes(site.api)) continue;
      const widget = site.resolved[1];
      if (widget === null) {
        dynamic.push({ file: file.rel, line: site.line, api: site.api, args: site.args });
        continue;
      }
      addRef(screen, widget, { file: file.rel, line: site.line, tag: "" });
    }
  }

  // 表驱动 / 拼名字：调用点长成 entry.widget、item.widget 这种认不出的形状，
  // 但真名一定以字面量或格式串的形式写在同一文件里（kClockWidgets 那种表）。
  // 规则：只要「引用了这个屏的文件」里出现过这个控件的名字，就算在用 —— 宁可少报死控件，不误杀。
  for (const screen of screens) {
    const owners = [...screenFiles.get(screen.id)].sort();
    for (const widget of screen.widgets) {
      const key = widgetKey(screen.id, widget.name);
      const known = refs.get(key) || [];
      // 已经有 App 代码的引用就不用兜底了；只有「仅验收脚本引用」才值得补一刀。
      if (known.some((ref) => !ref.file.startsWith("platform/"))) continue;
      for (const rel of owners) {
        if (!rel.startsWith("app/")) continue;
        const file = parsed.find((item) => item.rel === rel);
        const hit = findNameMention(file, widget.name);
        if (hit !== null) {
          addRef(screen.id, widget.name, { file: rel, line: hit.line, tag: hit.tag });
          break;
        }
      }
    }
  }

  // 引用排序：App 代码在前，验收脚本在后；同组按文件、行号。
  for (const list of refs.values()) {
    list.sort((a, b) => {
      const aTest = a.file.startsWith("platform/") ? 1 : 0;
      const bTest = b.file.startsWith("platform/") ? 1 : 0;
      if (aTest !== bTest) return aTest - bTest;
      if (a.file !== b.file) return a.file < b.file ? -1 : 1;
      return a.line - b.line;
    });
  }
  return { screens, exportSymbols, refs, dynamic, widgetKey };
}

function findNameMention(file, widgetName) {
  for (const item of file.strings) {
    if (item.value === widgetName) return { line: item.line, tag: "表/常量" };
  }
  for (const item of file.strings) {
    const matcher = templateMatcher(item.value);
    if (matcher !== null && matcher.test(widgetName)) return { line: item.line, tag: "拼名" };
  }
  return null;
}

// --- 渲染 -----------------------------------------------------------------------
// 输出里要写 markdown 的反引号与竖线转义，这里用码点拼，免得把转义符号写进源码。
const BT = String.fromCharCode(96);
const BS = String.fromCharCode(92);
const CR = String.fromCharCode(13);

// 设计器里挂了「调用函数」/其它事件的控件：点它屏自己会响应，不存在「没人用」。
function hasDesignerAction(widget) {
  return widget.events.some((event) => event.enabled);
}

function cell(value) {
  const text = String(value === undefined || value === null ? "" : value);
  return text.split("|").join(BS + "|");
}

function code(value) {
  return BT + value + BT;
}

function shortText(text, limit) {
  const flat = String(text).split(NL).join(" ").split(CR).join(" ");
  if (flat.length <= limit) return flat;
  return flat.slice(0, limit) + "…";
}

function refList(refs, limit) {
  if (refs.length === 0) return "—";
  const shown = refs.slice(0, limit).map((ref) => {
    let text = ref.file + ":" + String(ref.line);
    if (ref.file.startsWith("platform/")) text += "（验收）";
    else if (ref.tag !== "") text += "（" + ref.tag + "）";
    return text;
  });
  if (refs.length > limit) shown.push("+" + String(refs.length - limit));
  return shown.join("<br>");
}

function render(model, opts) {
  const lines = [];
  const totalWidgets = model.screens.reduce((sum, screen) => sum + screen.widgets.length, 0);
  lines.push("# 界面控件表（设计器 ↔ 代码 对照）");
  lines.push("");
  lines.push("> 自动生成，**别手改**：" + code("node tools/aidogui/gen_widget_doc.mjs") + "（改完屏跑一次）。");
  lines.push("> 来源：" + code(opts.aid) + " 的 " + String(model.screens.length) + " 个屏 / " +
    String(totalWidgets) + " 个控件，join " + opts.src.map((dir) => code(dir)).join("、") + " 里的调用点。");
  lines.push("> 设计器里改控件名，代码不会编译失败 —— 它只会静默拿到 nullptr。这张表就是那张对照表。");
  lines.push("");
  lines.push("## 怎么用");
  lines.push("");
  lines.push("1. **拿控件**：" + code('lv_obj_t* obj = aidogui_ui_bridge_widget("屏名", "控件名");') +
    " —— 屏没装载时返回 " + code("nullptr") + "，所以永远先判空。");
  lines.push("2. **写值**：拿到 " + code("lv_obj_t*") + " 之后就是普通 LVGL：" + code("lv_label_set_text") +
    " / " + code("lv_bar_set_value") + " / " + code("lv_calendar_set_today_date") +
    " …（LVGL 自己会比对，值没变不重绘，见 " + code("docs/concepts/index.md") + "）。");
  lines.push("3. **点一下干什么**：看每行的「事件」列 —— 设计器里的「调用函数」事件已经写死了动作。" +
    "要临时在 C 里挂，用 " + code("lv_obj_add_event_cb") +
    "，但控件随屏销毁，每次装载都得重挂（时机是 " + code("ui_backend_bind_repaint(屏名, 回调)") +
    "，见 ADR 0015）。");
  lines.push("");
  lines.push("## 概览");
  lines.push("");
  lines.push("| 屏（= App 名） | 控件 | 可点 | 死控件 |");
  lines.push("| --- | --- | --- | --- |");
  for (const screen of model.screens) {
    const clickable = screen.widgets.filter((widget) => widget.clickable).length;
    const dead = screen.widgets.filter(
      (widget) => (model.refs.get(model.widgetKey(screen.id, widget.name)) || []).length === 0,
    ).length;
    lines.push("| " + code(cell(screen.id)) + " | " + String(screen.widgets.length) + " | " +
      String(clickable) + " | " + String(dead) + " |");
  }
  lines.push("");

  for (const screen of model.screens) {
    lines.push("## " + screen.id);
    lines.push("");
    lines.push("屏名（写进 " + code("aidogui_ui_bridge_widget") + " 的第一个参数）：" + code(screen.id) + "。" +
      (screen.name !== screen.id ? " 设计器里的显示名：" + screen.name + "。" : ""));
    lines.push("");
    lines.push("| 控件名 | 类型 | 几何 | 文本 | 事件 | 代码里谁在用 |");
    lines.push("| --- | --- | --- | --- | --- | --- |");
    for (const widget of screen.widgets) {
      const refs = model.refs.get(model.widgetKey(screen.id, widget.name)) || [];
      const typeCell = widget.fontSize > 0
        ? widget.type + " " + String(widget.fontSize) + "px"
        : widget.type;
      const geometry = String(widget.x) + "," + String(widget.y) + " " +
        String(widget.width) + "×" + String(widget.height);
      let events = widget.events
        .filter((event) => event.enabled)
        .map((event) => event.type + "→" + code(event.action))
        .join("<br>");
      if (events === "") events = widget.clickable ? "可点（没绑动作）" : "—";
      const whoCell = refs.length === 0 && hasDesignerAction(widget) ? "设计器事件" : refList(refs, 2);
      const textCell = widget.text === "" ? "—" : code(shortText(cell(widget.text), 24));
      lines.push("| " + code(cell(widget.name)) + " | " + cell(typeCell) + " | " + cell(geometry) +
        " | " + textCell + " | " + events + " | " + whoCell + " |");
    }
    lines.push("");
  }

  lines.push("## 死控件（设计器里有，代码里没人取）");
  lines.push("");
  const deadLines = [];
  for (const screen of model.screens) {
    for (const widget of screen.widgets) {
      const refs = model.refs.get(model.widgetKey(screen.id, widget.name)) || [];
      if (refs.length !== 0 || hasDesignerAction(widget)) continue;
      deadLines.push("| " + code(cell(screen.id)) + " | " + code(cell(widget.name)) + " | " +
        cell(widget.type) + " | " +
        (widget.text === "" ? "—" : code(shortText(cell(widget.text), 16))) + " |");
    }
  }
  if (deadLines.length === 0) {
    lines.push("没有：每个控件都有人取。");
  } else {
    lines.push("共 " + String(deadLines.length) + " 个。容器（" + code("lv_obj") +
      "）没人写是常态 —— 它是给子控件当爹的，不需要代码。真正要看的是 " + code("lv_label") +
      " / " + code("lv_bar") + " 这类：没人写就意味着界面那一格永远停在设计器初值上。");
    lines.push("");
    lines.push("| 屏 | 控件 | 类型 | 设计器初值 |");
    lines.push("| --- | --- | --- | --- |");
    lines.push(...deadLines);
  }
  lines.push("");

  lines.push("## 只被验收脚本用（App 自己没写）");
  lines.push("");
  const onlyTests = [];
  for (const screen of model.screens) {
    for (const widget of screen.widgets) {
      const refs = model.refs.get(model.widgetKey(screen.id, widget.name)) || [];
      if (refs.length === 0) continue;
      if (widget.events.length > 0) continue;  // 设计器里挂了动作，屏自己就会响应
      if (refs.every((ref) => ref.file.startsWith("platform/"))) {
        onlyTests.push("| " + code(cell(screen.id)) + " | " + code(cell(widget.name)) + " | " +
          refList(refs, 2) + " |");
      }
    }
  }
  if (onlyTests.length === 0) {
    lines.push("没有。");
  } else {
    lines.push("这些控件只有 " + code("platform/host/") + " 下的验收故事在读它们。" +
      "如果它该随业务变，那 App 里少了一段代码。");
    lines.push("");
    lines.push("| 屏 | 控件 | 谁在用 |");
    lines.push("| --- | --- | --- |");
    lines.push(...onlyTests);
  }
  lines.push("");

  lines.push("## 孤儿引用（代码里取了，设计器里没有）");
  lines.push("");
  const orphans = [];
  for (const [key, list] of model.refs) {
    const parts = key.split(KEY_SEP);
    const screen = model.screens.find((item) => item.id === parts[0]);
    const exists = screen !== undefined && screen.widgets.some((widget) => widget.name === parts[1]);
    if (exists) continue;
    orphans.push({ screen: parts[0], widget: parts[1], list });
  }
  orphans.sort((a, b) => (a.screen + a.widget < b.screen + b.widget ? -1 : 1));
  if (orphans.length === 0) {
    lines.push("没有。");
  } else {
    lines.push("**这些是 bug**：控件名写错了，或设计器里删了。" + "运行期不报错，只是拿到 " +
      code("nullptr") + "，那一格永远不更新。");
    lines.push("");
    lines.push("| 屏 | 控件 | 谁在用 |");
    lines.push("| --- | --- | --- |");
    for (const orphan of orphans) {
      lines.push("| " + code(cell(orphan.screen)) + " | " + code(cell(orphan.widget)) + " | " +
        refList(orphan.list, 3) + " |");
    }
  }
  lines.push("");

  lines.push("## 认不出来的调用点（动态拼名 / 表里取的字段）");
  lines.push("");
  if (model.dynamic.length === 0) {
    lines.push("没有。");
  } else {
    lines.push("这些调用点的控件名是变量，静态看不出来。工具改用「同文件里出现过的字面量」兜底，" +
      "所以上面的控件可能被算成「在用」而其实没人写 —— 名字对不上时先看这里。");
    lines.push("");
    lines.push("| 文件 | 行 | 调用 |");
    lines.push("| --- | --- | --- |");
    for (const item of model.dynamic.slice(0, 40)) {
      lines.push("| " + code(item.file) + " | " + String(item.line) + " | " +
        code(item.api + "(" + shortText(item.args.join(", "), 40) + ")") + " |");
    }
    if (model.dynamic.length > 40) {
      lines.push("");
      lines.push("（还有 " + String(model.dynamic.length - 40) + " 处没列）");
    }
  }
  lines.push("");

  lines.push("## 导出目录过期（.aid 里有，生成的 C 里没这个符号）");
  lines.push("");
  const stale = [];
  for (const screen of model.screens) {
    const rootSymbol = "ui_" + screen.id;
    if (model.exportSymbols.size > 0 && !model.exportSymbols.has(rootSymbol)) {
      stale.push("整个屏 " + code(screen.id) + " 都没导出（缺 " + code(rootSymbol) +
        "）—— 跑 tools/aidogui/export.ps1");
      continue;
    }
    for (const widget of screen.widgets) {
      const symbol = "ui_" + screen.id + "_" + widget.name;
      if (!model.exportSymbols.has(symbol)) {
        stale.push("| " + code(cell(screen.id)) + " | " + code(cell(widget.name)) + " | " + symbol + " |");
      }
    }
  }
  if (model.exportSymbols.size === 0) {
    lines.push("没有导出目录可查（" + code(opts.exportDir) + " 不存在），这一节跳过。");
  } else if (stale.length === 0) {
    lines.push("没有：" + code(".aid") + " 里每个控件在导出的 C 里都有对应符号。");
  } else {
    lines.push("**导出目录比 " + code(".aid") + " 旧**：改了屏但没跑 " + code("tools/aidogui/export.ps1") + "。");
    lines.push("");
    for (const item of stale) lines.push(item);
  }
  lines.push("");
  return lines.join(NL);
}

function main() {
  const opts = parseArgs(process.argv.slice(2));
  const model = buildModel(opts);
  const text = render(model, opts);
  const outPath = resolve(REPO_ROOT, opts.out);
  if (opts.check) {
    if (!existsSync(outPath)) {
      console.error("过期：" + opts.out + " 还不存在。跑一次 node tools/aidogui/gen_widget_doc.mjs");
      process.exit(1);
    }
    const current = readFileSync(outPath, "utf8");
    if (current === text) {
      console.log("最新：" + opts.out + " 与源码一致");
      process.exit(0);
    }
    const have = current.split(NL);
    const want = text.split(NL);
    let firstDiff = 0;
    while (firstDiff < have.length && firstDiff < want.length && have[firstDiff] === want[firstDiff]) {
      firstDiff += 1;
    }
    console.error("过期：" + opts.out + " 与源码不一致（第一处差异在第 " + String(firstDiff + 1) +
      " 行；现有 " + String(have.length) + " 行，应为 " + String(want.length) + " 行）。");
    console.error("跑一次 node tools/aidogui/gen_widget_doc.mjs 再提交。");
    process.exit(1);
  }
  writeFileSync(outPath, text, "utf8");
  const totalWidgets = model.screens.reduce((sum, screen) => sum + screen.widgets.length, 0);
  console.log("已写 " + opts.out + "：" + String(model.screens.length) + " 屏 / " +
    String(totalWidgets) + " 控件");
}

main();
