#!/usr/bin/env node
// HTML → AiDoGUI .aid 屏编译链（草稿纸 → 像素真值 → .aid）
//
// 三个用法：
//   1) 正向（AI 写完草稿之后）：量像素、映射成控件、合并进 .aid 的某一屏
//      node tools/aidogui/html_to_aid.mjs --html draft/weather.html --screen weather [--apply]
//   2) 反向（把现有屏拉成草稿再改）
//      node tools/aidogui/html_to_aid.mjs --from-aid weather --out draft/weather.html
//   3) 自检（往返一致 + 重名必须报错）
//      node tools/aidogui/html_to_aid.mjs --self-check --screen weather
//
// 分工：AI 只写语义（这是标题、这是卡片、这是返回按钮），坐标由无头 Edge 量
// getBoundingClientRect()。本工具负责把量到的像素映射成 .aid 的 14 项 style 与 4 种控件类型。
// 草稿方言与映射规则见 docs/guides/html-to-screen.md。
//
// 纪律：
//   - 默认不落盘，加 --apply 才写 .aid；
//   - 只改 screens[<屏>].widgets，其余顶层键与其他 5 屏原样保留；
//   - 屏里原有控件在新草稿里消失了，默认报错（要删就显式 --allow-drop）。

import { readFileSync, writeFileSync, mkdtempSync, rmSync, existsSync, mkdirSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { tmpdir } from "node:os";
import { join, resolve, dirname } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = resolve(HERE, "..", "..");
const DEFAULT_AID = join(REPO_ROOT, "app", "aidogui_ui", "screen.aid");
const PROBE_PATH = join(HERE, "aid_probe.js");

// 标签 → 控件类型。语义优先：容器写 div/section，文字写 span/p/h1..h6，
// 进度条写 progress，日历只能靠 data-type="calendar"（HTML 没有对应标签）。
const TYPE_BY_TAG = {
  div: "lv_obj", section: "lv_obj", article: "lv_obj", header: "lv_obj", footer: "lv_obj",
  nav: "lv_obj", main: "lv_obj", aside: "lv_obj", ul: "lv_obj", li: "lv_obj", figure: "lv_obj",
  span: "lv_label", p: "lv_label", h1: "lv_label", h2: "lv_label", h3: "lv_label",
  h4: "lv_label", h5: "lv_label", h6: "lv_label", b: "lv_label", strong: "lv_label",
  em: "lv_label", small: "lv_label", i: "lv_label", label: "lv_label",
  progress: "lv_bar"
};

const TYPE_ALIASES = {
  obj: "lv_obj", lv_obj: "lv_obj",
  label: "lv_label", lv_label: "lv_label",
  bar: "lv_bar", lv_bar: "lv_bar",
  calendar: "lv_calendar", lv_calendar: "lv_calendar"
};

const ALIGN_BY_CSS = {
  start: "LEFT", left: "LEFT", center: "CENTER", end: "RIGHT", right: "RIGHT",
  justify: "LEFT", "justify-all": "LEFT", "match-parent": "LEFT"
};

const SCROLL_BY_DATA = {
  y: "VER", vertical: "VER", x: "HOR", horizontal: "HOR", both: "ALL", all: "ALL"
};

function log(line) { process.stdout.write(line + "\n"); }
function warn(line) { process.stdout.write("警告：" + line + "\n"); }
function fail(line) { process.stderr.write("错误：" + line + "\n"); process.exit(1); }

// ---------------------------------------------------------------- 参数

function usage() {
  log([
    "用法：",
    "  node tools/aidogui/html_to_aid.mjs --html <文件> --screen <屏名> [--apply] [--allow-drop]",
    "  node tools/aidogui/html_to_aid.mjs --from-aid <屏名> --out <文件.html>",
    "  node tools/aidogui/html_to_aid.mjs --self-check [--screen <屏名>]",
    "",
    "选项：",
    "  --aid <文件>        目标 .aid（默认 app/aidogui_ui/screen.aid）",
    "  --apply             真的写盘（不加就只打印差异）",
    "  --allow-drop        允许新草稿比原屏少控件",
    "  --browser <exe>     指定无头浏览器（默认自动找 msedge / chrome）",
    "  --help              本说明",
    "",
    "草稿方言见 docs/guides/html-to-screen.md。"
  ].join("\n"));
}

function parseArgs(argv) {
  const opts = { aid: DEFAULT_AID, apply: false, allowDrop: false, browser: null, screen: null, html: null, out: null, fromAid: null, selfCheck: false, help: false };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    const next = () => {
      i += 1;
      if (i >= argv.length) fail(arg + " 后面缺一个值");
      return argv[i];
    };
    if (arg === "--help" || arg === "-h") opts.help = true;
    else if (arg === "--aid") opts.aid = resolve(next());
    else if (arg === "--html") opts.html = resolve(next());
    else if (arg === "--screen") opts.screen = next();
    else if (arg === "--out") opts.out = resolve(next());
    else if (arg === "--from-aid") opts.fromAid = next();
    else if (arg === "--browser") opts.browser = resolve(next());
    else if (arg === "--apply") opts.apply = true;
    else if (arg === "--dry-run") opts.apply = false;
    else if (arg === "--allow-drop") opts.allowDrop = true;
    else if (arg === "--self-check") opts.selfCheck = true;
    else fail("认不出这个参数：" + arg + "（--help 看用法）");
  }
  return opts;
}

