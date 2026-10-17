// issue 02：重建 settings 屏（32 控件，Body 纵向滚动）+ 让 about 屏退役。
// 只做字节级文本手术：其余屏保持原字节（整体重序列化会把 "events": [] 改写成两行，产生无谓 diff）。
// 幂等：about 屏不存在时跳过删除；settings 用同名模板控件克隆，可重复运行。
const fs = require('fs');
const P = 'app/aidogui_ui/screen.aid';
const raw = fs.readFileSync(P, 'utf8');
const obj = JSON.parse(raw);

const SCROLL_ALL = { scrollbar_mode: 'AUTO', scroll_dir: 'ALL', scroll_snap_x: 'NONE', scroll_snap_y: 'NONE' };
const SCROLL_VER = { scrollbar_mode: 'AUTO', scroll_dir: 'VER', scroll_snap_x: 'NONE', scroll_snap_y: 'NONE' };
const TEXT = '#c7d0e6ff';
const SEC = '#7f8aa6ff';

const old = obj.screens.find((s) => s.id === 'settings');
if (!old) throw new Error('找不到 settings 屏');
const about = obj.screens.find((s) => s.id === 'about');

const tpl = {};
old.widgets.forEach((w) => { if (!tpl[w.name]) tpl[w.name] = w; });
['Desk', 'Btn_Back', 'Btn_Back_L', 'L_Title', 'Card_Bright'].forEach((n) => {
  if (!tpl[n]) throw new Error('模板控件缺失: ' + n);
});
const clone = (w) => JSON.parse(JSON.stringify(w));

// 容器（lv_obj）：样式键序沿用 Card_Bright 模板，避免导出差异。
function box(id, name, parent, x, y, w, h, styles, opts) {
  const o = clone(tpl.Card_Bright);
  o.id = 'settings_' + id; o.name = name; o.parentId = parent;
  o.x = x; o.y = y; o.width = w; o.height = h;
  o.style = { styles };
  o.events = [];
  o.flags = { clickable: !!(opts && opts.clickable), scrollable: !!(opts && opts.scrollable) };
  o.scrolling = (opts && opts.scrolling) || SCROLL_ALL;
  return o;
}

function lab(id, name, parent, x, y, w, h, text, size, color, align) {
  const o = clone(tpl.L_Title);
  o.id = 'settings_' + id; o.name = name; o.parentId = parent;
  o.x = x; o.y = y; o.width = w; o.height = h; o.text = text;
  o.style = { styles: { DEFAULT__MAIN: { textFont: 'ui_heiti.ttf', textColor: color, textFontSize: size, textAlign: align } } };
  return o;
}

const CARD = { DEFAULT__MAIN: { bgColor: '#18213aff', bgOpa: 255, borderWidth: 0, padLeft: 0, padRight: 0, padTop: 0, padBottom: 0, radius: 12 } };
const BTN = { DEFAULT__MAIN: { bgColor: '#2a3550ff', bgOpa: 255, borderWidth: 0, padLeft: 0, padRight: 0, padTop: 0, padBottom: 0, radius: 8 }, PRESSED__MAIN: { bgColor: '#3a4770ff' } };
const FLAT = { DEFAULT__MAIN: { bgColor: '#0b1020ff', bgOpa: 255, borderWidth: 0, padLeft: 0, padRight: 0, padTop: 0, padBottom: 0, radius: 0 } };

const w = [];
// 固定头部（不随滚动）
w.push(clone(tpl.Desk));
w.push(clone(tpl.Btn_Back));
w.push(clone(tpl.Btn_Back_L));
w.push(clone(tpl.L_Title));
// Body 必须 clickable：LVGL 的命中测试（lv_obj_pos.c hit_test）只认 CLICKABLE 的对象，
// 容器不可点击时按在卡片的空白处 → act_obj 是屏 → find_scroll_obj 无从祖先找到 Body，屏就不滚。
w.push(box('Body', 'Body', 'settings_Desk', 0, 36, 240, 276, FLAT, { scrollable: true, clickable: true, scrolling: SCROLL_VER }));

// ① 显示：亮度
w.push(lab('L_SecDisplay', 'L_SecDisplay', 'settings_Body', 14, 0, 212, 16, '显示', 11, SEC, 'LEFT'));
w.push(box('Card_Bright', 'Card_Bright', 'settings_Body', 14, 18, 212, 56, CARD, {}));
w.push(lab('L_BrightTitle', 'L_BrightTitle', 'settings_Card_Bright', 14, 18, 56, 20, '亮度', 14, TEXT, 'LEFT'));
w.push(box('Btn_Minus', 'Btn_Minus', 'settings_Card_Bright', 72, 12, 34, 32, BTN, { clickable: true }));
w.push(lab('Btn_Minus_L', 'Btn_Minus_L', 'settings_Btn_Minus', 0, 7, 34, 18, '-', 14, TEXT, 'CENTER'));
w.push(lab('L_BrightVal', 'L_BrightVal', 'settings_Card_Bright', 110, 19, 48, 20, '70%', 14, TEXT, 'CENTER'));
w.push(box('Btn_Plus', 'Btn_Plus', 'settings_Card_Bright', 162, 12, 34, 32, BTN, { clickable: true }));
w.push(lab('Btn_Plus_L', 'Btn_Plus_L', 'settings_Btn_Plus', 0, 7, 34, 18, '+', 14, TEXT, 'CENTER'));

