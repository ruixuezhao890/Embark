#!/usr/bin/env node
/* App 管理台 —— 本地网页控制台（零依赖，只用 Node 内置模块）。
 *
 * 它不做第二套逻辑：加 / 退 App 的登记知识全在 tools/new_app.mjs 里，这里只负责
 *   - 把 --status --json 的结果摆成一张表
 *   - 把 --json 的计划原样画出来（改了哪几行、插进去什么片段）
 *   - 点「建它」时依次跑 new_app.mjs --apply → tools/aidogui/export.ps1 → cmake 构建
 *   - 日志走 SSE，一个时刻只跑一个任务
 *
 * 不做（有意）：实时画面、点画面、串口 / 烧录、跑验收故事。那些是命令行的事
 * （见 docs/guides/tools.md 与 docs/guides/daily-loop.md）。
 */
import http from 'node:http';
import { spawn } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(HERE, '..', '..');
const NEW_APP = path.join(REPO_ROOT, 'tools', 'new_app.mjs');
const EXPORT_PS1 = path.join(REPO_ROOT, 'tools', 'aidogui', 'export.ps1');
const INDEX_HTML = path.join(HERE, 'index.html');
const BUILD_DIR = path.join(REPO_ROOT, 'build');

const START_PORT = Number(process.env.EMBARK_APPCONSOLE_PORT || 7788);
const MAX_PORT_TRIES = 12;
const RING = 800;
const NO_OPEN = process.argv.indexOf('--no-open') >= 0;

/* --- 日志：单流，按通道打标签，SSE 推给页面 --------------------------------- */

const ring = [];
const clients = new Set();

function cleanAnsi(text) {
  /* eslint-disable no-control-regex */
  return text.replace(/\u001b\[[0-9;]*[A-Za-z]/g, '').replace(/\r/g, '');
}

function log(channel, text) {
  for (const line of cleanAnsi(String(text)).split('\n')) {
    if (line.trim() === '') continue;
    const entry = { at: Date.now(), channel: channel, text: line };
    ring.push(entry);
    if (ring.length > RING) ring.shift();
    console.log('[' + channel + '] ' + line);
    const payload = 'data: ' + JSON.stringify(entry) + '\n\n';
    for (const client of clients) {
      try {
        client.write(payload);
      } catch (error) {
        clients.delete(client);
      }
    }
  }
}

/* --- 起子进程：输出逐行进日志 ---------------------------------------------- */

function run(file, args, channel, options) {
  const opts = options || {};
  return new Promise((resolve) => {
    let child = null;
    try {
      child = spawn(file, args, { cwd: REPO_ROOT, windowsHide: true, env: process.env });
    } catch (error) {
      log('error', file + ' 起不来：' + error.message);
      resolve({ code: -1, out: '', ok: false });
      return;
    }
    let out = '';
    child.stdout.on('data', (chunk) => {
      out += chunk.toString();
      if (!opts.quietStdout) log(channel, chunk.toString());
    });
    child.stderr.on('data', (chunk) => {
      out += chunk.toString();
      log(channel, chunk.toString());
    });
    child.on('error', (error) => {
      log('error', file + '：' + error.message);
      resolve({ code: -1, out: out, ok: false });
    });
    child.on('close', (code) => {
      const finalCode = code === null ? -1 : code;
      resolve({ code: finalCode, out: out, ok: finalCode === 0 });
    });
  });
}

/* new_app.mjs 的 --json 只往 stdout 写一段 JSON（人看的话都被静音了） */
async function runNewApp(args) {
  const res = await run('node', [NEW_APP].concat(args, ['--json']), 'new_app', { quietStdout: true });
  let data = null;
  const start = res.out.indexOf('{');
  if (start >= 0) {
    try {
      data = JSON.parse(res.out.slice(start));
    } catch (error) {
      data = null;
    }
  }
  if (data !== null) {
    for (const warning of data.warnings || []) log('warn', warning);
  }
  return { code: res.code, ok: res.ok, data: data, raw: res.out };
}

/* --- 一个时刻只跑一个任务（导出会清空 export/，两个任务撞上就废了）--------- */

let busy = null;

async function withTask(label, fn) {
  if (busy !== null) {
    const error = new Error('还有任务在跑：' + busy + '。等它结束再点。');
    error.status = 409;
    throw error;
  }
  busy = label;
  log('panel', '▶ ' + label);
  try {
    return await fn();
  } finally {
    busy = null;
    log('panel', '■ 结束：' + label);
  }
}

/* --- 三个动作：导出屏 / 构建宿主 / 打开设计器 ------------------------------ */

async function exportScreens() {
  if (!existsSync(EXPORT_PS1)) return { step: 'export', ok: false, code: -1, error: 'tools/aidogui/export.ps1 不在' };
  log('panel', '导出屏会先清空 app/aidogui_ui/export/ 再按 .aid 重新生成 —— export/ 里的东西别手改');
  const res = await run('pwsh', ['-NoProfile', '-File', EXPORT_PS1], 'export');
  return { step: 'export', ok: res.ok, code: res.code };
}

async function buildHost() {
  const configure = await run('cmake', ['-S', REPO_ROOT, '-B', BUILD_DIR], 'build');
  if (!configure.ok) return { step: 'build', ok: false, code: configure.code, error: 'cmake 配置没过' };
  const build = await run('cmake', ['--build', BUILD_DIR, '--target', 'embark_host_ui'], 'build');
  return { step: 'build', ok: build.ok, code: build.code };
}

async function designerTarget() {
  const state = await runNewApp(['--status']);
  const data = state.data || {};
  return {
    exe: data.designer && data.designer.ok ? data.designer.exe : null,
    aid: data.aid && data.aid.path ? data.aid.path : null,
    designer: data.designer || null,
  };
}

/* --- HTTP ------------------------------------------------------------------ */

function sendJson(res, status, payload) {
  const body = JSON.stringify(payload, null, 2);
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
  res.end(body);
}

function sendFile(res, file, type) {
  if (!existsSync(file)) {
    sendJson(res, 404, { ok: false, error: '少了文件：' + file });
    return;
  }
  res.writeHead(200, { 'Content-Type': type, 'Cache-Control': 'no-store' });
  res.end(readFileSync(file));
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let body = '';
    req.on('data', (chunk) => {
      body += chunk.toString();
      if (body.length > 200000) reject(new Error('请求体太大'));
    });
    req.on('end', () => {
      if (body.trim() === '') {
        resolve({});
        return;
      }
      try {
        resolve(JSON.parse(body));
      } catch (error) {
        reject(new Error('请求体不是 JSON'));
      }
    });
    req.on('error', reject);
  });
}

