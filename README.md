# lucimo-emulator

lucimo の 2.4GHz リモコン照明（VENTOTA Minoi CGY、リモコン LUC-RC-01）を、スマホのブラウザや HTTP から操作できるようにするスマートリモコンです。
M5Atom Lite がリモコンの代わりに電波を出します。
照明本体の改造や配線は要りません。

リモコンの電波は暗号で保護されていて、命令を組み立てて送ることはできません。
そこで、手元のリモコンを実際に押して電波を採取し、それを M5Atom Lite から送り直します。
採取は最初に一度だけ行えば済みます。

## できること

- **Web リモコン**：`http://lucimo.local/` を開くと、実機と同じ配列のボタンが並んだページが出ます。iPhone ではホーム画面に追加するとアプリのように使えます。
- **HTTP API**：`curl http://lucimo.local/cmd?k=ON` のように叩けるので、スマートホームの自動化やショートカットから呼べます。
- **本体ボタン**：M5Atom Lite のボタンを押すたびに ON と OFF を交互に送ります。LED は緑なら次が ON、赤なら次が OFF、送信中は青です。

リモコンの全21ボタン（ON、OFF、お好み①②、色調切替、明るさ切替、明るく、暗く、白色、暖色、ナイト、30分と60分のタイマー、グループ①〜④の入と切）に対応しています。

## 制限

- 長押しには対応していません。明るく、暗く、白色、暖色は1タップで1段階だけ動きます。
- 作者の手元のリモコン1台と照明1台でしか確かめていません。別の個体で採取がうまくいかないときは「[別のリモコンで使うとき](#別のリモコンで使うとき)」を見てください。

## 必要なもの

送信用（常設するもの）:

- M5Atom Lite
- USB 電源

採取用（最初の一度だけ使うもの）:

- Arduino Uno
- nRF24L01+ モジュールと 100µF のコンデンサ
- PC（macOS で確認。Linux でもポート名を読み替えれば動くはずです）

ソフトウェア:

- [arduino-cli](https://arduino.github.io/arduino-cli/)
- ボード定義 `m5stack:esp32` と `arduino:avr`
- ライブラリ WiFiManager（tzapu）と RF24（TMRh20）
- Python 3 と pyserial

```sh
arduino-cli core install m5stack:esp32 arduino:avr
arduino-cli lib install WiFiManager RF24
pip3 install pyserial
```

m5stack のボード定義は追加の URL が要ります。
導入方法は M5Stack の公式ドキュメントを参照してください。

## セットアップ

### 1. 採取用の受信機を作る

nRF24L01+ を Arduino Uno に次のように配線します。
VCC と GND のあいだに 100µF のコンデンサを入れると受信が安定します。

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

### 2. リモコンの電波を採取する

リポジトリに入っている `frames.json` は作者のリモコンから採ったものです。
自分のリモコンでは使えないので、先に空にしておきます。

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

### 3. M5Atom Lite に書き込む

M5Atom Lite を PC につなぎ、`build.sh` で書き込みます。
`build.sh` は `frames.json` から `lucimo_server/frames.h` を作ってから書き込みます。
手で `arduino-cli compile` すると古い `frames.h` のまま焼かれ、照明が反応しなくなるので、必ずこのスクリプトを使ってください。

```sh
PORT=/dev/cu.usbserial-XXXXXXXXXX ./build.sh
```

### 4. Wi-Fi につなぐ

初回の起動では、M5Atom Lite が `lucimo-setup` という Wi-Fi のアクセスポイントになります。
スマホでこれにつなぐと設定画面が開くので、家の Wi-Fi を選んでパスワードを入れます。
3分以内に設定しないと再起動するので、そのときはもう一度つなぎ直してください。

設定が済むと、同じネットワークから `http://lucimo.local/` で開けるようになります。
以後は電源を入れるだけで自動でつながります。

## 使い方

### Web リモコン

`http://lucimo.local/` を開き、ボタンをタップします。
照明は押してから約0.25秒で反応します。

### HTTP API

| リクエスト | 内容 |
|---|---|
| `GET /cmd?k=<ボタン名>` | ボタンを1回押したのと同じ電波を送る。`&ms=<ミリ秒>` で送信時間を変えられる（既定 250） |
| `GET /list` | 登録済みのボタンと、採取したトークンの数を返す |
| `GET /btnms?v=<ミリ秒>` | 本体ボタンの最低送信時間を変える（既定 250） |
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
ただし、しばらく（約16秒）何も受けていなければ、どの番号でも一度は受け付けます。
そのため、`harvest.py` で採ったトークンでも、ボタンを十数秒おきに押す普段の使い方なら問題なく動きます。

一方で、ON と OFF を素早く繰り返すような使い方では、2回目以降が弾かれることがあります。
これを避けるには、全ボタンを決めた順に1周ずつ押したものを数周ぶん、押した順のまま `frames.json` の `SEQ_ALL` に入れます。
ファームはこの列を前にだけ進めながら送るので、照明が受け付ける順番を保てます。

`SEQ_ALL` の形式は `[[ボタン名, フレーム], ...]` で、採取した順に並べます。
途中でリモコンを押していない、ひと続きの採取から作る必要があります。
今のところ、WATCH の出力からこの列を作るスクリプトはリポジトリにありません。
採り方の詳細は[解析の記録](research/README.md)にあります。

## 別のリモコンで使うとき

`ADDR 01E10FCF62` は作者のリモコンが送る電波の先頭部分で、受信機はこれを目印に電波を拾います。
同じ型番でも個体ごとに値が違う場合、`WATCH` は何も拾いません。
そのときは、受信機を BLE のアクセスアドレス（`8E89BED6`）に同期させてチャンネル2で受信し、自分のリモコンの値を調べる必要があります。
受信機のコマンドは `lucimo_sniff6/lucimo_sniff6.ino` の冒頭に、電波の形式は[解析の記録](research/README.md)にあります。
見つけた値は採取コマンドの `ADDR` と、`lucimo.py` の `PHDR`（フレームの先頭16バイト）の両方に反映します。
個体差があるかどうかは確かめていないので、試した結果を Issue で教えてもらえると助かります。

## うまく動かないとき

- **照明が反応しない**：`build.sh` を使わずに書き込むと、古いフレーム表のまま焼かれます。`build.sh` で書き込み直してください。
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
| `genframes.py` | `frames.json` から `lucimo_server/frames.h` を作る（`build.sh` が呼ぶ） |
| `lucimo.py` | フレームの CRC 検証と `frames.json` の読み書き。`python3 lucimo.py list` で登録内容を表示する |
| `clone.py` | ホワイトニングの計算（`lucimo.py` と `genframes.py` が読み込む） |
| `frames.json` | 採取したトークン |
| `build.sh` | `frames.h` を作ってから M5Atom Lite に書き込む |
| `research/` | 電波の解析の記録と、解析にだけ使ったツール |

## 仕組みと解析の記録

電波の形式、暗号で保護されていることの確認、通し番号の受け付け条件などを調べた経緯は [research/README.md](research/README.md) にまとめています。
実験の一部は再検証していないので、参考として読んでください。
