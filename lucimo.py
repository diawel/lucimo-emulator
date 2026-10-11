"""lucimo 照明リモコン: 採取ログの読み込みと frames.json の読み書き"""
import json, os, re, sys
exec(open(os.path.join(os.path.dirname(__file__), 'clone.py')).read().split('# ON を押した')[0])

DB   = os.path.join(os.path.dirname(__file__), 'frames.json')

def slot(frame38):
    w = wseq(39); p = [rev(x) ^ w[i] for i,x in enumerate([0xF3] + frame38)]
    assert p[1] == 37, p[1]
    return bytes(p[2:8]).hex().upper(), bytes(p[8:]).hex().upper()

def from_adv(addr_hex, data_hex):
    """M5Atom Lite の採取（アドレス6バイト+AdvData31バイト）を frame[0..37] に直す。slot() の逆"""
    p = [0x42, 37] + list(bytes.fromhex(addr_hex)) + list(bytes.fromhex(data_hex))
    w = wseq(39)
    return [rev(x ^ w[i]) for i, x in enumerate(p)][1:]

def read_capture(log):
    """採取ログの「<時刻> ADV <アドレス> <データ>」行を [(時刻, frame38), ...] にする"""
    out = []
    for l in open(log, errors='replace'):
        m = re.match(r'^(\d+) ADV ([0-9A-F]{12}) ([0-9A-F]{62})\s*$', l)
        if m: out.append((int(m.group(1)), from_adv(m.group(2), m.group(3))))
    return out

def load(): return json.load(open(DB)) if os.path.exists(DB) else {}
def save(d): json.dump(d, open(DB,'w'), indent=1, ensure_ascii=False)

if __name__ == "__main__":
    if sys.argv[1:] == ["list"]:
        for k, v in load().items():
            print(f"  {k:12} {len(v)}個")
    elif sys.argv[1:] == ["--demo"]:
        f = load()["ON"][0]                 # 採取形式に直して戻すと元に戻る
        assert from_adv(*slot(f)) == f
        print("demo ok")
