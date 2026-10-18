#!/usr/bin/env node
// Embark 开发台（后端）。零依赖：只用 Node 内置模块，不装任何 npm 包。
//
// 为什么是「本地网页」而不是原生窗口程序：用户不习惯命令行，管理类操作要走键鼠交互；
// 网页零安装、跨平台、改一次全部生效，命令行的东西一个都没删（出问题照样能查）。
// 它不重写任何逻辑 —— 只是把已有的脚本摆成按钮：
//   new_app.mjs（建 / 退 App）/ export.ps1 / html_to_aid.mjs / cmake / embark_host_ui /
//   embark_host_tour / tools/esp/idf.ps1（真机构建、烧录）/ tools/esp/serial.ps1（看串口）
//
// 启动：双击 tools/devpanel/start.cmd（或 node tools/devpanel/server.mjs --no-open）
// 只监听 127.0.0.1 —— 这是你自己机器上的开发工具，不对外。
import { createServer } from "node:http";
import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(HERE, "..", "..");
const HOST_EXE = path.join(REPO_ROOT, "build", "platform", "host", "embark_host_ui.exe");
const TOUR_EXE = path.join(REPO_ROOT, "build", "platform", "host", "embark_host_tour.exe");
// 预览图必须落在**仓库内**：宿主进程写仓库外的绝对路径会失败（docs/reference/pitfalls.md 记过）。
const LIVE_BMP_REL = path.join("build", "devpanel-live.bmp");
const LIVE_BMP = path.join(REPO_ROOT, LIVE_BMP_REL);
const INDEX_HTML = path.join(HERE, "index.html");
const ENV = Object.assign({}, process.env, { PYTHONUTF8: "1" });

// 界面工程（.aid）可以换：面板选中的那份只影响「导出」，导出目录永远是 app/aidogui_ui/export/
// —— 因为 cmake 在配置期就扫死了那个目录（cmake/embark_aidogui_tables.cmake）。
const AID_DEFAULT = "app/aidogui_ui/screen.aid";  // 统一用正斜杠，和 listAids() 的输出对得上
const STATE_FILE = path.join(HERE, ".state.json");
const state = { aid: AID_DEFAULT, port: "", screen: "" };

function loadState() {
  try {
    const raw = JSON.parse(fs.readFileSync(STATE_FILE, "utf8"));
    if (raw && typeof raw.aid === "string" && fs.existsSync(path.join(REPO_ROOT, raw.aid))) state.aid = raw.aid;
    if (raw && typeof raw.port === "string") state.port = raw.port;
    if (raw && typeof raw.screen === "string") state.screen = raw.screen;
  } catch (e) { /* 第一次跑还没有这个文件 */ }
}

function saveState() {
  try { fs.writeFileSync(STATE_FILE, JSON.stringify(state, null, 2) + "\n", "utf8"); } catch (e) { /* 写不了就算了 */ }
}
loadState();

// --- 日志总线：所有子进程输出都汇到这里，网页用 SSE 实时收 -------------------
const logLines = [];
const clients = new Set();
let logSeq = 0;

function emit(channel, text) {
  const stamp = Date.now();
  const parts = String(text).split(/\r?\n/);
  for (const line of parts) {
    if (line.trim() === "") continue;
    logSeq += 1;
    const rec = { id: logSeq, t: stamp, ch: channel, text: line };
    logLines.push(rec);
    if (logLines.length > 1500) logLines.splice(0, logLines.length - 1500);
    const payload = "data: " + JSON.stringify(rec) + "\n\n";
    for (const res of clients) {
      try { res.write(payload); } catch (e) { clients.delete(res); }
    }
  }
}

