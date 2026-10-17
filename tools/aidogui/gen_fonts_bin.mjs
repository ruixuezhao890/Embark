#!/usr/bin/env node
/**
 * 重生成 AiDoGUI 运行时字库（app/aidogui_ui/export/font/ui_font_ui_heiti_<size>.bin）。
 *
 * 为什么要有这个脚本
 * ---------------------------------------------------------------------------
 * 设计器导出 BIN 字库时，只把「设计稿里出现过的字符」打进每个字号（子集字库）。
 * 于是运行期一旦显示设计稿里没有的字，屏上就是空心方框（LVGL 的缺字形占位符，
 * lv_draw_sw_letter.c 画的那个圆角空心矩形），真机 2026-10-17 的时钟屏就是这样：
 *   48px 只收了数字和冒号 → 占位串 "--:--" 的 4 个 '-' 全成方框；
 *   14px 没有「星期」二字 → 日期行「----年--月--日 星期-」把「星期」画成方框。
 * 设计器重导出会把我们补的字又冲掉，所以补字必须做成「导出之后自动跑一遍」的步骤
 * （tools/aidogui/export.ps1 末尾调用本脚本）。
 *
 * 字符从哪来（三条线取并集，宁多勿缺）
 * ---------------------------------------------------------------------------
 *   1. export/screens/*.c 里的静态文案：lv_label_set_text(<控件>, "…") —— 设计稿里的字。
 *   2. App 源码里的字符串字面量：运行期推到控件上的字（「等待对时」「空气优」「雷雨」…）。
 *   3. 每个字号至少要有的骨架字符（ASCII 可打印；大字号的数字/冒号/减号）。
 * 第 2 条按「屏」收集，再按「这个字号在哪些屏上用过」并进去 —— App 用逻辑名写控件
 * （clock_time → L_Time），精确到控件的映射表在 App 里，脚本不去猜，按屏取并集即可。
 *
 * 为什么必须 --no-compress、--bpp 4
 * ---------------------------------------------------------------------------
 * 设计器产物 header 里 compression_id = 0（plain，未压缩）、bits_per_pixel = 4。
 * 用 lv_font_conv 默认的压缩（compression_id = 1）生成的 BIN 形态与设计器不同；
 * 运行时 lv_font_load 两条路都能读，但「与设计器一致」才是可对照的基线，出问题好定位。
 *
 * 用法
 *   node tools/aidogui/gen_fonts_bin.mjs            # 生成
 *   node tools/aidogui/gen_fonts_bin.mjs --dry-run  # 只打印每个字号收了多少字，不写文件
 */
import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const EXPORT_DIR = path.join(REPO_ROOT, 'app', 'aidogui_ui', 'export');
const FONT_DIR = path.join(EXPORT_DIR, 'font');
const SOURCE_FONT = path.join(REPO_ROOT, 'app', 'aidogui_ui', 'assets', 'font', 'ui_heiti.ttf');

const SIZES = [11, 12, 14, 22, 28, 48, 56];
const BPP = 4;
const DRY_RUN = process.argv.includes('--dry-run');

/** 屏名 → 该屏的 App 源码（第 2 条字符来源）。屏名 == App 名（ADR 0011）。 */
const SCREEN_SOURCES = {
  launcher: ['app/launcher/launcher_app.cpp', 'app/launcher/launcher_tiles.cpp'],
  clock: ['app/clock/clock_app.cpp'],
  settings: ['app/settings/settings_app.cpp'],
  weather: ['app/weather/weather_app.cpp', 'app/weather/weather_data.h'],
  music: ['app/music/music_app.cpp'],
  album: ['app/album/album_app.cpp'],
};

/**
 * 大字号（≥22px）只用于数字/图标/占位词，别把整屏中文都灌进去 ——
 * 22px 一个汉字 ≈ 240 字节、48px ≈ 1100 字节，灌 150 个字直接撑爆字库池。
 * 这里给大字号列「明确需要」的额外字符；11/12/14 才是正文，按屏取整个并集。
 */
const BIG_SIZE_EXTRA = {
  // 22px：主屏天气图标、天气屏当前图标（天气词表见 app/weather/weather_data.h:36 的 icon 列：
  // 晴/多云/阴/小雨/中雨/大雨/雷雨）；相册/音乐的占位词「占位屏」来自静态文案，自动带上。
  22: ['多', '云', '阴', '小', '中', '大', '雷', '雨', '雪', '雾', '霾'],
  // 28px：主屏/天气屏的温度「26°」
  28: ['°'],
  // 48px：时钟大时间「11:31」
  48: [],
  // 56px：主屏大时间「11:31」
  56: [],
};

