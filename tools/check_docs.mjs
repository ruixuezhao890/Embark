#!/usr/bin/env node
// 文档过期自检（issue 12）：文档说的和代码做的不一致时直接失败。
//
// 这个仓库栽过一次：工具早换成了 new_app.mjs，quickstart.md 还在教「cp -r + 两处登记」。
// 过期的指路比缺文档更害人 —— 读者会照着做。五类检查：
//
//   ① md 链接            [x](路径.md) 指向的文件/目录已不存在（改名忘更新）
//   ② 反引号里的仓库路径  `tools/xxx.mjs` / `docs/xxx.md` / `app/...` 已不存在
//   ③ embark_host_ui 开关 --ui-nav 这类必须真在 host/ui_demo.cpp 里
//   ④ 脚本参数            `dev.ps1 -SkipExport` / `new_app.mjs --apply` 必须真在那个脚本里
//   ⑤ 锚注释              反引号里带「锚点」的字符串必须真在源文件里（脚本按字面匹配它）
//
// 三条不查（都有理由，别以为是漏洞）：
//   * docs/adr/**：ADR 是决策史，本来就该提已删掉的文件（只查它里面的链接）
//   * 行里写了 check_docs:skip 标记的：文字在讲「这些文件已退役」这类历史
//   * ALLOW_MISSING 里的路径：构建期生成、本来就不在仓库里
//
// 纪律：宁可少查一条，也不要误报 —— 误报一次，这个脚本就没人跑了。
//
// 用法：
//   node tools/check_docs.mjs              扫一遍；有过期就 exit 1
//   node tools/check_docs.mjs --verbose    连每条检查扫了多少个也打出来
//   node tools/check_docs.mjs --self-test  用内存里的假样本验证每条检查真会报错

import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = resolve(HERE, '..');
const UI_DEMO = 'host/ui_demo.cpp';

// 文档里会写的仓库顶层目录（路径检查只认这些前缀，避免把 URL 片段当路径）
const ROOTS = [
  'tools', 'docs', 'app', 'core', 'host', 'device',
  'assets', '.github', 'third_party',
];

// docs/**/*.md 之外还要扫的当前状态文档
const SCAN_EXTRA = [
  'README.md', 'CONTRIBUTING.md', 'AGENTS.md', 'GLOSSARY.md',
  'app/aidogui_ui/README.md', 'device/README.md',
];

// 构建期生成、本来就不在仓库里的路径（只放这一类，别拿它当消音器）
const ALLOW_MISSING = new Set(['include/embark_aidogui_ui_tables.h']);

// 行内豁免标记：这一行讲的是历史（已删的文件/已退役的做法）
const SKIP_MARK = 'check_docs:skip';

// 文件名的备选扩展名：文档里常按模块名提到文件（写 host_filesystem 而不是 host_filesystem.h）
const EXTS = ['.h', '.hpp', '.cpp', '.c', '.md', '.mjs', '.js', '.cjs', '.ps1', '.cmake', '.aid', '.ttf', '.json', '.txt'];

const SCRIPT_TOKEN =
  /(?<![A-Za-z0-9_.\/-])(?:[A-Za-z0-9_.-]+\/)?tools\/[A-Za-z0-9_.-]+(?:\/[A-Za-z0-9_.-]+)*\.(?:mjs|js|cjs|ps1)(?![A-Za-z0-9_])/g;
const CODE_SPAN = /`([^`]+)`/g;
const MD_LINK = /\]\(([^)]+)\)/g;
const PATH_TOKEN = new RegExp(
  '(?<![A-Za-z0-9_./-])(' + ROOTS.map((r) => r.replace(/\./g, '\\.')).join('|') +
    ')/(?:[A-Za-z0-9_./+-]*[A-Za-z0-9_-])?',
  'g',
);
const HOST_FLAG = /--[a-z][a-z0-9-]+/g;
const SCRIPT_FLAG = /--[a-z][a-z0-9-]+|-[A-Z][A-Za-z0-9]+/g;
const BAD_PATH_CHARS = ['<', '>', '*', '$', '{', '}', '…', '`', '|'];
// 命令里出现这些词，说明 `embark_host_ui` 只是被构建出来的目标名，不是被调用的程序
const NON_HOST_CMDS = ['cmake', 'pwsh', 'node'];