// ② 时间：手动设表
w.push(lab('L_SecTime', 'L_SecTime', 'settings_Body', 14, 82, 212, 16, '时间', 11, SEC, 'LEFT'));
w.push(box('Card_Time', 'Card_Time', 'settings_Body', 14, 100, 212, 84, CARD, {}));
w.push(lab('L_TimeVal', 'L_TimeVal', 'settings_Card_Time', 12, 10, 188, 24, '2026-10-17 06:21', 14, TEXT, 'CENTER'));
w.push(lab('L_TimeField', 'L_TimeField', 'settings_Card_Time', 12, 44, 40, 18, '年', 12, TEXT, 'CENTER'));
w.push(box('Btn_TimeMinus', 'Btn_TimeMinus', 'settings_Card_Time', 56, 40, 34, 26, BTN, { clickable: true }));
w.push(lab('Btn_TimeMinus_L', 'Btn_TimeMinus_L', 'settings_Btn_TimeMinus', 0, 4, 34, 18, '-', 12, TEXT, 'CENTER'));
w.push(box('Btn_TimePlus', 'Btn_TimePlus', 'settings_Card_Time', 94, 40, 34, 26, BTN, { clickable: true }));
w.push(lab('Btn_TimePlus_L', 'Btn_TimePlus_L', 'settings_Btn_TimePlus', 0, 4, 34, 18, '+', 12, TEXT, 'CENTER'));
w.push(box('Btn_TimeNext', 'Btn_TimeNext', 'settings_Card_Time', 134, 40, 66, 26, BTN, { clickable: true }));
w.push(lab('Btn_TimeNext_L', 'Btn_TimeNext_L', 'settings_Btn_TimeNext', 0, 4, 66, 18, '下一字段', 12, TEXT, 'CENTER'));

// ③ 关于本机（原 about 屏七行搬到这张卡）
w.push(lab('L_SecAbout', 'L_SecAbout', 'settings_Body', 14, 192, 212, 16, '关于本机', 11, SEC, 'LEFT'));
w.push(box('Card_Info', 'Card_Info', 'settings_Body', 14, 210, 212, 212, CARD, {}));
const ROWS = [
  ['Platform', '平台：host'],
  ['Version', '版本：0.1.0'],
  ['Heap', '可用堆：-- KB'],
  ['Font', '字库：--/-- 份'],
  ['Stack', '栈水位：-- KB'],
  ['Disk', '存盘：未挂载'],
  ['Clock', '对时：未对时'],
];
ROWS.forEach((r, i) => {
  w.push(lab('L_' + r[0], 'L_' + r[0], 'settings_Card_Info', 12, 10 + i * 30, 188, 18, r[1], 12, TEXT, 'LEFT'));
});

if (w.length !== 32) throw new Error('控件数应为 32，实际 ' + w.length);

const newScreen = { id: 'settings', name: 'settings', isDefault: false, style: {}, widgets: w, animations: [] };

// —— 定位 screens 数组与每个屏对象的字节区间 ——
const anchor = raw.indexOf('"screens": [');
if (anchor < 0) throw new Error('找不到 screens 数组');
const open = raw.indexOf('[', anchor);
let depth = 0; let inStr = false; let close = -1;
const starts = []; const ends = [];
for (let i = open; i < raw.length; i += 1) {
  const ch = raw[i];
  if (inStr) { if (ch === '\\') i += 1; else if (ch === '"') inStr = false; continue; }
  if (ch === '"') { inStr = true; continue; }
  if (ch === '[' || ch === '{') { depth += 1; if (depth === 2 && ch === '{') starts.push(i); continue; }
  if (ch === '}' || ch === ']') {
    if (depth === 2 && ch === '}') ends.push(i + 1);
    depth -= 1;
    if (depth === 0 && ch === ']') { close = i; break; }
  }
}
if (close < 0) throw new Error('screens 数组没有闭合');
if (starts.length !== obj.screens.length) throw new Error('屏对象区间数与屏数不符');

const reindent = (text) => text.split('\n').map((l, i) => (i === 0 ? l : '    ' + l)).join('\n');
const parts = [];
obj.screens.forEach((s, idx) => {
  if (s.id === 'about') return;
  if (s.id === 'settings') { parts.push(reindent(JSON.stringify(newScreen, null, 2))); return; }
  parts.push(raw.slice(starts[idx], ends[idx]));
});

const next = raw.slice(0, open) + '[\n    ' + parts.join(',\n    ') + '\n  ' + raw.slice(close);

const check = JSON.parse(next);
const set2 = check.screens.find((s) => s.id === 'settings');
if (!set2 || set2.widgets.length !== 32) throw new Error('回读校验失败');
if (check.screens.some((s) => s.id === 'about')) throw new Error('about 屏没删掉');
const body = set2.widgets.find((x) => x.name === 'Body');
if (!body || body.flags.scrollable !== true || body.flags.clickable !== true || body.scrolling.scroll_dir !== 'VER') throw new Error('Body 滚动属性不对');

fs.writeFileSync(P, next, 'utf8');
console.log('屏表: ' + check.screens.map((s) => s.id + '(' + s.widgets.length + ')').join(', '));
console.log('settings 控件: ' + set2.widgets.map((x) => x.name).join(' '));
console.log('字节变化: ' + (next.length - raw.length));
