#!/usr/bin/env node
/**
 * 缺字审计（ADR 0017；BIN 时代的"对照子集清单"口径已随之退役）
 *
 * 解决的问题：界面文案里出现"卡上那份 TTF 画不出来的字"—— 编译期全绿，上屏才是一个空白/方框。
 * LVGL 对 cmap 里没有的码点不报错（lv_tiny_ttf 返回 NULL），所以只能构建期对照。
 *
 * 规则：把仓库里"要显示给人看的字符串"逐个字符，对照**字体文件**的 cmap —— 默认是
 * app/aidogui_ui/assets/font/ui_heiti.ttf，也就是真机 SD 卡 fonts/ 那份的源头。
 * 不在 cmap 里的字符 → 打印字符 + 文件:行号，退出码 1（CI 里就是红的）。
 *
 * 覆盖范围：app/、core/src/、core/common/、host/、device/（third_party / build* / .scratch 不看），
 * 只看**会输出到界面上的**调用点（lv_label_set_text 这一类 setter 的实参里的字符串字面量），
 * 不看日志、断言、路径名 —— 那些不上屏。
 *
 * 白名单是这套审计的软肋：调用不在 UI_TEXT_SETTERS 里，它的字就一个都扫不到，结果"全绿"是假的。
 * 所以脚本会顺带扫一遍"长得像上屏调用、但不在白名单里"的函数名（SUSPECT_PATTERNS），
 * 抓到就报错（exit 1）—— 要么加进白名单，要么确认它不上屏。
 *
 * 用法：
 *   node tools/font/audit_glyphs.mjs                  # 审计，缺字则 exit 1
 *   node tools/font/audit_glyphs.mjs --print-required # 打印界面文案用到的字符集（换字体时核对）
 *   node tools/font/audit_glyphs.mjs --selftest       # 自检：造含缺字的夹具，验证能报出来
 *   node tools/font/audit_glyphs.mjs --json           # 机器可读输出
 *   node tools/font/audit_glyphs.mjs --ttf 路径        # 换一份字体审计（默认见 DEFAULT_TTF）
 *   node tools/font/audit_glyphs.mjs --roots app,core/src  # 只扫指定根目录
 */
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(HERE, '..', '..');

/** 扫描根目录（仓库相对）。 */
const DEFAULT_ROOTS = ['app', 'core/src', 'core/common', 'host', 'device'];
/** 不进扫描的目录名 / 前缀。 */
const SKIP_DIR_NAMES = new Set(['third_party', '.git', '.scratch', 'node_modules', 'build', 'build-esp32', 'managed_components']);
/** 参与扫描的源码扩展名。 */
const SOURCE_EXTENSIONS = new Set(['.c', '.cc', '.cpp', '.h', '.hpp']);
/** 默认字体（仓库相对）：卡上 fonts/ 那份的源头。 */
const DEFAULT_TTF = 'app/aidogui_ui/assets/font/ui_heiti.ttf';

/**
 * LVGL 内置图标区：U+F000–U+F8FF。LV_SYMBOL_* 的字形就落在这里，由 LVGL 内置字体提供，
 * 与卡上的 TTF 无关（那份 TTF 里没有这些码点）。所以默认豁免这一区里的码点。
 * 想把图标也算进来就传 --include-symbols。
 */
const LVGL_SYMBOL_RANGE = { start: 0xf000, end: 0xf8ff };

/**
 * "会显示给人看的" LVGL setter：只取这些调用的实参里的字符串字面量。
 * 第一个实参是控件指针，不会是字符串，所以整段实参里的字面量都算文案（含串联字面量）。
 * 2026-10-17（issue 06）逐条对过仓库里的 8.3.11 头文件：删掉三条不存在的
 * lv_msgbox_add_text / _add_title / _add_close_button（8.3 的 msgbox 是 create 一把梭），
 * 补上 lv_msgbox_create 与 lv_dropdown_add_option。
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
  'lv_dropdown_add_option',
  'lv_roller_set_options',
  'lv_btnmatrix_set_map',
  'lv_table_set_cell_value',
  'lv_table_set_cell_value_fmt',
  'lv_tabview_add_tab',
  'lv_list_add_btn',
  'lv_msgbox_create',
];

const SETTER_PATTERN = new RegExp('\\b(' + UI_TEXT_SETTERS.join('|') + ')\\s*\\(', 'g');

/**
 * "长得像上屏调用"的名字特征。用名字片段而不是整名匹配，是为了在 LVGL 加新控件时也能抓住
 * （比如后加的 lv_win_add_title 这类）。命中且不在 UI_TEXT_SETTERS 里的 → 报错让人看。
 */