// ---------------------------------------------------------------- .aid 读写

function readAid(file) {
  if (!existsSync(file)) fail("找不到 .aid：" + file);
  const text = readFileSync(file, "utf8");
  let obj;
  try {
    obj = JSON.parse(text);
  } catch (err) {
    fail(".aid 不是合法 JSON：" + file + "（" + err.message + "）");
  }
  if (!obj || !Array.isArray(obj.screens)) fail(".aid 里没有 screens 数组：" + file);
  return { file, text, obj, dir: dirname(file) };
}

function screenNames(aid) { return aid.obj.screens.map((s) => s.name); }

function requireScreen(aid, name) {
  const screen = aid.obj.screens.find((s) => s.name === name);
  if (!screen) fail("屏 " + JSON.stringify(name) + " 不在 .aid 里；现有屏：" + screenNames(aid).join(" / "));
  return screen;
}

function serializeAid(obj) {
  // 设计器存盘格式就是 JSON.stringify(x, null, 2) + 换行；照这个写回去，diff 才只有内容变化。
  return JSON.stringify(obj, null, 2) + "\n";
}

// ---------------------------------------------------------------- 颜色

function num(value) {
  const n = parseFloat(value);
  return Number.isFinite(n) ? n : 0;
}

function parseCssColor(value) {
  if (!value) return null;
  const text = String(value).trim().toLowerCase();
  if (text === "transparent") return { r: 0, g: 0, b: 0, a: 0 };
  if (text.charAt(0) === "#") {
    const hex = text.slice(1);
    if (!/^[0-9a-f]+$/.test(hex)) return null;
    const pair = (i) => parseInt(hex.length <= 4 ? hex.charAt(i) + hex.charAt(i) : hex.slice(i * 2, i * 2 + 2), 16);
    if (hex.length === 3 || hex.length === 4) return { r: pair(0), g: pair(1), b: pair(2), a: hex.length === 4 ? pair(3) / 255 : 1 };
    if (hex.length === 6 || hex.length === 8) return { r: pair(0), g: pair(1), b: pair(2), a: hex.length === 8 ? pair(3) / 255 : 1 };
    return null;
  }
  const m = text.match(/^rgba?\(([^)]+)\)$/);
  if (!m) return null;
  const parts = m[1].split(/[\s,/]+/).filter((p) => p !== "");
  if (parts.length < 3) return null;
  const channel = (p) => {
    const n = p.endsWith("%") ? (parseFloat(p) / 100) * 255 : parseFloat(p);
    return Math.max(0, Math.min(255, Math.round(n)));
  };
  return {
    r: channel(parts[0]), g: channel(parts[1]), b: channel(parts[2]),
    a: parts.length > 3 ? Math.max(0, Math.min(1, parseFloat(parts[3]))) : 1
  };
}

function hex2(value) { return value.toString(16).padStart(2, "0"); }

function aidColor(color, alpha) {
  const a = alpha === undefined ? color.a : alpha;
  return "#" + hex2(color.r) + hex2(color.g) + hex2(color.b) + hex2(Math.round(a * 255));
}

// ---------------------------------------------------------------- 无头浏览器

function findBrowser(explicit) {
  if (explicit) {
    if (!existsSync(explicit)) fail("--browser 指的浏览器不存在：" + explicit);
    return explicit;
  }
  if (process.env.EMBARK_BROWSER && existsSync(process.env.EMBARK_BROWSER)) return process.env.EMBARK_BROWSER;
  const candidates = [
    "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
    "C:/Program Files/Microsoft/Edge/Application/msedge.exe",
    "C:/Program Files/Google/Chrome/Application/chrome.exe",
    "C:/Program Files (x86)/Google/Chrome/Application/chrome.exe"
  ];
  for (const c of candidates) if (existsSync(c)) return c;
  fail("找不到无头浏览器（Edge / Chrome）。要么装一个，要么用 --browser 指定可执行文件路径。");
  return null;
}

