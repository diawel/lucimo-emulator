"""WATCH の出力を、押下の時間間隔でボタンごとに振り分けて frames.json に取り込む。
  python3 harvest.py <採取ログ> <ボタン名1> <ボタン名2> ...
ボタンを変えるときに5秒以上あけて押すこと（そこで区切る）。"""
import json, re, sys, lucimo
GAP = 40                        # 4.0秒（WATCH の時刻は0.1秒単位）
log, names = sys.argv[1], sys.argv[2:]
rows = [(int(m.group(1)), m.group(2).strip())
        for m in (re.match(r'^(\d+) ((?:[0-9A-F]{2} ){24,})', l) for l in open(log)) if m]
groups, cur = [], []
for i, (t, h) in enumerate(rows):
    if cur and t - rows[i-1][0] > GAP: groups.append(cur); cur = []
    cur.append(h)
if cur: groups.append(cur)
print(f"検出したグループ: {[len(g) for g in groups]}  （指定ボタン: {len(names)}個）")
if len(groups) != len(names):
    print("!! グループ数とボタン数が合いません。押し間違いか、間の取り方が短い可能性があります")
    sys.exit(1)
db = lucimo.load()
for name, g in zip(names, groups):
    good = []
    for h in g:
        try: good.append(lucimo.check(h))
        except ValueError: pass
    db[name] = good
    print(f"  {name}: {len(good)}/{len(g)} 個（CRC検証済み）")
lucimo.save(db)
