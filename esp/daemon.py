# ボードのシリアルを開きっぱなしにして、FIFO経由でコマンドを流し、
# 出力を全部ログに落とす常駐プロセス。
# ポートを開き直すとボードがリセットされるので、開き続けてコマンドを流す。
import serial, os, select, sys, time

PORT = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbmodem101'
BAUD = 115200
D    = os.path.dirname(os.path.abspath(__file__))
LOG  = os.path.join(D, 'serial.log')
FIFO = os.path.join(D, 'cmd.fifo')

if not os.path.exists(FIFO):
    os.mkfifo(FIFO)

ser = serial.Serial(PORT, BAUD, timeout=0)
log = open(LOG, 'ab', 0)
log.write(f"\n===== daemon start {time.strftime('%H:%M:%S')} =====\n".encode())
fifo = os.open(FIFO, os.O_RDONLY | os.O_NONBLOCK)

while True:
    r, _, _ = select.select([ser.fileno(), fifo], [], [], 0.2)
    if ser.fileno() in r:
        d = ser.read(8192)
        if d:
            log.write(d)
    if fifo in r:
        c = os.read(fifo, 256)
        if c:
            ser.write(c)
            ser.flush()
        else:                      # writer が閉じた -> 開き直す
            os.close(fifo)
            fifo = os.open(FIFO, os.O_RDONLY | os.O_NONBLOCK)
