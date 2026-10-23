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
 *   node tools/new_app.mjs my_app --apply                                 真写文件 + 登记 7 行（宿主 4 + 真机 3）
 *   node tools/new_app.mjs --self-check                                   模板与 6 个锚点还认得出吗
 *   node tools/new_app.mjs --status                                       每个 App 的登记 / 屏 / 真机状态
 *   node tools/new_app.mjs my_app --delete --apply                         退掉一个 App（撤登记 + 删目录 + 撤那张占位屏）
 *
 * 屏侧：.aid 里没有同名屏时，顺手插一张「占位屏」（复制仓库里 album 那块占位屏的形状：
 * 背景 + 返回键 + 标题 + 占位卡片）。这样 App 一建出来点进去就有页面，而不是一屏黑。
 * 屏代码仍由设计器导出（tools/aidogui/export.ps1），脚本不生成 C。
 *
 * 加一个 App 要动的面是六处：两个新文件 + 宿主源清单 + 真机源清单 + 宿主 App 表 +
 * 真机 App 表（platform/esp32/project/main/main.cpp）+ 设计器导航入口 + .aid 里一张同名屏。
 * --no-device 表示「只做宿主」，那两处真机登记就不动（真机的磁贴也就不会有它）。
 *
 * 给 tools/appconsole/（App 管理台）用的机器接口是 --json / --status：stdout 只有一段 JSON，
 * 人看的那套打印全部跳过。管理台只做界面 + 起子进程 —— 登记逻辑只有这一份，别在那边再写一遍。
 *
 * 它不动 ui_tour.cpp / user_main.cpp 那两张表（各有用途，不是所有 App 都该进）。
 */
import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = resolve(HERE, '..');  // 本脚本在 tools/ 下（tools/font/*.mjs 才要两层）

const TPL_H = join(REPO_ROOT, 'app', 'music', 'music_app.h');
const TPL_CPP = join(REPO_ROOT, 'app', 'music', 'music_app.cpp');
const CMAKE_SOURCES = join(REPO_ROOT, 'cmake', 'embark_sources.cmake');
const UI_DEMO = join(REPO_ROOT, 'platform', 'host', 'ui_demo.cpp');
const UI_HOOKS = join(REPO_ROOT, 'app', 'common', 'aidogui_ui_hooks.h');
const AID_DEFAULT = join(REPO_ROOT, 'app', 'aidogui_ui', 'screen.aid');
const ESP32_MAIN = join(REPO_ROOT, 'platform', 'esp32', 'project', 'main', 'main.cpp');
const DESIGNER_EXE = 'AiDoGUIDesigner.exe';

/* 占位屏抄谁：album 是仓库里那块「界面还没做」的占位屏（背景 + 返回键 + 标题 + 占位卡片）。 */
const PLACEHOLDER_TEMPLATE_SCREEN = 'album';

const USAGE = [
  '用法：',
  '  node tools/new_app.mjs <app名> [--title 中文名] [--icon LV_SYMBOL_X]   只看计划（dry-run，默认）',
  '  node tools/new_app.mjs <app名> --apply                                真写文件 + 登记 7 行 + .aid 里补一张占位屏',
  '  node tools/new_app.mjs <app名> --delete [--apply]                     退掉一个 App（撤登记 + 删目录 + 撤占位屏）',
  '  node tools/new_app.mjs --status                                       每个 App 的登记 / 屏 / 真机状态',
  '  node tools/new_app.mjs --self-check                                   检查模板、锚点、.aid 占位屏还在',
  '',
  '  --device / --no-device  真机三处（cmake 的 DEVICE 清单 + platform/esp32/project/main/main.cpp 的头与表）：',
  '                          默认登记；--no-device 只做宿主（真机上就没有它的磁贴）',
  '  --aid <文件>            目标 .aid（默认 app/aidogui_ui/screen.aid）',
  '  --json                  只往 stdout 打一段 JSON（App 管理台用）：计划 / 状态 / 错误都在里面',
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
  [ESP32_MAIN, 'EMBARK_APP_TABLE(embark::demo::LauncherApp', 'platform/esp32/project/main/main.cpp · App 表'],
  [ESP32_MAIN, '#include "weather_app.h"', 'platform/esp32/project/main/main.cpp · App 头'],
];

/* 主屏不能删：启动器是磁贴的来源，删了就回不到桌面。 */
const PROTECTED_APPS = ['launcher'];

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

/* --- .aid 侧：给新 App 补一张占位屏 ---------------------------------------
   为什么脚本要动 .aid：桥是按「屏名 == App 名」找屏的，没有那张屏 App 一点进去就是一屏黑。
   设计器自己建屏当然也行，但那意味着「加一个 App」永远要来回切两个程序。 */

function relOf(target) {
  return relative(REPO_ROOT, target).split('\\').join('/');
}

function resolveAidPath(options) {
  if (options.aid !== null) return { path: resolve(REPO_ROOT, options.aid), source: '命令行 --aid' };
  return { path: AID_DEFAULT, source: '默认' };
}

/* 设计器装在哪：与 tools/aidogui/export.ps1 同一套查找顺序（参数 → 环境变量 →
   .aidogui-home → tools/aidogui/home.txt）。管理台要「用设计器打开」这个按钮，就靠它。 */
function resolveDesigner() {
  const candidates = [];
  if (process.env.AIDOGUI_HOME) candidates.push({ dir: process.env.AIDOGUI_HOME, source: '$env:AIDOGUI_HOME' });
  const files = [
    [join(REPO_ROOT, '.aidogui-home'), '.aidogui-home'],
    [join(REPO_ROOT, 'tools', 'aidogui', 'home.txt'), 'tools/aidogui/home.txt'],
  ];
  for (const [file, source] of files) {
    if (!existsSync(file)) continue;
    const first = readText(file).split(/\r?\n/).map((line) => line.trim()).find((line) => line !== '');
    if (first) candidates.push({ dir: first, source });
  }
  for (const candidate of candidates) {
    const exe = join(candidate.dir, DESIGNER_EXE);
    if (existsSync(exe)) return { ok: true, exe: exe, home: candidate.dir, source: candidate.source, tried: [] };
  }
  return { ok: false, exe: null, home: null, source: null, tried: candidates.map((c) => c.dir) };
}

