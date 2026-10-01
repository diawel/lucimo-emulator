def rev(b): return int(f"{b:08b}"[::-1], 2)
def wseq(n, chan=37):
    lfsr = (chan & 0x3F) | 0x40; out=[]
    for _ in range(n):
        b=0
        for i in range(8):
            if lfsr & 1: lfsr ^= 0x88; b |= (1<<i)
            lfsr >>= 1
        out.append(b)
    return out

# ON を押した時のフレームQ（5バイト同期語の直後から32バイト）
Q = "EF 5B 6F F7 31 3B 97 D6 73 E2 B8 71 0F 55 AA 98 43 AF 0B 46 46 46 28 51 60 51 60 81 10 56 27 27"
payload = [int(x,16) for x in Q.split()]
target = [0xF3] + payload          # 同期語5バイト目 + ペイロード = AA直後に必要な並び
w = wseq(48)

print(f"AA直後に必要なバイト数: {len(target)}  （枠: ヘッダ2 + AdvA6 + AdvData31 = 39）")
print()
# 平文[i] = ビット反転(必要な値) XOR 系列[i]
plain = [rev(t) ^ w[i] for i, t in enumerate(target)]

print("pos0 = PDUヘッダ[0] -> 平文 0x%02X" % plain[0])
t = plain[0]
print(f"   PDUタイプ={t & 0x0F} ChSel={(t>>5)&1} TxAdd={(t>>6)&1} RxAdd={(t>>7)&1}")
print("   => ADV_NONCONN_IND(2) + TxAdd=1(ランダムアドレス)" if (t & 0x0F)==2 and (t>>6)&1 else "   => 要検討")
print()
print("pos1 = PDUヘッダ[1] 長さ -> 平文 0x%02X = %d" % (plain[1], plain[1]))
print(f"   AdvA 6 + AdvData {plain[1]-6} = {plain[1]}  " + ("OK" if 0 <= plain[1]-6 <= 31 else "NG"))
print()
advA = plain[2:8]                  # 送出順(LSByte先)
print("AdvA (送出順)   :", " ".join(f"{b:02X}" for b in advA))
print("AdvA (表記順)   :", ":".join(f"{b:02X}" for b in reversed(advA)))
top = advA[5] >> 6
kind = {0b11:"static random (OK)", 0b00:"non-resolvable private (OK)",
        0b01:"resolvable private", 0b10:"不正"}[top]
print(f"   最上位2ビット = {top:02b} -> {kind}")
print()
adv = plain[8:]
print(f"AdvData ({len(adv)}バイト) :", " ".join(f"{b:02X}" for b in adv))
print(f"   残り {31-len(adv)} バイトは自由（何を入れてもよい）")