/** 各字号要带上的 ASCII/符号码点区间（第 3 条字符来源）。 */
const SIZE_RANGES = {
  11: ['0x20-0x7e'],
  12: ['0x20-0x7e'],
  14: ['0x20-0x7e'],
  22: ['0x20-0x7e'],
  28: ['0x20', '0x25', '0x2b', '0x2d', '0x2e', '0x2f', '0x30-0x39', '0x43', '0xb0'],
  48: ['0x20', '0x2d', '0x30-0x39', '0x3a'],
  56: ['0x20', '0x2d', '0x30-0x39', '0x3a'],
};

const TEXT_SIZES = new Set([11, 12, 14]);

/** C 字符串字面量反转义（只需认几种常见转义；转义成控制字符的整条丢掉）。 */
function unescapeC(s) {
  let out = '';
  for (let i = 0; i < s.length; i += 1) {
    const ch = s[i];
    if (ch !== '\\') { out += ch; continue; }
    const next = s[(i += 1)];
    if (next === 'n' || next === 't' || next === 'r' || next === '0') return null; // 换行/制表不算文案
    if (next === 'x') {
      let hex = '';
      while (/[0-9A-Fa-f]/.test(s[i + 1] ?? '')) hex += s[(i += 1)];
      out += String.fromCodePoint(parseInt(hex, 16));
      continue;
    }
    if (next === 'u') { out += String.fromCodePoint(parseInt(s.slice(i + 1, i + 5), 16)); i += 4; continue; }
    out += next ?? '';
  }
  return out;
}

function listFiles(dir, exts) {
  if (!existsSync(dir)) return [];
  const out = [];
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) { if (e.name !== 'font' && e.name !== 'image') out.push(...listFiles(p, exts)); }
    else if (exts.some((x) => e.name.endsWith(x))) out.push(p);
  }
  return out;
}

/** 抽出一段文本里所有 UTF-8 字符（只保留可打印的，丢掉控制字符与 BOM）。 */
function addChars(target, text) {
  if (text === null) return;
  for (const ch of text) {
    const cp = ch.codePointAt(0);
    if (cp >= 0x20 && cp !== 0x7f) target.add(ch);
  }
}

/** 1) 设计稿静态文案 + 控件 → 字号映射（从 export 的 C 代码里抽）。 */
function parseExport() {
  const widgetSize = new Map();   // ui_clock_L_Time → 48
  const screenOf = new Map();     // 控件变量 → 屏名（变量名形如 ui_<屏>_<控件>）
  const staticChars = new Map(SIZES.map((s) => [s, new Set()]));
  const screensUsingSize = new Map(SIZES.map((s) => [s, new Set()]));

  for (const file of listFiles(EXPORT_DIR, ['.c'])) {
    const src = readFileSync(file, 'utf8');
    for (const m of src.matchAll(/lv_obj_set_style_text_font\s*\(\s*([A-Za-z0-9_]+)\s*,\s*ui_font_ui_heiti_(\d+)\s*,/g)) {
      const size = Number(m[2]);
      widgetSize.set(m[1], size);
      const screen = /^ui_([a-z0-9]+)_/.exec(m[1])?.[1];
      if (screen) { screenOf.set(m[1], screen); screensUsingSize.get(size)?.add(screen); }
    }
  }

  for (const file of listFiles(EXPORT_DIR, ['.c'])) {
    const src = readFileSync(file, 'utf8');
    for (const m of src.matchAll(/lv_label_set_text(?:_fmt|_static)?\s*\(\s*([A-Za-z0-9_]+)\s*,\s*"((?:[^"\\]|\\.)*)"/g)) {
      const size = widgetSize.get(m[1]);
      if (size === undefined) continue;
      addChars(staticChars.get(size), unescapeC(m[2]));
    }
  }
  return { widgetSize, staticChars, screensUsingSize };
}