function escapeHtml(text) {
  return String(text)
    .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

function unescapeHtml(text) {
  return String(text)
    .replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&quot;/g, '"')
    .replace(/&#0*39;/g, "'").replace(/&amp;/g, "&");
}

function fontFamily(name) {
  // .aid 里记的是文件名（ui_heiti.ttf），CSS 里写的是 family（ui_heiti）。
  return String(name).replace(/\.[A-Za-z0-9]+$/, "");
}

function injectHarness(html, opts) {
  const faces = opts.fonts.map((f) => {
    if (!f.absolute) return null;
    return '@font-face { font-family: "' + fontFamily(f.name) + '"; src: url("' + f.url + '") format("truetype"); }';
  }).filter((line) => line !== null);

  const harness = [
    '<meta charset="utf-8">',
    '<style id="embark-harness">',
    "html, body { margin: 0 !important; padding: 0 !important; }",
    "html, body, *, *::before, *::after { box-sizing: border-box; }",
    "html, body { width: " + opts.width + "px; height: " + opts.height + "px; overflow: hidden; }"
  ].concat(faces).concat([
    "body { font-family: \"" + opts.defaultFamily + "\"; }",
    "</style>"
  ]).join("\n");

  let doc = html;
  const headOpen = doc.match(/<head[^>]*>/i);
  if (headOpen) {
    const at = headOpen.index + headOpen[0].length;
    doc = doc.slice(0, at) + harness + "\n" + doc.slice(at);
  } else {
    const htmlOpen = doc.match(/<html[^>]*>/i);
    if (htmlOpen) {
      const at = htmlOpen.index + htmlOpen[0].length;
      doc = doc.slice(0, at) + "\n<head>" + harness + "</head>\n" + doc.slice(at);
    } else {
      doc = "<!doctype html><html><head>" + harness + "</head><body>" + doc + "</body></html>";
    }
  }

  const probe = '<script src="' + opts.probeUrl + '"></script>';
  const bodyClose = doc.search(/<\/body>/i);
  if (bodyClose >= 0) doc = doc.slice(0, bodyClose) + probe + "\n" + doc.slice(bodyClose);
  else doc = doc + "\n" + probe + "\n";
  return doc;
}

function extractIsland(dom) {
  const m = dom.match(/<script[^>]*id="embark-probe"[^>]*>([\s\S]*?)<\/script>/);
  if (!m) return null;
  let parsed;
  try {
    parsed = JSON.parse(unescapeHtml(m[1]));
  } catch (err) {
    fail("量测脚本吐出来的不是合法 JSON（" + err.message + "）");
  }
  return parsed;
}

function measure(htmlText, opts) {
  const dir = mkdtempSync(join(tmpdir(), "embark-h2a-"));
  try {
    const file = join(dir, "screen.html");
    writeFileSync(file, injectHarness(htmlText, opts), "utf8");
    const args = [
      "--headless=new", "--disable-gpu", "--no-sandbox", "--dump-dom", "--hide-scrollbars",
      "--allow-file-access-from-files", "--force-device-scale-factor=1",
      "--window-size=" + opts.width + "," + opts.height,
      "--virtual-time-budget=" + opts.budget,
      pathToFileURL(file).href
    ];
    let dom;
    try {
      dom = execFileSync(opts.browser, args, { encoding: "utf8", maxBuffer: 64 * 1024 * 1024, timeout: 120000, stdio: ["ignore", "pipe", "ignore"] });
    } catch (err) {
      fail("无头浏览器跑失败：" + err.message + "（浏览器：" + opts.browser + "）");
    }
    let rows = extractIsland(dom);
    if (rows === null && opts.budget < 8000) {
      opts.budget = 8000;
      return measure(htmlText, opts);
    }
    if (rows === null) fail("量不到结果：浏览器没跑完注入脚本。把 --window-size 之外的东西简化后再试，或用 --browser 换个浏览器。");
    if (!Array.isArray(rows)) fail("量测脚本自己报错了：" + JSON.stringify(rows.error));
    return rows;
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

// ---------------------------------------------------------------- 像素 → 控件

function normalizeType(row) {
  const raw = row.type ? String(row.type).trim().toLowerCase() : TYPE_BY_TAG[row.tag];
  if (!raw) throw new Error("认不出 <" + row.tag + "> 是哪类控件：给它加 data-type（lv_obj / lv_label / lv_bar / lv_calendar），或换个标签");
  const type = TYPE_ALIASES[raw];
  if (!type) throw new Error("<" + row.tag + " data-type=\"" + raw + "\"> 不是 .aid 认识的类型（只有 lv_obj / lv_label / lv_bar / lv_calendar）");
  return type;
}

function actionFunction(raw) {
  const action = String(raw || "").trim();
  if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(action)) throw new Error("data-action 只能是 C 函数名的形状（字母开头 + 字母数字下划线），拿到的是 " + JSON.stringify(raw));
  return action.indexOf("embark_ui_") === 0 ? action : "embark_ui_" + action;
}

function fontFor(row, aid) {
  if (row.font) {
    const hit = (aid.obj.fonts || []).find((f) => fontFamily(f.name) === row.font || f.name === row.font);
    if (!hit) throw new Error("data-font=\"" + row.font + "\" 不在 .aid 的 fonts[] 里");
    return hit.name;
  }
  const declared = String(row.style.fontFamily || "").split(",")[0].trim().replace(/^["']|["']$/g, "").toLowerCase();
  const known = (aid.obj.fonts || []).filter((f) => fontFamily(f.name).toLowerCase() === declared || String(f.name).toLowerCase() === declared);
  if (known.length === 1) return known[0].name;
  const all = aid.obj.fonts || [];
  if (all.length === 1) return all[0].name;
  throw new Error("控件 " + row.name + " 该用哪套字体说不清（.aid 里有 " + all.length + " 套）：加 data-font=\"" + (all[0] ? fontFamily(all[0].name) : "字体名") + "\"");
}

function toWidgets(rows, ctx) {
  const screen = ctx.screen;
  const aid = ctx.aid;
  const widgets = [];
  const seen = new Set();
  const missingSize = [];

  for (const row of rows) {
    const name = String(row.name || "").trim();
    if (!/^[A-Za-z][A-Za-z0-9_]*$/.test(name)) throw new Error("控件名不合法（字母开头的字母数字下划线）：" + JSON.stringify(row.name) + "（用 data-name 命名）");
    if (seen.has(name)) throw new Error("控件名重复：" + name + "（同一屏里 data-name 必须唯一 —— 重名的第二个元素会被顶掉，没法猜你要哪个）");
    seen.add(name);

    const type = normalizeType(row);
    let parentId = screen;
    if (row.parentName !== null && row.parentName !== undefined && row.parentName !== "") {
      const parentName = String(row.parentName).trim();
      if (!seen.has(parentName)) throw new Error("控件 " + name + " 的父元素 " + parentName + " 不在它前面 —— 文档顺序里父元素必须先出现（内层元素写在祖先里面就行）");
      parentId = screen + "_" + parentName;
    }

    const x = Math.round(row.rect[0] - row.origin[0]) || 0;
    const y = Math.round(row.rect[1] - row.origin[1]) || 0;
    const width = Math.round(row.rect[2]);
    const height = Math.round(row.rect[3]);
    if (width <= 0 || height <= 0) throw new Error("控件 " + name + " 量到的尺寸是 " + width + "x" + height + "：元素没占空间（大概没写 width/height，或被 display:none 了）");
    if (width > 4096 || height > 4096) throw new Error("控件 " + name + " 量到的尺寸是 " + width + "x" + height + "：超出屏幕了（草稿画布要是 " + ctx.width + "x" + ctx.height + "）");

    const main = {};
    const bg = parseCssColor(row.style.backgroundColor);
    if (bg && bg.a > 0) {
      // .aid 的两个字段分工：bgColor 只负责 RGB，透明度一律走 bgOpa（这样改透明度不用动颜色）。
      main.bgColor = aidColor(bg, 1);
      main.bgOpa = Math.round(bg.a * 255);
    }
    const radius = Math.round(num(row.style.borderRadius));
    if (radius > 0) main.radius = radius;
    const borderWidth = Math.round(num(row.style.borderTopWidth));
    if (borderWidth > 0) main.borderWidth = borderWidth;
    const pads = row.style.padding.map((v) => Math.round(num(v)));
    if (pads.some((v) => v > 0)) {
      main.padLeft = pads[0];
      main.padTop = pads[1];
      main.padRight = pads[2];
      main.padBottom = pads[3];
    }

    // 谁能有文字属性：label 一律有；其它类型（lv_calendar 会自己画字）只在草稿标了 data-text、
    // 或元素真的装了文字时才写 —— 否则 lv_bar 会被硬塞一套根本没用的字体字段。
    const wantsText = type === "lv_label" || row.textMark === true || String(row.text || "").length > 0;
    if (wantsText) {
      const color = parseCssColor(row.style.color) || { r: 0, g: 0, b: 0, a: 1 };
      main.textColor = aidColor(color);
      main.textFont = fontFor(row, aid);
      const fontSize = Math.round(num(row.style.fontSize));
      if (fontSize <= 0) throw new Error("控件 " + name + " 量不到字号");
      main.textFontSize = fontSize;
      main.textAlign = ALIGN_BY_CSS[String(row.style.textAlign || "").toLowerCase()] || "LEFT";
      if (String(row.style.textAlign || "").toLowerCase() === "start" && row.tag !== "div") missingSize.push(name);
    }

    const styles = { DEFAULT__MAIN: main };
    if (row.pressedBg) {
      const pressed = parseCssColor(row.pressedBg) || parseCssColor("#" + String(row.pressedBg).replace(/^#/, "")) || null;
      if (!pressed) throw new Error("控件 " + name + " 的 data-pressed-bg 不是颜色：" + JSON.stringify(row.pressedBg));
      styles.PRESSED__MAIN = { bgColor: aidColor(pressed, 1) };
    }

    const events = [];
    if (row.action) {
      const fn = actionFunction(row.action);
      events.push({
        id: "event_" + fn + "_clicked",
        eventType: "CLICKED",
        actionType: "CALL_FUNCTION",
        actionParams: { functionName: fn },
        enabled: true
      });
    }

    const scroll = row.scroll ? SCROLL_BY_DATA[String(row.scroll).toLowerCase()] : null;
    if (row.scroll && !scroll) throw new Error("控件 " + name + " 的 data-scroll 只认 x / y / both，拿到的是 " + JSON.stringify(row.scroll));

    const widget = {
      id: screen + "_" + name,
      parentId: parentId,
      type: type,
      name: name,
      x: x,
      y: y,
      width: width,
      height: height
    };
    if (type === "lv_label") widget.text = row.text || "";
    widget.style = { styles: styles };
    widget.events = events;
    widget.flags = { clickable: events.length > 0 || row.clickable === true, scrollable: scroll !== null };
    widget.state = {};
    widget.scrolling = { scrollbar_mode: "AUTO", scroll_dir: scroll || "ALL", scroll_snap_x: "NONE", scroll_snap_y: "NONE" };
    widget.align = "TOP_LEFT";
    widgets.push(widget);
  }

  if (widgets.length === 0) throw new Error("一个 [data-name] 都没量到：草稿里的控件要用 data-name 命名");
  if (missingSize.length > 0) warn("这些文字控件没有显式 text-align（浏览器默认 start）：" + missingSize.slice(0, 6).join(", "));
  return widgets;
}

// ---------------------------------------------------------------- 控件 → HTML（反推草稿）

function cssColor(color, forceAlpha) {
  const a = forceAlpha === undefined ? color.a : forceAlpha;
  if (Math.abs(a - 1) < 0.002) return "#" + hex2(color.r) + hex2(color.g) + hex2(color.b);
  return "rgba(" + color.r + ", " + color.g + ", " + color.b + ", " + (Math.round(a * 1000) / 1000) + ")";
}

function parseAidColor(value) {
  const text = String(value || "").trim();
  const m = text.match(/^#([0-9a-fA-F]{6})([0-9a-fA-F]{2})?$/);
  if (!m) return null;
  const rgb = m[1];
  const alpha = m[2] === undefined ? 1 : parseInt(m[2], 16) / 255;
  return { r: parseInt(rgb.slice(0, 2), 16), g: parseInt(rgb.slice(2, 4), 16), b: parseInt(rgb.slice(4, 6), 16), a: alpha };
}

function hasTextStyle(main) {
  return main.textFont !== undefined || main.textColor !== undefined || main.textFontSize !== undefined;
}

function cssForWidget(widget) {
  const styles = (widget.style && widget.style.styles) || {};
  const main = styles.DEFAULT__MAIN || {};
  const pressed = styles.PRESSED__MAIN || {};
  const out = ["position: absolute"];
  out.push("left: " + widget.x + "px");
  out.push("top: " + widget.y + "px");
  out.push("width: " + widget.width + "px");
  out.push("height: " + widget.height + "px");

  const bg = parseAidColor(main.bgColor);
  const bgOpa = main.bgOpa === undefined ? 255 : main.bgOpa;
  if (bg && bgOpa > 0) out.push("background: " + cssColor(bg, (bgOpa / 255) * bg.a));
  if (main.radius) out.push("border-radius: " + main.radius + "px");
  if (main.borderWidth) out.push("border: " + main.borderWidth + "px solid transparent");
  if (main.padLeft || main.padTop || main.padRight || main.padBottom) {
    out.push("padding: " + (main.padTop || 0) + "px " + (main.padRight || 0) + "px " + (main.padBottom || 0) + "px " + (main.padLeft || 0) + "px");
  }
  if (hasTextStyle(main)) {
    const color = parseAidColor(main.textColor);
    if (color) out.push("color: " + cssColor(color));
    if (main.textFontSize) out.push("font-size: " + main.textFontSize + "px");
    if (main.textFont) out.push("font-family: " + fontFamily(main.textFont));
    const align = String(main.textAlign || "LEFT").toLowerCase();
    out.push("text-align: " + (align === "center" ? "center" : align === "right" ? "right" : "left"));
    out.push("line-height: 1.2");
  }
  return out.join("; ");
}

function attrsForWidget(widget) {
  const styles = (widget.style && widget.style.styles) || {};
  const pressed = styles.PRESSED__MAIN || {};
  const attrs = ['data-name="' + escapeHtml(widget.name) + '"'];
  if (widget.type === "lv_bar" || widget.type === "lv_calendar") attrs.push('data-type="' + widget.type + '"');
  if (widget.flags && widget.flags.clickable && widget.events && widget.events.length > 0) {
    const fn = (widget.events[0].actionParams || {}).functionName;
    if (fn) attrs.push('data-action="' + escapeHtml(fn) + '"');
  }
  // 点了有反应、但动作绑在 C 里（aidogui_ui_nav_bind_click）的控件没有 events[]：
  // 靠 data-clickable 把「它是可点的」这件事记在草稿里，否则往返一趟就退化成不响应。
  if (widget.flags && widget.flags.clickable && !(widget.events && widget.events.length > 0)) attrs.push('data-clickable="true"');
  if (hasTextStyle(styles.DEFAULT__MAIN || {})) attrs.push('data-text="true"');
  if (pressed.bgColor) attrs.push('data-pressed-bg="' + escapeHtml(pressed.bgColor) + '"');
  if (widget.flags && widget.flags.scrollable) {
    const dir = String((widget.scrolling || {}).scroll_dir || "ALL");
    attrs.push('data-scroll="' + (dir === "VER" ? "y" : dir === "HOR" ? "x" : "both") + '"');
  }
  return attrs.join(" ");
}

function renderScreenHtml(aid, screen) {
  const widgets = screen.widgets || [];
  const byParent = new Map();
  for (const widget of widgets) {
    const list = byParent.get(widget.parentId) || [];
    list.push(widget);
    byParent.set(widget.parentId, list);
  }
  const settings = aid.obj.settings || {};
  const width = settings.width || 240;
  const height = settings.height || 320;

  const lines = [];
  lines.push("<!doctype html>");
  lines.push('<html lang="zh-CN">');
  lines.push("<head>");
  lines.push('<meta charset="utf-8">');
  lines.push("<title>" + escapeHtml(screen.name) + " 屏草稿（由 html_to_aid.mjs --from-aid 生成）</title>");
  lines.push("<style>");
  lines.push("  /* 这份草稿是现有屏的忠实快照：每个元素都绝对定位到它在 .aid 里的坐标。");
  lines.push("     要改布局就改这里的 left/top/width/height（或重写成 flex），跑一遍工具再量回来。 */");
  lines.push("  #screen { position: relative; width: " + width + "px; height: " + height + "px; }");
  lines.push("</style>");
  lines.push("</head>");
  lines.push("<body>");
  lines.push('<div id="screen">');

  const renderOne = (widget, depth) => {
    const pad = "  ".repeat(depth);
    const tag = widget.type === "lv_label" ? "span" : widget.type === "lv_bar" ? "div" : "div";
    const attrs = attrsForWidget(widget);
    const css = cssForWidget(widget);
    const text = widget.type === "lv_label" ? escapeHtml(widget.text || "") : "";
    const kids = byParent.get(widget.id) || [];
    if (kids.length === 0) {
      lines.push(pad + "<" + tag + " " + attrs + ' style="' + css + '">' + text + "</" + tag + ">");
      return;
    }
    lines.push(pad + "<" + tag + " " + attrs + ' style="' + css + '">' + text);
    for (const kid of kids) renderOne(kid, depth + 1);
    lines.push(pad + "</" + tag + ">");
  };

  const roots = byParent.get(screen.name) || [];
  const orphanCount = widgets.length - roots.reduce((acc, w) => acc + countSubtree(w, byParent), 0);
  for (const root of roots) renderOne(root, 1);
  lines.push("</div>");
  lines.push("</body>");
  lines.push("</html>");
  if (orphanCount !== 0) warn("屏 " + screen.name + " 里有 " + orphanCount + " 个控件的 parentId 指不到任何控件（草稿里会漏掉）");
  return lines.join("\n") + "\n";
}

function countSubtree(widget, byParent) {
  let total = 1;
  for (const kid of byParent.get(widget.id) || []) total += countSubtree(kid, byParent);
  return total;
}

// ---------------------------------------------------------------- 比对

function canonicalStyle(widget) {
  const styles = (widget.style && widget.style.styles) || {};
  const main = styles.DEFAULT__MAIN || {};
  const pressed = styles.PRESSED__MAIN || {};
  const bgOpa = main.bgOpa === undefined ? 255 : Number(main.bgOpa);
  // 透明度 0 与「没写背景色」在屏幕上完全一样，比对时必须归一，否则 #00000000 与缺键会被判成不等。
  const bgColor = main.bgColor && bgOpa > 0 ? String(main.bgColor).slice(0, 7).toLowerCase() : null;
  return {
    textFont: main.textFont || null,
    textColor: main.textColor ? String(main.textColor).toLowerCase() : null,
    textFontSize: main.textFontSize === undefined ? null : Number(main.textFontSize),
    textAlign: String(main.textAlign || "LEFT").toUpperCase(),
    bgColor: bgColor,
    bgOpa: bgColor === null ? 0 : bgOpa,
    radius: Number(main.radius || 0),
    borderWidth: Number(main.borderWidth || 0),
    padLeft: Number(main.padLeft || 0),
    padTop: Number(main.padTop || 0),
    padRight: Number(main.padRight || 0),
    padBottom: Number(main.padBottom || 0),
    pressedBg: pressed.bgColor ? String(pressed.bgColor).toLowerCase() : null
  };
}

function canonicalWidget(widget) {
  return {
    name: widget.name,
    parentId: widget.parentId,
    type: widget.type,
    box: [Number(widget.x), Number(widget.y), Number(widget.width), Number(widget.height)],
    text: widget.text === undefined ? null : String(widget.text),
    style: canonicalStyle(widget),
    clickable: !!(widget.flags && widget.flags.clickable),
    scrollable: !!(widget.flags && widget.flags.scrollable),
    action: ((widget.events || [])[0] && ((widget.events[0].actionParams || {}).functionName)) || null
  };
}

function diffWidgets(before, after) {
  const beforeByName = new Map(before.map((w) => [w.name, w]));
  const afterByName = new Map(after.map((w) => [w.name, w]));
  const kept = [];
  const changed = [];
  const added = [];
  const removed = [];
  for (const [name, widget] of afterByName) {
    const old = beforeByName.get(name);
    if (!old) { added.push(name); continue; }
    const a = canonicalWidget(old);
    const b = canonicalWidget(widget);
    const fields = Object.keys(b).filter((key) => JSON.stringify(a[key]) !== JSON.stringify(b[key]));
    if (fields.length === 0) kept.push(name);
    else changed.push({ name: name, fields: fields, before: a, after: b });
  }
  for (const name of beforeByName.keys()) if (!afterByName.has(name)) removed.push(name);
  return { kept: kept, changed: changed, added: added, removed: removed };
}

function describeChange(entry) {
  const bits = [];
  for (const field of entry.fields) {
    bits.push(field + ": " + JSON.stringify(entry.before[field]) + " → " + JSON.stringify(entry.after[field]));
  }
  return "  " + entry.name + "  " + bits.join("；");
}

// ---------------------------------------------------------------- 三个命令

// 「零值」形状的样式值：新草稿里没写它，并不代表用户想删掉它（.aid 里显式 0 与缺键等价）。
function isZeroLikeStyle(key, value) {
  if (value === 0 || value === "0") return true;
  if (key === "textAlign") return String(value).toUpperCase() === "LEFT";
  if (key === "bgColor") return String(value).slice(7).toLowerCase() === "00";
  if (key === "textColor") return false;
  return false;
}

function mergeStyle(oldStyle, newStyle) {
  const oldStyles = (oldStyle || {}).styles || {};
  const newStyles = (newStyle || {}).styles || {};
  const styles = {};
  const parts = new Set(Object.keys(oldStyles).concat(Object.keys(newStyles)));
  for (const part of parts) {
    const oldPart = Object.assign({}, oldStyles[part] || {});
    const newPart = newStyles[part] || {};
    const merged = {};
    for (const key of new Set(Object.keys(oldPart).concat(Object.keys(newPart)))) {
      if (Object.prototype.hasOwnProperty.call(newPart, key)) merged[key] = newPart[key];
      else if (isZeroLikeStyle(key, oldPart[key])) merged[key] = oldPart[key];
    }
    styles[part] = merged;
  }
  return { styles: styles };
}

// 有变化的控件也只覆盖真变了的字段：键序、显式 0、没用到的字段一律保持原样。
function mergeWidget(original, rebuilt, fields) {
  const out = Object.assign({}, original);
  if (fields.indexOf("box") !== -1) { out.x = rebuilt.x; out.y = rebuilt.y; out.width = rebuilt.width; out.height = rebuilt.height; }
  if (fields.indexOf("parentId") !== -1) out.parentId = rebuilt.parentId;
  if (fields.indexOf("type") !== -1) out.type = rebuilt.type;
  if (fields.indexOf("text") !== -1) out.text = rebuilt.text;
  if (fields.indexOf("clickable") !== -1 || fields.indexOf("scrollable") !== -1) { out.flags = rebuilt.flags; out.scrolling = rebuilt.scrolling; }
  if (fields.indexOf("action") !== -1) out.events = rebuilt.events;
  if (fields.indexOf("style") !== -1) out.style = mergeStyle(original.style, rebuilt.style);
  return out;
}

function measureOptions(aid, opts) {
  const settings = aid.obj.settings || {};
  const width = settings.width || 240;
  const height = settings.height || 320;
  const fonts = (aid.obj.fonts || []).map((font) => {
    const abs = font.sourcePath ? resolve(aid.dir, font.sourcePath) : null;
    const present = abs !== null && existsSync(abs);
    return { name: font.name || font.family, absolute: present, url: present ? pathToFileURL(abs).href : null };
  });
  if (fonts.length > 0 && fonts.every((f) => !f.absolute)) {
    warn("字体源文件一个都找不到（.aid 的 fonts[].sourcePath 是相对 " + aid.dir + " 的）：量出来的文字宽度是回退字体的，给文字控件写死 width/height 就不受影响");
  }
  return {
    width: width, height: height, fonts: fonts,
    defaultFamily: fonts.length > 0 ? fontFamily(fonts[0].name) : "sans-serif",
    probeUrl: pathToFileURL(PROBE_PATH).href,
    browser: findBrowser(opts.browser),
    budget: 3000
  };
}

function runForward(opts) {
  if (!opts.screen) throw new Error("--html 必须配 --screen（要合并进哪一屏）");
  const aid = readAid(opts.aid);
  const screen = requireScreen(aid, opts.screen);
  if (!existsSync(opts.html)) throw new Error("找不到草稿：" + opts.html);
  const rows = measure(readFileSync(opts.html, "utf8"), measureOptions(aid, opts));
  const widgets = toWidgets(rows, { screen: opts.screen, aid: aid, width: (aid.obj.settings || {}).width || 240, height: (aid.obj.settings || {}).height || 320 });
  const before = screen.widgets || [];
  const diff = diffWidgets(before, widgets);

  log("屏 " + opts.screen + "：草稿量出 " + widgets.length + " 个控件（原屏 " + before.length + " 个）");
  log("  保持不变 " + diff.kept.length + " / 有变化 " + diff.changed.length + " / 新增 " + diff.added.length + " / 消失 " + diff.removed.length);
  for (const entry of diff.changed.slice(0, 8)) log(describeChange(entry));
  if (diff.changed.length > 8) log("  …还有 " + (diff.changed.length - 8) + " 个控件有变化");
  if (diff.added.length > 0) log("  新增：" + diff.added.join(", "));
  if (diff.removed.length > 0) {
    log("  消失：" + diff.removed.join(", "));
    if (!opts.allowDrop) throw new Error("草稿比原屏少了控件（见上面的「消失」）。确认是要删，就加 --allow-drop；不是要删，就是草稿漏了元素或 data-name 写错");
  }

  if (!opts.apply) {
    log("（未写盘：加 --apply 才写回 " + opts.aid + "）");
    return;
  }
  // 只替换真的变了的控件：没动的控件原样留着（连键序和显式 0 都不动），
  // 这样「草稿没改什么」跑一趟的 diff 是空的，而不是满屏键序抖动。
  const changedFields = new Map(diff.changed.map((entry) => [entry.name, entry.fields]));
  const rebuiltByName = new Map(widgets.map((widget) => [widget.name, widget]));
  const beforeNames = new Set(before.map((widget) => widget.name));
  const merged = [];
  for (const old of before) {
    const rebuilt = rebuiltByName.get(old.name);
    if (!rebuilt) continue;
    merged.push(changedFields.has(old.name) ? mergeWidget(old, rebuilt, changedFields.get(old.name)) : old);
  }
  for (const widget of widgets) if (!beforeNames.has(widget.name)) merged.push(widget);
  screen.widgets = merged;
  writeFileSync(aid.file, serializeAid(aid.obj), "utf8");
  log("已写回 " + opts.aid + "（只动了 screens[" + opts.screen + "].widgets）");
  log("下一步：pwsh tools/aidogui/export.ps1 → cmake --build build → 跑对应的验收故事");
}

function runFromAid(opts) {
  if (!opts.out) throw new Error("--from-aid 必须配 --out <文件.html>");
  const aid = readAid(opts.aid);
  const screen = requireScreen(aid, opts.fromAid);
  const html = renderScreenHtml(aid, screen);
  mkdirSync(dirname(opts.out), { recursive: true });
  writeFileSync(opts.out, html, "utf8");
  log("已生成 " + opts.out + "（屏 " + opts.fromAid + "，" + (screen.widgets || []).length + " 个控件）");
  log("改完这样量回去：node tools/aidogui/html_to_aid.mjs --html " + opts.out + " --screen " + opts.fromAid + " --apply");
}

function selfCheck(opts) {
  const screenName = opts.screen || "weather";
  const aid = readAid(opts.aid);
  const screen = requireScreen(aid, screenName);
  let failures = 0;

  // ① 重名必须报错（纯函数，不启浏览器）
  const dupRows = [
    { name: "A", tag: "span", type: null, parentName: null, rect: [0, 0, 10, 10], origin: [0, 0],
      style: { color: "rgb(0, 0, 0)", backgroundColor: "rgba(0, 0, 0, 0)", fontSize: "14px", fontFamily: "x", textAlign: "left", borderRadius: "0px", borderTopWidth: "0px", padding: ["0px", "0px", "0px", "0px"] }, text: "x" },
    { name: "A", tag: "span", type: null, parentName: null, rect: [0, 0, 10, 10], origin: [0, 0],
      style: { color: "rgb(0, 0, 0)", backgroundColor: "rgba(0, 0, 0, 0)", fontSize: "14px", fontFamily: "x", textAlign: "left", borderRadius: "0px", borderTopWidth: "0px", padding: ["0px", "0px", "0px", "0px"] }, text: "x" }
  ];
  let dupRejected = false;
  try {
    toWidgets(dupRows, { screen: screenName, aid: aid, width: 240, height: 320 });
  } catch (err) {
    dupRejected = /重复/.test(err.message);
  }
  if (dupRejected) log("[通过] 重名控件被拒（并且不会走到写盘）");
  else { log("[失败] 重名控件没被拒"); failures += 1; }

  // ② 往返一致：.aid → HTML → 像素 → 控件，逐控件语义全等
  const html = renderScreenHtml(aid, screen);
  const rows = measure(html, measureOptions(aid, opts));
  const rebuilt = toWidgets(rows, { screen: screenName, aid: aid, width: (aid.obj.settings || {}).width || 240, height: (aid.obj.settings || {}).height || 320 });
  const before = (screen.widgets || []).map(canonicalWidget);
  const after = rebuilt.map(canonicalWidget);
  const beforeByName = new Map(before.map((w) => [w.name, w]));
  let mismatched = 0;
  for (const widget of after) {
    const old = beforeByName.get(widget.name);
    if (!old) { log("[失败] 草稿里多出控件 " + widget.name); mismatched += 1; continue; }
    const fields = Object.keys(widget).filter((key) => JSON.stringify(old[key]) !== JSON.stringify(widget[key]));
    if (fields.length === 0) continue;
    mismatched += 1;
    const bits = fields.map((field) => field + ": " + JSON.stringify(old[field]) + " → " + JSON.stringify(widget[field]));
    log("[失败] " + widget.name + "  " + bits.join("；"));
  }
  for (const name of beforeByName.keys()) if (!after.some((w) => w.name === name)) { log("[失败] 往返后少了控件 " + name); mismatched += 1; }
  if (mismatched === 0) log("[通过] 往返一致：屏 " + screenName + " 的 " + after.length + " 个控件逐字段全等");
  else { log("[失败] 往返有 " + mismatched + " 个控件对不上"); failures += 1; }

  log(failures === 0 ? "〔自检通过〕" : "〔自检失败：" + failures + " 项〕");
  process.exitCode = failures === 0 ? 0 : 1;
}

function main() {
  const opts = parseArgs(process.argv.slice(2));
  if (opts.help) { usage(); return; }
  try {
    if (opts.selfCheck) return selfCheck(opts);
    if (opts.fromAid) return runFromAid(opts);
    if (opts.html) return runForward(opts);
    usage();
    process.exitCode = 1;
  } catch (err) {
    fail(err && err.message ? err.message : String(err));
  }
}

main();
