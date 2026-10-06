#!/usr/bin/env node
/**
 * gen_font.mjs —— 生成 Embark 的 LVGL 静态子集字库（C 数组）+ 覆盖清单
 * =====================================================================
 * 产物（两个，都在仓库内，可重复生成、幂等覆盖）：
 *   1. assets/fonts/embark_zh_14.c   —— lv_font_conv 生成的 LVGL C 字体
 *   2. tools/font/embark_zh_14.json  —— 覆盖清单，issue 17「缺字审计」的唯一依据
 *
 * 用法（仓库根执行）：
 *   node tools/font/gen_font.mjs
 *
 * ---------------------------------------------------------------------
 * 为什么是这些参数
 * ---------------------------------------------------------------------
 * 目标 LVGL 是 third_party/lvgl/ 内树（8.3.11，见 third_party/lvgl/lvgl.h:16-18）。
 * 参考样板是同仓库生成的 third_party/lvgl/src/font/lv_font_montserrat_14.c，
 * 本脚本的参数与它同类。
 *
 * --size 14 / --bpp 4
 *     LVGL 内置默认字体就是 montserrat_14 / bpp4，同字号同色深；
 *     16 位 RGB565 屏上 4bpp（16 级灰阶）抗锯齿足够，也是 LVGL 压缩位图支持的 bpp。
 *
 * --format lvgl
 *     生成 lv_font_fmt_txt_dsc_t 结构（LVGL 原生格式），而不是 bin/dump。
 *
 * 压缩（默认开启，故意不传 --no-compress）
 *     lv_font_conv 默认对每条扫描线做 XOR 预滤波再 RLE，写进 .c 的是
 *     .bitmap_format = 1（LV_FONT_FMT_TXT_COMPRESSED，见
 *     third_party/lvgl/src/font/lv_font_fmt_txt.h:148-152）。
 *     LVGL 8.3.11 的 third_party/lvgl/src/font/lv_font_fmt_txt.c:89-133 正好按
 *     bitmap_format 分流，1/2 走 decompress()，所以压缩格式与本版本兼容；
 *     但要求宿主 lv_conf.h 里 LV_USE_FONT_COMPRESSED = 1，否则运行时只会
 *     LV_LOG_WARN 并返回 NULL（lv_font_fmt_txt.c:130-132）。
 *     注意：本脚本不改 lv_conf.h（写入范围受限），这一项由调用方负责。
 *
 * --lv-include lvgl.h
 *     生成的 .c 里 include 是双分支：
 *       #ifdef LV_LVGL_H_INCLUDE_SIMPLE  ->  #include "lvgl.h"
 *       #else                            ->  #include "<--lv-include 的值>"
 *     本工程打开 LV_LVGL_H_INCLUDE_SIMPLE=ON（见 cmake/embark_middleware.cmake:23
 *     与 build/CMakeCache.txt:353），include 根就是 third_party/lvgl 仓库根，
 *     应用侧一律 #include <lvgl.h>（app/<app名>/ 各 App 头、platform/common/lvgl_port.h:32）。
 *     所以这里传 lvgl.h（不是 lvgl/lvgl.h），保证两条分支都对。
 *
 * --lv-font-name embark_zh_14
 *     显式钉住 C 符号名。虽然 -o 的 basename 已经是 embark_zh_14，
 *     显式传参后即使改输出路径，符号名也不会漂。
 *
 * --font <CJK 源字体> -r 0x20-0x7F --symbols <14 个界面汉字>
 * --font <FontAwesome woff> -r 0xF013,...
 *     lv_font_conv 的 -r / --symbols 属于「前一个 --font」，多字体多段是合法写法
 *     （见 lv_font_conv/lib/cli.js:24-37 的 ActionFontRangeAdd，以及 montserrat_14
 *     文件头 Opts 行的同样写法）。每个源字体后面必须至少跟一段 -r/--symbols，
 *     否则 lv_font_conv/lib/cli.js:293-296 直接报错退出。
 *
 * 源字体选择
 *     C:\Windows\Fonts\NotoSansSC-VF.ttf 优先：Noto Sans SC 是 OFL 许可，
 *     可自由分发/嵌入；而 simhei / Deng / msyh 都是 Windows 随附的专有字体，
 *     随仓库分发有授权问题。该文件虽然是可变字体（VF），但 lv_font_conv 底层走
 *     FreeType，取默认实例（wght=400 Regular），实测生成正常、无缺字。
 *     备选顺序 simhei.ttf -> Deng.ttf -> msyh.ttc，只在首选不可用/报错时启用；
 *     .ttc 是字体集合，解析器可能要求 "文件#索引" 写法，代码里会自动重试 #0。
 *
 * FontAwesome5-Solid+Brands+Regular.woff 的路径
 *     lv_font_conv 并不自带这份 woff（包里没有任何 .woff/.ttf 资源），
 *     montserrat_14 的 Opts 行里那个名字只是"文件名"写法，实际必须给路径。
 *     这里用 LVGL 内树自带的那份：
 *       third_party/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff
 *     好处是不引入仓库外依赖，且与 LVGL 上游生成内置字体用的是同一份文件。
 *
 * ---------------------------------------------------------------------
 * 文本写入说明
 * ---------------------------------------------------------------------
 * 本仓库禁的是 PowerShell 5.1 的 Set-Content / Out-File / > 重定向（会把 UTF-8
 * 转成 GBK 并加 BOM）。本脚本是 Node：.c 由 lv_font_conv 自己写，.json 用
 * fs.writeFileSync(..., 'utf8') 写，不带 BOM。末尾会断言两个文件都没有 BOM。
 */