const SUSPECT_PATTERNS = [
  /(?:^|_)set_text(?:_|$)/,
  /(?:^|_)text_static(?:_|$)/,
  /(?:^|_)set_options(?:_|$)/,
  /(?:^|_)add_option(?:s)?(?:_|$)/,
  /(?:^|_)add_btn(?:_|$)/,
  /(?:^|_)add_tab(?:_|$)/,
  /(?:^|_)set_map(?:_|$)/,
  /(?:^|_)set_cell_value(?:_|$)/,
  /(?:^|_)add_title(?:_|$)/,
  /(?:^|_)add_text(?:_|$)/,
  /(?:^|_)set_placeholder(?:_|$)/,
  /(?:^|_)msgbox_create(?:_|$)/,
];

const SETTER_NAMES = new Set(UI_TEXT_SETTERS);

// ---------------------------------------------------------------- 字体 cmap

/**
 * 读 TTF/OTF 的 cmap，返回它认识的全部码点（Set<number>）。
 * 只解两种子表格式：4（BMP 分段映射，几乎所有中文字体都有）与 12（分段覆盖，含非 BMP）。
 * 零依赖手解的原因：CI 里不装包，而"字体认识哪些字"只有 cmap 说了算。
 */
function readTtfCoverage(file) {
  const buf = fs.readFileSync(file);
  const toTag = (at) => buf.toString('ascii', at, at + 4);
  if (buf.length < 12 || !['\u0000\u0001\u0000\u0000', 'OTTO', 'true', 'ttcf'].includes(toTag(0))) {
    throw new Error('不是 TrueType/OpenType 字体文件：' + file);
  }
  const numTables = buf.readUInt16BE(4);
  let cmapOffset = 0;
  for (let i = 0; i < numTables; i += 1) {
    const record = 12 + i * 16;
    if (toTag(record) === 'cmap') {
      cmapOffset = buf.readUInt32BE(record + 8);
      break;
    }
  }
  if (cmapOffset === 0) {
    throw new Error('字体里没有 cmap 表（拿什么画字？）：' + file);
  }
  const codepoints = new Set();
  const subtables = [];
  const numSubtables = buf.readUInt16BE(cmapOffset + 2);
  for (let i = 0; i < numSubtables; i += 1) {
    const record = cmapOffset + 4 + i * 8;
    const platformId = buf.readUInt16BE(record);
    const encodingId = buf.readUInt16BE(record + 2);
    const subtable = cmapOffset + buf.readUInt32BE(record + 4);
    const format = buf.readUInt16BE(subtable);
    const unicode = platformId === 0 || (platformId === 3 && (encodingId === 1 || encodingId === 10));
    if (!unicode) {
      continue;
    }
    subtables.push({ platformId, encodingId, format });
    if (format === 4) {
      readFormat4(buf, subtable, codepoints);
    } else if (format === 12) {
      readFormat12(buf, subtable, codepoints);
    }
  }
  if (subtables.length === 0) {
    throw new Error('字体没有 Unicode 子表（只有符号/旧编码映射）：' + file);
  }
  return { codepoints, subtables };
}