/* 拿不到 .aid 不算致命：App 的 C++ 薄壳照建，屏那一步打个招呼（加 --aid 指对位置就行）。 */
function loadAidOrWarn(aidChoice, warnings) {
  try {
    return readAid(aidChoice.path);
  } catch (error) {
    const message = error.message + ' —— 屏那一步跳过（App 薄壳照建）。';
    if (Array.isArray(warnings)) warnings.push(message);
    else console.warn('提醒：' + message);
    return null;
  }
}

function readAid(file) {
  if (!existsSync(file)) fail('找不到 .aid：' + file + '（--aid 指错了？）');
  const raw = readText(file);
  let obj;
  try {
    obj = JSON.parse(raw);
  } catch (error) {
    return fail('.aid 不是合法 JSON：' + file + '（' + error.message + '）');
  }
  if (!obj || !Array.isArray(obj.screens)) fail('.aid 里没有 screens 数组：' + file);
  return { path: file, raw: raw, obj: obj };
}

/* 设计器写回的是 JSON.stringify(obj, null, 2)，且原文件末尾没有换行 —— 照抄这个形状，
   「加了又撤」之后 git diff 才正好是空的。 */
function serializeAid(aidFile) {
  const trailing = aidFile.raw.endsWith('\n') ? '\n' : '';
  return JSON.stringify(aidFile.obj, null, 2) + trailing;
}

function cloneDeep(value) {
  return JSON.parse(JSON.stringify(value));
}

function pickTemplateScreen(aidObj) {
  const named = aidObj.screens.find((screen) => screen.name === PLACEHOLDER_TEMPLATE_SCREEN);
  if (named && Array.isArray(named.widgets) && named.widgets.length > 0) return named;
  const usable = aidObj.screens.filter((screen) => Array.isArray(screen.widgets) && screen.widgets.length > 1
    && JSON.stringify(screen.widgets).includes('embark_ui_open_launcher'));
  if (usable.length === 0) {
    fail('.aid 里找不到可抄的占位屏（需要一块带 embark_ui_open_launcher 返回键的屏）—— 先在设计器里做一块，或用 --aid 指到对的文件');
  }
  return usable[0];
}

function buildPlaceholderScreen(aidObj, app) {
  const template = pickTemplateScreen(aidObj);
  const prefix = template.name + '_';
  const rename = (value) => {
    const old = String(value || '');
    if (old === template.name) return app.name;
    if (old.indexOf(prefix) === 0) return app.name + '_' + old.slice(prefix.length);
    return old;
  };
  const widgets = (template.widgets || []).map((source) => {
    const widget = cloneDeep(source);
    widget.id = rename(widget.id);
    widget.parentId = rename(widget.parentId);
    if (Array.isArray(widget.events)) {
      widget.events = widget.events.map((event) => {
        const copy = cloneDeep(event);
        copy.id = String(copy.id || '').split(template.name).join(app.name);
        return copy;
      });
    }
    if (widget.name === 'L_Title') widget.text = app.title;
    if (widget.name === 'L_PlaceholderHint') widget.text = '用 HTML 草稿画这屏';
    return widget;
  });
  return {
    id: app.name,
    name: app.name,
    isDefault: false,
    style: cloneDeep(template.style || {}),
    widgets: widgets,
    animations: cloneDeep(template.animations || []),
    template: template.name,  // 只是给人看的（printPlan 里说「抄的是哪块」），不进 .aid
  };
}

function planScreen(aidFile, app) {
  const rel = relOf(aidFile.path);
  const existing = aidFile.obj.screens.find((screen) => screen.name === app.name);
  if (existing) {
    const widgets = Array.isArray(existing.widgets) ? existing.widgets : [];
    if (widgets.length > 0) return { rel: rel, action: 'exists', widgets: widgets.length };
    /* 空屏（设计器里刚建、一个控件都还没画）：正好把占位屏填进去 —— 空屏没有内容可覆盖，
       省得用户建完 App 再回设计器手工克隆一遍。 */
    const filled = buildPlaceholderScreen(aidFile.obj, app);
    existing.widgets = filled.widgets;
    return { rel: rel, action: 'fill_empty', widgets: filled.widgets.length, template: filled.template };
  }
  const screen = buildPlaceholderScreen(aidFile.obj, app);
  const templateName = screen.template;
  delete screen.template;  // 只给人看，别写进 .aid
  aidFile.obj.screens.push(screen);
  return { rel: rel, action: 'insert', widgets: screen.widgets.length, template: templateName };
}

