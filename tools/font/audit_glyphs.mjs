#!/usr/bin/env node
/**
 * 缺字审计（决定记录：docs/adr/0007-font-management.md，落地 issue 17）
 *
 * 解决的问题：界面加了字、字库没跟着重新生成 —— 编译期全绿，到了屏上才是一个空白/方块。
 * LVGL 的静态字库里没有的码点不会报错，只是画不出来，所以这条只能靠构建期对照。
 *
 * 规则：把仓库里"要显示给人看的字符串"逐个字符，对照静态子集字库的覆盖清单
 * （tools/font/*.json 的 chars，由 gen_font.mjs 生成、也是重新生成字库的唯一输入）。
 * 不在覆盖里的字符 → 打印字符 + 文件:行号，退出码 1（CI 里就是红的）。
 *
 * 覆盖范围：app/、src/、platform/ 三个根目录（third_party / build* / .scratch 不看），
 * 只看**会输出到界面上的**调用点（lv_label_set_text 这一类 setter 的实参里的字符串字面量），
 * 不看日志、断言、路径名 —— 那些不上屏。
 *
 * 用法：
 *   node tools/font/audit_glyphs.mjs                  # 审计，缺字则 exit 1
 *   node tools/font/audit_glyphs.mjs --print-required # 打印"应该进清单的字符集"（改字库时用）
 *   node tools/font/audit_glyphs.mjs --selftest       # 自检：造一个含缺字的夹具，验证能报出来
 *   node tools/font/audit_glyphs.mjs --json           # 机器可读输出
 *   node tools/font/audit_glyphs.mjs --roots app,src  # 只扫指定根目录
 */
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(HERE, '..', '..');

/** 扫描根目录（仓库相对）。 */
const DEFAULT_ROOTS = ['app', 'src', 'platform'];
/** 不进扫描的目录名 / 前缀。 */
const SKIP_DIR_NAMES = new Set(['third_party', '.git', '.scratch', 'node_modules', 'build', 'build-esp32', 'managed_components']);
/** 参与扫描的源码扩展名。 */
const SOURCE_EXTENSIONS = new Set(['.c', '.cc', '.cpp', '.h', '.hpp']);

/**
 * LVGL 内置图标区：U+F000–U+F8FF。LV_SYMBOL_* 的字形就落在这里，由 LVGL 内置字体提供，
 * 不进静态子集字库（issue 17 备注：LV_SYMBOL 图标码点是内置字形，不进字库范围）。
 * 所以默认豁免这一区里的码点 —— 否则每加一个图标都要往子集字库里塞图标字形。
 * 想把图标也算进来（真要用子集字库画图标）就传 --include-symbols。
 */
const LVGL_SYMBOL_RANGE = { start: 0xf000, end: 0xf8ff };

/**
 * "会显示给人看的" LVGL setter：只取这些调用的实参里的字符串字面量。
 * 第一个实参是控件指针，不会是字符串，所以整段实参里的字面量都算文案（含串联字面量）。
 */
const UI_TEXT_SETTERS = [
  'lv_label_set_text',
  'lv_label_set_text_fmt',
  'lv_label_set_text_static',
  'lv_checkbox_set_text',
  'lv_checkbox_set_text_static',
  'lv_textarea_set_text',
  'lv_textarea_add_text',
  'lv_textarea_set_placeholder_text',
  'lv_dropdown_set_options',
  'lv_dropdown_set_options_static',
  'lv_roller_set_options',
  'lv_btnmatrix_set_map',
  'lv_table_set_cell_value',
  'lv_table_set_cell_value_fmt',
  'lv_tabview_add_tab',
  'lv_list_add_btn',
  'lv_msgbox_add_text',
  'lv_msgbox_add_title',
  'lv_msgbox_add_close_button',
];

const SETTER_PATTERN = new RegExp('\\b(' + UI_TEXT_SETTERS.join('|') + ')\\s*\\(', 'g');

// ---------------------------------------------------------------- 覆盖清单

/** "0x20-0x7F" / "0x20" → 码点数组。 */
function parseRange(spec) {
  const text = String(spec).trim();
  const match = /^(0[xX][0-9a-fA-F]+|\d+)\s*(?:-\s*(0[xX][0-9a-fA-F]+|\d+))?$/.exec(text);
  if (!match) {
    throw new Error('看不懂的码点范围：' + text);
  }
  const toNumber = (token) => (token.toLowerCase().startsWith('0x') ? parseInt(token, 16) : parseInt(token, 10));
  const start = toNumber(match[1]);
  const end = match[2] === undefined ? start : toNumber(match[2]);
  const out = [];
  for (let cp = start; cp <= end; cp += 1) {
    out.push(cp);
  }
  return out;
}

