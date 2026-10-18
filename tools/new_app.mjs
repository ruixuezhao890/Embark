#!/usr/bin/env node
/**
 * tools/new_app.mjs —— 生成一个新 App 的薄壳（AiDoGUI 版）
 * 工单：.scratch/app-platform/issues/11-scaffold-app.md
 *
 * 设计：脚本里不存模板。模板是仓库里的 app/music/music_app.{h,cpp}（编译通过、跑过验收的
 * 那一份）：脚本读它 → 替换标识符与文案 → 写到 app/<名>/<名>_app.{h,cpp} → 登记 4 处。
 * 于是没有第二份模板可以漂移 —— music 的形状变了，脚本跟着变（--self-check 就是这条的硬检查）。
 *
 * 用法：
 *   node tools/new_app.mjs my_app --title 我的应用 --icon LV_SYMBOL_FILE   只看计划（dry-run，默认）
 *   node tools/new_app.mjs my_app --apply                                 真写文件 + 登记 4 处
 *   node tools/new_app.mjs --self-check                                   模板与 4 个锚点还认得出吗
 *
 * 它不生成 .aid / 屏代码（那是设计器的活，见 04 号票的工具），也不动另外三张 App 表
 * （ui_tour.cpp / user_main.cpp / esp32 main.cpp 各有用途，不是所有 App 都该进）。
 */
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = resolve(HERE, '..');  // 本脚本在 tools/ 下（tools/font/*.mjs 才要两层）

const TPL_H = join(REPO_ROOT, 'app', 'music', 'music_app.h');
const TPL_CPP = join(REPO_ROOT, 'app', 'music', 'music_app.cpp');
const CMAKE_SOURCES = join(REPO_ROOT, 'cmake', 'embark_sources.cmake');
const UI_DEMO = join(REPO_ROOT, 'platform', 'host', 'ui_demo.cpp');
const UI_HOOKS = join(REPO_ROOT, 'app', 'common', 'aidogui_ui_hooks.h');

const USAGE = [
  '用法：',
  '  node tools/new_app.mjs <app名> [--title 中文名] [--icon LV_SYMBOL_X]   只看计划（dry-run，默认）',
  '  node tools/new_app.mjs <app名> --apply                                真写文件 + 登记 4 处',
  '  node tools/new_app.mjs --self-check                                   检查模板与锚点还在',
  '',
  '规则：App 名 ^[a-z][a-z0-9_]*$，不能以 _sub 结尾（子页保留后缀），同名目录已存在则拒绝。',
].join('\n');

/* 模板里必须还存在的标记：少一个就说明 app/music/ 的形状变了，脚本得跟着改。 */
const TEMPLATE_MARKERS_H = [
  ['class MusicApp final : public App', '类声明'],
  ['name() const override { return "music"; }', 'App 名'],
  ['const char* title() const override', '标题'],
  ['const char* icon() const override', '图标'],
  ['void onCreate(Framework& fw) override;', 'onCreate 声明'],
  ['void onForegroundTick(std::uint32_t now_ms) override;', 'onForegroundTick 声明'],
  ['EMBARK_APP_MUSIC_APP_H', '头文件 guard'],
];

const TEMPLATE_MARKERS_CPP = [
  ['#if defined(EMBARK_AIDOGUI_UI)', '后端开关'],
  ['#include "music_app.h"', '自头'],
  ['ui_backend_load_for_app(name());', '按屏名挂屏'],
  ['ui_backend_tick();', '泵一帧'],
  ['void MusicApp::onCreate(Framework& fw) {', '实现前缀'],
];

const REGISTRATION_ANCHORS = [
  [CMAKE_SOURCES, '# 新 App 登记锚点（加在这里）', 'cmake/embark_sources.cmake · EMBARK_APP_SOURCES'],
  [CMAKE_SOURCES, 'set(EMBARK_APP_SOURCES_DEVICE', 'cmake/embark_sources.cmake · EMBARK_APP_SOURCES_DEVICE'],
  [UI_DEMO, 'EMBARK_APP_TABLE(embark::demo::LauncherApp', 'platform/host/ui_demo.cpp · App 表'],
  [UI_HOOKS, '#define EMBARK_AIDOGUI_HOOK_APPS(X)', 'app/common/aidogui_ui_hooks.h · 打开 App 的 C 入口'],
];

function fail(message) {
  throw new Error(message);
}

