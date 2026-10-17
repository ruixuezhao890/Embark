import sys, time, serial
port, out = sys.argv[1], sys.argv[2]
s = serial.Serial(port, 115200, timeout=0.05)
s.setDTR(False); s.setRTS(True); time.sleep(0.1); s.setRTS(False); s.setDTR(False)
t0 = time.monotonic()
buf = b''
lines = []
deadline = t0 + float(sys.argv[3] if len(sys.argv) > 3 else 30)
while time.monotonic() < deadline:
    chunk = s.read(4096)
    if not chunk: continue
    buf += chunk
    while b'\n' in buf:
        line, buf = buf.split(b'\n', 1)
        ms = (time.monotonic() - t0) * 1000.0
        lines.append('%10.1f ms  %s' % (ms, line.decode('utf-8', 'replace').rstrip()))
        if 'framework' in line.decode('utf-8','replace') or '框架就绪' in line.decode('utf-8','replace'):
            deadline = min(deadline, time.monotonic() + 1.0)
open(out, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
print('lines=%d' % len(lines))