function readText(rel) {
  try {
    return readFileSync(join(REPO_ROOT, rel), 'utf8');
  } catch {
    return null;
  }
}

function walk(dir, out) {
  for (const name of readdirSync(dir)) {
    if (name.startsWith('.')) continue;
    const p = join(dir, name);
    const st = statSync(p);
    if (st.isDirectory()) walk(p, out);
    else if (name.endsWith('.md')) out.push(p);
  }
}

function collectDocs() {
  const abs = [];
  walk(join(REPO_ROOT, 'docs'), abs);
  for (const rel of SCAN_EXTRA) {
    const p = join(REPO_ROOT, rel);
    if (existsSync(p)) abs.push(p);
  }
  return abs.map((p) => relative(REPO_ROOT, p).split('\\').join('/')).sort();
}

function hostFlags() {
  const text = readText(UI_DEMO) || '';
  const set = new Set();
  for (const m of text.matchAll(/"(--[a-z0-9-]+)"/g)) set.add(m[1]);
  return set;
}

// 锚点检查要在一片源文件里找字面串；进内存一次就够
let anchorCorpus = null;
function anchorsCorpus() {
  if (anchorCorpus) return anchorCorpus;
  const chunks = [];
  const exts = ['.cmake', '.h', '.hpp', '.cpp', '.c', '.mjs', '.ps1', '.js', '.cjs'];
  const roots = ['core', 'host', 'device', 'app', 'tools'];
  const visit = (dir) => {
    for (const name of readdirSync(dir)) {
      if (name === 'third_party' || name.startsWith('.')) continue;
      const p = join(dir, name);
      const st = statSync(p);
      if (st.isDirectory()) visit(p);
      else if (exts.some((e) => name.endsWith(e))) chunks.push(readFileSync(p, 'utf8'));
    }
  };
  for (const r of roots) {
    const p = join(REPO_ROOT, r);
    if (existsSync(p)) visit(p);
  }
  anchorCorpus = chunks.join('\n');
  return anchorCorpus;
}

// `src/` 这个根有二义性：文档里既指仓库根的 `core/src/embark/...`，也常把
// `device/src/` 简写成 `src/`。只对这一个根做「路径后缀」兜底。
let repoFileIndex = null;
function repoIndex() {
  if (repoFileIndex) return repoFileIndex;
  const out = [];
  const visit = (dir, rel) => {
    for (const name of readdirSync(dir)) {
      if (name.startsWith('.') || name === 'third_party') continue;
      const abs = join(dir, name);
      const childRel = rel === '' ? name : rel + '/' + name;
      const st = statSync(abs);
      if (st.isDirectory()) visit(abs, childRel);
      else out.push(childRel);
    }
  };
  visit(REPO_ROOT, '');
  repoFileIndex = out;
  return repoFileIndex;
}

function pathExists(rel, token) {
  if (ALLOW_MISSING.has(token)) return true;
  const cands = [join(REPO_ROOT, token), resolve(REPO_ROOT, dirname(rel), token)];
  for (const c of cands) if (existsSync(c)) return true;
  const base = token.slice(token.lastIndexOf('/') + 1);
  if (token.startsWith('src/')) {
    for (const cand of [token, ...EXTS.map((e) => token + e)]) {
      const suffix = '/' + cand;
      if (repoIndex().some((p) => p.endsWith(suffix))) return true;
    }
  }
  if (!base.includes('.')) {
    // 按模块名提到文件（host_filesystem ⇒ host_filesystem.cpp）也算存在
    for (const c of cands) for (const e of EXTS) if (existsSync(c + e)) return true;
  }
  return false;
}

// ---- 五类检查 ----
function checkLinks(rel, line) {
  const out = [];
  for (const m of line.matchAll(MD_LINK)) {
    let target = m[1].trim();
    if (target.startsWith('#')) continue;
    if (/^[a-z][a-z0-9+.-]*:/i.test(target)) continue;
    if (target.startsWith('/')) continue;
    target = target.split('#')[0];
    if (!target) continue;
    if (BAD_PATH_CHARS.some((c) => target.includes(c))) continue;
    const near = resolve(REPO_ROOT, dirname(rel), target);
    const root = resolve(REPO_ROOT, target);
    if (!existsSync(near) && !existsSync(root)) {
      out.push({ kind: '链接指向的文件不存在', detail: target });
    }
  }
  return out;
}