/**
 * 清单里的一项字符描述 → 码点数组。两种写法都接受：
 *   - 字面字符串（清单的 ascii / uiText 字段就是这种：直接是那串字符）
 *   - 码点范围（"0x20-0x7F"、"0xF013"）
 */
function codepointsOfSpec(spec) {
  const text = String(spec);
  // 注意：只对范围判定做 trim —— 字面字符串里的空格（U+0020）是真实字符，不能顺手 trim 掉
  if (/^(?:0[xX][0-9a-fA-F]+|\d+)(?:\s*-\s*(?:0[xX][0-9a-fA-F]+|\d+))?$/.test(text.trim())) {
    return parseRange(text);
  }
  const out = [];
  for (const char of text) {
    // for..of 按码点迭代，代理对安全
    out.push(char.codePointAt(0));
  }
  return out;
}

/** 读 tools/font/*.json 汇总覆盖码点。返回 { codepoints:Set<number>, manifests:[{file,...}] }。 */
function loadCoverage() {
  const codepoints = new Set();
  const manifests = [];
  const files = fs
    .readdirSync(HERE)
    .filter((name) => name.endsWith('.json'))
    .sort();
  if (files.length === 0) {
    throw new Error('tools/font/ 下没有覆盖清单（*.json）—— 先跑 node tools/font/gen_font.mjs');
  }
  for (const name of files) {
    const file = path.join(HERE, name);
    const manifest = JSON.parse(fs.readFileSync(file, 'utf8'));
    const chars = manifest.chars ?? {};
    const ascii = Array.isArray(chars.ascii) ? chars.ascii : [chars.ascii];
    for (const spec of ascii) {
      for (const cp of codepointsOfSpec(spec)) {
        codepoints.add(cp);
      }
    }
    for (const char of String(chars.uiText ?? '')) {
      codepoints.add(char.codePointAt(0));
    }
    for (const symbol of chars.symbols ?? []) {
      for (const cp of codepointsOfSpec(symbol)) {
        codepoints.add(cp);
      }
    }
    manifests.push({ file: path.relative(REPO_ROOT, file).split(path.sep).join('/'), symbol: manifest.symbol ?? name });
  }
  return { codepoints, manifests };
}

// ---------------------------------------------------------------- 源码扫描

/** 递归收集待扫文件（仓库相对路径）。 */
function collectSources(roots) {
  const found = [];
  const walk = (absolute) => {
    const stat = fs.statSync(absolute);
    if (stat.isDirectory()) {
      for (const entry of fs.readdirSync(absolute).sort()) {
        if (SKIP_DIR_NAMES.has(entry)) {
          continue;
        }
        walk(path.join(absolute, entry));
      }
      return;
    }
    if (SOURCE_EXTENSIONS.has(path.extname(absolute))) {
      found.push(absolute);
    }
  };
  for (const root of roots) {
    const absolute = path.join(REPO_ROOT, root);
    if (fs.existsSync(absolute)) {
      walk(absolute);
    }
  }
  return found;
}

/**
 * 把注释换成等长空格（保留换行，行号才准）。
 * 字符串里的 "//" 不能被当注释，所以这里手写状态机，不图省事用正则。
 */
function blankComments(text) {
  const out = text.split('');
  let state = 'code';
  for (let i = 0; i < text.length; i += 1) {
    const char = text[i];
    const next = text[i + 1];
    if (state === 'code') {
      if (char === '/' && next === '/') {
        state = 'line-comment';
      } else if (char === '/' && next === '*') {
        state = 'block-comment';
      } else if (char === '"') {
        state = 'string';
      } else if (char === "'") {
        state = 'char';
      }
      continue;
    }
    if (state === 'line-comment') {
      if (char === '\n') {
        state = 'code';
      } else {
        out[i] = ' ';
      }
      continue;
    }
    if (state === 'block-comment') {
      if (char === '*' && next === '/') {
        out[i] = ' ';
        out[i + 1] = ' ';
        i += 1;
        state = 'code';
      } else if (char !== '\n') {
        out[i] = ' ';
      }
      continue;
    }
    // state === 'string' | 'char'：整体保留（含转义），让后面的解析器自己解码
    if (char === '\\') {
      i += 1;
      continue;
    }
    if ((state === 'string' && char === '"') || (state === 'char' && char === "'")) {
      state = 'code';
    }
  }
  return out.join('');
}