// --- 子进程 ----------------------------------------------------------------
function run(cmd, args, opts) {
  const options = opts || {};
  return new Promise((resolve) => {
    emit("cmd", "$ " + [cmd].concat(args).join(" "));
    let child;
    try {
      child = spawn(cmd, args, {
        cwd: options.cwd || REPO_ROOT,
        env: ENV,
        windowsHide: true,
      });
    } catch (err) {
      emit("error", "起不来：" + String(err.message));
      resolve(-1);
      return;
    }
    child.stdout.on("data", (buf) => emit(options.channel || "out", buf.toString("utf8")));
    child.stderr.on("data", (buf) => emit(options.channel || "out", buf.toString("utf8")));
    child.on("error", (err) => {
      emit("error", "起不来：" + String(err.message) + "（命令不存在或没装？）");
      resolve(-1);
    });
    child.on("close", (code) => {
      const value = code === null ? -1 : code;
      emit("panel", "↳ 退出码 " + value);
      resolve(value);
    });
  });
}

// --- PowerShell 用哪个：有 pwsh 就用 pwsh，没有就退回 Windows PowerShell ------
let SHELL = "pwsh";

async function pickShell() {
  const probe = await capture("pwsh", ["-NoProfile", "-Command", "exit 0"]);
  if (probe.code !== 0) SHELL = "powershell";
}

// --- 一次性取输出（不往日志总线里灌）--------------------------------------
function capture(cmd, args) {
  return new Promise((resolve) => {
    let out = "";
    let child;
    try {
      child = spawn(cmd, args, { cwd: REPO_ROOT, env: ENV, windowsHide: true });
    } catch (err) {
      resolve({ code: -1, out: "" });
      return;
    }
    child.stdout.on("data", (buf) => { out += buf.toString("utf8"); });
    child.stderr.on("data", () => { /* 端口枚举不看 stderr */ });
    child.on("error", () => resolve({ code: -1, out: "" }));
    child.on("close", (code) => resolve({ code: code === null ? -1 : code, out: out }));
  });
}

async function listPorts() {
  const res = await capture("powershell", ["-NoProfile", "-Command", "[System.IO.Ports.SerialPort]::getportnames() -join ','"]);
  return res.out.split(",").map((s) => s.trim()).filter((s) => s.length > 0);
}

/* 找仓库里所有 .aid（跳过构建目录与历史工单目录）。 */
const AID_SKIP_DIRS = [".git", "node_modules", ".scratch", "third_party", "build", ".backup"];

function listAids() {
  const found = [];
  const walk = (dir, depth) => {
    if (depth > 6) return;
    let entries;
    try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch (e) { return; }
    for (const entry of entries) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) {
        if (AID_SKIP_DIRS.indexOf(entry.name) >= 0 || entry.name.startsWith("build-")) continue;
        walk(full, depth + 1);
      } else if (entry.name.endsWith(".aid")) {
        found.push(path.relative(REPO_ROOT, full).split(path.sep).join("/"));
      }
    }
  };
  walk(REPO_ROOT, 0);
  found.sort();
  return found;
}

// --- 串口（看真机日志）----------------------------------------------------
let serial = null;
let serialPort = null;

function stopSerial(reason) {
  if (!serial) return;
  emit("panel", "停串口" + (serialPort ? "（" + serialPort + "）" : "") + (reason ? "：" + reason : ""));
  try { serial.kill(); } catch (e) { /* 已经退了 */ }
  serial = null;
  serialPort = null;
}

function startSerial(port) {
  stopSerial("换一个口");
  if (!port) {
    emit("error", "先选一个串口（上面点「刷新端口」）");
    return false;
  }
  emit("panel", "看真机串口 " + port + " @115200 —— 要烧录请先点「停串口」（串口是独占的）");
  try {
    serial = spawn(SHELL, ["-NoProfile", "-File", path.join("tools", "esp", "serial.ps1"), "-Port", port],
      { cwd: REPO_ROOT, env: ENV, windowsHide: true });
  } catch (err) {
    emit("error", "串口起不来：" + String(err.message));
    return false;
  }
  serialPort = port;
  state.port = port;
  saveState();
  serial.stdout.on("data", (buf) => emit("serial", buf.toString("utf8")));
  serial.stderr.on("data", (buf) => emit("serial", buf.toString("utf8")));
  serial.on("error", (err) => emit("error", "串口出错：" + String(err.message)));
  serial.on("close", (code) => {
    emit("panel", "串口已断开（退出码 " + (code === null ? "?" : code) + "）");
    serial = null;
    serialPort = null;
  });
  return true;
}