/** 2) App 源码里的字符串字面量，按屏收集。 */
function parseAppChars() {
  const perScreen = new Map();
  for (const [screen, files] of Object.entries(SCREEN_SOURCES)) {
    const set = new Set();
    for (const rel of files) {
      const abs = path.join(REPO_ROOT, rel);
      if (!existsSync(abs)) { console.warn('  ! 缺源文件：' + rel); continue; }
      const src = readFileSync(abs, 'utf8')
        .replace(/\/\*[\s\S]*?\*\//g, ' ')   // 块注释
        .replace(/\/\/[^\n]*/g, ' ');        // 行注释
      for (const m of src.matchAll(/"((?:[^"\\]|\\.)*)"/g)) addChars(set, unescapeC(m[1]));
    }
    perScreen.set(screen, set);
  }
  return perScreen;
}

/** 组装每个字号的最终字符集。 */
function buildCharsets() {
  const { staticChars, screensUsingSize } = parseExport();
  const appChars = parseAppChars();

  const result = [];
  for (const size of SIZES) {
    const chars = new Set(staticChars.get(size) ?? []);
    if (TEXT_SIZES.has(size)) {
      for (const screen of screensUsingSize.get(size) ?? []) {
        for (const ch of appChars.get(screen) ?? []) chars.add(ch);
      }
    } else {
      for (const ch of BIG_SIZE_EXTRA[size] ?? []) chars.add(ch);
    }
    // 区间字符由 SIZE_RANGES 交给 lv_font_conv，这里不重复算，只为报告统计一下覆盖面。
    const cjk = [...chars].filter((c) => c.codePointAt(0) >= 0x7f).sort();
    result.push({ size, chars, ascii: [...chars].filter((c) => c.codePointAt(0) < 0x7f).sort(), cjk });
  }
  return result;
}

/**
 * 读回 lv_font_conv 产出的 BIN，抽出 cmap 覆盖的码点。
 * 本机没有 LVGL 的 lv_font_loader 可用，但 BIN 的布局是固定的（见 lv_font_loader.c）：
 *   [u32 段长（含 8 字节头）][4 字节 ASCII 标签][载荷] 依次排列，标签为 head/cmap/loca/glyf(/kern)。
 *   cmap 载荷：[u32 子表数][每条 16 字节：data_offset u32, range_start u32, range_length u16,
 *              glyph_id_start u16, data_entries u16, format u8, padding u8]
 *   format 0/2 = 连续表（range_start 起 range_length 个码点）；
 *   format 1/3 = 稀疏表，data_offset 处是 u16 列表，元素是「相对 range_start 的偏移」（不是绝对码点）。
 */
function readBinCmap(absPath) {
  const buf = readFileSync(absPath);
  const sections = {};
  let pos = 0;
  while (pos + 8 <= buf.length) {
    const len = buf.readUInt32LE(pos);
    const tag = buf.toString('ascii', pos + 4, pos + 8);
    sections[tag] = { start: pos, body: pos + 8 };
    if (len < 8) break;
    pos += len;
  }
  if (!sections.cmap) throw new Error('导出的 BIN 里没有 cmap 段：' + absPath);
  const cmap = sections.cmap.body;
  const count = buf.readUInt32LE(cmap);
  const cps = new Set();
  for (let i = 0; i < count; i += 1) {
    const entry = cmap + 4 + i * 16;
    const dataOffset = buf.readUInt32LE(entry);
    const rangeStart = buf.readUInt32LE(entry + 4);
    const rangeLength = buf.readUInt16LE(entry + 8);
    const dataEntries = buf.readUInt16LE(entry + 12);
    const format = buf[entry + 14];
    if (format === 0 || format === 2) {
      for (let k = 0; k < rangeLength; k += 1) cps.add(rangeStart + k);
    } else {
      const base = sections.cmap.start + dataOffset;
      for (let k = 0; k < dataEntries; k += 1) cps.add(rangeStart + buf.readUInt16LE(base + k * 2));
    }
  }
  return cps;
}