/*
 * ---------------------------------------------------------------------
 * alpha 提升（后处理）
 * ---------------------------------------------------------------------
 * lv_font_conv v1.5.3 底层用 opentype.js 光栅化（非 FreeType），14px 小字号下
 * 细笔画（尤其是 CJK）只覆盖 ~40%：实测「时」字形 alpha 直方图主体 = 6/15、
 * 最大 = 13/15，显示出来明显偏淡；而 FreeType（Pillow）同字号实测主体 ≈ 11/15、
 * 最大 15/15（实心）。
 * 本脚本在 lv_font_conv 输出后对 4bpp 位图做线性提升 v' = min(15, round(v*GAIN))，
 * 保持边缘抗锯齿梯度、把笔画主体抬到接近 15，逼近 FreeType 效果，同时继续用
 * Noto Sans SC（OFL，可随仓库分发；simhei/msyh 的实心效果来自专有字体，不用）。
 * 字形几何（adv_w / box / ofs / kerning / cmap）一概不动，只重写 glyph_bitmap[]
 * 载荷，并用与 third_party/lvgl/src/font/lv_font_fmt_txt.c:514-572 解码状态机
 * 逐位对应的编码器（预滤波 XOR + RLE）回写；回写后对每个字形做
 * 「解码(编码(提升后)) == 提升后」回环断言，不满足直接抛错。
 * 开关：ALPHA_GAIN = 0 关闭提升。
 * ---------------------------------------------------------------------
 */
import { spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

// ---------------------------------------------------------------- 路径锚点

/** 本文件所在目录：<repo>/tools/font */
const HERE = path.dirname(fileURLToPath(import.meta.url));
/** 仓库根：<repo>。所有仓库相对路径都相对它解析。 */
const REPO_ROOT = path.resolve(HERE, '..', '..');

// ---------------------------------------------------------------- 参数（全部写死）

/** 输出①：LVGL C 字体。仓库相对路径，同时作为 -o 的实参。 */
const OUTPUT_C = 'assets/fonts/embark_zh_14.c';
/** 输出②：覆盖清单。仓库相对路径。 */
const OUTPUT_JSON = 'tools/font/embark_zh_14.json';

/** C 符号名：生成 const lv_font_t embark_zh_14。 */
const SYMBOL = 'embark_zh_14';
/** 字号（px）。与内置 montserrat_14 对齐。 */
const SIZE = 14;
/** 每像素位数。4 = 16 级灰阶，LVGL 压缩位图支持的 bpp。 */
const BPP = 4;
/** 是否压缩。true => 不传 --no-compress，生成 .bitmap_format = 1。 */
const COMPRESS = true;

/** alpha 提升增益（0 = 关闭）。见头注释「alpha 提升（后处理）」。 */
const ALPHA_GAIN = 2.4;

/**
 * 传给 lv_font_conv 的 ASCII 范围：0x20(空格)..0x7F。
 *
 * 注意：U+007F 是 DEL（删除控制符），不是可打印字符，任何字体里都没有它的字形。
 * lv_font_conv 不会因此报错，而是把生成的 cmap 收窄到 0x20..0x7E：
 * 实测输出 .range_start = 32, .range_length = 95（32+95-1 = 126 = 0x7E）。
 * LVGL 上游自己的 lv_font_montserrat_14.c 用的也是同一条 -r 0x20-0x7F，
 * 它同样只产出 0x20..0x7E 共 95 个字形。所以本字库的 ASCII 实际覆盖是 95 个，
 * 总字形数 95 + 14 + 5 = 114（见 EXPECTED_CODEPOINTS）。
 */
const ASCII_RANGE = '0x20-0x7F';
/** 界面文案（CJK，14 字）：启动器 / 时钟 / 设置 / 心跳 / 你好 / 悬 / 任务。 */
const UI_TEXT = '启动器时钟设置心跳你好悬任务';
/** FontAwesome 码点（5 个）：齿轮 / 房子 / 刷新 / 雪佛龙左 / 箭头右。 */
const ICON_SYMBOLS = ['0xF013', '0xF015', '0xF021', '0xF053', '0xF079'];

/** 首选 CJK 源字体：OFL 许可，可自由分发。 */
const SOURCE_FONT = 'C:\\Windows\\Fonts\\NotoSansSC-VF.ttf';
/** 备选 CJK 源字体：Windows 专有，仅在首选不可用/报错时按序启用。 */
const SOURCE_FONT_FALLBACKS = [
  'C:\\Windows\\Fonts\\simhei.ttf',
  'C:\\Windows\\Fonts\\Deng.ttf',
  'C:\\Windows\\Fonts\\msyh.ttc',
];

/** 图标字体：LVGL 内树自带，仓库相对路径（npx 的 cwd 是仓库根）。 */
const ICON_FONT = 'third_party/lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff';
/** .c 里 #include 的路径（非 LV_LVGL_H_INCLUDE_SIMPLE 分支）。 */
const LV_INCLUDE = 'lvgl.h';

// ---------------------------------------------------------------- 期望字符集

/**
 * 实际能产出字形的 ASCII：0x20..0x7E，共 95 个。
 * 上界取 0x7E('~') 而不是 0x7F：0x7F 是 DEL，没有字形（见 ASCII_RANGE 注释）。
 * 这就是覆盖清单 chars.ascii 的内容——审计时可以直接 chars.ascii.includes(c)。
 */
const ASCII_CHARS = Array.from({ length: 0x7f - 0x20 }, (_, i) => String.fromCharCode(0x20 + i)).join('');

/** 期望覆盖的全部码点，顺序：ASCII -> CJK -> FontAwesome。 */
const EXPECTED_CODEPOINTS = [
  ...Array.from({ length: 0x7f - 0x20 }, (_, i) => 0x20 + i),
  ...[...UI_TEXT].map((c) => c.codePointAt(0)),
  ...ICON_SYMBOLS.map((s) => Number.parseInt(s, 16)),
];
/** 期望字形数：95 + 14 + 5 = 114。 */
const EXPECTED_GLYPH_COUNT = EXPECTED_CODEPOINTS.length;

// ---------------------------------------------------------------- 执行 lv_font_conv

/**
 * 为记录到清单里的"命令行原文"做引号处理。
 * 只对含空白/引号/非 ASCII 的实参加双引号——这样 cmd 和 PowerShell 都能直接粘贴执行。
 */
function shellQuote(arg) {
  return /[\s"']/.test(arg) || /[^\x20-\x7E]/.test(arg) ? '"' + arg + '"' : arg;
}

/** 组装 argv（不含 npx 本身）。顺序即最终命令行的顺序。 */
function buildArgs(sourceFont, outputRel) {
  const args = [
    'lv_font_conv',
    '--size', String(SIZE),
    '--bpp', String(BPP),
    '--format', 'lvgl',
    // 第 1 个源字体：CJK + ASCII
    '--font', sourceFont,
    '-r', ASCII_RANGE,
    '--symbols', UI_TEXT,
    // 第 2 个源字体：FontAwesome 图标（-r 属于它自己）
    '--font', ICON_FONT,
    '-r', ICON_SYMBOLS.join(','),
    '--lv-include', LV_INCLUDE,
    '--lv-font-name', SYMBOL,
    '-o', outputRel,
  ];
  // COMPRESS=true 时刻意不追加 --no-compress（压缩是 lv_font_conv 的默认行为）。
  if (!COMPRESS) args.push('--no-compress');
  return args;
}

/** 调一次 npx lv_font_conv，返回是否成功 + 命令行原文 + 原始输出。 */
function runLvFontConv(sourceFont, outputRel) {
  const args = buildArgs(sourceFont, outputRel);
  const command = ['npx', ...args].map(shellQuote).join(' ');

  // Windows 上 npx 实际是 npx.cmd，execFileSync 直接调用会 EINVAL；
  // 必须交给 cmd.exe 解析。这里传"单个命令字符串 + shell:true"而不是
  // "args 数组 + shell:true"：后者在 Node 24 会触发 DEP0190 弃用警告
  // （args 只做拼接、不做转义），两者语义相同但前者不吵。
  const res = spawnSync(command, { cwd: REPO_ROOT, shell: true, encoding: 'utf8' });

  const outPath = path.join(REPO_ROOT, outputRel);
  const ok = res.status === 0 && existsSync(outPath);
  return { ok, command, stdout: res.stdout || '', stderr: res.stderr || '' };
}

/** 按优先级挑源字体并生成；返回实际用上的字体 + 命令行原文 + 尝试轨迹。 */
function generate() {
  const attempts = [];
  const candidates = [SOURCE_FONT, ...SOURCE_FONT_FALLBACKS];

  for (const raw of candidates) {
    // .ttc 是字体集合，先按原样试；失败再试 "文件#0"。
    const tries = /\.ttc$/i.test(raw) ? [raw, raw + '#0'] : [raw];

    for (const font of tries) {
      const probePath = font.split('#')[0];
      if (!existsSync(probePath)) {
        attempts.push({ font, ok: false, why: 'not found' });
        continue;
      }

      const r = runLvFontConv(font, OUTPUT_C);
      const why = r.ok
        ? 'ok'
        : (r.stderr.trim().split('\n').filter(Boolean).pop() || 'lv_font_conv failed');
      attempts.push({ font, ok: r.ok, why });

      if (r.ok) return { sourceFont: font, command: r.command, attempts };
    }
  }

  throw new Error(
    'no usable source font; attempts:\n' +
      attempts.map((a) => '  - ' + a.font + ' :: ' + a.why).join('\n'),
  );
}

// ---------------------------------------------------------------- 校验

/** 解析 .c 里的 cmaps[] 表体，拿到 range_start / range_length / list_length / type。 */
function parseCmaps(src) {
  const head = src.indexOf('static const lv_font_fmt_txt_cmap_t cmaps[]');
  if (head < 0) return [];
  const body = src.slice(head, src.indexOf('};', head));
  const cmaps = [];
  const re =
    /\.range_start\s*=\s*(\d+),\s*\.range_length\s*=\s*(\d+),\s*\.glyph_id_start\s*=\s*(\d+),\s*\.unicode_list\s*=\s*(\w+|NULL),\s*\.glyph_id_ofs_list\s*=\s*(\w+|NULL),\s*\.list_length\s*=\s*(\d+),\s*\.type\s*=\s*(\w+)/g;
  for (const m of body.matchAll(re)) {
    cmaps.push({
      rangeStart: Number(m[1]),
      rangeLength: Number(m[2]),
      glyphIdStart: Number(m[3]),
      unicodeList: m[4],
      listLength: Number(m[6]),
      type: m[7],
    });
  }
  return cmaps;
}

/** 校验生成的 .c。返回 { checks, facts }，任一 check 失败即抛错。 */
function verify(cRel) {
  const cPath = path.join(REPO_ROOT, cRel);
  const raw = readFileSync(cPath);
  const src = raw.toString('utf8');
  const checks = [];
  const check = (name, pass, detail) => {
    checks.push({ name, pass, detail: detail || '' });
    return pass;
  };

  // 1) 无 BOM（PowerShell 写法的典型污染）
  const hasBom = raw[0] === 0xef && raw[1] === 0xbb && raw[2] === 0xbf;
  check('no UTF-8 BOM', !hasBom, 'first3=0x' + [...raw.slice(0, 3)].map((b) => b.toString(16)).join(' '));

  // 2) 符号名与头文件护栏
  check('has symbol ' + SYMBOL, src.includes('lv_font_t ' + SYMBOL + ' = {'));
  check('has guard ' + SYMBOL.toUpperCase(), src.includes('#ifndef ' + SYMBOL.toUpperCase()));

  // 3) 压缩位图相关结构
  check(
    'compressed bitmap_format = 1',
    /\.bitmap_format\s*=\s*1\b/.test(src),
    'LV_FONT_FMT_TXT_COMPRESSED',
  );
  check(
    'compressed glyph cache struct',
    src.includes('lv_font_fmt_txt_glyph_cache_t cache;') && src.includes('.cache = &cache'),
    'LVGL 8 解压缓存（lv_font_fmt_txt.h:154-157）',
  );

  // 4) 关键字段
  check('.bpp = ' + BPP, new RegExp('\\.bpp\\s*=\\s*' + BPP + '\\b').test(src));
  check('Size: ' + SIZE + ' px', src.includes('Size: ' + SIZE + ' px'));
  check('#include "' + LV_INCLUDE + '"', src.includes('#include "' + LV_INCLUDE + '"'));

  // 5) 8.3.11 不认识的字段 / 大字体护栏
  check('no LV_FONT_FMT_TXT_LARGE guard', !src.includes('#  error'));

  // 6) 字形统计：lv_font_conv 1.5.3 的输出不含 /* U+XXXX */ 注释，
  //    字形清单以 cmaps（FORMAT0 范围 + SPARSE 列表）为准。
  const cmaps = parseCmaps(src);
  const glyphSet = new Set();
  for (const cm of cmaps) {
    if (cm.type.includes('FORMAT0')) {
      for (let i = 0; i < cm.rangeLength; i++) glyphSet.add(cm.rangeStart + i);
    } else if (cm.unicodeList !== 'NULL') {
      const listBody = src.slice(
        src.indexOf(cm.unicodeList + '[] = {'),
        src.indexOf('};', src.indexOf(cm.unicodeList + '[] = {')),
      );
      for (const v of listBody.matchAll(/0x[0-9a-fA-F]+/g)) glyphSet.add(cm.rangeStart + Number.parseInt(v[0], 16));
    }
  }
  const glyphCodepoints = [...glyphSet];
  const expectedSet = new Set(EXPECTED_CODEPOINTS);
  const missing = [...expectedSet].filter((c) => !glyphSet.has(c)).map((c) => 'U+' + c.toString(16).toUpperCase());
  const extra = [...glyphSet].filter((c) => !expectedSet.has(c)).map((c) => 'U+' + c.toString(16).toUpperCase());

  check(
    'glyph count = ' + EXPECTED_GLYPH_COUNT,
    glyphCodepoints.length === EXPECTED_GLYPH_COUNT,
    'actual=' + glyphCodepoints.length,
  );
  check('no missing glyph', missing.length === 0, missing.join(','));
  check('no extra glyph', extra.length === 0, extra.join(','));

  // 7) glyph_dsc 条目 = 字形数 + 1（id 0 保留给"找不到"）
  const dscEntries = (src.match(/\.bitmap_index\s*=/g) || []).length;
  check(
    'glyph_dsc entries = ' + (EXPECTED_GLYPH_COUNT + 1),
    dscEntries === EXPECTED_GLYPH_COUNT + 1,
    'actual=' + dscEntries,
  );

  // 8) cmaps：ASCII 走 FORMAT0_TINY（unicode_list = NULL，覆盖 = range_length），
  //    非 ASCII 走 SPARSE_TINY（覆盖 = list_length）。
  const covered = cmaps.reduce(
    (n, c) => n + (c.type.includes('FORMAT0') ? c.rangeLength : c.listLength),
    0,
  );
  const sparseLists = [...src.matchAll(/unicode_list_(\d+)\[\]\s*=\s*\{([\s\S]*?)\};/g)].map((m) => ({
    name: 'unicode_list_' + m[1],
    length: (m[2].match(/0x[0-9a-fA-F]+/g) || []).length,
  }));
  check('cmap covered codepoints = ' + EXPECTED_GLYPH_COUNT, covered === EXPECTED_GLYPH_COUNT, 'actual=' + covered);

  // 9) 语法自检（可选）：拿内树 LVGL 头 + 工程 lv_conf.h 真编一次。
  let syntaxCheck = 'skipped (gcc not found)';
  const gcc = spawnSync('gcc', ['--version'], { encoding: 'utf8', shell: true });
  if (gcc.status === 0) {
    const g = spawnSync(
      'gcc',
      [
        '-fsyntax-only', '-std=c99',
        '-I', path.join(REPO_ROOT, 'third_party', 'lvgl'),
        '-I', path.join(REPO_ROOT, 'config'),
        '-DLV_LVGL_H_INCLUDE_SIMPLE', '-DLV_CONF_INCLUDE_SIMPLE',
        cPath,
      ],
      { encoding: 'utf8', shell: true },
    );
    syntaxCheck = g.status === 0 ? 'gcc -fsyntax-only OK' : 'FAILED: ' + (g.stderr || '').trim().split('\n')[0];
  }
  check('gcc -fsyntax-only', !syntaxCheck.startsWith('FAILED'), syntaxCheck);

  const failed = checks.filter((c) => !c.pass);
  if (failed.length) {
    throw new Error(
      'font verification failed:\n' +
        failed.map((c) => '  - ' + c.name + (c.detail ? ' (' + c.detail + ')' : '')).join('\n'),
    );
  }

  return {
    checks,
    facts: {
      bytes: raw.length,
      glyphCount: glyphCodepoints.length,
      glyphDscEntries: dscEntries,
      cmaps,
      sparseLists,
      coveredCodepoints: covered,
      syntaxCheck,
    },
  };
}

// ---------------------------------------------------------------- alpha 提升

/** 位读取器：大端位序，与 lv_font_fmt_txt.c 的 get_bits 一致（MSB first）。 */
function makeBitReader(bytes) {
  return {
    bytes,
    pos: 0,
    read(len) {
      let v = 0;
      for (let i = 0; i < len; i++) {
        const bit = this.pos + i;
        const b = this.bytes[bit >> 3];
        v = (v << 1) | ((b >> (7 - (bit & 7))) & 1);
      }
      this.pos += len;
      return v;
    },
  };
}

/**
 * RLE 解码（一 glyph，不做预滤波 XOR）。
 * 与 lv_font_fmt_txt.c:514-572（rle_next 状态机）逐位对应：
 * SINGLE 读到与上一值相同的 bpp 值 -> REPEATE(cnt=0)；REPEATE 读 1 位：
 *   1 -> 输出上一值，cnt++，cnt==11 时读 6 位长计数 -> COUNTER
 *       （6 位 = 0 时改读新 bpp 值回 SINGLE）；0 -> 读新 bpp 值回 SINGLE；
 * COUNTER 输出上一值 cnt 次，减到 0 读新 bpp 值回 SINGLE。
 */
function rleDecode(rd, bpp, w, h) {
  const vals = [];
  let state = 'SINGLE';
  let prev = 0;
  let cnt = 0;
  for (let i = 0; i < w * h; i++) {
    let v;
    if (state === 'SINGLE') {
      v = rd.read(bpp);
      if (i !== 0 && v === prev) { cnt = 0; state = 'REPEATE'; }
    } else if (state === 'REPEATE') {
      const b = rd.read(1);
      cnt += 1;
      if (b === 1) {
        v = prev;
        if (cnt === 11) {
          cnt = rd.read(6);
          if (cnt !== 0) state = 'COUNTER';
          else { v = rd.read(bpp); state = 'SINGLE'; }
        }
      } else {
        v = rd.read(bpp);
        state = 'SINGLE';
      }
    } else {
      v = prev;
      cnt -= 1;
      if (cnt === 0) { v = rd.read(bpp); state = 'SINGLE'; }
    }
    prev = v;
    vals.push(v);
  }
  return vals;
}

/**
 * RLE 编码（一 glyph，不做预滤波 XOR），rleDecode 的逆：
 * 首像素写 bpp 位；之后：与上一值相同 -> REPEATE 写 1 位，连续 11 个后
 * 剩余 run 写 6 位长计数（0 = 不启用，直接读新值）；
 * 不同 -> 写 0 位 + 新值 bpp 位。长 run（>12）靠计数段 0 位代价续写，
 * 解码端在计数段结束后必然回 SINGLE 读新值（可能 == 上一值 -> 再进 REPEATE）。
 */
function rleEncode(values, bpp) {
  const bits = [];
  const pushBits = (v, len) => {
    for (let k = len - 1; k >= 0; k--) bits.push((v >> k) & 1);
  };
  const n = values.length;
  let state = 'SINGLE';
  let prev = -1;
  let cnt = 0;
  for (let i = 0; i < n; i++) {
    const v = values[i];
    if (state === 'SINGLE') {
      pushBits(v, bpp);
      if (i !== 0 && v === prev) {
        cnt = 0;
        state = 'REPEATE';
      } else prev = v;
    } else {
      if (v === prev) {
        cnt += 1;
        bits.push(1);
        if (cnt === 11) {
          let R = 0;
          while (i + 1 + R < n && values[i + 1 + R] === prev) R++;
          const c = Math.min(R + 1, 63);
          pushBits(c, 6);
          if (c > 1) i += c - 1;
          state = 'SINGLE';
        }
      } else {
        bits.push(0);
        pushBits(v, bpp);
        prev = v;
        state = 'SINGLE';
        cnt = 0;
      }
    }
  }
  const bytes = new Uint8Array(Math.ceil(bits.length / 8));
  for (let i = 0; i < bits.length; i++) bytes[i >> 3] |= (bits[i] & 1) << (7 - (i & 7));
  return bytes;
}

/** 解码一 glyph 的完整 4bpp 值序列（format 1 时含预滤波 XOR 还原）。 */
function decodeGlyph(bytes, bi, bpp, w, h, prefilter) {
  const rd = makeBitReader(bytes.subarray(bi));
  const vals = rleDecode(rd, bpp, w, h);
  if (prefilter) {
    for (let y = 1; y < h; y++) {
      for (let x = 0; x < w; x++) vals[y * w + x] ^= vals[(y - 1) * w + x];
    }
  }
  return vals;
}

/** 编码一 glyph：format 1 先做行间 XOR 预滤波再 RLE（0 不做）。 */
function encodeGlyph(vals, bpp, w, h, prefilter) {
  let lines = vals;
  if (prefilter) {
    lines = vals.slice();
    for (let y = h - 1; y >= 1; y--) {
      for (let x = 0; x < w; x++) lines[y * w + x] ^= lines[(y - 1) * w + x];
    }
  }
  return rleEncode(lines, bpp);
}

/** 线性提升：保持 0，其余 min(15, round(v * GAIN))。 */
function boostValue(v) {
  return v === 0 ? 0 : Math.min(15, Math.round(v * ALPHA_GAIN));
}

/**
 * 对生成的 .c 做 alpha 提升并回写：只重写 glyph_bitmap[] 载荷，
 * 保持 glyph_dsc / unicode_list / cmaps / font_dsc 全部不动。
 * 每个字形做完「解码(编码(提升后)) == 提升后」回环断言。
 */
function boostAlphaInPlace(cRel) {
  const cPath = path.join(REPO_ROOT, cRel);
  let srcText = readFileSync(cPath, 'utf8');

  const bppM = srcText.match(/\.bpp\s*=\s*(\d+)/);
  const bpp = bppM ? Number(bppM[1]) : BPP;
  const fmtM = srcText.match(/\.bitmap_format\s*=\s*(\d+)/);
  const fmt = fmtM ? Number(fmtM[1]) : 1;
  const prefilter = fmt === 1;
  if (bpp !== 4) throw new Error('alpha boost supports bpp=4 only, got ' + bpp);

  const gdHead = srcText.indexOf('glyph_dsc[] = {');
  if (gdHead < 0) throw new Error('glyph_dsc[] not found');
  const gdBody = srcText.slice(gdHead, srcText.indexOf('};', gdHead));
  const glyphs = [...gdBody.matchAll(
    /\.bitmap_index\s*=\s*(\d+),\s*\.adv_w\s*=\s*\d+,\s*\.box_w\s*=\s*(\d+),\s*\.box_h\s*=\s*(\d+)/g,
  )].map((m) => ({ bi: Number(m[1]), w: Number(m[2]), h: Number(m[3]) }));

  const bHead = srcText.indexOf('glyph_bitmap[] = {');
  if (bHead < 0) throw new Error('glyph_bitmap[] not found');
  const bStart = srcText.indexOf('{', bHead) + 1;
  const bEnd = srcText.indexOf('};', bStart);
  const oldBytes = Uint8Array.from(
    (srcText.slice(bStart, bEnd).match(/0x[0-9a-fA-F]{1,2}/g) || []).map((t) => parseInt(t, 16)),
  );

  let oldTotal = 0;
  let newTotal = 0;
  let changedGlyphs = 0;
  let boostedPixels = 0;
  const parts = [];
  for (const g of glyphs) {
    const vals = decodeGlyph(oldBytes, g.bi, bpp, g.w, g.h, prefilter);
    const boosted = vals.map(boostValue);
    for (let i = 0; i < vals.length; i++) if (boosted[i] !== vals[i]) boostedPixels++;
    const enc = encodeGlyph(boosted, bpp, g.w, g.h, prefilter);
    const re = decodeGlyph(enc, 0, bpp, g.w, g.h, prefilter);
    for (let i = 0; i < boosted.length; i++) {
      if (re[i] !== boosted[i]) {
        throw new Error('round-trip mismatch at glyph bitmap_index=' + g.bi + ' px=' + i);
      }
    }
    let differs = false;
    for (let i = 0; i < vals.length; i++) if (boosted[i] !== vals[i]) { differs = true; break; }
    if (differs) changedGlyphs++;
    parts.push(enc);
    newTotal += enc.length;
  }

  // 原始子段总长：按 glyph 顺序，从下一个 glyph 的 bitmap_index 减当前。
  const segLen = (i) => (i + 1 < glyphs.length ? glyphs[i + 1].bi - glyphs[i].bi : oldBytes.length - glyphs[i].bi);
  oldTotal = glyphs.reduce((a, _g, i) => a + segLen(i), 0);

  const newBytesTotal = parts.reduce((a, b) => a + b.length, 0);
  const fmtHex = (arr) => {
    const rows = [];
    for (let i = 0; i < arr.length; i += 12) {
      rows.push('  ' + [...arr.slice(i, i + 12)].map((b) => '0x' + b.toString(16).padStart(2, '0')).join(', '));
    }
    return rows.join(',\n');
  };
  srcText = srcText.slice(0, bStart) + '\n' + fmtHex(parts.flatMap((p) => [...p])) + '\n' + srcText.slice(bEnd);
  writeFileSync(cPath, srcText, 'utf8');

  return {
    bpp,
    format: fmt,
    prefilter,
    glyphs: glyphs.length,
    changedGlyphs,
    boostedPixels,
    bytesBefore: oldTotal,
    bytesAfter: newBytesTotal,
  };
}

// ---------------------------------------------------------------- 覆盖清单

function writeManifest(sourceFont, command, boostInfo) {
  const manifest = {
    fontFile: OUTPUT_C,
    symbol: SYMBOL,
    size: SIZE,
    bpp: BPP,
    compress: COMPRESS,
    sourceFont,
    chars: {
      ascii: ASCII_CHARS,
      uiText: UI_TEXT,
      symbols: ICON_SYMBOLS,
    },
    command,
    boost: ALPHA_GAIN > 0
      ? { gain: ALPHA_GAIN, glyphs: boostInfo.glyphs, changedGlyphs: boostInfo.changedGlyphs, boostedPixels: boostInfo.boostedPixels }
      : null,
  };
  const jsonPath = path.join(REPO_ROOT, OUTPUT_JSON);
  writeFileSync(jsonPath, JSON.stringify(manifest, null, 2) + '\n', 'utf8');
  return manifest;
}

// ---------------------------------------------------------------- main

function main() {
  mkdirSync(path.join(REPO_ROOT, path.dirname(OUTPUT_C)), { recursive: true });
  mkdirSync(path.join(REPO_ROOT, path.dirname(OUTPUT_JSON)), { recursive: true });

  const { sourceFont, command, attempts } = generate();
  if (attempts.length > 1) {
    console.log('[gen_font] source-font attempts:');
    for (const a of attempts) console.log('  - ' + a.font + ' :: ' + a.why);
  }

  const boostInfo = ALPHA_GAIN > 0 ? boostAlphaInPlace(OUTPUT_C) : {
    glyphs: 0,
    changedGlyphs: 0,
    boostedPixels: 0,
    bytesBefore: 0,
    bytesAfter: 0,
    format: -1,
    prefilter: false,
  };
  const { checks, facts } = verify(OUTPUT_C);
  const manifest = writeManifest(sourceFont, command, boostInfo);

  // 清单自身也要是干净 UTF-8、无 BOM、且能被读回。
  const jsonRaw = readFileSync(path.join(REPO_ROOT, OUTPUT_JSON));
  const bom = jsonRaw[0] === 0xef && jsonRaw[1] === 0xbb && jsonRaw[2] === 0xbf;
  if (bom) throw new Error(OUTPUT_JSON + ' has a UTF-8 BOM');
  const reread = JSON.parse(jsonRaw.toString('utf8'));
  if (reread.chars.uiText !== UI_TEXT) {
    throw new Error('manifest uiText round-trip mismatch: ' + reread.chars.uiText);
  }

  console.log('');
  console.log('[gen_font] source font : ' + sourceFont);
  console.log('[gen_font] command     : ' + command);
  console.log('[gen_font] font file   : ' + OUTPUT_C + '  ' + facts.bytes + ' bytes');
  console.log('[gen_font] manifest    : ' + OUTPUT_JSON + '  ' + jsonRaw.length + ' bytes');
  console.log(
    '[gen_font] glyphs      : ' + facts.glyphCount +
      ' (ascii 0x20-0x7E ' + ASCII_CHARS.length + ' + uiText ' + [...UI_TEXT].length +
      ' + icons ' + ICON_SYMBOLS.length + '), glyph_dsc entries ' + facts.glyphDscEntries,
  );
  console.log(
    '[gen_font] cmaps       : ' + facts.cmaps.length + ', covered codepoints ' + facts.coveredCodepoints,
  );
  for (const c of facts.cmaps) {
    console.log(
      '             - range 0x' + c.rangeStart.toString(16).toUpperCase() +
        '+0x' + c.rangeLength.toString(16).toUpperCase() +
        ' type=' + c.type + ' list_length=' + c.listLength,
    );
  }
  for (const s of facts.sparseLists) {
    console.log('             - ' + s.name + ' = ' + s.length + ' entries');
  }
  console.log('[gen_font] compress    : ' + (COMPRESS ? 'on (.bitmap_format = 1)' : 'off (.bitmap_format = 0)'));
  if (ALPHA_GAIN > 0) {
    console.log(
      '[gen_font] alpha boost : gain ' + ALPHA_GAIN + ' (format ' + boostInfo.format +
        ', prefilter ' + boostInfo.prefilter + ') -> ' + boostInfo.glyphs + ' glyphs, ' +
        boostInfo.changedGlyphs + ' changed, ' + boostInfo.boostedPixels + ' px boosted, ' +
        boostInfo.bytesBefore + ' B -> ' + boostInfo.bytesAfter + ' B',
    );
  }
  console.log('[gen_font] syntax      : ' + facts.syntaxCheck);
  console.log('[gen_font] checks      : ' + checks.length + '/' + checks.length + ' passed');
}

main();
