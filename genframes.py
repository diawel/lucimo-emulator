"""frames.json から lucimo_server/frames.h を生成する。
frames.json の値は「フレーム1個」でも「フレームの配列」でもよい。
配列なら押されるたびに順に使われる（照明は直近の数個を覚えていて同じものを弾くため）。"""
import json, os
os.chdir(os.path.dirname(os.path.abspath(__file__)))
exec(open('clone.py').read().split('# ON を押した')[0])
JP = {"ON":"ON","OFF":"OFF","FAV1":"お好み①","FAV2":"お好み②","COLOR":"色調切替",
      "BRIGHTNESS":"明るさ切替","BRIGHTER":"明るく","DIMMER":"暗く","WHITE":"白色","WARM":"暖色",
      "NIGHT":"ナイトモード","TIMER30":"30分","TIMER60":"60分",
      "G1_ON":"①入","G1_OFF":"①切","G2_ON":"②入","G2_OFF":"②切",
      "G3_ON":"③入","G3_OFF":"③切","G4_ON":"④入","G4_OFF":"④切"}

def slot(f):
    w = wseq(39); p = [rev(x) ^ w[i] for i, x in enumerate([0xF3] + f)]
    assert p[1] == 37
    return p[2:8], p[8:]

db = json.load(open('frames.json'))
seq = db.pop('SEQ_ALL', [])              # 全ボタンのグローバル連番列 [[cmdKey, frame], ...] カウンタ順
tok, cmds = [], []                       # tok=全トークン、cmds=(名前, 開始位置, 個数)
for k, v in db.items():
    frames = v if v and isinstance(v[0], list) else [v]
    cmds.append((k, len(tok), len(frames)))
    tok += [slot(f) for f in frames]
cmdidx = {k: i for i, (k, _, _) in enumerate(cmds)}   # cmdKey -> CMDS内インデックス

out = ["// frames.json から自動生成（genframes.py）。手で編集しない",
       "struct Tok { uint8_t addr[6]; uint8_t data[31]; };",
       "struct Cmd { const char *key; const char *jp; uint8_t first, n; };",
       "static const Tok TOKS[] PROGMEM = {"]
for a, d in tok:
    out.append("  {{%s},{%s}}," % (",".join(f"0x{b:02X}" for b in a),
                                   ",".join(f"0x{b:02X}" for b in d)))
out += ["};", "static const Cmd CMDS[] PROGMEM = {"]
for k, first, n in cmds:
    out.append(f'  {{"{k}", "{JP.get(k,k)}", {first}, {n}}},')
out += ["};", f"static const uint8_t NCMD = {len(cmds)};",
              f"static const uint16_t NTOK = {len(tok)};"]
# グローバル連番列: カウンタ順。各トークンに命令ID(CMDS内の位置)を付けて順送り再生する
out += ["static const Tok SEQ[] PROGMEM = {"]
for k, f in seq:
    a, d = slot(f)
    out.append("  {{%s},{%s}}," % (",".join(f"0x{b:02X}" for b in a),
                                   ",".join(f"0x{b:02X}" for b in d)))
out += ["};", "static const uint8_t SEQCMD[] PROGMEM = {"]
out.append("  " + ",".join(str(cmdidx[k]) for k, _ in seq) + ("," if seq else ""))
out += ["};", f"static const uint16_t NSEQ = {len(seq)};"]
open('lucimo_server/frames.h','w').write("\n".join(out) + "\n")
print(f"frames.h 生成: {len(cmds)}コマンド / トークン計{len(tok)}個 / SEQ {len(seq)}個")
from collections import Counter
c = Counter(k for k, _ in seq)
if c: print("  SEQ内訳:", dict(c))
for k, first, n in cmds:
    if n > 1: print(f"    {k}: {n}個")