function lineOf(text, index) {
  let line = 1;
  for (let i = 0; i < index && i < text.length; i += 1) {
    if (text[i] === '\n') {
      line += 1;
    }
  }
  return line;
}

/**
 * 从一个 setter 调用的 "(" 开始，取整段实参里的字符串字面量。
 * 括号配平用 depth（圆括号；花括号/方括号里的字面量也是文案，比如 btnmatrix 的 map 数组）。
 */
function literalsOfCall(text, openParenIndex) {
  const literals = [];
  let depth = 0;
  for (let i = openParenIndex; i < text.length; i += 1) {
    const char = text[i];
    if (char === '(' || char === '[' || char === '{') {
      depth += 1;
      continue;
    }
    if (char === ')' || char === ']' || char === '}') {
      depth -= 1;
      if (depth === 0) {
        break;
      }
      continue;
    }
    if (char !== '"') {
      continue;
    }
    const start = i;
    i += 1;
    let raw = '';
    while (i < text.length && text[i] !== '"') {
      if (text[i] === '\\') {
        raw += text[i] + (text[i + 1] ?? '');
        i += 2;
        continue;
      }
      raw += text[i];
      i += 1;
    }
    literals.push({ raw, line: lineOf(text, start) });
  }
  return literals;
}

/** C 字符串字面量 → 码点数组（只保留可打印字符；\n 之类的控制字符不上屏）。 */
function codepointsOfLiteral(raw) {
  let decoded = '';
  for (let i = 0; i < raw.length; i += 1) {
    let char = raw[i];
    if (char === '\\') {
      const escaped = raw[i + 1];
      i += 1;
      if (escaped === 'x') {
        const hex = /^[0-9a-fA-F]{1,2}/.exec(raw.slice(i + 1));
        if (hex) {
          char = String.fromCodePoint(parseInt(hex[0], 16));
          i += hex[0].length;
        } else {
          char = 'x';
        }
      } else if (escaped === 'u') {
        const hex = /^[0-9a-fA-F]{4}/.exec(raw.slice(i + 1));
        if (hex) {
          char = String.fromCodePoint(parseInt(hex[0], 16));
          i += 4;
        } else {
          char = 'u';
        }
      } else if (escaped === 'n') {
        char = '\n';
      } else if (escaped === 't') {
        char = '\t';
      } else if (escaped === 'r') {
        char = '\r';
      } else if (escaped === '0') {
        char = '\0';
      } else {
        char = escaped ?? '';
      }
    }
    decoded += char;
  }
  // 用 for..of 迭代解码后的字符串：代理对（非 BMP 字符）到这里才会合成一个码点
  const out = [];
  for (const char of decoded) {
    const cp = char.codePointAt(0);
    if (cp === undefined || cp < 0x20 || cp === 0x7f) {
      continue;
    }
    out.push(cp);
  }
  return out;
}

/** 扫一个文件：返回 [{ codepoint, line, literal, setter }]。 */
function scanFile(absolute) {
  const raw = fs.readFileSync(absolute, 'utf8');
  const text = blankComments(raw);
  const relative = path.relative(REPO_ROOT, absolute).split(path.sep).join('/');
  const hits = [];
  SETTER_PATTERN.lastIndex = 0;
  let match = SETTER_PATTERN.exec(text);
  while (match !== null) {
    const openParen = text.indexOf('(', match.index);
    for (const literal of literalsOfCall(text, openParen)) {
      for (const codepoint of codepointsOfLiteral(literal.raw)) {
        hits.push({ file: relative, line: literal.line, codepoint, setter: match[1] });
      }
    }
    match = SETTER_PATTERN.exec(text);
  }
  return hits;
}

function displayOf(codepoint) {
  return String.fromCodePoint(codepoint);
}

function hexOf(codepoint) {
  return 'U+' + codepoint.toString(16).toUpperCase().padStart(4, '0');
}

// ---------------------------------------------------------------- 主流程