function checkPaths(rel, code) {
  const out = [];
  for (const m of code.matchAll(PATH_TOKEN)) {
    const token = m[0].replace(/[.,;:)\uff09\u3002\uff0c]+$/, '');
    if (BAD_PATH_CHARS.some((c) => token.includes(c))) continue;
    const rest = code.slice(m.index + m[0].length);
    // `xxx.{h,cpp}` / `xxx.<name>` 这类模板写法：match 停在花括号/尖括号/星号之前
    if (rest.startsWith('.{') || '{<*'.includes(rest.charAt(0))) continue;
    if (!pathExists(rel, token)) out.push({ kind: '路径不存在', detail: token });
  }
  return out;
}

function checkHostFlags(rel, code, flags) {
  const at = code.indexOf('embark_host_ui');
  if (at < 0) return [];
  const before = code.slice(0, at);
  if (NON_HOST_CMDS.some((c) => before.includes(c))) return [];
  const out = [];
  for (const m of code.slice(at).matchAll(HOST_FLAG)) {
    if (!flags.has(m[0])) out.push({ kind: 'embark_host_ui 没有这个开关', detail: m[0] });
  }
  return out;
}

function checkScriptFlags(rel, code) {
  const out = [];
  const seen = new Set();
  for (const m of code.matchAll(SCRIPT_TOKEN)) {
    const script = m[0];
    if (seen.has(script)) continue;
    seen.add(script);
    const text = readText(script);
    if (text === null) {
      out.push({ kind: '脚本文件不存在', detail: script });
      continue;
    }
    // 只看脚本路径之后的参数：`pwsh -NoProfile -File tools/x.ps1 -Screen clock` 里前两个是 pwsh 的
    const tail = code.slice(m.index + script.length);
    for (const f of tail.matchAll(SCRIPT_FLAG)) {
      if (!text.includes(f[0])) out.push({ kind: script + ' 没有这个参数', detail: f[0] });
    }
  }
  return out;
}

function checkAnchors(rel, span) {
  if (!span.startsWith('#') || !span.includes('锚点')) return [];
  if (anchorsCorpus().includes(span.trim())) return [];
  return [{ kind: '锚注释在源文件里找不到（脚本按字面匹配它）', detail: span.trim() }];
}