function readText(path) {
  try {
    return readFileSync(path, 'utf8');
  } catch (error) {
    return fail('读不到 ' + path + '：' + error.message);
  }
}

function requireIncludes(text, needle, where) {
  if (!text.includes(needle)) fail(where + ' 里找不到「' + needle + '」—— 结构变了，先看那份文件');
}

function replaceAllText(text, from, to, label) {
  if (!text.includes(from)) {
    fail('模板里找不到「' + from + '」（' + label + '）—— app/music/ 的形状变了，脚本要跟着改');
  }
  return text.split(from).join(to);
}

/* 去掉模板的头注释块：那一块写的是 music 自己的来历，对新 App 是错的。 */
function stripDocBlock(text, path) {
  if (!text.startsWith('/**')) fail(path + ' 不是以 /** 开头，没法把头注释换掉');
  const end = text.indexOf('*/');
  if (end < 0) fail(path + ' 的头注释没有收尾 */');
  let rest = text.slice(end + 2);
  if (rest.startsWith('\r\n')) rest = rest.slice(2);
  else if (rest.startsWith('\n')) rest = rest.slice(1);
  return rest;
}

/* 类名 = 大驼峰。模板类叫 MusicApp（music + App），所以再补一个 App；
   已经以 App 结尾的不叠（my_app → MyApp，与工单里的期望一致）。 */
function classFor(name) {
  const pascal = name
    .split('_')
    .filter((part) => part.length > 0)
    .map((part) => part[0].toUpperCase() + part.slice(1))
    .join('');
  return pascal.endsWith('App') ? pascal : pascal + 'App';
}

function buildHeader(app) {
  const upper = app.name.toUpperCase();
  let out = stripDocBlock(readText(TPL_H), 'app/music/music_app.h');
  out = replaceAllText(out, 'MusicApp', app.pascal, '类名');
  out = replaceAllText(out, 'MUSIC_APP_H', upper + '_APP_H', '头文件 guard');
  out = replaceAllText(out, '"music"', '"' + app.name + '"', 'App 名');
  out = replaceAllText(out, '"音乐"', '"' + app.title + '"', '标题');
  out = replaceAllText(out, 'LV_SYMBOL_AUDIO', app.icon, '图标');
  out = replaceAllText(out, '/// 占位 App：', '/// App：', '类注释');
  out = replaceAllText(out, '（占位屏）', '（屏名 == App 名）', '类注释');
  out = replaceAllText(out, '/// App：音乐。', '/// App：' + app.title + '。', '类注释');
  const doc = [
    '/**',
    ' * Embark · ' + app.title + ' App。',
    ' *',
    ' * 由 tools/new_app.mjs 从 app/music/music_app.h 生成（' + app.date + '）—— 生成之后这就是',
    ' * 你的代码，脚本不会再回来改它。',
    ' *',
    ' * 薄壳纪律（模板 = app/music/，那份是真身）：',
    ' *   - onCreate 只记账 + 持有框架句柄；',
    ' *   - onEnter/onResume 只按屏名加载自己的屏（缺屏只记日志，不崩）；',
    ' *   - onForegroundTick 泵一帧；',
    ' *   - 界面与导航都在 AiDoGUI 工程里（屏名 == App 名），C++ 侧不碰控件；',
    ' *   - 回主屏由屏上的返回按钮经设计器事件 embark_ui_open_launcher 承担（ADR 0014）。',
    ' *',
    ' * 后台策略 = 默认 suspend。',
    ' */',
    '',
  ].join('\n');
  return doc + out;
}

function buildSource(app) {
  let out = stripDocBlock(readText(TPL_CPP), 'app/music/music_app.cpp');
  out = replaceAllText(out, 'MusicApp', app.pascal, '类名');
  out = replaceAllText(out, 'music_app', app.name + '_app', '自头文件名');
  out = replaceAllText(out, '界面 = AiDoGUI 占位屏', '界面 = AiDoGUI 屏', '日志文案');
  const doc = [
    '/**',
    ' * Embark · ' + app.title + ' App 实现：见 ' + app.name + '_app.h 头注释。',
    ' *',
    ' * 由 tools/new_app.mjs 从 app/music/music_app.cpp 生成（' + app.date + '）。薄壳纪律：',
    ' * onCreate 只记账；onEnter/onResume 只按屏名挂自己的屏（后端由 app/common/ui_backend.h 选）；',
    ' * onForegroundTick 泵一帧。界面与导航都在 AiDoGUI 工程里，C++ 侧不碰任何控件。',
    ' */',
    '',
  ].join('\n');
  return doc + out;
}

