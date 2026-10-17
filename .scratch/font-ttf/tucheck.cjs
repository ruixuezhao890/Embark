const fs = require('fs');
const { spawnSync } = require('child_process');
const db = JSON.parse(fs.readFileSync(process.env.ROOT + '/build-esp32/compile_commands.json', 'utf8'));
const norm = (p) => p.split(String.fromCharCode(92)).join('/');
const targets = process.argv.slice(2);
let failed = 0;
for (const t of targets) {
  const e = db.find((x) => norm(x.file).endsWith(t));
  if (!e) { console.log('MISS ' + t); failed++; continue; }
  let argv = e.command ? e.command.match(/"[^"]*"|\S+/g) : [e.arguments[0], ...e.arguments.slice(1)];
  argv = argv.map((a) => (a.length > 1 && a[0] === '"' && a[a.length - 1] === '"' ? a.slice(1, -1) : a));
  const out = [argv[0]];
  for (let i = 1; i < argv.length; i++) {
    if (argv[i] === '-c') { out.push('-fsyntax-only'); continue; }
    if (argv[i] === '-o') { i++; continue; }
    out.push(argv[i]);
  }
  const r = spawnSync(out[0], out.slice(1), { cwd: e.directory, encoding: 'utf8', maxBuffer: 1 << 26 });
  const ok = r.status === 0;
  if (!ok) failed++;
  console.log((ok ? 'OK   ' : 'FAIL ') + t + (ok ? '' : '\n' + (r.stdout || '') + (r.stderr || '')));
}
process.exit(failed ? 1 : 0);