function parseArgs(argv) {
  const options = { roots: DEFAULT_ROOTS, printRequired: false, selftest: false, json: false, includeSymbols: false };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--print-required') {
      options.printRequired = true;
    } else if (arg === '--selftest') {
      options.selftest = true;
    } else if (arg === '--json') {
      options.json = true;
    } else if (arg === '--include-symbols') {
      options.includeSymbols = true;
    } else if (arg === '--roots') {
      options.roots = String(argv[i + 1] ?? '').split(',').filter(Boolean);
      i += 1;
    } else if (arg.startsWith('--roots=')) {
      options.roots = arg.slice('--roots='.length).split(',').filter(Boolean);
    } else if (arg === '--help' || arg === '-h') {
      console.log('用法：node tools/font/audit_glyphs.mjs [--print-required] [--selftest] [--json] [--include-symbols] [--roots app,src,platform]');
      process.exit(0);
    } else {
      console.error('未知参数：' + arg);
      process.exit(2);
    }
  }
  return options;
}

/** 记录一个码点的出处（每个码点最多留 3 处，去重同一行）。 */
function remember(map, hit) {
  if (!map.has(hit.codepoint)) {
    map.set(hit.codepoint, []);
  }
  const list = map.get(hit.codepoint);
  if (list.length < 3 && !list.some((item) => item.file === hit.file && item.line === hit.line)) {
    list.push({ file: hit.file, line: hit.line, setter: hit.setter });
  }
}

/**
 * 把扫描结果分三类：`miss` = 清单里没有、要补的；`exempted` = 清单里没有但按规则豁免的（内置图标）。
 * 单独拆出来是为了能在 --selftest 里直接对一个夹具调用，不必真扫整个仓库。
 */
function classify(hits, codepoints, includeSymbols) {
  const required = new Set(hits.map((hit) => hit.codepoint));
  const missing = new Map();
  const exempted = new Map();
  for (const hit of hits) {
    if (codepoints.has(hit.codepoint)) {
      continue;
    }
    const inSymbolRange = hit.codepoint >= LVGL_SYMBOL_RANGE.start && hit.codepoint <= LVGL_SYMBOL_RANGE.end;
    if (inSymbolRange && !includeSymbols) {
      remember(exempted, hit);
    } else {
      remember(missing, hit);
    }
  }
  return { required, missing, exempted };
}

/** 审计主逻辑：返回 { files, hits, required, missing, exempted, manifests }。 */
function audit(roots, options = {}) {
  const { codepoints, manifests } = loadCoverage();
  const files = collectSources(roots);
  const hits = [];
  for (const file of files) {
    hits.push(...scanFile(file));
  }
  const { required, missing, exempted } = classify(hits, codepoints, options.includeSymbols === true);
  return { files, hits, required, missing, exempted, manifests };
}

function report(result, options) {
  const { files, hits, required, missing, exempted, manifests } = result;
  console.log('');
  console.log('缺字审计：扫描 ' + options.roots.join(', ') + ' 下 ' + files.length + ' 个源文件，' + hits.length + ' 个字符引用（去重 ' + required.size + ' 个）');
  for (const manifest of manifests) {
    console.log('  覆盖清单：' + manifest.file + '（符号 ' + manifest.symbol + '）');
  }
  if (exempted.size > 0) {
    console.log('  豁免 ' + exempted.size + ' 个内置图标（U+F000–U+F8FF，LVGL 自带字形）：' + [...exempted.keys()].sort((a, b) => a - b).map(hexOf).join(' '));
  }
  console.log('');
  if (missing.size === 0) {
    console.log('✓ 界面文案全部落在静态子集字库的覆盖里（' + files.length + ' 个文件）');
    return 0;
  }
  console.log('✗ 有 ' + missing.size + ' 个字符不在覆盖清单里：');
  for (const [codepoint, places] of [...missing.entries()].sort((a, b) => a[0] - b[0])) {
    console.log('  ' + hexOf(codepoint) + ' 「' + displayOf(codepoint) + '」 — ' + places.map((p) => p.file + ':' + p.line).join('、'));
  }
  console.log('');
  console.log('修法：把这些字加进 tools/font/gen_font.mjs 的 UI_TEXT，然后');
  console.log('  node tools/font/gen_font.mjs   # 重新生成 assets/fonts/embark_zh_14.c 与清单');
  console.log('（要贴的字符集可用 --print-required 直接打印）');
  return 1;
}

