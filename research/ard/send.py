# send.py <送る文字> <待つ秒数>
# 送信前のログ位置を覚えて、待った後の増分だけを表示する。
import os, sys, time

D    = os.path.dirname(os.path.abspath(__file__))
LOG  = os.path.join(D, 'serial.log')
FIFO = os.path.join(D, 'cmd.fifo')
chars = sys.argv[1] if len(sys.argv) > 1 else ''
wait  = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0

start = os.path.getsize(LOG) if os.path.exists(LOG) else 0
if chars:
    with open(FIFO, 'wb') as f:
        f.write(chars.encode())
time.sleep(wait)
with open(LOG, 'rb') as f:
    f.seek(start)
    sys.stdout.write(f.read().decode('utf-8', 'replace'))