// --- 设备窗口（embark_host_ui）---------------------------------------------
let host = null;
let hostScreen = null;
let foreground = null;

function stopHost(reason) {
  if (!host) return;
  emit("panel", "停止设备窗口" + (hostScreen ? "（" + hostScreen + "）" : "") + (reason ? "：" + reason : ""));
  try { host.kill(); } catch (e) { /* 已经退出了 */ }
  host = null;
  hostScreen = null;
  foreground = null;
}

function startHost(screen) {
  stopHost("换一个屏");
  if (!fs.existsSync(HOST_EXE)) {
    emit("error", "还没构建：找不到 " + HOST_EXE + "（先点「构建」）");
    return false;
  }
  const args = ["--shot-live", LIVE_BMP_REL, "--shot-live-ms", "400"];
  if (screen) args.unshift("--screen", screen);
  emit("panel", "起设备窗口：" + (screen || "launcher（默认）") + " —— 直接用鼠标点那个窗口，右边预览会跟着变");
  try {
    host = spawn(HOST_EXE, args, { cwd: REPO_ROOT, env: ENV, windowsHide: false });
  } catch (err) {
    emit("error", "设备窗口起不来：" + String(err.message));
    return false;
  }
  hostScreen = screen || "launcher";
  host.stdout.on("data", (buf) => {
    const text = buf.toString("utf8");
    emit("host", text);
    /* 前台 App 从日志推：钩子里会打「App clock onEnter/onResume」。不是权威，但够面板显示。 */
    const fg = /App (\w+) on(?:Enter|Resume)/.exec(text);
    if (fg) foreground = fg[1];
  });
  host.stderr.on("data", (buf) => emit("host", buf.toString("utf8")));
  host.on("error", (err) => emit("error", "设备窗口出错：" + String(err.message)));
  host.on("close", (code) => {
    emit("panel", "设备窗口已关闭（退出码 " + (code === null ? "?" : code) + "）");
    host = null;
    hostScreen = null;
  });
  return true;
}

// --- 任务：同一时刻只跑一个（避免两个 cmake 抢 build/ 目录）------------------
let busy = false;
let busyName = null;

async function withTask(name, fn) {
  if (busy) {
    emit("error", "「" + busyName + "」还在跑，等它结束再点");
    return -2;
  }
  busy = true;
  busyName = name;
  emit("panel", "▶ " + name);
  try {
    return await fn();
  } catch (err) {
    emit("error", String(err && err.message ? err.message : err));
    return -1;
  } finally {
    busy = false;
    busyName = null;
  }
}

async function configureAndBuild() {
  const code = await run("cmake", ["-S", ".", "-B", "build"]);
  if (code !== 0) {
    emit("error", "配置失败：上面那几行就是原因");
    return code;
  }
  return await run("cmake", ["--build", "build", "--target", "embark_host_ui"]);
}

async function taskBuild() {
  stopHost("构建要重写 exe，先把窗口关掉");
  const code = await configureAndBuild();
  emit(code === 0 ? "panel" : "error", code === 0 ? "构建好了" : "构建失败");
  return code;
}

async function taskExport() {
  stopHost("导出会重建 export/");
  emit("panel", "界面工程：" + state.aid + "（只换源文件，导出目录永远是 app/aidogui_ui/export/）");
  const code = await run(SHELL, ["-NoProfile", "-File", path.join("tools", "aidogui", "export.ps1"), "-Project", state.aid]);
  if (code !== 0) {
    emit("error", "导出失败 —— 设计器没装 / 路径找不到时就是这句；也可以手工在设计器里导出");
    return code;
  }
  emit("panel", "导出完，接着构建");
  return await configureAndBuild();
}