// ---- 扫描 ----
function scan(verbose) {
  const docs = collectDocs();
  const flags = hostFlags();
  const stats = { files: docs.length, history: 0, links: 0, paths: 0, hostFlags: 0, scripts: 0, spans: 0 };
  const findings = [];

  for (const rel of docs) {
    const text = readText(rel);
    if (text === null) continue;
    // ADR 是决策史：只查链接，不查路径/开关/锚点（它们本来就该提已删的东西）
    const history = rel.startsWith('docs/adr/');
    if (history) stats.history += 1;
    let inFence = false;
    text.split('\n').forEach((line, i) => {
      const at = rel + ':' + (i + 1);
      const push = (list) => {
        for (const f of list) findings.push({ at, kind: f.kind, detail: f.detail });
      };
      if (/^\s*(?:```|~~~)/.test(line)) {
        inFence = !inFence;
        return;
      }
      if (line.includes(SKIP_MARK)) return;
      stats.links += (line.match(MD_LINK) || []).length;
      push(checkLinks(rel, line));
      if (history) return;

      const codes = [];
      if (inFence) codes.push(line);
      else for (const m of line.matchAll(CODE_SPAN)) codes.push(m[1]);
      for (const code of codes) {
        stats.spans += 1;
        stats.paths += (code.match(PATH_TOKEN) || []).length;
        stats.hostFlags += (code.match(HOST_FLAG) || []).length;
        stats.scripts += (code.match(SCRIPT_TOKEN) || []).length;
        push(checkPaths(rel, code));
        push(checkHostFlags(rel, code, flags));
        push(checkScriptFlags(rel, code));
        push(checkAnchors(rel, code));
      }
    });
  }

  if (verbose) {
    console.log('扫了 ' + stats.files + ' 个文件（其中 ' + stats.history + ' 个 ADR 只查链接）：');
    console.log('  ' + stats.links + ' 条链接 · ' + stats.paths + ' 个路径 · ' +
      stats.hostFlags + ' 个主机开关 · ' + stats.scripts + ' 处脚本调用 · ' + stats.spans + ' 段代码');
  }
  return findings;
}

// ---- 自检：拿内存里的假样本验证每条检查真会报错 ----
function selfTest() {
  const flags = hostFlags();
  const cases = [
    { what: '链接指向不存在的文件', run: () => checkLinks('docs/guides/__self_test__.md', '看 [这里](does/not/exist.md) 吧') },
    { what: '反引号里的路径不存在', run: () => checkPaths('docs/guides/daily-loop.md', '跑 `tools/no_such_tool.mjs` 就行') },
    { what: 'embark_host_ui 没有的开关', run: () => checkHostFlags('docs/guides/tools.md', 'embark_host_ui --scren clock', flags) },
    { what: '脚本没有的参数', run: () => checkScriptFlags('docs/guides/tools.md', 'node tools/new_app.mjs my_app --nope') },
    {
      what: '锚注释在源文件里找不到',
      // 拼接：样本本身写在这个文件里，直接写字面量会被自己的语料命中
      run: () => checkAnchors('docs/guides/new-app-guide.md', '# ' + 'zzz_no_such_' + '锚点' + '（加在这里）'),
    },
  ];
  let failed = 0;
  for (const c of cases) {
    const got = c.run();
    if (got.length === 0) {
      console.log('  [失败] ' + c.what + '：本该报错却什么都没报');
      failed += 1;
    } else {
      console.log('  [通过] ' + c.what + '：' + got[0].kind + ' -> ' + got[0].detail);
    }
  }
  const good = [
    checkLinks('docs/guides/daily-loop.md', '见 [tools](tools.md) 与 [index](../index.md)'),
    checkPaths('docs/guides/daily-loop.md', '跑 `node tools/check_docs.mjs`，再 `cmake --build build`'),
    checkPaths('docs/guides/daily-loop.md', '细节在 `host/host_filesystem` 里'),
    checkHostFlags('docs/guides/daily-loop.md', 'embark_host_ui --ui --delay 40', flags),
    checkHostFlags('docs/guides/daily-loop.md', 'cmake --build build --target embark_host_ui', flags),
    checkScriptFlags('docs/guides/daily-loop.md', 'pwsh -NoProfile -File tools/aidogui/dev.ps1 -SkipExport'),
    checkAnchors('docs/guides/new-app-guide.md', '# 新 App 登记锚点（加在这里）'),
  ];
  const noise = good.flat();
  if (noise.length > 0) {
    console.log('  [失败] 正向样本被误报：' + noise.map((n) => n.kind + ' ' + n.detail).join('; '));
    failed += 1;
  } else {
    console.log('  [通过] 正向样本零误报（模块名、cmake 目标名、锚点都对得上）');
  }
  if (failed > 0) {
    console.error('〔自检失败〕' + failed + ' 项 —— 检查本身出了问题，先修它。');
    return 1;
  }
  console.log('〔自检通过〕五类检查都真的在查，且不误报。');
  return 0;
}

function main(argv) {
  if (argv.includes('--help') || argv.includes('-h')) {
    console.log('用法: node tools/check_docs.mjs [--verbose] [--self-test]');
    return 0;
  }
  if (argv.includes('--self-test')) {
    console.log('〔check_docs 自检〕');
    return selfTest();
  }
  const findings = scan(argv.includes('--verbose'));
  if (findings.length === 0) {
    console.log('〔文档自检通过〕没有过期指路。');
    return 0;
  }
  for (const f of findings) console.error('〔过期〕' + f.at + '  ' + f.kind + '：' + f.detail);
  console.error('');
  console.error('〔文档自检失败〕' + findings.length + ' 处：文档说的和代码做的不一致。');
  console.error('改文档或者改代码，别让下一个人照着过期的话做事。');
  return 1;
}

process.exitCode = main(process.argv.slice(2));