function planFor(app) {
  const srcLine = '        "${EMBARK_ROOT}/app/' + app.name + '/' + app.name + '_app.cpp"';

  /* ① + ② 都在 cmake/embark_sources.cmake 上 */
  const cmakeText = readText(CMAKE_SOURCES);
  const anchor = '        # 新 App 登记锚点（加在这里）';
  requireIncludes(cmakeText, anchor, 'cmake/embark_sources.cmake');
  const anchorAt = cmakeText.indexOf(anchor);
  let cmakeOut = cmakeText.slice(0, anchorAt) + srcLine + '\n' + cmakeText.slice(anchorAt);

  const devMarker = 'set(EMBARK_APP_SOURCES_DEVICE';
  const devAt = cmakeOut.indexOf(devMarker);
  if (devAt < 0) fail('cmake/embark_sources.cmake 里找不到 ' + devMarker);
  const glueNeedle = 'app/common/aidogui_ui_nav.cpp"';
  const glueAt = cmakeOut.indexOf(glueNeedle, devAt);
  if (glueAt < 0) fail('EMBARK_APP_SOURCES_DEVICE 里找不到胶水源 ' + glueNeedle);
  const glueLineStart = cmakeOut.lastIndexOf('\n', glueAt) + 1;
  cmakeOut = cmakeOut.slice(0, glueLineStart) + srcLine + '\n' + cmakeOut.slice(glueLineStart);

  /* ③ 宿主与真机模拟器的 App 表（launcher 必须留在第一位）。两处：include + 表项 ——
     App 表引用的是类名，头不可见就编不过。 */
  let demoText = readText(UI_DEMO);
  const includeNeedle = '#include "music/music_app.h"';
  requireIncludes(demoText, includeNeedle, 'platform/host/ui_demo.cpp');
  const includeAt = demoText.indexOf(includeNeedle) + includeNeedle.length;
  demoText = demoText.slice(0, includeAt) + '\n#include "' + app.name + '/' + app.name + '_app.h"' + demoText.slice(includeAt);
  const tableNeedle = 'EMBARK_APP_TABLE(embark::demo::LauncherApp';
  requireIncludes(demoText, tableNeedle, 'platform/host/ui_demo.cpp');
  const tableAt = demoText.indexOf(tableNeedle);
  const closeAt = demoText.indexOf(')', tableAt);
  if (closeAt < 0) fail('platform/host/ui_demo.cpp 的 App 表没有收尾括号');
  const closeLineStart = demoText.lastIndexOf('\n', closeAt) + 1;
  const indent = demoText.slice(closeLineStart, closeAt).match(/^[ ]*/)[0];
  const tableItem = ',\n' + indent + 'embark::demo::' + app.pascal;
  const demoOut = demoText.slice(0, closeAt) + tableItem + demoText.slice(closeAt);

  /* ④ 设计器事件能「调用函数」打开它 */
  const hooksText = readText(UI_HOOKS);
  const hookMarker = '#define EMBARK_AIDOGUI_HOOK_APPS(X)';
  requireIncludes(hooksText, hookMarker, 'app/common/aidogui_ui_hooks.h');
  const hookLines = hooksText.split('\n');
  const hookStart = hookLines.findIndex((line) => line.includes(hookMarker));
  if (hookStart < 0) fail('app/common/aidogui_ui_hooks.h 里找不到宏定义行');
  let terminator = -1;
  for (let i = hookStart; i < hookLines.length; i += 1) {
    if (!hookLines[i].trimEnd().endsWith('\\')) {
      terminator = i;
      break;
    }
  }
  if (terminator < 0) fail('app/common/aidogui_ui_hooks.h 的宏体找不到终止行');
  const refLine = hookLines[terminator - 1];
  const col = refLine.lastIndexOf('\\');
  if (col < 0) fail('app/common/aidogui_ui_hooks.h 宏体倒数第二行没有续行反斜杠');
  const base = hookLines[terminator].replace(/\\$/, '').trimEnd();
  hookLines[terminator] = base + ' '.repeat(Math.max(1, col - base.length)) + '\\';
  hookLines.splice(terminator + 1, 0, '  X(' + app.name + ')');
  const hooksOut = hookLines.join('\n');

  return {
    srcLine,
    files: [
      {
        rel: 'app/' + app.name + '/' + app.name + '_app.h',
        path: join(REPO_ROOT, 'app', app.name, app.name + '_app.h'),
        content: buildHeader(app),
      },
      {
        rel: 'app/' + app.name + '/' + app.name + '_app.cpp',
        path: join(REPO_ROOT, 'app', app.name, app.name + '_app.cpp'),
        content: buildSource(app),
      },
    ],
    edits: [
      { rel: 'cmake/embark_sources.cmake', path: CMAKE_SOURCES, updated: cmakeOut },
      { rel: 'platform/host/ui_demo.cpp', path: UI_DEMO, updated: demoOut },
      { rel: 'app/common/aidogui_ui_hooks.h', path: UI_HOOKS, updated: hooksOut },
    ],
  };
}