const STORIES = [
  ["--ui 字体与屏表", ["--ui", "--frames", "40", "--delay", "40"]],
  ["--click 点击往返", ["--click", "--delay", "40"]],
  ["--ui-nav 双向导航", ["--ui-nav", "--delay", "40"]],
  ["--ui-weather 天气", ["--ui-weather", "--delay", "40"]],
  ["--ui-launcher-pages 主屏分页", ["--ui-launcher-pages", "--delay", "40"]],
];

async function taskAccept() {
  stopHost("验收要独占窗口");
  const results = [];
  for (const [name, args] of STORIES) {
    emit("panel", "▷ " + name);
    const code = await run(HOST_EXE, args, { channel: "out" });
    results.push([name, code]);
  }
  if (fs.existsSync(TOUR_EXE)) {
    emit("panel", "▷ 全量自检清单（embark_host_tour）");
    const code = await run(TOUR_EXE, ["--delay", "40"], { channel: "out" });
    results.push(["全量自检清单", code]);
  }
  const bad = results.filter((row) => row[1] !== 0);
  if (bad.length === 0) {
    emit("panel", "验收全绿：" + results.length + " 项都通过");
  } else {
    emit("error", "验收失败 " + bad.length + " 项：" + bad.map((r) => r[0] + "（" + r[1] + "）").join(" · "));
  }
  return bad.length === 0 ? 0 : 1;
}

async function exportCurrentAid(reason) {
  emit("panel", "重新导出界面（" + reason + "）：" + state.aid);
  const code = await run(SHELL, ["-NoProfile", "-File", path.join("tools", "aidogui", "export.ps1"), "-Project", state.aid]);
  if (code !== 0) {
    emit("error", "导出失败 —— .aid 里那张屏的 C 代码没生成，构建会报「找不到 ui_<app>」；也可以在设计器里手工导出");
    return code;
  }
  return 0;
}

async function taskNewApp(body) {
  const args = [path.join("tools", "new_app.mjs"), body.name];
  if (body.title) args.push("--title", body.title);
  if (body.icon) args.push("--icon", body.icon);
  if (fs.existsSync(path.join(REPO_ROOT, state.aid))) args.push("--aid", state.aid);
  args.push(body.apply ? "--apply" : "--dry-run");
  const code = await run("node", args);
  if (code !== 0) return code;
  if (!body.apply) {
    emit("panel", "上面是预览（什么都没写）。确认无误再点「真的建出来」");
    return 0;
  }
  stopHost("导出会重建 export/");
  const exported = await exportCurrentAid("新屏的 C 代码要到这一步才生成");
  if (exported !== 0) return exported;
  emit("panel", "薄壳 + 占位屏都齐了，接着构建");
  return await configureAndBuild();
}

async function taskDeleteApp(body) {
  const args = [path.join("tools", "new_app.mjs"), body.name, "--delete"];
  if (fs.existsSync(path.join(REPO_ROOT, state.aid))) args.push("--aid", state.aid);
  args.push(body.apply ? "--apply" : "--dry-run");
  const code = await run("node", args);
  if (code !== 0) return code;
  if (!body.apply) {
    emit("panel", "上面是预览（什么都没删）。确认无误再点「真的删掉」");
    return 0;
  }
  stopHost("导出会重建 export/");
  const exported = await exportCurrentAid("把这块屏的 C 代码一起撤掉");
  if (exported !== 0) return exported;
  emit("panel", "登记撤完了，接着构建（主屏磁贴会少一个）");
  return await configureAndBuild();
}

async function taskEspBuild() {
  emit("panel", "真机构建（第一次要几分钟，之后只编改动的）");
  return await run(SHELL, ["-NoProfile", "-File", path.join("tools", "esp", "idf.ps1"), "-Task", "build"], { channel: "out" });
}