function printRequired(result) {
  const isSymbol = (cp) => cp >= LVGL_SYMBOL_RANGE.start && cp <= LVGL_SYMBOL_RANGE.end;
  const needed = [...result.required].filter((cp) => !isSymbol(cp)).sort((a, b) => a - b);
  const chars = needed.map(displayOf);
  console.log('本次扫描到的界面文案共 ' + result.required.size + ' 个不同字符（其中 ' + (result.required.size - needed.length) + ' 个是内置图标，已剔除）；');
  console.log('ASCII 之外的（可贴进 gen_font.mjs 的 UI_TEXT）：');
  console.log(chars.filter((char) => char.codePointAt(0) > 0x7f).join(''));
  console.log('');
  console.log('全部字符：' + chars.join(''));
  console.log('码点：' + needed.map(hexOf).join(' '));
  return 0;
}

/** 自检：造一个含缺字的夹具，验证脚本真的能报出来（并验证全命中时不报错）。 */
function selftest() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'embark-audit-'));
  const missingChar = '\u9f98';  // 龘：BMP 内、肯定不在子集里
  const astralChar = '\u{1d11e}';  // 增四度谱号：非 BMP，顺带验证代理对
  try {
    const dirty = path.join(dir, 'dirty.c');
    fs.writeFileSync(
        dirty,
        'void f(lv_obj_t* label) {\n  lv_label_set_text(label, "' + missingChar + astralChar + '");\n}\n',
        'utf8');
    // 夹具在仓库外，所以直接喂给扫描器 + classify，不走 audit（audit 只扫仓库内的根目录）
    const codepoints = loadCoverage().codepoints;
    const hits = scanFile(dirty);
    const dirtyClass = classify(hits, codepoints, false);
    const okDirty = dirtyClass.missing.has(missingChar.codePointAt(0)) && dirtyClass.missing.has(astralChar.codePointAt(0));
    const dirtyLines = [...dirtyClass.missing.values()].flat().map((place) => place.line);
    const okLine = dirtyLines.every((line) => line === 2);

    const clean = path.join(dir, 'clean.c');
    fs.writeFileSync(clean, 'void g(lv_obj_t* label) {\n  lv_label_set_text(label, "OK");\n}\n', 'utf8');
    const cleanHits = scanFile(clean);
    const okClean = cleanHits.length === 2 && cleanHits.every((hit) => hit.line === 2);

    // 内置图标区的字符：默认豁免，--include-symbols 时才当缺字
    const icon = path.join(dir, 'icon.c');
    fs.writeFileSync(icon, 'void h(lv_obj_t* label) {\n  lv_label_set_text(label, "\uf0b9");\n}\n', 'utf8');
    const iconHits = scanFile(icon);
    const lenient = classify(iconHits, codepoints, false);
    const strict = classify(iconHits, codepoints, true);
    const okIcon = lenient.missing.size === 0 && lenient.exempted.size === 1 && strict.missing.size === 1 && strict.exempted.size === 0;

    console.log('自检 ' + (okDirty && okLine && okClean && okIcon ? 'PASS' : 'FAIL'));
    console.log('  缺字夹具：' + [...dirtyClass.missing.keys()].map(hexOf).join(' ') + '（期望含 ' + hexOf(missingChar.codePointAt(0)) + ' 与 ' + hexOf(astralChar.codePointAt(0)) + '）');
    console.log('  行号：' + [...new Set(dirtyLines)].join(',') + '（期望 2）');
    console.log('  全命中夹具：' + (okClean ? '识别 2 个字符且行号正确' : '识别失败'));
    console.log('  图标豁免：' + (okIcon ? '默认豁免 ' + hexOf(0xf0b9) + '，加 --include-symbols 后报缺字' : '失败'));
    return okDirty && okLine && okClean && okIcon ? 0 : 1;
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
}

const options = parseArgs(process.argv.slice(2));
if (options.selftest) {
  process.exit(selftest());
}
const result = audit(options.roots, options);
if (options.printRequired) {
  process.exit(printRequired(result));
}
if (options.json) {
  console.log(JSON.stringify({
    files: result.files.length,
    references: result.hits.length,
    required: [...result.required].sort((a, b) => a - b),
    missing: [...result.missing.entries()].map(([codepoint, places]) => ({ codepoint, places })),
    exempted: [...result.exempted.entries()].map(([codepoint, places]) => ({ codepoint, places })),
  }));
  process.exit(0);
}
process.exit(report(result, options));
