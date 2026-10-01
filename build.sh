#!/bin/sh
# frames.json から frames.h を再生成してからビルド・書き込みする。
# 手で compile すると古いフレーム表のまま焼けるので、必ずこれを使う。
set -e
cd "$(dirname "$0")"
# M5Atom Lite のシリアルポート。環境に合わせて PORT=... ./build.sh で上書きする
PORT="${PORT:-/dev/cu.usbserial-51522BCB3F}"
# ボード ID。M5Stack のボード定義 2.0.8 で確認（このIDはコアの版で変わる:
# 2.0.x は m5stack:esp32:atom、3.x 以降は m5stack:esp32:m5stack_atom）。
# 自分のコアに合わせて FQBN=m5stack:esp32:m5stack_atom ./build.sh のように上書きする。
FQBN="${FQBN:-m5stack:esp32:atom}"
python3 genframes.py
pgrep -f "daemon.py $PORT" | xargs -r kill || true
sleep 1
arduino-cli compile --fqbn "$FQBN" lucimo_server | tail -1
arduino-cli upload -p "$PORT" --fqbn "$FQBN" lucimo_server | tail -1
(cd esp && nohup python3 daemon.py "$PORT" >/dev/null 2>&1 &)
sleep 13
