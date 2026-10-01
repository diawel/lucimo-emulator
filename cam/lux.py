"""lux.log から、指定時刻の直前/直後の輝度差を出す。自動露出が戻る前の立ち上がりを見る。"""
import os, sys, time
LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "lux.log")
def series():
    out = []
    for l in open(LOG):
        try: t, v = l.split(); out.append((float(t), float(v)))
        except ValueError: pass
    return out
def delta(t0, pre=1.0, post=0.8):
    s = series()
    a = [v for t, v in s if t0 - pre <= t < t0]
    b = [v for t, v in s if t0 + 0.15 <= t < t0 + post]
    if not a or not b: return None
    return sum(b)/len(b) - sum(a)/len(a)
if __name__ == "__main__":
    s = series()
    print(f"サンプル数 {len(s)}  直近値 {s[-1][1] if s else '--'}  "
          f"更新 {time.time()-s[-1][0]:.1f}秒前" if s else "データなし")