/* 把表单拼成 new_app.mjs 的参数（页面只传事实，校验仍在脚本里） */
function newAppArgs(body, apply) {
  const mode = body.mode === 'delete' ? 'delete' : 'create';
  const name = String(body.name || '').trim();
  if (name === '') {
    const error = new Error('先填 App 名');
    error.status = 400;
    throw error;
  }
  const args = [name];
  if (mode === 'delete') {
    args.push('--delete');
  } else {
    if (body.title) args.push('--title', String(body.title));
    if (body.icon) args.push('--icon', String(body.icon));
    args.push(body.device === false ? '--no-device' : '--device');
  }
  args.push(apply ? '--apply' : '--dry-run');
  return { mode: mode, name: name, args: args };
}

function wantedSteps(body) {
  const steps = Array.isArray(body.steps) ? body.steps : ['export', 'build'];
  return steps.filter((step) => step === 'export' || step === 'build');
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://127.0.0.1');
  try {
    if (req.method === 'GET' && (url.pathname === '/' || url.pathname === '/index.html')) {
      sendFile(res, INDEX_HTML, 'text/html; charset=utf-8');
      return;
    }

    if (req.method === 'GET' && url.pathname === '/api/events') {
      res.writeHead(200, {
        'Content-Type': 'text/event-stream; charset=utf-8',
        'Cache-Control': 'no-store',
        Connection: 'keep-alive',
      });
      res.write('retry: 2000\n\n');
      for (const entry of ring) res.write('data: ' + JSON.stringify(entry) + '\n\n');
      clients.add(res);
      const keep = setInterval(() => {
        try {
          res.write(': keep\n\n');
        } catch (error) {
          clearInterval(keep);
        }
      }, 20000);
      req.on('close', () => {
        clearInterval(keep);
        clients.delete(res);
      });
      return;
    }

    if (req.method === 'GET' && url.pathname === '/api/state') {
      const state = await runNewApp(['--status']);
      if (state.data === null) {
        sendJson(res, 500, { ok: false, error: 'new_app.mjs --status 没给出 JSON', raw: state.raw.slice(0, 2000) });
        return;
      }
      state.data.busy = busy;
      state.data.port = server.address() ? server.address().port : START_PORT;
      state.data.repo = REPO_ROOT;
      sendJson(res, 200, state.data);
      return;
    }

    if (req.method === 'POST' && url.pathname === '/api/plan') {
      const body = await readBody(req);
      const plan = newAppArgs(body, false);
      const result = await runNewApp(plan.args);
      if (result.data === null) {
        sendJson(res, 500, { ok: false, error: '计划没给出 JSON', raw: result.raw.slice(0, 2000) });
        return;
      }
      result.data.mode = plan.mode;
      sendJson(res, result.data.ok === false ? 400 : 200, result.data);
      return;
    }

    if (req.method === 'POST' && url.pathname === '/api/apply') {
      const body = await readBody(req);
      const plan = newAppArgs(body, true);
      const steps = wantedSteps(body);
      const label = (plan.mode === 'delete' ? '退掉 ' : '新建 ') + plan.name
        + (steps.length > 0 ? '（+ ' + steps.join(' / ') + '）' : '');
      const result = await withTask(label, async () => {
        const applied = await runNewApp(plan.args);
        if (applied.data === null || applied.data.ok === false) {
          return {
            ok: false,
            mode: plan.mode,
            error: (applied.data && applied.data.error) || '文件没改成（看日志）',
            data: applied.data,
            steps: [],
          };
        }
        const done = [];
        for (const step of steps) {
          const outcome = step === 'export' ? await exportScreens() : await buildHost();
          done.push(outcome);
          if (!outcome.ok) break;
        }
        return {
          ok: done.every((item) => item.ok),
          mode: plan.mode,
          data: applied.data,
          steps: done,
        };
      });
      sendJson(res, 200, result);
      return;
    }

    if (req.method === 'POST' && url.pathname === '/api/export') {
      const outcome = await withTask('导出屏', exportScreens);
      sendJson(res, 200, { ok: outcome.ok, steps: [outcome] });
      return;
    }

    if (req.method === 'POST' && url.pathname === '/api/build') {
      const outcome = await withTask('构建 embark_host_ui', buildHost);
      sendJson(res, 200, { ok: outcome.ok, steps: [outcome] });
      return;
    }

    if (req.method === 'POST' && url.pathname === '/api/designer') {
      const target = await designerTarget();
      if (target.exe === null) {
        sendJson(res, 400, {
          ok: false,
          error: '没找到 AiDoGUIDesigner.exe：设环境变量 AIDOGUI_HOME，或在仓库根写一行 .aidogui-home，'
            + '或写 tools/aidogui/home.txt（见 tools/aidogui/export.ps1 的查找顺序）',
          designer: target.designer,
        });
        return;
      }
      const args = target.aid === null ? [] : [target.aid];
      const child = spawn(target.exe, args, { cwd: REPO_ROOT, detached: true, stdio: 'ignore' });
      child.unref();
      log('panel', '打开设计器：' + target.exe + (target.aid ? ' ' + target.aid : ''));
      sendJson(res, 200, { ok: true, exe: target.exe, aid: target.aid });
      return;
    }

    sendJson(res, 404, { ok: false, error: '没有这个接口：' + req.method + ' ' + url.pathname });
  } catch (error) {
    sendJson(res, error.status || 500, { ok: false, error: error.message });
  }
});

