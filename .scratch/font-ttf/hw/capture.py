"""抓一段串口日志：复位目标后读 N 秒，UTF-8 落盘。用法: capture.py COM3 12 out.log"""
import sys
import time

import serial


def main():
    port = sys.argv[1]
    seconds = float(sys.argv[2])
    out = sys.argv[3]
    ser = serial.Serial(port, 115200, timeout=0.1)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    # 只复位不进下载模式：RTS 拉 EN 低再放开，DTR 全程释放（IO0 高）
    ser.setDTR(False)
    ser.setRTS(True)
    time.sleep(0.1)
    ser.setRTS(False)
    ser.setDTR(False)
    time.sleep(0.1)
    buf = bytearray()
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
        else:
            time.sleep(0.02)
    ser.close()
    text = buf.decode('utf-8', errors='replace')
    with open(out, 'w', encoding='utf-8') as f:
        f.write(text)
    print('bytes=%d lines=%d -> %s' % (len(buf), text.count(chr(10)), out))


main()
