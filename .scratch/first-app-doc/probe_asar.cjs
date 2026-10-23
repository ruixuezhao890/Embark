'use strict';
const fs = require('fs');
const p = 'E:\\00_Software\\03_Dev_Env\\AIDes\\AiDoGUIDesigner\\resources\\app.asar';
const s = fs.readFileSync(p).toString('latin1');
const needle = Buffer.from('未配置自定义字符范围', 'utf8').toString('latin1');
const hits = [];
let i = s.indexOf(needle);
while (i !== -1 && hits.length < 5) { hits.push(i); i = s.indexOf(needle, i + 1); }
console.log('hits', hits);
if (hits.length) {
  const start = Math.max(0, hits[0] - 2500);
  console.log(Buffer.from(s.slice(start, hits[0] + 300), 'latin1').toString('utf8'));
}
