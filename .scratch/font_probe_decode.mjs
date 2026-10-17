
import { readFileSync } from 'node:fs';
const cPath = process.argv[2];
const src = readFileSync(cPath, 'utf8');

const bpp = Number((src.match(/\.bpp\s*=\s*(\d+)/) || [0, 4])[1]);
const fmtM = src.match(/\.bitmap_format\s*=\s*(\d+)/);
const fmt = fmtM ? Number(fmtM[1]) : 0;

const gdHead = src.indexOf('glyph_dsc[] = {');
const gdBody = src.slice(gdHead, src.indexOf('};', gdHead));
const glyphs = [...gdBody.matchAll(/\.bitmap_index\s*=\s*(\d+),\s*\.adv_w\s*=\s*\d+,\s*\.box_w\s*=\s*(\d+),\s*\.box_h\s*=\s*(\d+)/g)]
  .map(m => ({ bi: Number(m[1]), w: Number(m[2]), h: Number(m[3]) }));

const bHead = src.indexOf('glyph_bitmap[] = {');
const bStart = src.indexOf('{', bHead) + 1;
const bEnd = src.indexOf('};', bStart);
const bytes = Uint8Array.from((src.slice(bStart, bEnd).match(/0x[0-9a-fA-F]{1,2}/g) || []).map(t => parseInt(t, 16)));

function getBits(buf, bitPos, len) {
  let v = 0;
  for (let i = 0; i < len; i++) {
    const bp = bitPos + i;
    const byte = buf[bp >> 3] ?? 0;
    v = (v << 1) | ((byte >> (7 - (bp & 7))) & 1);
  }
  return v;
}

function makeRle(buf, base, rb) {
  let rdp = 0, prevV = 0, cnt = 0, state = 'SINGLE';
  function next() {
    let v = 0, ret = 0;
    if (state === 'SINGLE') {
      ret = getBits(buf, base + rdp, rb);
      if (rdp !== 0 && prevV === ret) { cnt = 0; state = 'REPEATE'; }
      prevV = ret; rdp += rb;
    } else if (state === 'REPEATE') {
      v = getBits(buf, base + rdp, 1);
      cnt++; rdp += 1;
      if (v === 1) {
        ret = prevV;
        if (cnt === 11) {
          cnt = getBits(buf, base + rdp, 6); rdp += 6;
          if (cnt !== 0) state = 'COUNTER';
          else { ret = getBits(buf, base + rdp, rb); prevV = ret; rdp += rb; state = 'SINGLE'; }
        }
      } else {
        ret = getBits(buf, base + rdp, rb); prevV = ret; rdp += rb; state = 'SINGLE';
      }
    } else {
      ret = prevV; cnt--;
      if (cnt === 0) { ret = getBits(buf, base + rdp, rb); prevV = ret; rdp += rb; state = 'SINGLE'; }
    }
    return ret;
  }
  return { next, get rdp() { return rdp; } };
}

function decodeGlyph(g, prefilter) {
  const rle = makeRle(bytes, g.bi * 8, bpp);
  const w = g.w, h = g.h, px = new Uint8Array(w * h);
  let line = new Uint8Array(w);
  for (let x = 0; x < w; x++) line[x] = rle.next();
  for (let x = 0; x < w; x++) px[x] = line[x];
  for (let y = 1; y < h; y++) {
    const l2 = new Uint8Array(w);
    for (let x = 0; x < w; x++) l2[x] = rle.next();
    if (prefilter) { for (let x = 0; x < w; x++) line[x] = l2[x] ^ line[x]; }
    else line = l2;
    for (let x = 0; x < w; x++) px[y * w + x] = line[x];
  }
  return { px, consumedBits: rle.rdp };
}

const prefilter = fmt === 1;
console.log('font:', cPath.split(/[\\/]/).pop(), 'bpp=' + bpp, 'fmt=' + fmt, 'prefilter=' + prefilter, 'glyphs=' + glyphs.length, 'bytes=' + bytes.length);

let bad = 0; const rep = [];
for (let i = 0; i < glyphs.length; i++) {
  const g = glyphs[i];
  if (g.w === 0 || g.h === 0) continue;
  const { consumedBits } = decodeGlyph(g, prefilter);
  const nextBi = i + 1 < glyphs.length ? glyphs[i + 1].bi : bytes.length;
  const segBits = (nextBi - g.bi) * 8;
  const diff = segBits - consumedBits;
  if (diff < 0 || diff > 7) { bad++; if (rep.length < 10) rep.push({ i, bi: g.bi, w: g.w, h: g.h, segBits, consumedBits, diff }); }
}
console.log('offset mismatches:', bad, '/', glyphs.length);
for (const r of rep) console.log('  ', JSON.stringify(r));

function show(idx, label) {
  const g = glyphs[idx]; if (!g) return;
  const { px } = decodeGlyph(g, prefilter);
  const ch = ' .:-=+*#%@';
  console.log('--- glyph#' + idx + ' ' + label + ' box=' + g.w + 'x' + g.h);
  for (let y = 0; y < g.h; y++) {
    let line = '';
    for (let x = 0; x < g.w; x++) { const v = px[y*g.w+x]; line += v > 9 ? 'X' : ch[v]; }
    console.log('   |' + line + '|');
  }
}
const nz = glyphs.map((g,i)=>i);
show(34, 'U+0022?');
show(36, 'U+0024?');