async function taskEspFlash(port) {
  if (!port) {
    emit("error", "先选一个串口（点「刷新端口」列出来）");
    return -1;
  }
  stopSerial("烧录要独占串口");
  emit("panel", "烧到 " + port + " —— 板子会复位，日志里最后会出现「Leaving... Hard resetting」");
  const code = await run(SHELL, ["-NoProfile", "-File", path.join("tools", "esp", "idf.ps1"), "-Task", "flash", "-Port", port], { channel: "out" });
  if (code !== 0) {
    emit("error", "烧录失败 —— 上面那几行就是原因（板子没插 / 口选错 / 端口被别的程序占着）");
    return code;
  }
  emit("panel", "烧好了，自动开串口看它启动");
  startSerial(port);
  return 0;
}

async function taskHtml(body) {
  const tmp = path.join(HERE, ".upload-" + process.pid + ".html");
  fs.writeFileSync(tmp, body.html, "utf8");
  const args = [
    path.join("tools", "aidogui", "html_to_aid.mjs"),
    "--html", path.relative(REPO_ROOT, tmp),
    "--screen", body.screen,
    "--apply",
  ];
  emit("panel", "HTML 草稿 → 屏 " + body.screen + "（只改 screen.aid，还要导出 + 构建才上屏）");
  const code = await run("node", args);
  try { fs.unlinkSync(tmp); } catch (e) { /* 没了就算了 */ }
  return code;
}

// --- 读仓库现状（给网页的下拉框）--------------------------------------------
function listApps() {
  const dir = path.join(REPO_ROOT, "app");
  try {
    return fs.readdirSync(dir).filter((name) => {
      try {
        return fs.existsSync(path.join(dir, name, name + "_app.cpp"));
      } catch (e) { return false; }
    }).sort();
  } catch (e) { return []; }
}

function listScreens() {
  const dir = path.join(REPO_ROOT, "app", "aidogui_ui", "export", "screens");
  try {
    return fs.readdirSync(dir)
      .filter((name) => name.startsWith("ui_") && name.endsWith(".h"))
      .map((name) => name.slice(3, -2))
      .sort();
  } catch (e) { return []; }
}

function buildState() {
  return {
    apps: listApps(),
    screens: listScreens(),
    exe: fs.existsSync(HOST_EXE),
    host: host ? { running: true, screen: hostScreen } : { running: false, screen: null },
    busy: busy,
    busyName: busyName,
    liveAt: fs.existsSync(LIVE_BMP) ? fs.statSync(LIVE_BMP).mtimeMs : 0,
    aid: state.aid,
    aids: listAids(),
    foreground: foreground,
    esp: { running: serial !== null, port: serialPort, lastPort: state.port },
  };
}

