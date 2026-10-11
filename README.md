# lucimo-emulator

lucimo の 2.4GHz リモコン照明（VENTOTA Minoi CGY、リモコン LUC-RC-01）を、スマホのブラウザや HTTP から操作できるようにするスマートリモコンです。
M5Atom Lite がリモコンの代わりに電波を出します。
照明本体の改造や配線は要りません。

VENTOTA 株式会社とは関係のない非公式のツールです。
使用は自己責任でお願いします。

リモコンの電波には署名が付与されていて、命令のバイト列を自分で組み立てて送っても照明は反応しません。
このリポジトリには、作者のリモコンから採取した電波（`frames.json`）が入っています。
lucimo の照明は、ペアリングしたリモコンの電波を覚えて反応する仕組みです。
そのため、M5Atom Lite にこの電波を書き込み、照明をそれとペアリングすれば、手元のリモコンの電波を採取しなくても使えます。

## できること

- **Web リモコン**：`http://lucimo.local/` を開くと、実機と同じ配列のボタンが並んだページが出ます。iPhone ではホーム画面に追加するとアプリのように使えます。
- **HTTP API**：`curl http://lucimo.local/cmd?k=ON` のように叩けるので、スマートホームの自動化やショートカットから呼べます。
- **本体ボタン**：M5Atom Lite のボタンを押すたびに ON と OFF を交互に送ります。LED は緑なら次が ON、赤なら次が OFF、送信中は青です。

リモコンの全21ボタン（ON、OFF、お好み①②、色調切替、明るさ切替、明るく、暗く、白色、暖色、ナイト、30分と60分のタイマー、グループ①〜④の入と切）に対応しています。

## 制限

- 長押しには対応していません。明るく、暗く、白色、暖色は1タップで1段階だけ動きます。
- 作者の手元の照明でしか確かめていません。同梱の電波で別の家の照明をペアリングできるかは、まだ試していません。試した結果を Issue で教えてもらえると助かります。

## 必要なもの