function planScreenRemoval(aidFile, app) {
  const rel = relOf(aidFile.path);
  const index = aidFile.obj.screens.findIndex((screen) => screen.name === app.name);
  if (index < 0) return { rel: rel, action: 'absent' };
  const screen = aidFile.obj.screens[index];
  const widgets = screen.widgets || [];
  let expected = null;
  if (app.name !== PLACEHOLDER_TEMPLATE_SCREEN) {
    try {
      expected = buildPlaceholderScreen(aidFile.obj, app).widgets.map((widget) => widget.name).join(',');
    } catch (error) {
      expected = null;
    }
  }
  const actual = widgets.map((widget) => widget.name).join(',');
  if (widgets.length > 0 && actual !== expected) return { rel: rel, action: 'keep', widgets: widgets.length };
  aidFile.obj.screens.splice(index, 1);
  return { rel: rel, action: widgets.length === 0 ? 'remove_empty' : 'remove_placeholder' };
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

/* 把一个 include 行插在已知锚点之后（新 App 追加在组尾，与 App 表的顺序同口径）。 */
function addInclude(text, needle, line, where) {
  requireIncludes(text, needle, where);
  const at = text.indexOf(needle) + needle.length;
  return text.slice(0, at) + '\n' + line + text.slice(at);
}

/* App 表项插在收尾括号前，缩进照抄收尾括号那一行（launcher 留在第一位）。 */
function addToAppTable(text, app, where) {
  const tableNeedle = 'EMBARK_APP_TABLE(embark::demo::LauncherApp';
  requireIncludes(text, tableNeedle, where);
  const tableAt = text.indexOf(tableNeedle);
  const closeAt = text.indexOf(')', tableAt);
  if (closeAt < 0) fail(where + ' 的 App 表没有收尾括号');
  const closeLineStart = text.lastIndexOf('\n', closeAt) + 1;
  const indent = text.slice(closeLineStart, closeAt).match(/^[ ]*/)[0];
  return text.slice(0, closeAt) + ',\n' + indent + 'embark::demo::' + app.pascal + text.slice(closeAt);
}

/* 计划预览：把「刚插进去的那一行」找出来，连上下文两行一起报（不做完整 diff）。 */
function insertEntry(rel, label, updatedText, needle, occurrence) {
  const lines = updatedText.split('\n');
  let seen = 0;
  let at = -1;
  for (let i = 0; i < lines.length; i += 1) {
    if (lines[i].includes(needle)) {
      seen += 1;
      if (seen === occurrence) { at = i; break; }
    }
  }
  if (at < 0) fail('计划预览找不到刚插进去的「' + needle + '」（' + rel + '）—— 脚本自己对不上，别信这份计划');
  return {
    rel: rel,
    label: label,
    kind: 'insert',
    line: at + 1,
    lines: [lines[at]],
    before: lines.slice(Math.max(0, at - 2), at),
    after: lines.slice(at + 1, at + 3),
  };
}

function removeEntry(rel, label, oldText, index, removedLines) {
  const lines = oldText.split('\n');
  return {
    rel: rel,
    label: label,
    kind: 'remove',
    line: index + 1,
    lines: removedLines || [lines[index]],
    before: lines.slice(Math.max(0, index - 2), index),
    after: lines.slice(index + 1, index + 3),
  };
}

function planFor(app) {
  const srcLine = '        "${EMBARK_ROOT}/app/' + app.name + '/' + app.name + '_app.cpp"';

  /* ① + ② 都在 cmake/embark_sources.cmake 上 */
  const cmakeText = readText(CMAKE_SOURCES);
  const anchor = '        # 新 App 登记锚点（加在这里）';
  requireIncludes(cmakeText, anchor, 'cmake/embark_sources.cmake');
  const anchorAt = cmakeText.indexOf(anchor);
  let cmakeOut = cmakeText.slice(0, anchorAt) + srcLine + '\n' + cmakeText.slice(anchorAt);

  /* ② 真机源清单：--no-device 时不动 */
  if (app.device) {
    const devMarker = 'set(EMBARK_APP_SOURCES_DEVICE';
    const devAt = cmakeOut.indexOf(devMarker);
    if (devAt < 0) fail('cmake/embark_sources.cmake 里找不到 ' + devMarker);
    const glueNeedle = 'app/common/aidogui_ui_nav.cpp"';
    const glueAt = cmakeOut.indexOf(glueNeedle, devAt);
    if (glueAt < 0) fail('EMBARK_APP_SOURCES_DEVICE 里找不到胶水源 ' + glueNeedle);
    const glueLineStart = cmakeOut.lastIndexOf('\n', glueAt) + 1;
    cmakeOut = cmakeOut.slice(0, glueLineStart) + srcLine + '\n' + cmakeOut.slice(glueLineStart);
  }

  /* ③ 宿主 App 表（launcher 必须留在第一位）。两处：include + 表项 ——
     App 表引用的是类名，头不可见就编不过。 */
  const demoOut = addToAppTable(
    addInclude(readText(UI_DEMO), '#include "music/music_app.h"',
      '#include "' + app.name + '/' + app.name + '_app.h"', 'platform/host/ui_demo.cpp'),
    app, 'platform/host/ui_demo.cpp');

  /* ④ 真机 App 表：与宿主同序（main.cpp 的注释要求两边登记一致）。--no-device 时整段不动。 */
  const mainOut = app.device
    ? addToAppTable(
      addInclude(readText(ESP32_MAIN), '#include "weather_app.h"',
        '#include "' + app.name + '_app.h"', 'platform/esp32/project/main/main.cpp'),
      app, 'platform/esp32/project/main/main.cpp')
    : '';

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

  const edits = [
    { rel: 'cmake/embark_sources.cmake', path: CMAKE_SOURCES, updated: cmakeOut },
    { rel: 'platform/host/ui_demo.cpp', path: UI_DEMO, updated: demoOut },
    { rel: 'app/common/aidogui_ui_hooks.h', path: UI_HOOKS, updated: hooksOut },
  ];
  if (app.device) edits.push({ rel: 'platform/esp32/project/main/main.cpp', path: ESP32_MAIN, updated: mainOut });

  /* 计划预览：逐处说清「插进去的是哪一行、前后两行长什么样」。 */
  const report = [insertEntry('cmake/embark_sources.cmake', 'EMBARK_APP_SOURCES', cmakeOut, srcLine.trim(), 1)];
  if (app.device) {
    report.push(insertEntry('cmake/embark_sources.cmake', 'EMBARK_APP_SOURCES_DEVICE', cmakeOut, srcLine.trim(), 2));
  }
  report.push(insertEntry('platform/host/ui_demo.cpp', 'include', demoOut, '#include "' + app.name + '/' + app.name + '_app.h"', 1));
  report.push(insertEntry('platform/host/ui_demo.cpp', 'App 表项 embark::demo::' + app.pascal, demoOut, 'embark::demo::' + app.pascal, 1));
  if (app.device) {
    report.push(insertEntry('platform/esp32/project/main/main.cpp', 'include', mainOut, '#include "' + app.name + '_app.h"', 1));
    report.push(insertEntry('platform/esp32/project/main/main.cpp', 'App 表项 embark::demo::' + app.pascal, mainOut, 'embark::demo::' + app.pascal, 1));
  }
  report.push(insertEntry('app/common/aidogui_ui_hooks.h', 'EMBARK_AIDOGUI_HOOK_APPS(X)', hooksOut, 'X(' + app.name + ')', 1));

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
    edits: edits,
    report: report,
  };
}

