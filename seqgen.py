"""M5Atom Lite の採取ログから、全ボタンを決めた順で1周×数周押したログを SEQ_ALL にする。
  python3 seqgen.py <採取ログ> <ボタン名1> <ボタン名2> ... <ボタン名n>
周の間を2.5秒以上空けて押すこと（そこで周を区切る）。連続してちょうどn個そろった
周のまとまりだけを、押した順のまま SEQ_ALL にする（照明の通し番号を連続させるため）。
採り方は README「SEQ_ALL を採る」を参照。"""
import sys, lucimo

GAP = 25   # 2.5秒（採取ログの時刻は0.1秒単位）。これ以上空けば次の周とみなす


def split_rounds(times, gap=GAP):
    """時刻の列を gap 超の間隔で区切る。各要素は元のインデックスのリスト。"""
    rounds, cur = [], []
    for i, t in enumerate(times):
        if cur and t - times[i - 1] > gap:
            rounds.append(cur); cur = []
        cur.append(i)
    if cur:
        rounds.append(cur)
    return rounds


def longest_good_run(rounds, n):
    """ちょうど n 個そろった周が連続する、最長のまとまりを返す。"""
    best, cur = [], []
    for r in rounds + [None]:
        if r is not None and len(r) == n:
            cur.append(r)
        else:
            if len(cur) > len(best):
                best = cur
            cur = []
    return best


def main():
    if len(sys.argv) < 3:
        sys.exit("使い方: python3 seqgen.py <採取ログ> <ボタン名1> ... <ボタン名n>")
    log, names = sys.argv[1], sys.argv[2:]
    n = len(names)
    good = lucimo.read_capture(log)
    rounds = split_rounds([t for t, _ in good])
    print(f"受信した有効フレーム {len(good)} / 検出した周 {len(rounds)}（各 {[len(r) for r in rounds]} 個）")
    run = longest_good_run(rounds, n)
    if not run:
        sys.exit(f"ちょうど{n}個そろった周がありません。押し方を見直して採り直してください")
    print(f"採用: 連続してそろった {len(run)} 周（= 各ボタン {len(run)} 個）")

    seq = [[names[pos], good[idx][1]] for r in run for pos, idx in enumerate(r)]
    db = lucimo.load()
    db['SEQ_ALL'] = seq
    first = {}
    for name, frame in seq:
        first.setdefault(name, frame)
    for name, frame in first.items():
        db.setdefault(name, [frame])              # CMDS用の代表（無いボタンだけ）
    lucimo.save(db)

    from collections import Counter
    print("SEQ_ALL", len(seq), "個を frames.json に保存:", dict(Counter(k for k, _ in seq)))


def demo():
    # 周内は0.1s間隔、周間は3s(=30)空ける → 2周に割れる
    assert [len(x) for x in split_rounds([0, 10, 20, 50, 60, 70], gap=25)] == [3, 3]
    # good(3) bad(2) good(3) good(3) → 連続する末尾2周を選ぶ
    assert longest_good_run([[0, 1, 2], [3, 4], [5, 6, 7], [8, 9, 10]], 3) == [[5, 6, 7], [8, 9, 10]]
    print("demo ok")


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--demo":
        demo()
    else:
        main()