- M5Atom Lite と USB 電源
- PC（macOS で確認。Linux でもポート名を読み替えれば動くはずです）
- [arduino-cli](https://arduino.github.io/arduino-cli/) とボード定義 `m5stack:esp32`
- Arduino ライブラリ WiFiManager（tzapu）
- Python 3 と pyserial（ボード定義 2.0.8 の書き込みツール esptool が使うほか、`build.sh` が書き込み後に起動するシリアルの常駐プロセスも使う）

```sh
arduino-cli config add board_manager.additional_urls https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json
arduino-cli core update-index
arduino-cli core install m5stack:esp32@2.0.8
arduino-cli lib install WiFiManager
pip3 install pyserial
```

次の版でビルド・書き込みを確かめています。

| 対象 | 版 |
|---|---|
| m5stack:esp32 | 2.0.8 |
| WiFiManager | 2.0.17 |
| arduino:avr（受信機） | 1.8.8 |
| RF24（受信機） | 1.6.2 |

`build.sh` の既定のボード ID は、2.0.8 での `m5stack:esp32:atom` です。
3.x 以降のコアでは ID が `m5stack:esp32:m5stack_atom` に変わっているので、`FQBN=m5stack:esp32:m5stack_atom ./build.sh` のように上書きしてください。
`arduino-cli board listall m5stack` で手元の正しい ID を確認できます。

自分のリモコンの電波を採取する場合は、ほかに Arduino Uno と nRF24L01+ が要ります（「[自分のリモコンの電波を使うとき](#自分のリモコンの電波を使うとき)」を参照）。

## セットアップ

### 1. M5Atom Lite に書き込む

M5Atom Lite を PC につなぎ、`build.sh` で書き込みます。
`build.sh` は `frames.json` から `lucimo_server/frames.h` を作ってから書き込みます。
手で `arduino-cli compile` すると古い `frames.h` のまま焼かれ、照明が反応しなくなるので、必ずこのスクリプトを使ってください。

```sh
PORT=/dev/cu.usbserial-XXXXXXXXXX ./build.sh
```

### 2. Wi-Fi につなぐ

初回の起動では、M5Atom Lite が `lucimo-setup` という Wi-Fi のアクセスポイントになります。
スマホでこれにつなぐと設定画面が開くので、家の Wi-Fi を選んでパスワードを入れます。
3分以内に設定しないと再起動するので、そのときはもう一度つなぎ直してください。

設定が済むと、同じネットワークから `http://lucimo.local/` で開けるようになります。
以後は電源を入れるだけで自動でつながります。

### 3. 照明とペアリングする

照明を M5Atom Lite とペアリングします。
手順は [VENTOTA の公式ページ](https://www.ventota.co.jp/install/lucimo-series/)の動画（個別ペアリング、まとめてペアリング）のとおりで、リモコンのボタンの代わりに Web リモコンの同じボタンを押します。
グループ①〜④の入と切もそのまま使えます。

ペアリングをやり直したいときは、同じページの解除の手順で元に戻せます。

## 使い方

### Web リモコン

`http://lucimo.local/` を開き、ボタンをタップします。
照明は押すとすぐに反応します。

### HTTP API

| リクエスト | 内容 |
|---|---|
| `GET /cmd?k=<ボタン名>` | ボタンを1回押したのと同じ電波を送る。`&ms=<ミリ秒>` で送信時間を変えられる（既定 80） |
| `GET /list` | 登録済みのボタンと、採取したトークンの数を返す |
| `GET /btnms?v=<ミリ秒>` | 本体ボタンの最低送信時間を変える（既定 80） |
| `GET /wifioff` | Wi-Fi を止める。以後は本体ボタンだけで操作でき、電源を入れ直すと戻る |

### ボタン名

| ボタン名 | リモコンの表示 |
|---|---|
| `ON` / `OFF` | 入 / 切 |
| `FAV1` / `FAV2` | お好み① / お好み② |
| `COLOR` | 色調切替 |
| `BRIGHTNESS` | 明るさ切替 |
| `BRIGHTER` / `DIMMER` | 明るく / 暗く |
| `WHITE` / `WARM` | 白色 / 暖色 |
| `NIGHT` | ナイト |
| `TIMER30` / `TIMER60` | 30分 / 60分 |
| `G1_ON` 〜 `G4_ON` | グループ①〜④ 入 |
| `G1_OFF` 〜 `G4_OFF` | グループ①〜④ 切 |

## 素早く連打したいとき

リモコンの電波には通し番号が入っていて、照明は前に受けたものより古い番号を弾きます。
ただし、直前の受信から**約2.5秒**あけば再同期して、どの番号でも受け付けます（実測）。
また、**同じ**電波の撃ち直しは**約11〜16秒**弾かれます（リプレイ防止。別軸）。
同梱の `frames.json` には、全ボタンを決めた順に1周ずつ押したものを数周ぶん、押した順のまま並べた列（`SEQ_ALL`）が入っています。
ファームはこの列を前にだけ進めながら送り、末尾まで来たら先頭へ巻き戻します。
巻き戻しは「先頭のトークンが16秒以上前に使われ、かつ最後の送信から3.5秒以上」のときだけ行い、
それ以外は照明が再同期するまで送らずに待ちます。これで、ゆっくりの操作でも巻き戻しで詰まりません。

自分のリモコンから採取した場合、`harvest.py` で採ったトークンだけでも、ボタンを十数秒おきに押す普段の使い方なら問題なく動きます。
ON と OFF を素早く繰り返すような使い方では、2回目以降が弾かれることがあります。
これを避けるには、全ボタンを決めた順に1周ずつ押したものを数周ぶん、押した順のまま `SEQ_ALL` に入れます。

### SEQ_ALL を採る

全ボタンを決めた順で1周、それを数周くりかえして採取し、`seqgen.py` で `SEQ_ALL` を作ります。
通し番号が連続している必要があるので、**ひと続きの受信で**、周の区切りだけ少し長め（3秒ほど）に空けて押します。

```sh
python3 ard/send.py 'REINIT
ADDR 01E10FCF62
WATCH 240
' 246 > /tmp/seq.txt
```

受信しているあいだに、次の21ボタンをこの順で1周、周の間を3秒ほど空けて、数周くりかえします。
周の中は1秒おきくらいの一定のペースで押します。

```
ON OFF FAV1 FAV2 COLOR BRIGHTNESS BRIGHTER WHITE NIGHT WARM DIMMER
TIMER30 TIMER60 G1_ON G2_ON G3_ON G4_ON G1_OFF G2_OFF G3_OFF G4_OFF
```

受信後、同じ順を渡して取り込みます。

```sh
python3 seqgen.py /tmp/seq.txt \
  ON OFF FAV1 FAV2 COLOR BRIGHTNESS BRIGHTER WHITE NIGHT WARM DIMMER \
  TIMER30 TIMER60 G1_ON G2_ON G3_ON G4_ON G1_OFF G2_OFF G3_OFF G4_OFF
```

`seqgen.py` は周の切れ目（2.5秒以上の間）で区切り、ちょうど21個そろった周だけを使います。
通し番号が途切れないよう、連続してそろった周のまとまりを `SEQ_ALL` にします。
採れた周が少なければ、そのぶん連打できる回数も減ります（数周あれば実用上は十分です）。
取り込んだら「[M5Atom Lite に書き込む](#1-m5atom-lite-に書き込む)」の手順で書き込み直します。

`SEQ_ALL` の形式は `[[ボタン名, フレーム], ...]` で、採取した順に並びます。
採り方の背景は[解析の記録](research/README.md)にあります。

## 自分のリモコンの電波を使うとき

同梱の電波ではなく、手元のリモコンの電波を使うこともできます。
照明のペアリングを変えずに済むほか、同梱の電波でペアリングできなかったときの代わりの手段になります。
採取には Arduino Uno、nRF24L01+、100µF のコンデンサ、RF24 ライブラリ（TMRh20）を使います。

```sh
arduino-cli core install arduino:avr
arduino-cli lib install RF24
```

### 受信機を作る

nRF24L01+ を Arduino Uno に次のように配線します。
作者の受信機では、100µF のコンデンサを nRF24L01+ の VCC と GND に並列に入れています。
nRF24L01+ では一般に推奨される部品で、受信だけなら無くても動く可能性がありますが、試していません。

| nRF24L01+ | Uno |
|---|---|
| CE | D9 |
| CSN | D10 |
| SCK | D13 |
| MOSI | D11 |
| MISO | D12 |
| VCC | 3.3V |
| GND | GND |

受信用のスケッチを書き込み、シリアルを開きっぱなしにする常駐プロセスを起動します。
ポートを開き直すと Uno がリセットされるので、以後のコマンドはこの常駐プロセス経由で送ります。

```sh
arduino-cli compile --fqbn arduino:avr:uno lucimo_sniff6
arduino-cli upload -p /dev/cu.usbmodem101 --fqbn arduino:avr:uno lucimo_sniff6
python3 ard/daemon.py /dev/cu.usbmodem101 &
```

`/dev/cu.usbmodem101` は自分の環境のポート名に置き換えてください（`arduino-cli board list` で確認できます）。

### 電波を採取する

同梱の `frames.json` は作者のリモコンのものなので、先に空にしておきます。
残しておくと、採取しなかったボタンや連打用の列（後述の `SEQ_ALL`）に作者のリモコンの電波が使われます。

```sh
echo '{}' > frames.json
```

受信を始めます。
次のコマンドは180秒間受信し、拾った電波を `/tmp/h.txt` に保存します。

```sh
python3 ard/send.py 'REINIT
ADDR 01E10FCF62
WATCH 180
' 186 > /tmp/h.txt
```

受信しているあいだに、リモコンのボタンを押します。
同じボタンは続けて何回押してもかまいません。
ボタンを変えるときだけ、5秒以上あけてください（`harvest.py` はこの間隔でボタンを区切ります）。
1回の受信で全ボタンを押しきれないときは、何回かに分けて採取します。

受信が終わったら、押した順にボタン名を並べて取り込みます。

```sh
python3 harvest.py /tmp/h.txt ON OFF FAV1 FAV2 COLOR BRIGHTNESS
```

ボタンの数と区切りの数が合わないと、何も書き込まずに止まります。
その場合は押し方を見直して採取し直してください。
使えるボタン名は「[ボタン名](#ボタン名)」の表にあります。

取り込んだら、「[M5Atom Lite に書き込む](#1-m5atom-lite-に書き込む)」の手順で書き込み直します。

### 採取できないとき

`ADDR 01E10FCF62` は作者のリモコンが送る電波の先頭部分で、受信機はこれを目印に電波を拾います。
リモコンの個体ごとに値が違う場合、`WATCH` は何も拾いません。
そのときは、受信機を BLE のアクセスアドレス（`8E89BED6`）に同期させてチャンネル2で受信し、自分のリモコンの値を調べる必要があります。
受信機のコマンドは `lucimo_sniff6/lucimo_sniff6.ino` の冒頭に、電波の形式は[解析の記録](research/README.md)にあります。
見つけた値は採取コマンドの `ADDR` と、`lucimo.py` の `PHDR`（フレームの先頭16バイト）の両方に反映します。

## うまく動かないとき

- **照明が反応しない**：まず照明が M5Atom Lite とペアリングされているかを確かめてください。ペアリング済みなら、`build.sh` を使わずに書き込んだせいで古いフレーム表のまま焼かれている可能性があります。`build.sh` で書き込み直してください。
- **WATCH が何も拾わない**：nRF24L01+ がときどき応答しなくなります。採取コマンドの先頭に `REINIT` を入れているのはこのためです。状態は次のコマンドで確かめられ、`connected=0` なら受信機が応答していません。

  ```sh
  python3 ard/send.py 'DIAG
  ' 1
  ```

- **`lucimo.local` が開けない**：mDNS に対応していない端末やネットワークでは、M5Atom Lite のシリアル出力（起動時に `READY http://<IPアドレス>/` と出ます）にある IP アドレスで開いてください。

## ファイル構成

| パス | 内容 |
|---|---|
| `lucimo_server/` | M5Atom Lite のファーム（Web リモコン、HTTP API、本体ボタン） |
| `lucimo_sniff6/` | Arduino Uno + nRF24L01+ の受信機 |
| `ard/`、`esp/` | 各ボードのシリアルを開きっぱなしにする常駐プロセスと、そこへコマンドを送るスクリプト |
| `harvest.py` | 採取ログをボタンごとに振り分けて `frames.json` に取り込む |
| `seqgen.py` | 全ボタン1周×数周の採取ログから連打用の列 `SEQ_ALL` を作る |
| `genframes.py` | `frames.json` から `lucimo_server/frames.h` を作る（`build.sh` が呼ぶ） |
| `lucimo.py` | フレームの CRC 検証と `frames.json` の読み書き。`python3 lucimo.py list` で登録内容を表示する |
| `clone.py` | ホワイトニングの計算（`lucimo.py` と `genframes.py` が読み込む） |
| `frames.json` | 採取したトークン |
| `build.sh` | `frames.h` を作ってから M5Atom Lite に書き込む |
| `research/` | 電波の解析の記録と、解析にだけ使ったツール |

## 仕組みと解析の記録

電波の形式、署名が付与されていることの確認、通し番号の受け付け条件などを調べた経緯は [research/README.md](research/README.md) にまとめています。
実験の一部は再検証していないので、参考として読んでください。

## ライセンス

[MIT](LICENSE) です。

受信機（`lucimo_sniff6`）が使う RF24 ライブラリは GPL-2.0 です。
受信機をビルドしたバイナリを配る場合は、そのバイナリに GPL-2.0 の条件が適用されます。