/* --- 退掉一个 App：正向 5 处登记的反向操作 ---------------------------------
   不去猜、不靠正则模糊匹配：逐处按「生成时写进去的那一行」精确比对，找不到就报错（说明有人
   手工改过，脚本不该替你瞎猜）。 */
function escapeRegExp(text) {
  return text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

/* 撤一行：找得到就撤（返回新文本 + 它原来在第几行），找不到返回 null ——
   由调用方决定「必须存在」（宿主那几处）还是「容忍缺失」（真机那几处，老 App 本来就没有）。 */
function removeExactLine(text, line) {
  const target = line.trim();
  const lines = text.split('\n');
  const at = lines.findIndex((l) => l.trim() === target);
  if (at < 0) return null;
  lines.splice(at, 1);
  return { text: lines.join('\n'), index: at };
}

function removeRequiredLine(text, line, where) {
  const out = removeExactLine(text, line);
  if (out === null) {
    fail(where + ' 里找不到要撤的那一行：「' + line.trim() + '」—— 有人手工改过？先看一眼再删');
  }
  return out;
}

/* App 表里那一项可能是「上一行末尾逗号 + 换行 + 缩进 + 类名」，也可能跟别人挤在同一行
   （table 里 album 就挨着 music）。两种都按相邻逗号撤：前一项有逗号就撤逗号到本项末尾，
   本项是表头第一个（前面是括号）就撤本项到后面的逗号。
   required=false 时找不到就跳过（真机表可能本来就没有它）。 */
function removeTableItem(text, app, where, required) {
  const needle = 'embark::demo::' + app.pascal;
  const re = new RegExp(escapeRegExp(needle) + '(?=[ \t]*[),])');
  const m = re.exec(text);
  if (!m) {
    if (required) fail(where + ' 的 App 表里找不到 ' + needle + '（可能被手工改过）');
    return null;
  }
  let start = m.index;
  let end = m.index + m[0].length;
  let cut = start;
  while (cut > 0 && (text[cut - 1] === ' ' || text[cut - 1] === '\t' || text[cut - 1] === '\n'
    || text[cut - 1] === '\r')) cut -= 1;
  if (text[cut - 1] === ',') {
    /* 前一项还留着，撤「逗号 → 本项末尾」（中间换行缩进一起没了） */
    start = cut - 1;
    while (start > 0 && (text[start - 1] === ' ' || text[start - 1] === '\t')) start -= 1;
    end = m.index + m[0].length;
  } else {
    /* 本项是表头第一个：撤「本项 → 后面的逗号 + 换行缩进」 */
    let after = end;
    while (after < text.length && (text[after] === ' ' || text[after] === '\t')) after += 1;
    if (text[after] === ',') {
      after += 1;
      if (text[after] === '\r') after += 1;
      if (text[after] === '\n') after += 1;
      while (after < text.length && (text[after] === ' ' || text[after] === '\t')) after += 1;
      end = after;
    }
  }
  const index = text.slice(0, m.index).split('\n').length - 1;
  return { text: text.slice(0, start) + text.slice(end), index: index, removed: needle };
}

function planDelete(app) {
  const dir = join(REPO_ROOT, 'app', app.name);
  if (!existsSync(join(dir, app.name + '_app.cpp'))) {
    fail('app/' + app.name + '/ 不像一个 App（缺 ' + app.name + '_app.cpp）—— 不敢按 App 删');
  }
  const srcLine = '        "${EMBARK_ROOT}/app/' + app.name + '/' + app.name + '_app.cpp"';
  const edits = [];
  const report = [];
  const absent = [];  // 「本来就没有」的真机登记：预览里说清楚，别让人以为撤漏了

  /* ① 宿主源清单（必须有）+ ② 真机源清单（老 App 可能本来没有，容忍） */
  const cmakeText = readText(CMAKE_SOURCES);
  const cmakeHost = removeRequiredLine(cmakeText, srcLine, 'cmake/embark_sources.cmake · EMBARK_APP_SOURCES');
  report.push(removeEntry('cmake/embark_sources.cmake', 'EMBARK_APP_SOURCES', cmakeText, cmakeHost.index));
  const cmakeDev = removeExactLine(cmakeHost.text, srcLine);
  if (cmakeDev === null) {
    absent.push('cmake/embark_sources.cmake · EMBARK_APP_SOURCES_DEVICE');
  } else {
    report.push(removeEntry('cmake/embark_sources.cmake', 'EMBARK_APP_SOURCES_DEVICE', cmakeHost.text, cmakeDev.index));
  }
  edits.push({
    rel: 'cmake/embark_sources.cmake',
    path: CMAKE_SOURCES,
    updated: cmakeDev === null ? cmakeHost.text : cmakeDev.text,
  });

  /* ③ 宿主 include + App 表项（必须有） */
  const demoText = readText(UI_DEMO);
  const demoInc = removeRequiredLine(demoText, '#include "' + app.name + '/' + app.name + '_app.h"', 'platform/host/ui_demo.cpp · include');
  report.push(removeEntry('platform/host/ui_demo.cpp', 'include', demoText, demoInc.index));
  const demoItem = removeTableItem(demoInc.text, app, 'platform/host/ui_demo.cpp', true);
  report.push(removeEntry('platform/host/ui_demo.cpp', 'App 表项 embark::demo::' + app.pascal,
    demoInc.text, demoItem.index, [demoItem.removed]));
  edits.push({ rel: 'platform/host/ui_demo.cpp', path: UI_DEMO, updated: demoItem.text });

  /* ④ 真机 include + App 表项（老 App 多半没有：容忍） */
  if (existsSync(ESP32_MAIN)) {
    const mainText = readText(ESP32_MAIN);
    let mainOut = mainText;
    const mainInc = removeExactLine(mainOut, '#include "' + app.name + '_app.h"');
    if (mainInc === null) {
      absent.push('platform/esp32/project/main/main.cpp · include');
    } else {
      report.push(removeEntry('platform/esp32/project/main/main.cpp', 'include', mainOut, mainInc.index));
      mainOut = mainInc.text;
    }
    const mainItem = removeTableItem(mainOut, app, 'platform/esp32/project/main/main.cpp', false);
    if (mainItem === null) {
      absent.push('platform/esp32/project/main/main.cpp · App 表项');
    } else {
      report.push(removeEntry('platform/esp32/project/main/main.cpp', 'App 表项 embark::demo::' + app.pascal,
        mainOut, mainItem.index, [mainItem.removed]));
      mainOut = mainItem.text;
    }
    if (mainOut !== mainText) edits.push({ rel: 'platform/esp32/project/main/main.cpp', path: ESP32_MAIN, updated: mainOut });
  }

  /* ⑤ hooks 宏体那行 X(名)（有的 App 从没登记过，容忍）；撤完把末行多余的续行反斜杠去掉 */
  const hooksText = readText(UI_HOOKS);
  const hookLines = hooksText.split('\n');
  const hookAt = hookLines.findIndex((l) => l.trim() === 'X(' + app.name + ')');
  if (hookAt < 0) {
    absent.push('app/common/aidogui_ui_hooks.h · X(' + app.name + ')');
  } else {
    report.push(removeEntry('app/common/aidogui_ui_hooks.h', 'EMBARK_AIDOGUI_HOOK_APPS(X)', hooksText, hookAt));
    hookLines.splice(hookAt, 1);
    for (let i = hookAt - 1; i >= 0; i -= 1) {
      if (hookLines[i].trim() === '') continue;
      hookLines[i] = hookLines[i].replace(/[ \t]*\\[ \t]*$/, '');
      break;
    }
    edits.push({ rel: 'app/common/aidogui_ui_hooks.h', path: UI_HOOKS, updated: hookLines.join('\n') });
  }

  return { dir: dir, rel: 'app/' + app.name + '/', edits: edits, report: report, absent: absent };
}

function printEntries(report, indent) {
  for (const entry of report) {
    const mark = entry.kind === 'insert' ? '+' : '-';
    console.log(indent + mark + ' ' + entry.rel + ' · ' + entry.label + '（第 ' + entry.line + ' 行）');
    for (const line of entry.before) console.log(indent + '      ' + line);
    for (const line of entry.lines) console.log(indent + '    ' + mark + ' ' + line);
    for (const line of entry.after) console.log(indent + '      ' + line);
  }
}

function printDeletePlan(app, plan, aidChoice) {
  console.log('退掉 App：' + app.name + '（类 ' + app.pascal + '）');
  console.log('');
  console.log('要删的目录：' + plan.rel + '（整个目录）');
  console.log('');
  console.log('要撤的登记（' + plan.report.length + ' 行）：');
  printEntries(plan.report, '  ');
  if (plan.absent.length > 0) {
    console.log('');
    console.log('它本来就没有的登记（老 App 允许，不用管）：');
    for (const item of plan.absent) console.log('  - ' + item);
  }
  console.log('');
  console.log('界面工程（' + aidChoice.source + '）：' + plan.screen.rel);
  if (plan.screen.action === 'no_aid') {
    console.log('  没读到 .aid，屏那一步跳过。');
  } else if (plan.screen.action === 'absent') {
    console.log('  里面本来就没有叫 ' + app.name + ' 的屏 —— 没什么要撤的。');
  } else if (plan.screen.action === 'keep') {
    console.log('  那块屏有 ' + plan.screen.widgets + ' 个控件，不是脚本当初抄出来的占位屏 —— 看着像你改过了，我不动它。');
    console.log('  真要删，只能你自己在设计器里删。');
  } else {
    console.log('  要删那张屏 ' + app.name + '（' + (plan.screen.action === 'remove_empty' ? '空屏' : '仍是脚本抄出来的占位屏') + '）。');
  }
  console.log('');
  console.log('它不动的东西（要你自己决定）：git 里的历史、别的 App 对它的引用。');
}

function applyDelete(app, plan, aidFile) {
  for (const edit of plan.edits) writeFileSync(edit.path, edit.updated, 'utf8');
  rmSync(plan.dir, { recursive: true, force: true });
  if (aidFile !== null && plan.screen.action !== 'absent' && plan.screen.action !== 'keep' && plan.screen.action !== 'no_aid') {
    writeFileSync(aidFile.path, serializeAid(aidFile), 'utf8');
  }
  console.log('');
  console.log('已撤 ' + plan.report.length + ' 行登记，已删 ' + plan.rel);
  if (aidFile !== null && (plan.screen.action === 'remove_empty' || plan.screen.action === 'remove_placeholder')) {
    console.log('已从 ' + plan.screen.rel + ' 里撤掉那块屏 ' + app.name + '。');
  }
  console.log('下一步：pwsh -File tools/aidogui/export.ps1（导出目录里还留着它的旧屏文件），再 cmake -S . -B build && cmake --build build。');
}
function printPlan(app, plan, aidChoice) {
  console.log('App：' + app.name + ' → 类 ' + app.pascal + '，标题「' + app.title + '」，图标 ' + app.icon
    + '，' + (app.device ? '真机也登记' : '只做宿主（--no-device）'));
  console.log('');
  console.log('要写的文件（2 个）：');
  for (const file of plan.files) {
    console.log('  ' + file.rel + '（' + file.content.split('\n').length + ' 行）');
  }
  console.log('');
  console.log('要登记的位置（' + plan.report.length + ' 行）：');
  printEntries(plan.report, '  ');
  console.log('');
  console.log('界面工程（' + aidChoice.source + '）：' + plan.screen.rel);
  if (plan.screen.action === 'no_aid') {
    console.log('  没读到 .aid，屏这一步跳过（用 --aid 指对位置）');
  } else if (plan.screen.action === 'exists') {
    console.log('  已经有一块叫 ' + app.name + ' 的屏（' + plan.screen.widgets + ' 个控件）—— 不动它，直接用。');
  } else if (plan.screen.action === 'fill_empty') {
    console.log('  .aid 里已经有一块叫 ' + app.name + ' 的**空屏**（0 个控件）—— 把 ' + plan.screen.template
      + ' 那套占位屏的 ' + plan.screen.widgets + ' 个控件填进去（空屏没内容可覆盖）。');
  } else {
    console.log('  要加一张屏 ' + app.name + '：抄 ' + plan.screen.template + ' 那块占位屏的 ' + plan.screen.widgets
      + ' 个控件（背景 / 返回键 / 标题 / 占位卡片），id 前缀换成 ' + app.name + '_');
    console.log('  返回键的事件沿用 embark_ui_open_launcher：建出来点进去就有页面，也能退回主屏。');
  }
}

function todoLines(app) {
  return [
    'pwsh -File tools/aidogui/export.ps1 重新导出（这一步才会生成屏的 C 代码）',
    'cmake -S . -B build && cmake --build build --target embark_host_ui',
    '改界面：node tools/aidogui/html_to_aid.mjs --from-aid ' + app.name
      + ' --out .scratch/draft/' + app.name + '.html，改完量回去：--html .scratch/draft/' + app.name
      + '.html --screen ' + app.name + ' --apply（也可以在设计器里直接画，屏名还是 ' + app.name + '）',
    '想让别的屏打开它：那个按钮的「调用函数」写 embark_ui_open_' + app.name
      + '（X(' + app.name + ') 已在 app/common/aidogui_ui_hooks.h 里）',
    'node tools/aidogui/gen_widget_doc.mjs 刷控件表；node tools/aidogui/dev.ps1 -Screen ' + app.name + ' 日常循环',
  ];
}

function printTodos(app) {
  console.log('');
  console.log('屏侧（.aid 里已经补了一张占位屏，屏名 == App 名）：');
  todoLines(app).forEach((line, i) => console.log('  ' + (i + 1) + '. ' + line));
  console.log('');
  console.log('没做的（有意）：不生成屏的 C 代码（那是设计器导出的事）。');
}

function writePlan(app, plan, aidFile) {
  mkdirSync(join(REPO_ROOT, 'app', app.name), { recursive: true });
  for (const file of plan.files) writeFileSync(file.path, file.content, 'utf8');
  for (const edit of plan.edits) writeFileSync(edit.path, edit.updated, 'utf8');
  console.log('');
  console.log('已写（UTF-8 无 BOM）：');
  for (const file of plan.files) console.log('  ' + file.rel);
  console.log('已登记 ' + plan.report.length + ' 行：');
  for (const entry of plan.report) console.log('  + ' + entry.rel + ' · ' + entry.label);
  if (plan.screen.action === 'insert') {
    writeFileSync(aidFile.path, serializeAid(aidFile), 'utf8');
    console.log('已在 ' + plan.screen.rel + ' 里加了一张屏 ' + app.name + '（' + plan.screen.widgets + ' 个控件）');
  } else {
    console.log('界面工程里已经有一块叫 ' + app.name + ' 的屏，没动它');
  }
  console.log('没有自动 git add —— 先 git diff 看一眼再决定。');
}

function parseArgs(argv) {
  const options = {
    name: null, title: null, icon: null, aid: null,
    apply: false, delete: false, selfCheck: false, status: false, json: false,
    device: true, help: false, warnings: [],
  };
  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--apply') options.apply = true;
    else if (arg === '--delete') options.delete = true;
    else if (arg === '--dry-run') options.apply = false;
    else if (arg === '--self-check') options.selfCheck = true;
    else if (arg === '--status') options.status = true;
    else if (arg === '--json') options.json = true;
    else if (arg === '--device') options.device = true;
    else if (arg === '--no-device') options.device = false;
    else if (arg === '--help' || arg === '-h') options.help = true;
    else if (arg === '--title') {
      i += 1;
      if (i >= argv.length) fail('--title 后面要跟中文名');
      options.title = argv[i];
    } else if (arg === '--icon') {
      i += 1;
      if (i >= argv.length) fail('--icon 后面要跟 LV_SYMBOL_X');
      options.icon = argv[i];
    } else if (arg === '--aid') {
      i += 1;
      if (i >= argv.length) fail('--aid 后面要跟 .aid 文件路径');
      options.aid = argv[i];
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
  if (options.delete) {
    if (PROTECTED_APPS.indexOf(name) >= 0) {
      fail(name + ' 是主屏（启动器）—— 删了就回不到桌面，不给删');
    }
    if (!existsSync(join(REPO_ROOT, 'app', name))) {
      fail('app/' + name + ' 不存在 —— 没东西可删');
    }
    return { name: name, title: name, icon: 'LV_SYMBOL_FILE', pascal: classFor(name), date: '', device: options.device };
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
  const titleChars = Array.from(title).length;
  if (titleChars > 5) {
    options.warnings.push('标题「' + title + '」有 ' + titleChars + ' 个字：主屏磁贴的标签宽 = 磁贴宽、'
      + '14px 字号、默认换行，一行约 3 个汉字 —— 超了会被裁且不报错，建议 ≤5 个字。');
  }
  return { name: name, title: title, icon: icon, pascal: classFor(name), date: date, device: options.device };
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
  /* .aid 侧：占位屏还抄得出来，且原样写回字节不变（否则「加了又撤」会留 diff）。 */
  if (!existsSync(AID_DEFAULT)) {
    problems.push('找不到 .aid：' + relOf(AID_DEFAULT));
  } else {
    try {
      const aid = readAid(AID_DEFAULT);
      const template = pickTemplateScreen(aid.obj);
      if (template.name !== PLACEHOLDER_TEMPLATE_SCREEN) {
        problems.push('.aid 里没有 ' + PLACEHOLDER_TEMPLATE_SCREEN + ' 那块占位屏，改成抄 ' + template.name + '（能跑，但占位屏的样式可能不对）');
      }
      if (!JSON.stringify(template.widgets || []).includes('embark_ui_open_launcher')) {
        problems.push('占位屏 ' + template.name + ' 的返回键不再绑 embark_ui_open_launcher');
      }
      buildPlaceholderScreen(aid.obj, { name: 'zz_self_check', title: '自检' });
      if (serializeAid(aid) !== aid.raw) {
        problems.push('.aid 原样写回不是字节一致（JSON.stringify(obj, null, 2) 的形状变了？加了又撤会留 diff）');
      }
    } catch (error) {
      problems.push('.aid 侧演练失败：' + error.message);
    }
  }
  if (problems.length === 0) {
    /* 再演练一次生成：模板替换、guard、App 表缩进、宏续行这些只有真跑才暴露。 */
    try {
      planFor({ name: 'zz_self_check', title: '自检', icon: 'LV_SYMBOL_FILE', pascal: 'ZzSelfCheck', date: '2026-01-01', device: true });
    } catch (error) {
      problems.push('演练生成失败：' + error.message);
    }
  }
  if (problems.length > 0) {
    console.error('〔自检失败〕脚本依赖的东西已经变了：');
    for (const problem of problems) console.error('  - ' + problem);
    return 1;
  }
  console.log('〔自检通过〕模板 app/music/、6 个登记锚点（落成 7 行改动）、.aid 里的占位屏 ' + PLACEHOLDER_TEMPLATE_SCREEN + ' 都还在，照现在的形状生成没问题。');
  return 0;
}

/* --- --status：每个 App 登记齐了没有 --------------------------------------
   App 管理台不重复这份知识（哪几处登记、占位屏长什么样）：它只转发这里的结果。 */

function describeApp(name, ctx) {
  const dir = join(REPO_ROOT, 'app', name);
  const headerPath = join(dir, name + '_app.h');
  const header = existsSync(headerPath) ? readText(headerPath) : '';
  const pascalMatch = /class\s+(\w+)\s+final\s*:\s*public\s+App/.exec(header);
  const pascal = pascalMatch ? pascalMatch[1] : classFor(name);
  const titleMatch = /title\(\)\s*const\s*override\s*\{\s*return\s*"([^"]*)"/.exec(header);
  const iconMatch = /icon\(\)\s*const\s*override[\s\S]{0,200}?(LV_SYMBOL_[A-Z0-9_]+)/.exec(header);
  const srcNeedle = '"${EMBARK_ROOT}/app/' + name + '/' + name + '_app.cpp"';
  const srcCount = ctx.cmakeText.split(srcNeedle).length - 1;
  const registrations = [
    { key: 'cmake_host', label: '宿主源清单', present: srcCount >= 1 },
    { key: 'host_include', label: '宿主 App 头', present: ctx.demoText.includes('#include "' + name + '/' + name + '_app.h"') },
    { key: 'host_table', label: '宿主 App 表', present: ctx.demoText.includes('embark::demo::' + pascal) },
    { key: 'hooks', label: '导航入口', present: ctx.hooksText.includes('X(' + name + ')') },
    { key: 'cmake_device', label: '真机源清单', present: srcCount >= 2 },
    { key: 'device_include', label: '真机 App 头', present: ctx.mainText !== '' && ctx.mainText.includes('#include "' + name + '_app.h"') },
    { key: 'device_table', label: '真机 App 表', present: ctx.mainText !== '' && ctx.mainText.includes('embark::demo::' + pascal) },
  ];
  const hostKeys = ['cmake_host', 'host_include', 'host_table', 'hooks'];
  const deviceKeys = ['cmake_device', 'device_include', 'device_table'];

  let screen = { name: name, state: 'unknown', widgets: 0 };
  if (ctx.aidObj) {
    const found = (ctx.aidObj.screens || []).find((item) => item.name === name);
    if (!found) {
      screen = { name: name, state: 'missing', widgets: 0 };
    } else {
      const widgets = Array.isArray(found.widgets) ? found.widgets : [];
      let placeholder = false;
      try {
        const expected = buildPlaceholderScreen(ctx.aidObj, { name: name, title: titleMatch ? titleMatch[1] : name })
          .widgets.map((widget) => widget.name).join(',');
        placeholder = widgets.length > 0 && widgets.map((widget) => widget.name).join(',') === expected;
      } catch (error) {
        placeholder = false;
      }
      screen = {
        name: name,
        state: widgets.length === 0 ? 'empty' : (placeholder ? 'placeholder' : 'designed'),
        widgets: widgets.length,
      };
    }
  }

  return {
    name: name,
    pascal: pascal,
    title: titleMatch ? titleMatch[1] : '',
    icon: iconMatch ? iconMatch[1] : '',
    protected: PROTECTED_APPS.indexOf(name) >= 0,
    registrations: registrations,
    host: { done: registrations.filter((item) => item.present && hostKeys.indexOf(item.key) >= 0).length, total: hostKeys.length },
    device: { done: registrations.filter((item) => item.present && deviceKeys.indexOf(item.key) >= 0).length, total: deviceKeys.length },
    screen: screen,
  };
}

function statusReport() {
  const aidChoice = resolveAidPath({ aid: null });
  const aid = { rel: relOf(aidChoice.path), path: aidChoice.path, source: aidChoice.source, ok: false, error: null, screens: [] };
  let aidObj = null;
  try {
    const file = readAid(aidChoice.path);
    aidObj = file.obj;
    aid.ok = true;
    aid.screens = file.obj.screens.map((screen) => ({
      name: screen.name,
      widgets: Array.isArray(screen.widgets) ? screen.widgets.length : 0,
    }));
  } catch (error) {
    aid.error = error.message;
  }
  const ctx = {
    cmakeText: readText(CMAKE_SOURCES),
    demoText: readText(UI_DEMO),
    hooksText: readText(UI_HOOKS),
    mainText: existsSync(ESP32_MAIN) ? readText(ESP32_MAIN) : '',
    aidObj: aidObj,
  };
  const apps = [];
  for (const entry of readdirSync(join(REPO_ROOT, 'app'), { withFileTypes: true })) {
    if (!entry.isDirectory()) continue;
    if (!existsSync(join(REPO_ROOT, 'app', entry.name, entry.name + '_app.cpp'))) continue;
    apps.push(describeApp(entry.name, ctx));
  }
  apps.sort((a, b) => (a.protected === b.protected ? a.name.localeCompare(b.name) : (a.protected ? -1 : 1)));
  return { ok: true, aid: aid, designer: resolveDesigner(), apps: apps, warnings: [] };
}

function printStatus(report) {
  console.log('界面工程：' + report.aid.rel
    + (report.aid.ok ? '（' + report.aid.screens.length + ' 块屏）' : '（读不到：' + report.aid.error + '）'));
  console.log('设计器  ：' + (report.designer.ok ? report.designer.exe : '没找到（设 AIDOGUI_HOME 或写 .aidogui-home）'));
  console.log('');
  for (const app of report.apps) {
    console.log('  ' + app.name + ' · ' + app.pascal + ' ·「' + app.title + '」 · ' + app.icon
      + ' · 宿主 ' + app.host.done + '/' + app.host.total
      + ' · 真机 ' + app.device.done + '/' + app.device.total
      + ' · 屏 ' + app.screen.state + (app.screen.widgets ? '(' + app.screen.widgets + ')' : ''));
  }
}

function silenceHumanOutput() {
  const muted = () => {};
  console.log = muted;
  console.warn = muted;
}

function reportJson(payload) {
  process.stdout.write(JSON.stringify(payload, null, 2) + '\n');
  return payload.ok === false ? 1 : 0;
}

function main() {
  const options = parseArgs(process.argv.slice(2));
  if (options.help) {
    console.log(USAGE);
    return 0;
  }
  if (options.selfCheck) return selfCheck();
  if (options.json) silenceHumanOutput();
  if (options.status) {
    const report = statusReport();
    if (options.json) return reportJson(report);
    printStatus(report);
    return 0;
  }
  const app = validate(options);
  if (!options.json) {
    for (const warning of options.warnings) console.warn('提醒：' + warning);
  }
  const aidChoice = resolveAidPath(options);
  const aidFile = loadAidOrWarn(aidChoice, options.warnings);
  const base = {
    ok: true,
    app: { name: app.name, pascal: app.pascal, title: app.title, icon: app.icon, device: app.device },
    aid: { rel: relOf(aidChoice.path), source: aidChoice.source },
    applied: options.apply,
    warnings: options.warnings,
  };

  if (options.delete) {
    const gone = planDelete(app);
    gone.screen = aidFile === null ? { rel: relOf(aidChoice.path), action: 'no_aid' } : planScreenRemoval(aidFile, app);
    printDeletePlan(app, gone, aidChoice);
    if (!options.apply) {
      console.log('');
      console.log('（dry-run：一个字节都没删。加 --apply 才真删。）');
    } else {
      applyDelete(app, gone, aidFile);
    }
    if (options.json) {
      return reportJson(Object.assign(base, {
        mode: 'delete',
        dir: gone.rel,
        edits: gone.report,
        absent: gone.absent,
        screen: gone.screen,
      }));
    }
    return 0;
  }

  const plan = planFor(app);
  plan.screen = aidFile === null ? { rel: relOf(aidChoice.path), action: 'no_aid' } : planScreen(aidFile, app);
  printPlan(app, plan, aidChoice);
  if (!options.apply) {
    console.log('');
    console.log('（dry-run：一个字节都没写。加 --apply 才落盘。）');
  } else {
    writePlan(app, plan, aidFile);
    printTodos(app);
  }
  if (options.json) {
    return reportJson(Object.assign(base, {
      mode: 'create',
      files: plan.files.map((file) => ({ rel: file.rel, lines: file.content.split('\n').length })),
      edits: plan.report,
      absent: [],
      screen: plan.screen,
      next: todoLines(app),
    }));
  }
  return 0;
}

const jsonMode = process.argv.indexOf('--json') >= 0;
try {
  process.exitCode = main();
} catch (error) {
  if (jsonMode) {
    process.stdout.write(JSON.stringify({ ok: false, error: error.message }, null, 2) + '\n');
  } else {
    console.error('错误：' + error.message);
  }
  process.exitCode = 1;
}

