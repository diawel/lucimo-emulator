"""lucimo 照明リモコン: 採取したPフレームの検証・保存・送信"""
import json, os, subprocess, sys, time
exec(open(os.path.join(os.path.dirname(__file__), 'clone.py')).read().split('# ON を押した')[0])

PHDR = [int(x,16) for x in "EF 50 21 B4 B9 1F 05 EE 0C C8 CA 01 E1 0F CF 62".split()]
DB   = os.path.join(os.path.dirname(__file__), 'frames.json')

def crc24(pdu, init=0xAAAAAA):      # 反転表現。0xAAAAAA は規格値 0x555555 のビット反転
    r = init
    for b in pdu:
        for i in range(8):
            top = r & 1; r >>= 1
            if top ^ ((b >> i) & 1): r ^= 0xDA6000
    return r

def check(tail_hex):
    """スニファが ADDR 01E10FCF62 で拾った25バイト以上(=frame[16..40]-)を検証し frame[0..37] を返す"""
    t = [int(x,16) for x in tail_hex.split()]
    if len(t) < 25: raise ValueError(f"25バイト以上必要（{len(t)}バイト）")
    f, w = PHDR + t, wseq(48)
    pdu  = [rev(x) ^ w[i] for i,x in enumerate([0xF3] + f[:38])]
    c    = crc24(pdu)
    calc = [rev(((c >> (8*i)) & 0xFF) ^ w[39+i]) for i in range(3)]
    if calc != f[38:41]:
        raise ValueError(f"CRC不一致 実測={f[38:41]} 計算={calc}")
    return f[:38]

def slot(frame38):
    w = wseq(39); p = [rev(x) ^ w[i] for i,x in enumerate([0xF3] + frame38)]
    assert p[1] == 37, p[1]
    return bytes(p[2:8]).hex().upper(), bytes(p[8:]).hex().upper()

def load(): return json.load(open(DB)) if os.path.exists(DB) else {}
def save(d): json.dump(d, open(DB,'w'), indent=1, ensure_ascii=False)

def esp(c):
    subprocess.run(["python3", os.path.join(os.path.dirname(__file__),"esp/send.py"), c, "0.3"],
                   capture_output=True)

def play(name, bursts=2):
    f = load()[name]
    a, d = slot(f)
    esp(f"PWR 7\nIVL 8\nMODE A\nA {a} {d}\n")
    for _ in range(bursts):                 # 実機同様 1.5秒バースト＝約190発
        esp("GO\n");   time.sleep(1.5)
        esp("HALT\n"); time.sleep(0.4)

if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "add":                        # add <名前> "<32バイトhex>"
        d = load(); d[sys.argv[2]] = check(sys.argv[3]); save(d)
        print(f"保存: {sys.argv[2]}  CRC検証OK")
    elif cmd == "list":
        for k, v in load().items():
            print(f"  {k:12} {' '.join(f'{b:02X}' for b in v[:8])} ...")
    else:
        play(cmd); print(f"送信: {cmd}")