function shellQuote(arg) {
  return /[\s"']/.test(arg) || /[^\x20-\x7E]/.test(arg) ? '"' + arg + '"' : arg;
}

function generate(size, charSet) {
  const chars = [...charSet];
  const outRel = path.relative(REPO_ROOT, path.join(FONT_DIR, 'ui_font_ui_heiti_' + size + '.bin')).replace(/\\/g, '/');
  const args = ['npx', 'lv_font_conv', '--size', String(size), '--bpp', String(BPP), '--format', 'bin', '--no-compress', '--font', SOURCE_FONT];
  for (const range of SIZE_RANGES[size]) args.push('-r', range);
  const symbols = chars.filter((c) => c.codePointAt(0) > 0x7e || c === ' ' || c === '-' || c === ':' || c === '%' || c === '+' || c === '.' || c === '/').join('');
  if (symbols) args.push('--symbols', symbols);
  args.push('-o', outRel);

  const command = args.map(shellQuote).join(' ');
  // Windows 上 npx 是 npx.cmd：必须过 cmd.exe（同 tools/font/gen_font.mjs 的做法）。
  const res = spawnSync(command, { cwd: REPO_ROOT, shell: true, encoding: 'utf8' });
  const abs = path.join(REPO_ROOT, outRel);
  return {
    ok: res.status === 0 && existsSync(abs),
    command,
    abs,
    bytes: existsSync(abs) ? statSync(abs).size : 0,
    output: ((res.stdout || '') + (res.stderr || '')).trim(),
  };
}

/** 字库池上限（platform 侧 font_alloc_scope 的池子），超过就别再往里塞了。 */
const POOL_BUDGET_BYTES = 110 * 1024;

function main() {
  if (!existsSync(SOURCE_FONT)) throw new Error('源字体不存在：' + SOURCE_FONT);
  const charsets = buildCharsets();
  const totalPlanned = charsets.reduce((n, c) => n + c.chars.size, 0);
  console.log('源字体 : ' + path.relative(REPO_ROOT, SOURCE_FONT));
  console.log('输出目录: ' + path.relative(REPO_ROOT, FONT_DIR));
  console.log('字号    : ' + SIZES.join(' / ') + '（bpp ' + BPP + '、未压缩）');
  console.log('候选字符: ' + totalPlanned + ' 个（各字号合计，含 ASCII 骨架）');
  for (const c of charsets) {
    if (DRY_RUN) {
      console.log('  ' + String(c.size).padStart(2) + 'px  中文 ' + String(c.cjk.length).padStart(3) + ' 个 · ASCII ' + String(c.ascii.length).padStart(3) + ' 个  ' + c.cjk.join(''));
    }
  }
  if (DRY_RUN) { console.log('（--dry-run：没有写文件）'); return; }

  let total = 0;
  const failed = [];
  const missingChars = [];
  for (const c of charsets) {
    const r = generate(c.size, c.chars);
    if (!r.ok) { failed.push({ size: c.size, output: r.output }); console.log('  ✗ ' + c.size + 'px 生成失败'); continue; }
    total += r.bytes;
    console.log('  ✓ ' + String(c.size).padStart(2) + 'px  ' + String(r.bytes).padStart(6) + ' B  中文 ' + String(c.cjk.length).padStart(3) + ' 个');

    // 覆盖自检：源字体里没有的字，lv_font_conv 只是"跳过"不报错，运行期就该画方框了。
    // 所以生成后必须把 BIN 读回来，逐个核对打算放进去的非 ASCII 字是不是真的进了 cmap。
    const cps = readBinCmap(r.abs);
    const absent = [...c.chars].filter((ch) => ch.codePointAt(0) >= 0x7f && !cps.has(ch.codePointAt(0)));
    if (absent.length > 0) {
      missingChars.push({ size: c.size, absent });
      console.log('      ✗ 源字体里缺这些字（会画成方框）：' + absent.join(''));
    }
  }
  if (failed.length > 0) {
    for (const f of failed) console.error(f.size + 'px:\n' + f.output);
    throw new Error('有 ' + failed.length + ' 个字号生成失败');
  }
  console.log('合计 : ' + total + ' B（字库池预算 ' + POOL_BUDGET_BYTES + ' B，余量 ' + (POOL_BUDGET_BYTES - total) + ' B）');
  if (total > POOL_BUDGET_BYTES) throw new Error('字库合计 ' + total + ' B 超过池预算 ' + POOL_BUDGET_BYTES + ' B，请收窄字符集');
  if (missingChars.length > 0) {
    for (const m of missingChars) console.error('  ' + m.size + 'px 缺字：' + m.absent.join(''));
    throw new Error('源字体 ' + path.relative(REPO_ROOT, SOURCE_FONT) + ' 缺少上面这些字：换成含它们的字体，或从字符集里去掉');
  }
  console.log('自检 : 每个字号的 cmap 都覆盖了计划内的全部字符');
}

main();