/** cmap format 4：分段 + idDelta / idRangeOffset，字形号 0 = 没有这个字。 */
function readFormat4(buf, subtable, codepoints) {
  const segCount = buf.readUInt16BE(subtable + 6) / 2;
  const endBase = subtable + 14;
  const startBase = endBase + segCount * 2 + 2;  // 跳过 reservedPad
  const deltaBase = startBase + segCount * 2;
  const rangeBase = deltaBase + segCount * 2;
  for (let seg = 0; seg < segCount; seg += 1) {
    const end = buf.readUInt16BE(endBase + seg * 2);
    const start = buf.readUInt16BE(startBase + seg * 2);
    const delta = buf.readInt16BE(deltaBase + seg * 2);
    const rangeOffset = buf.readUInt16BE(rangeBase + seg * 2);
    if (start === 0xffff) {
      continue;
    }
    for (let cp = start; cp <= end; cp += 1) {
      let glyph;
      if (rangeOffset === 0) {
        glyph = (cp + delta) & 0xffff;
      } else {
        const at = rangeBase + seg * 2 + rangeOffset + (cp - start) * 2;
        if (at + 2 > buf.length) {
          continue;
        }
        glyph = buf.readUInt16BE(at);
        if (glyph !== 0) {
          glyph = (glyph + delta) & 0xffff;
        }
      }
      if (glyph !== 0) {
        codepoints.add(cp);
      }
    }
  }
}

/** cmap format 12：groups(startCharCode, endCharCode, startGlyphID)。 */
function readFormat12(buf, subtable, codepoints) {
  const numGroups = buf.readUInt32BE(subtable + 12);
  for (let group = 0; group < numGroups; group += 1) {
    const base = subtable + 16 + group * 12;
    const start = buf.readUInt32BE(base);
    const end = buf.readUInt32BE(base + 4);
    const startGlyph = buf.readUInt32BE(base + 8);
    for (let cp = start; cp <= end; cp += 1) {
      if (startGlyph + (cp - start) !== 0) {
        codepoints.add(cp);
      }
    }
  }
}

function resolveTtf(ttfPath) {
  const absolute = path.isAbsolute(ttfPath) ? ttfPath : path.join(REPO_ROOT, ttfPath);
  if (!fs.existsSync(absolute)) {
    throw new Error('字体文件不存在：' + absolute + '\n  默认找 ' + DEFAULT_TTF + '（卡上 fonts/ 那份的源头），可用 --ttf 指定');
  }
  return absolute;
}

// ---------------------------------------------------------------- 源码扫描

/** 递归收集待扫文件（绝对路径）。 */
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

/**
 * C 字符串字面量 → 码点数组（只保留可打印字符；\n 之类的控制字符不上屏）。
 * 先还原成字节再整体按 UTF-8 解：一条字面量里可能混着 \x 字节转义（"\xE6\xB1\x89"）
 * 与裸 UTF-8 字符，只有按字节流解才不会把「汉」拆成三个拉丁字母。
 */
function codepointsOfLiteral(raw) {
  const bytes = [];
  const pushText = (text) => {
    for (const byte of Buffer.from(text, 'utf8')) {
      bytes.push(byte);
    }
  };
  const pushByte = (value) => {
    bytes.push(value & 0xff);
  };
  for (let i = 0; i < raw.length; i += 1) {
    const char = raw[i];
    if (char !== '\\') {
      const cp = raw.codePointAt(i);
      if (cp > 0xffff) {
        // 非 BMP 字面量在 UTF-16 里占两个码元：一次吞掉整个码点，否则会被拆成两个孤立代理
        pushText(String.fromCodePoint(cp));
        i += 1;
      } else {
        pushText(char);
      }
      continue;
    }
    const escaped = raw[i + 1] ?? '';
    i += 1;
    if (escaped === 'x') {
      const hex = /^[0-9a-fA-F]{1,2}/.exec(raw.slice(i + 1));
      if (hex) {
        pushByte(parseInt(hex[0], 16));
        i += hex[0].length;
      } else {
        pushText('x');
      }
    } else if (escaped === 'u') {
      const hex = /^[0-9a-fA-F]{4}/.exec(raw.slice(i + 1));
      if (hex) {
        pushText(String.fromCodePoint(parseInt(hex[0], 16)));
        i += 4;
      } else {
        pushText('u');
      }
    } else if (/[0-7]/.test(escaped)) {
      const octal = /^[0-7]{1,3}/.exec(raw.slice(i));
      pushByte(parseInt(octal[0], 8));
      i += octal[0].length - 1;
    } else {
      const named = { n: 0x0a, t: 0x09, r: 0x0d, '0': 0x00, '"': 0x22, "'": 0x27, '\\': 0x5c };
      if (escaped in named) {
        pushByte(named[escaped]);
      } else {
        pushText(escaped);
      }
    }
  }
  const decoded = Buffer.from(bytes).toString('utf8');
  const out = [];
  for (const char of decoded) {
    const cp = char.codePointAt(0);
    if (cp === undefined || cp < 0x20 || cp === 0x7f || cp === 0xfffd) {
      continue;
    }
    out.push(cp);
  }
  return out;
}