// --- HTTP ------------------------------------------------------------------
function sendJson(res, code, obj) {
  const body = Buffer.from(JSON.stringify(obj), "utf8");
  res.writeHead(code, { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store" });
  res.end(body);
}

function readBody(req) {
  return new Promise((resolve) => {
    const chunks = [];
    req.on("data", (buf) => chunks.push(buf));
    req.on("end", () => resolve(Buffer.concat(chunks).toString("utf8")));
    req.on("error", () => resolve(""));
  });
}

function parseJson(text) {
  if (!text) return {};
  try { return JSON.parse(text); } catch (e) { return {}; }
}

const server = createServer(async (req, res) => {
  const url = new URL(req.url, "http://127.0.0.1");
  const route = url.pathname;

  if (route === "/" || route === "/index.html") {
    try {
      const html = fs.readFileSync(INDEX_HTML);
      res.writeHead(200, { "Content-Type": "text/html; charset=utf-8", "Cache-Control": "no-store" });
      res.end(html);
    } catch (err) {
      res.writeHead(500, { "Content-Type": "text/plain; charset=utf-8" });
      res.end("找不到 index.html：" + String(err.message));
    }
    return;
  }

  if (route === "/api/live.bmp") {
    let data = null;
    try { data = fs.readFileSync(LIVE_BMP); } catch (e) { data = null; }
    if (data === null || data.length < 100) {
      res.writeHead(204, { "Cache-Control": "no-store" });
      res.end();
      return;
    }
    res.writeHead(200, { "Content-Type": "image/bmp", "Cache-Control": "no-store", "Content-Length": data.length });
    res.end(data);
    return;
  }

  if (route === "/api/events") {
    res.writeHead(200, {
      "Content-Type": "text/event-stream; charset=utf-8",
      "Cache-Control": "no-store",
      "Connection": "keep-alive",
    });
    res.write(": hello\n\n");
    clients.add(res);
    const beat = setInterval(() => {
      try { res.write(": ping\n\n"); } catch (e) { /* 断了 */ }
    }, 20000);
    req.on("close", () => { clearInterval(beat); clients.delete(res); });
    return;
  }

  if (route.indexOf("/doc/") === 0) {
    // 面板里的「看文档」：只放行 docs/guides 下的 .md，原样当纯文本给出（浏览器能直接读）
    const name = path.basename(route.slice("/doc/".length));
    const file = path.join(REPO_ROOT, "docs", "guides", name);
    if (!name.endsWith(".md") || !fs.existsSync(file)) {
      sendJson(res, 404, { ok: false, error: "没有这份文档：" + name });
      return;
    }
    res.writeHead(200, { "Content-Type": "text/plain; charset=utf-8", "Cache-Control": "no-store" });
    res.end(fs.readFileSync(file));
    return;
  }

  if (route === "/api/state") { sendJson(res, 200, buildState()); return; }
  if (route === "/api/ports") { sendJson(res, 200, { ports: await listPorts(), port: state.port }); return; }
  if (route === "/api/logs") { sendJson(res, 200, { lines: logLines.slice(-400) }); return; }

  if (req.method === "POST") {
    const body = parseJson(await readBody(req));
    switch (route) {
      case "/api/run":
        sendJson(res, 200, { ok: startHost(body.screen) });
        return;
      case "/api/stop":
        stopHost("你点了停止");
        sendJson(res, 200, { ok: true });
        return;
      case "/api/build":
        withTask("构建", taskBuild);
        sendJson(res, 200, { ok: true });
        return;
      case "/api/export":
        withTask("重新导出屏", taskExport);
        sendJson(res, 200, { ok: true });
        return;
      case "/api/accept":
        withTask("跑验收", taskAccept);
        sendJson(res, 200, { ok: true });
        return;
      case "/api/newapp":
        if (!/^[a-z][a-z0-9_]*$/.test(String(body.name || ""))) {
          sendJson(res, 400, { ok: false, error: "App 名要小写字母开头，只含小写字母/数字/下划线" });
          return;
        }
        withTask(body.apply ? "建 App（真的写盘）" : "预览要建哪些文件", () => taskNewApp(body));
        sendJson(res, 200, { ok: true });
        return;
      case "/api/aid": {
        const rel = String(body.rel || "");
        if (!rel.endsWith(".aid") || !fs.existsSync(path.join(REPO_ROOT, rel))) {
          sendJson(res, 400, { ok: false, error: "没有这份 .aid：" + rel });
          return;
        }
        state.aid = rel.split(path.sep).join("/");
        saveState();
        emit("panel", "界面工程换成 " + state.aid + "（点「重新导出屏」才会生效）");
        sendJson(res, 200, { ok: true, aid: state.aid });
        return;
      }
      case "/api/deleteapp":
        if (!/^[a-z][a-z0-9_]*$/.test(String(body.name || ""))) {
          sendJson(res, 400, { ok: false, error: "App 名要小写字母开头，只含小写字母/数字/下划线" });
          return;
        }
        withTask(body.apply ? "退掉 App（真的删）" : "预览要撤哪些登记", () => taskDeleteApp(body));
        sendJson(res, 200, { ok: true });
        return;
      case "/api/esp": {
        const action = String(body.action || "");
        if (action === "build") withTask("真机构建", taskEspBuild);
        else if (action === "flash") withTask("烧录真机", () => taskEspFlash(String(body.port || state.port || "")));
        else if (action === "serial") startSerial(String(body.port || state.port || ""));
        else if (action === "stopserial") stopSerial("你点了停串口");
        else { sendJson(res, 400, { ok: false, error: "不认识的 action：" + action }); return; }
        sendJson(res, 200, { ok: true });
        return;
      }
      case "/api/openfolder": {
        const rel = String(body.rel || "");
        const abs = rel ? path.join(REPO_ROOT, rel) : REPO_ROOT;
        if (!fs.existsSync(abs)) { sendJson(res, 400, { ok: false, error: "没有这个目录：" + rel }); return; }
        spawn("explorer", [abs], { detached: true, stdio: "ignore" }).unref();
        sendJson(res, 200, { ok: true });
        return;
      }
      case "/api/designer": {
        let home = "";
        try { home = fs.readFileSync(path.join(REPO_ROOT, "tools", "aidogui", "home.txt"), "utf8").trim(); } catch (e) { home = ""; }
        const exe = path.join(home, "AiDoGUIDesigner.exe");
        if (!home || !fs.existsSync(exe)) {
          sendJson(res, 400, { ok: false, error: "找不到设计器（tools/aidogui/home.txt 里那个目录）—— 先手工打开它" });
          return;
        }
        spawn(exe, [path.join(REPO_ROOT, state.aid)], { detached: true, stdio: "ignore" }).unref();
        emit("panel", "已用设计器打开 " + state.aid);
        sendJson(res, 200, { ok: true });
        return;
      }
      case "/api/html":
        if (!body.html || !body.screen) {
          sendJson(res, 400, { ok: false, error: "要选一个 .html 文件，并填屏名" });
          return;
        }
        withTask("HTML 转屏", () => taskHtml(body));
        sendJson(res, 200, { ok: true });
        return;
      default:
        sendJson(res, 404, { ok: false, error: "没有这条路：" + route });
        return;
    }
  }

  sendJson(res, 404, { ok: false, error: "没有这条路：" + route });
});

// --- 起服务（端口被占就往后挪）---------------------------------------------
const noOpen = process.argv.includes("--no-open");
const startPort = Number(process.env.EMBARK_DEVPANEL_PORT || 7788);

function announce(port) {
  const url = "http://127.0.0.1:" + port + "/";
  console.log("Embark 开发台已启动：" + url);
  console.log("（只监听本机；关掉这个窗口或按 Ctrl+C 就停）");
  if (noOpen) return;
  if (process.platform === "win32") {
    spawn("cmd", ["/c", "start", "", url], { detached: true, stdio: "ignore", windowsHide: true }).unref();
  } else if (process.platform === "darwin") {
    spawn("open", [url], { detached: true, stdio: "ignore" }).unref();
  } else {
    spawn("xdg-open", [url], { detached: true, stdio: "ignore" }).unref();
  }
}

function listen(port, tries) {
  const onError = (err) => {
    if (err.code === "EADDRINUSE" && tries > 0) {
      listen(port + 1, tries - 1);
      return;
    }
    console.error("开发台起不来：" + String(err.message));
    process.exitCode = 1;
  };
  server.once("error", onError);
  server.listen(port, "127.0.0.1", () => {
    server.removeListener("error", onError);
    announce(port);
  });
}

const shutdown = () => {
  stopHost("开发台要关了");
  stopSerial("开发台要关了");
  server.close();
};
process.on("SIGINT", shutdown);
process.on("SIGTERM", shutdown);

pickShell().then(() => listen(startPort, 12));
