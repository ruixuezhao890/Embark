/**
 * AiDoGUI Designer 无头代码生成驱动器（在 AiDoGUIDesigner.exe 的 Node 环境里跑）
 *
 * 用法：ELECTRON_RUN_AS_NODE=1 AiDoGUIDesigner.exe tools/aidogui/gen_ui.cjs <工程.aid> <输出目录> [图片格式] [字体格式]
 * 一般来说不用直接调它，走 tools/aidogui/export.ps1。
 *
 * 为什么要这么绕：AiDoGUI 的生成器（generateLvglCCode）藏在 resources/app.asar/out/main/index-*.js
 * 里，那个 bundle 在模块顶层就 require('electron')，普通 Node 起不来。所以：
 *   1) 用官方 exe + ELECTRON_RUN_AS_NODE=1 借它的 Node 运行时（不需要开 GUI）；
 *   2) 拦 Module._load，把 electron / electron-log 换成惰性桩（任何属性都能读写调用）；
 *   3) 遍历 out/main/index-*.js，挑出真正导出 generateLvglCCode 的那个，避免写死打包哈希。
 * 模板目录用 app.asar.unpacked/resources/lvgl/templates/c（磁盘上的真文件，不在 asar 里）。
 */
'use strict';
const fs = require('fs');
const path = require('path');
const Module = require('module');

const [projPath, outDir, imgFmt = 'BIN', fontFmt = 'BIN'] = process.argv.slice(2);
if (!projPath || !outDir) {
  console.error('用法: gen_ui.cjs <工程.aid> <输出目录> [BIN|C] [BIN|C]');
  process.exit(2);
}

// AiDoGUI 安装目录 ← 当前进程的 exe 路径，无需外部传参
const appRoot = path.dirname(process.execPath);
const asar = path.join(appRoot, 'resources', 'app.asar');
const templateRoot = path.join(appRoot, 'resources', 'app.asar.unpacked', 'resources', 'lvgl', 'templates', 'c');
if (!fs.existsSync(asar)) { console.error('找不到 app.asar: ' + asar); process.exit(2); }
if (!fs.existsSync(templateRoot)) { console.error('找不到 C 模板目录: ' + templateRoot); process.exit(2); }

// electron / electron-log 惰性桩：任意深度的属性读写、调用、new 都不炸
process.type = 'browser';
const makeStub = (name) => new Proxy(function () {}, {
  get(t, k) {
    if (k === 'then' || k === Symbol.toPrimitive || k === 'toJSON') return undefined;
    if (k === 'toString') return () => '[stub ' + name + ']';
    if (k === 'isPackaged') return false;
    if (k === 'whenReady') return () => new Promise(function () {});
    if (k === 'on' || k === 'once') return () => undefined;
    if (k === 'requestSingleInstanceLock') return () => true;
    if (k === 'getPath') return () => path.dirname(outDir);
    if (k === 'getAppPath') return () => asar;
    if (!(k in t)) t[k] = makeStub(name + '.' + String(k));
    return t[k];
  },
  apply() { return makeStub(name + '()'); },
  construct() { return makeStub('new ' + name); },
});
const electronStub = makeStub('electron');
const logStub = makeStub('electron-log');
const origLoad = Module._load;
Module._load = function (request) {
  if (request === 'electron') return electronStub;
  if (typeof request === 'string' && request.startsWith('electron-log')) return logStub;
  return origLoad.apply(this, arguments);
};

function loadGenerator() {
  const mainDir = path.join(asar, 'out', 'main');
  const candidates = fs.readdirSync(mainDir).filter((f) => /^index-.*\.js$/.test(f));
  const errors = [];
  for (const file of candidates) {
    try {
      const mod = require(path.join(mainDir, file));
      if (mod && typeof mod.generateLvglCCode === 'function') { return { mod, file }; }
    } catch (err) { errors.push(file + ': ' + (err && err.message)); }
  }
  throw new Error('没有找到导出 generateLvglCCode 的 bundle（试过 ' + candidates.join(', ') + '）: ' + errors.join(' | '));
}

(async () => {
  const { mod, file } = loadGenerator();
  const project = JSON.parse(fs.readFileSync(projPath, 'utf8'));
  project.imageOutputFormat = imgFmt;
  project.fontOutputFormat = fontFmt;
  project.exportSettings = Object.assign({}, project.exportSettings, {
    uiOutputPath: outDir, flatExport: false, imageBinRootPath: 'S:/images', fontBinRootPath: 'S:/fonts',
  });
  const started = Date.now();
  const result = await mod.generateLvglCCode(JSON.stringify(project), { outputDir: outDir, templateRoot, projectFilePath: projPath });
  console.log('RESULT: ' + JSON.stringify(Object.assign({ bundle: file, seconds: Number(((Date.now() - started) / 1000).toFixed(2)) }, result)));
  if (result && result.success === false) process.exit(1);
})().catch((err) => { console.error('生成失败: ' + (err && err.stack ? err.stack : String(err))); process.exit(1); });
