
const fs = require('fs');
const p = 'app/aidogui_ui/screen.aid';
const raw = fs.readFileSync(p, 'utf8');

const KEY_COLOR = '#c7d0e6ff';
const ROW_COLOR = '#c7d0e6ff';
const sc = { scrollbar_mode: 'AUTO', scroll_dir: 'ALL', scroll_snap_x: 'NONE', scroll_snap_y: 'NONE' };

function label(id, name, parent, x, y, w, h, text, size, color, align) {
  return {
    id, parentId: parent, type: 'lv_label', name,
    x, y, width: w, height: h, text,
    style: { styles: { DEFAULT__MAIN: { textFont: 'ui_heiti.ttf', textColor: color, textFontSize: size, textAlign: align } } },
    events: [], flags: { clickable: false, scrollable: false }, state: {}, scrolling: { ...sc }, align: 'TOP_LEFT',
  };
}

// 行文案 = 设计稿初值（运行期由 AboutApp 重推成真实值）。七行 key：值 一体，单标签一行。
const rows = [
  ['平台', 'host'],
  ['版本', '0.1.0'],
  ['可用堆', '-- 字节'],
  ['堆底', '-- 字节'],
  ['栈水位', '-- 字节'],
  ['存盘', '未挂载'],
  ['对时', '未对时'],
];

const widgets = [
  {
    id: 'about_Desk', parentId: 'about', type: 'lv_obj', name: 'Desk',
    x: 0, y: 0, width: 240, height: 320,
    style: { styles: { DEFAULT__MAIN: { bgColor: '#0b1020ff', bgOpa: 255, borderWidth: 0, padLeft: 0, padRight: 0, padTop: 0, padBottom: 0, radius: 0 } } },
    events: [], flags: { clickable: false, scrollable: false }, state: {}, scrolling: { ...sc }, align: 'TOP_LEFT',
  },
  {
    id: 'about_Btn_Back', parentId: 'about_Desk', type: 'lv_obj', name: 'Btn_Back',
    x: 8, y: 4, width: 64, height: 28,
    style: { styles: { DEFAULT__MAIN: { bgColor: '#18213aff', bgOpa: 255, borderWidth: 0, padLeft: 0, padRight: 0, padTop: 0, padBottom: 0, radius: 8 }, PRESSED__MAIN: { bgColor: '#2a3550ff' } } },
    events: [{ id: 'event_about_back_clicked', eventType: 'CLICKED', actionType: 'CALL_FUNCTION', actionParams: { functionName: 'embark_ui_open_launcher' }, enabled: true }],
    flags: { clickable: true, scrollable: false }, state: {}, scrolling: { ...sc }, align: 'TOP_LEFT',
  },
  label('about_Btn_Back_L', 'Btn_Back_L', 'about_Btn_Back', 0, 5, 64, 18, '返回', 14, KEY_COLOR, 'CENTER'),
  label('about_L_Title', 'L_Title', 'about_Desk', 84, 8, 72, 20, '关于', 14, KEY_COLOR, 'CENTER'),
  {
    id: 'about_Card_Info', parentId: 'about_Desk', type: 'lv_obj', name: 'Card_Info',
    x: 14, y: 44, width: 212, height: 236,
    style: { styles: { DEFAULT__MAIN: { bgColor: '#18213aff', bgOpa: 255, borderWidth: 0, padLeft: 0, padRight: 0, padTop: 0, padBottom: 0, radius: 12 } } },
    events: [], flags: { clickable: false, scrollable: false }, state: {}, scrolling: { ...sc }, align: 'TOP_LEFT',
  },
];

const names = ['Platform', 'Version', 'Heap', 'HeapMin', 'Stack', 'Disk', 'Clock'];
rows.forEach((r, i) => {
  widgets.push(label('about_L_' + names[i], 'L_' + names[i], 'about_Card_Info', 12, 14 + i * 30, 188, 18,
    r[0] + '：' + r[1], 12, ROW_COLOR, 'LEFT'));
});

const screen = { id: 'about', name: 'about', isDefault: false, style: {}, widgets, animations: [] };

const obj = JSON.parse(raw);
if (obj.screens.some((s) => s.id === 'about')) throw new Error('屏 about 已存在');
obj.screens.push(screen);

// 文本手术：只在 screens 数组尾部插入，其余字节保持原样（整体重序列化会把空数组改写成 [] 产生无谓 diff）。
const anchor = raw.indexOf('"screens": [');
if (anchor < 0) throw new Error('找不到 screens 数组');
let i = raw.indexOf('[', anchor);
let depth = 0;
let inStr = false;
let end = -1;
for (; i < raw.length; i += 1) {
  const ch = raw[i];
  if (inStr) { if (ch === '\\') i += 1; else if (ch === '"') inStr = false; continue; }
  if (ch === '"') { inStr = true; continue; }
  if (ch === '[') depth += 1;
  else if (ch === ']') { depth -= 1; if (depth === 0) { end = i; break; } }
}
if (end < 0) throw new Error('screens 数组没有闭合');

const body = JSON.stringify(screen, null, 2).split('\n').map((l, n) => (n === 0 ? l : '    ' + l)).join('\n');
let head = raw.slice(0, end);
if (!head.endsWith('\n  ')) throw new Error('screens 数组尾部形态不是预期的 "\\n  "');
head = head.slice(0, -3);
const next = head + ',\n    ' + body + '\n  ' + raw.slice(end);

JSON.parse(next);  // 语法必须仍然合法
fs.writeFileSync(p, next, 'utf8');
console.log('屏数 ' + JSON.parse(next).screens.length + ' · 新增字节 ' + (next.length - raw.length));
console.log('屏表: ' + JSON.parse(next).screens.map((s) => s.id + '(' + s.widgets.length + ')').join(', '));