function printPlan(app, plan) {
  console.log('App：' + app.name + ' → 类 ' + app.pascal + '，标题「' + app.title + '」，图标 ' + app.icon);
  console.log('');
  console.log('要写的文件（2 个）：');
  for (const file of plan.files) {
    console.log('  ' + file.rel + '（' + file.content.split('\n').length + ' 行）');
  }
  console.log('');
  console.log('要登记的位置（5 处 / 4 个文件）：');
  console.log('  1. cmake/embark_sources.cmake · EMBARK_APP_SOURCES：插在「# 新 App 登记锚点（加在这里）」之前');
  console.log('       + ' + plan.srcLine.trim());
  console.log('  2. cmake/embark_sources.cmake · EMBARK_APP_SOURCES_DEVICE：插在胶水源 aidogui_ui_nav.cpp 那一行之前');
  console.log('       + ' + plan.srcLine.trim());
  console.log('  3. platform/host/ui_demo.cpp · include：在 music 那一行之后补一行（表里要用类名）');
  console.log('       + #include "' + app.name + '/' + app.name + '_app.h"');
  console.log('  4. platform/host/ui_demo.cpp · App 表：在收尾括号前补一项（launcher 留在第一位）');
  console.log('       + , embark::demo::' + app.pascal);
  console.log('  5. app/common/aidogui_ui_hooks.h · EMBARK_AIDOGUI_HOOK_APPS(X)：宏体末补一行');
  console.log('       + X(' + app.name + ')');
}

function printTodos(app) {
  console.log('');
  console.log('屏侧待办（脚本碰不到 .aid，只能你来做）：');
  console.log('  1. AiDoGUI 设计器里建一块屏，屏名必须正好是 "' + app.name + '"（屏名 == App 名，导航桥靠它翻名字）');
  console.log('  2. 屏上放返回按钮，事件用「调用函数」写 embark_ui_open_launcher（回主屏只有这一条路，ADR 0014）');
  console.log('  3. 想让别的屏打开它，就在那个按钮的「调用函数」里写 embark_ui_open_' + app.name);
  console.log('     （第 4 处登记已经把 X(' + app.name + ') 加进 app/common/aidogui_ui_hooks.h，函数已经在了）');
  console.log('  4. pwsh -File tools/aidogui/export.ps1 重新导出，再 cmake -S . -B build && cmake --build build');
  console.log('  5. node tools/aidogui/gen_widget_doc.mjs 刷控件表；node tools/aidogui/dev.ps1 -Screen ' + app.name + ' 日常循环');
  console.log('');
  console.log('没做的（有意）：不生成 .aid / 屏代码（设计器的活，见 04 号票），也不动');
  console.log('ui_tour.cpp / user_main.cpp / esp32 main.cpp 那三张 App 表。');
}

function writePlan(app, plan) {
  mkdirSync(join(REPO_ROOT, 'app', app.name), { recursive: true });
  for (const file of plan.files) writeFileSync(file.path, file.content, 'utf8');
  for (const edit of plan.edits) writeFileSync(edit.path, edit.updated, 'utf8');
  console.log('');
  console.log('已写（UTF-8 无 BOM）：');
  for (const file of plan.files) console.log('  ' + file.rel);
  console.log('已登记 5 行：cmake/embark_sources.cmake（2）、platform/host/ui_demo.cpp（include + 表项）、app/common/aidogui_ui_hooks.h（1）');
  console.log('没有自动 git add —— 先 git diff 看一眼再决定。');
}

