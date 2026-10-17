
const fs = require('fs');
const p = 'app/aidogui_ui/screen.aid';
let s = fs.readFileSync(p, 'utf8');
const repl = [
  ['"about_L_HeapMin"', '"about_L_Font"'],
  ['"name": "L_HeapMin"', '"name": "L_Font"'],
  ['"text": "堆底：-- 字节"', '"text": "字库：--/-- 份"'],
  ['"text": "可用堆：-- 字节"', '"text": "可用堆：-- KB"'],
  ['"text": "栈水位：-- 字节"', '"text": "栈水位：-- KB"'],
];
for (const [a, b] of repl) {
  const n = s.split(a).length - 1;
  if (n !== 1) throw new Error(a + ' 出现 ' + n + ' 次，预期 1 次');
  s = s.replace(a, b);
}
JSON.parse(s);
fs.writeFileSync(p, s, 'utf8');
const j = JSON.parse(s);
const scr = j.screens.find((x) => x.id === 'about');
console.log(scr.widgets.map((w) => w.id + (w.text ? ' | ' + w.text : '')).join('\n'));