/** 名字像"会把字符串送上屏"、但不在白名单里的调用（软肋守卫）。 */
function unknownSettersOf(text) {
  const found = [];
  const callPattern = /\b(lv_[a-z0-9_]+)\s*\(/g;
  let match = callPattern.exec(text);
  while (match !== null) {
    const name = match[1];
    if (!SETTER_NAMES.has(name) && SUSPECT_PATTERNS.some((pattern) => pattern.test(name))) {
      found.push({ name, line: lineOf(text, match.index) });
    }
    match = callPattern.exec(text);
  }
  return found;
}

/** 扫一个文件：返回 { hits: [{ codepoint, line, setter }], unknown: [{ file, name, line }] }。 */
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
  const unknown = unknownSettersOf(text).map((item) => ({ file: relative, name: item.name, line: item.line }));
  return { hits, unknown };
}

function displayOf(codepoint) {
  return String.fromCodePoint(codepoint);
}

function hexOf(codepoint) {
  return 'U+' + codepoint.toString(16).toUpperCase().padStart(4, '0');
}

// ---------------------------------------------------------------- 主流程

function parseArgs(argv) {
  const options = { roots: DEFAULT_ROOTS, ttf: DEFAULT_TTF, printRequired: false, selftest: false, json: false, includeSymbols: false };
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
    } else if (arg === '--ttf') {
      options.ttf = String(argv[i + 1] ?? '');
      i += 1;
    } else if (arg.startsWith('--ttf=')) {
      options.ttf = arg.slice('--ttf='.length);
    } else if (arg === '--roots') {
      options.roots = String(argv[i + 1] ?? '').split(',').filter(Boolean);
      i += 1;
    } else if (arg.startsWith('--roots=')) {
      options.roots = arg.slice('--roots='.length).split(',').filter(Boolean);
    } else if (arg === '--help' || arg === '-h') {
      console.log('用法：node tools/font/audit_glyphs.mjs [--print-required] [--selftest] [--json] [--include-symbols] [--ttf 路径] [--roots app,core/src,host,device]');
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
 * 把扫描结果分三类：miss = 字体没有、要处理的；exempted = 字体没有但按规则豁免的（内置图标）。
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

/** 审计主逻辑：返回 { files, hits, required, missing, exempted, unknown, font }。 */
function audit(roots, options = {}) {
  const ttfPath = resolveTtf(options.ttf ?? DEFAULT_TTF);
  const { codepoints, subtables } = readTtfCoverage(ttfPath);
  const files = collectSources(roots);
  const hits = [];
  const unknown = [];
  for (const file of files) {
    const scanned = scanFile(file);
    hits.push(...scanned.hits);
    unknown.push(...scanned.unknown);
  }
  const { required, missing, exempted } = classify(hits, codepoints, options.includeSymbols === true);
  return {
    files,
    hits,
    required,
    missing,
    exempted,
    unknown,
    font: {
      file: path.relative(REPO_ROOT, ttfPath).split(path.sep).join('/'),
      codepoints: codepoints.size,
      subtables,
    },
  };
}

function report(result, options) {
  const { files, hits, required, missing, exempted, unknown, font } = result;
  console.log('');
  console.log('缺字审计：扫描 ' + options.roots.join(', ') + ' 下 ' + files.length + ' 个源文件，' + hits.length + ' 个字符引用（去重 ' + required.size + ' 个）');
  console.log('  字体：' + font.file + '（cmap ' + font.codepoints + ' 个码点）');
  if (exempted.size > 0) {
    console.log('  豁免 ' + exempted.size + ' 个内置图标（U+F000–U+F8FF，LVGL 自带字形）：' + [...exempted.keys()].sort((a, b) => a - b).map(hexOf).join(' '));
  }
  console.log('');
  let failed = false;
  if (unknown.length > 0) {
    failed = true;
    const names = [...new Set(unknown.map((item) => item.name))];
    console.log('✗ 有 ' + names.length + ' 个调用看着会上屏，但不在审计白名单里（它们的字一个都没查）：');
    for (const name of names) {
      const places = unknown.filter((item) => item.name === name).slice(0, 3).map((item) => item.file + ':' + item.line);
      console.log('  ' + name + ' — ' + places.join('、'));
    }
    console.log('  修法：确认它确实上屏就加进本脚本的 UI_TEXT_SETTERS；不上屏就换个名字/包一层。');
    console.log('');
  }
  if (missing.size === 0) {
    console.log((failed ? '⚠' : '✓') + ' 界面文案全部落在字体的 cmap 里（' + files.length + ' 个文件）');
    return failed ? 1 : 0;
  }
  console.log('✗ 有 ' + missing.size + ' 个字符不在字体里：');
  for (const [codepoint, places] of [...missing.entries()].sort((a, b) => a[0] - b[0])) {
    console.log('  ' + hexOf(codepoint) + ' 「' + displayOf(codepoint) + '」 — ' + places.map((p) => p.file + ':' + p.line).join('、'));
  }
  console.log('');
  console.log('修法（三选一）：');
  console.log('  - 换一份能画出这些字的字体：' + DEFAULT_TTF + '（卡上 fonts/ 那份要同步换，两处必须是同一份）');
  console.log('  - 改掉这些文案（或用别的字表达同一件事）');
  console.log('  - 若它是设计器里的 label 忘了指定 textFont（于是落到内置 embark_zh_14，只有 184 字形），补上 textFont');
  return 1;
}

function printRequired(result) {
  const isSymbol = (cp) => cp >= LVGL_SYMBOL_RANGE.start && cp <= LVGL_SYMBOL_RANGE.end;
  const needed = [...result.required].filter((cp) => !isSymbol(cp)).sort((a, b) => a - b);
  const missingChars = needed.filter((cp) => result.missing.has(cp));
  console.log('本次扫描到的界面文案共 ' + result.required.size + ' 个不同字符（其中 ' + (result.required.size - needed.length) + ' 个是内置图标，已剔除）；');
  console.log('字体 ' + result.font.file + ' 缺 ' + missingChars.length + ' 个：' + missingChars.map(hexOf).join(' '));
  console.log('');
  console.log('全部字符：' + needed.map(displayOf).join(''));
  return 0;
}

/** 自检：造含缺字的夹具，验证脚本真的能报出来（并验证全命中、图标豁免、白名单漏项都能识别）。 */
function selftest() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'embark-audit-'));
  const missingChar = '\u9f98';  // 龘：BMP 内、这份 TTF 没有
  const astralChar = '\u{1d11e}';  // 增四度谱号：非 BMP（TTF 只有 format 4 ⇒ 必然没有），顺带验证代理对
  const coveredChar = '\u542f';  // 启：TTF 有
  try {
    const { codepoints } = readTtfCoverage(resolveTtf(DEFAULT_TTF));

    const dirty = path.join(dir, 'dirty.c');
    fs.writeFileSync(
        dirty,
        'void f(lv_obj_t* label) {\n  lv_label_set_text(label, "' + missingChar + astralChar + '");\n}\n',
        'utf8');
    // 夹具在仓库外，所以直接喂给扫描器 + classify，不走 audit（audit 只扫仓库内的根目录）
    const dirtyScan = scanFile(dirty);
    const dirtyClass = classify(dirtyScan.hits, codepoints, false);
    const okDirty = dirtyClass.missing.has(missingChar.codePointAt(0)) && dirtyClass.missing.has(astralChar.codePointAt(0));
    const dirtyLines = [...dirtyClass.missing.values()].flat().map((place) => place.line);
    const okLine = dirtyLines.every((line) => line === 2);

    const clean = path.join(dir, 'clean.c');
    fs.writeFileSync(clean, 'void g(lv_obj_t* label) {\n  lv_label_set_text(label, "' + coveredChar + '");\n}\n', 'utf8');
    const cleanScan = scanFile(clean);
    const okClean = cleanScan.hits.length === 1 && cleanScan.hits[0].line === 2 && classify(cleanScan.hits, codepoints, false).missing.size === 0;

    // 内置图标区的字符：默认豁免，--include-symbols 时才当缺字
    const icon = path.join(dir, 'icon.c');
    fs.writeFileSync(icon, 'void h(lv_obj_t* label) {\n  lv_label_set_text(label, "\uf0b9");\n}\n', 'utf8');
    const iconHits = scanFile(icon).hits;
    const lenient = classify(iconHits, codepoints, false);
    const strict = classify(iconHits, codepoints, true);
    const okIcon = lenient.missing.size === 0 && lenient.exempted.size === 1 && strict.missing.size === 1 && strict.exempted.size === 0;

    // 白名单漏项守卫：名字像上屏、但不在白名单里的调用必须被点出来
    const sneaky = path.join(dir, 'sneaky.c');
    fs.writeFileSync(sneaky, 'void i(lv_obj_t* box) {\n  lv_msgbox_add_title(box, "\u9f98");\n}\n', 'utf8');
    const sneakyScan = scanFile(sneaky);
    const okSneaky = sneakyScan.unknown.length === 1 && sneakyScan.unknown[0].name === 'lv_msgbox_add_title' && sneakyScan.unknown[0].line === 2;

    console.log('自检 ' + (okDirty && okLine && okClean && okIcon && okSneaky ? 'PASS' : 'FAIL'));
    console.log('  缺字夹具：' + [...dirtyClass.missing.keys()].map(hexOf).join(' ') + '（期望含 ' + hexOf(missingChar.codePointAt(0)) + ' 与 ' + hexOf(astralChar.codePointAt(0)) + '）');
    console.log('  行号：' + [...new Set(dirtyLines)].join(',') + '（期望 2）');
    console.log('  全命中夹具：' + (okClean ? '识别 1 个字符、行号正确、不报缺字' : '识别失败'));
    console.log('  图标豁免：' + (okIcon ? '默认豁免 ' + hexOf(0xf0b9) + '，加 --include-symbols 后报缺字' : '失败'));
    console.log('  白名单漏项：' + (okSneaky ? '抓到 lv_msgbox_add_title' : '没抓到'));
    return okDirty && okLine && okClean && okIcon && okSneaky ? 0 : 1;
  } finally {
    fs.rmSync(dir, { recursive: true, force: true });
  }
}

const options = parseArgs(process.argv.slice(2));
try {
  if (options.selftest) {
    process.exit(selftest());
  }
  const result = audit(options.roots, options);
  if (options.printRequired) {
    process.exit(printRequired(result));
  }
  if (options.json) {
    console.log(JSON.stringify({
      font: { file: result.font.file, codepoints: result.font.codepoints },
      files: result.files.length,
      references: result.hits.length,
      required: [...result.required].sort((a, b) => a - b),
      missing: [...result.missing.entries()].map(([codepoint, places]) => ({ codepoint, places })),
      exempted: [...result.exempted.entries()].map(([codepoint, places]) => ({ codepoint, places })),
      unknownSetters: [...new Set(result.unknown.map((item) => item.name))],
    }));
    process.exit(result.unknown.length > 0 ? 1 : 0);
  }
  process.exit(report(result, options));
} catch (error) {
  console.error('缺字审计跑不起来：' + (error instanceof Error ? error.message : String(error)));
  process.exit(2);
}