function parseArgs(argv) {
  const options = { name: null, title: null, icon: null, apply: false, selfCheck: false, help: false };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--apply') options.apply = true;
    else if (arg === '--dry-run') options.apply = false;
    else if (arg === '--self-check') options.selfCheck = true;
    else if (arg === '--help' || arg === '-h') options.help = true;
    else if (arg === '--title') {
      i += 1;
      if (i >= argv.length) fail('--title 后面要跟中文名');
      options.title = argv[i];
    } else if (arg === '--icon') {
      i += 1;
      if (i >= argv.length) fail('--icon 后面要跟 LV_SYMBOL_X');
      options.icon = argv[i];
    } else if (arg.startsWith('--')) {
      fail('不认识的参数：' + arg + '\n\n' + USAGE);
    } else if (options.name === null) {
      options.name = arg;
    } else {
      fail('多余的位置参数：' + arg);
    }
  }
  return options;
}

function validate(options) {
  const name = options.name;
  if (name === null) fail('没给 App 名。\n\n' + USAGE);
  if (!/^[a-z][a-z0-9_]*$/.test(name)) {
    fail('App 名只能是小写字母开头的小写字母/数字/下划线：' + name);
  }
  if (name.endsWith('_sub')) {
    fail('App 名不能以 _sub 结尾 —— 那是子页屏名的保留后缀（见 app/common/ui_screen_names.h）');
  }
  if (existsSync(join(REPO_ROOT, 'app', name))) {
    fail('app/' + name + ' 已经存在 —— 不覆盖（要重做请先自己删掉那个目录）');
  }
  const icon = options.icon === null ? 'LV_SYMBOL_FILE' : options.icon;
  if (!/^LV_SYMBOL_[A-Z0-9_]+$/.test(icon)) {
    fail('--icon 要写成 LV_SYMBOL_XXX（例如 LV_SYMBOL_FILE、LV_SYMBOL_AUDIO）：' + icon);
  }
  const today = new Date();
  const date = today.getFullYear() + '-' + String(today.getMonth() + 1).padStart(2, '0')
    + '-' + String(today.getDate()).padStart(2, '0');
  const title = options.title === null ? name : options.title;
  const title_chars = Array.from(title).length;
  if (title_chars > 5) {
    console.warn('提醒：标题「' + title + '」有 ' + title_chars + ' 个字。主屏磁贴的标签宽 = 磁贴宽、'
      + '14px 字号、默认换行，一行约 3 个汉字 —— 超了会被裁且不报错，建议 ≤5 个字。');
  }
  return { name, title, icon, pascal: classFor(name), date };
}

function selfCheck() {
  const header = readText(TPL_H);
  const source = readText(TPL_CPP);
  const problems = [];
  for (const [needle, label] of TEMPLATE_MARKERS_H) {
    if (!header.includes(needle)) problems.push('app/music/music_app.h 缺「' + needle + '」（' + label + '）');
  }
  for (const [needle, label] of TEMPLATE_MARKERS_CPP) {
    if (!source.includes(needle)) problems.push('app/music/music_app.cpp 缺「' + needle + '」（' + label + '）');
  }
  for (const [path, needle, where] of REGISTRATION_ANCHORS) {
    if (!readText(path).includes(needle)) problems.push(where + ' 缺锚点「' + needle + '」');
  }
  if (problems.length === 0) {
    /* 再演练一次生成：模板替换、guard、App 表缩进、宏续行这些只有真跑才暴露。 */
    try {
      planFor({ name: 'zz_self_check', title: '自检', icon: 'LV_SYMBOL_FILE', pascal: 'ZzSelfCheck', date: '2026-01-01' });
    } catch (error) {
      problems.push('演练生成失败：' + error.message);
    }
  }
  if (problems.length > 0) {
    console.error('〔自检失败〕脚本依赖的东西已经变了：');
    for (const problem of problems) console.error('  - ' + problem);
    return 1;
  }
  console.log('〔自检通过〕模板 app/music/ 与 4 处登记锚点（落成 5 行改动）都还在，照现在的形状生成没问题。');
  return 0;
}

function main() {
  const options = parseArgs(process.argv.slice(2));
  if (options.help) {
    console.log(USAGE);
    return 0;
  }
  if (options.selfCheck) return selfCheck();
  const app = validate(options);
  const plan = planFor(app);
  printPlan(app, plan);
  if (!options.apply) {
    console.log('');
    console.log('（dry-run：一个字节都没写。加 --apply 才落盘。）');
    return 0;
  }
  writePlan(app, plan);
  printTodos(app);
  return 0;
}

try {
  process.exitCode = main();
} catch (error) {
  console.error('错误：' + error.message);
  process.exitCode = 1;
}