/* --- 端口：7788 被占就 +1，最多试 12 次 ------------------------------------ */

function listen(port, tries) {
  server.once('error', (error) => {
    if (error.code === 'EADDRINUSE' && tries > 0) {
      log('panel', '端口 ' + port + ' 被占，试 ' + (port + 1));
      listen(port + 1, tries - 1);
      return;
    }
    console.error('起不来：' + error.message);
    process.exitCode = 1;
  });
  server.listen(port, '127.0.0.1', () => {
    const actual = server.address().port;
    const address = 'http://127.0.0.1:' + actual + '/';
    console.log('App 管理台：' + address);
    console.log('仓库：' + REPO_ROOT);
    console.log('（Ctrl+C 停。加 --no-open 不自动开浏览器；端口用 EMBARK_APPCONSOLE_PORT 改）');
    log('panel', 'App 管理台在 ' + address);
    if (!NO_OPEN) {
      const command = process.platform === 'win32' ? 'cmd' : (process.platform === 'darwin' ? 'open' : 'xdg-open');
      const args = process.platform === 'win32' ? ['/c', 'start', '', address] : [address];
      try {
        spawn(command, args, { detached: true, stdio: 'ignore', windowsHide: true }).unref();
      } catch (error) {
        console.log('（自动开浏览器失败，自己点上面那个地址）');
      }
    }
  });
}

listen(START_PORT, MAX_PORT_TRIES);
