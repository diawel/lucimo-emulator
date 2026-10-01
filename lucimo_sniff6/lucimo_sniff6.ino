// lucimo_sniff6.ino - 同期語をシリアルから変えられるスニファ (nRF24L01+ / Uno R3)
//
// v5までは同期語がコンパイル時固定だった。Packet-in-Packet の検証では
//   (1) BLEのアクセスアドレス 8E89BED6 に同期して「生のBLEパケット」を受ける
//   (2) lucimoの同期語 6B7D9171F3 に戻して「埋め込んだパケット」を受ける
// を行き来する必要があるため、実行時に切り替えられるようにした。
//
// (1) は、既知の広告データを送って受信結果と突き合わせることで
// BLEのホワイトニング系列を「実測」するために使う。仕様の読み違いを排除できる。
//
// 行単位のコマンド（\n 終端）。機械から叩く前提で出力にプレフィクスを付けてある。
//   ADDR 6B7D9171F3   電波上の順で3〜5バイト。内部でRF24向けに逆順化する
//   CH 2              チャンネル
//   CAP 10            到着順に n 発ダンプ（最大10）
//   TALLY 5           n 秒集計して出現回数順に表示
//   CLR / CFG
//
// 配線据え置き: CE->D9 CSN->D10 SCK->D13 MOSI->D11 MISO->D12 VCC->3.3V / 100uF

#include <SPI.h>
#include <RF24.h>

RF24 radio(9, 10);

uint8_t addr[5] = { 0x6B, 0x7D, 0x91, 0x71, 0xF3 };   // 電波上の順
uint8_t awid    = 5;
uint8_t crcMode = 0;   // 0=なし 1=CRC8 2=CRC16
uint8_t chan    = 2;

#define NCAP 10
#define NSLOT 6
uint8_t  cap[NCAP][32];
uint8_t  smp[NSLOT][32];
uint16_t cnt[NSLOT];
uint8_t  used = 0;
uint32_t total = 0, other = 0;

char line[48];
uint8_t lineLen = 0;

void applyConfig() {
  uint8_t a[5];
  for (uint8_t i = 0; i < awid; i++) a[i] = addr[awid - 1 - i];   // RF24はLSByte先
  radio.stopListening();
  radio.setChannel(chan);
  radio.setDataRate(RF24_1MBPS);
  radio.setAutoAck(false);
  radio.disableCRC();
  radio.setPayloadSize(32);
  radio.setAddressWidth(awid);
  radio.openReadingPipe(1, a);
  radio.startListening();
}

void printCfg() {
  Serial.print(F("CFG crc=")); Serial.print(crcMode); Serial.print(F(" ch=")); Serial.print(chan);
  Serial.print(F(" ("));      Serial.print(2400 + chan); Serial.print(F("MHz)"));
  Serial.print(F(" width="));  Serial.print(awid);
  Serial.print(F(" addr="));
  for (uint8_t i = 0; i < awid; i++) {
    if (addr[i] < 16) Serial.print('0');
    Serial.print(addr[i], HEX);
  }
  Serial.println();
}

void hexLine(uint8_t *p) {
  for (uint8_t i = 0; i < 32; i++) {
    if (p[i] < 16) Serial.print('0');
    Serial.print(p[i], HEX);
    if (i != 31) Serial.print(' ');
  }
  Serial.println();
}

void doCap(uint8_t n) {
  if (n > NCAP) n = NCAP;
  Serial.print(F("CAP-START n=")); Serial.println(n);
  uint8_t got = 0;
  uint32_t t0 = millis();
  while (got < n && millis() - t0 < 10000) {
    if (radio.available()) { radio.read(cap[got], 32); got++; }
  }
  for (uint8_t i = 0; i < got; i++) { Serial.print(i); Serial.print(F(": ")); hexLine(cap[i]); }
  Serial.print(F("CAP-END got=")); Serial.println(got);
}

void tallyPut(uint8_t *p) {
  for (uint8_t i = 0; i < used; i++)
    if (!memcmp(smp[i], p, 8)) { if (cnt[i] < 65535) cnt[i]++; return; }
  if (used < NSLOT) { memcpy(smp[used], p, 32); cnt[used] = 1; used++; return; }
  uint8_t mi = 0;
  for (uint8_t i = 1; i < NSLOT; i++) if (cnt[i] < cnt[mi]) mi = i;
  memcpy(smp[mi], p, 32); cnt[mi] = 1;
}

void doTally(uint16_t sec) {
  used = 0; total = 0;
  Serial.print(F("TALLY-START sec=")); Serial.println(sec);
  uint32_t t0 = millis();
  uint8_t b[32];
  while (millis() - t0 < (uint32_t)sec * 1000) {
    if (radio.available()) { radio.read(b, 32); total++; tallyPut(b); }
  }
  Serial.print(F("TOTAL ")); Serial.println(total);
  for (uint8_t n = 0; n < used; n++) {
    int8_t bi = -1; uint16_t bc = 0;
    for (uint8_t i = 0; i < used; i++) if (cnt[i] > bc) { bc = cnt[i]; bi = i; }
    if (bi < 0) break;
    Serial.print(F("x")); Serial.print(bc); Serial.print(F(" "));
    hexLine(smp[bi]);
    cnt[bi] = 0;
  }
  Serial.println(F("TALLY-END"));
}

int8_t hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

void handle(char *s) {
  while (*s == ' ') s++;
  if (!strncmp(s, "ADDR ", 5)) {
    char *h = s + 5;
    while (*h == ' ') h++;
    uint8_t n = 0, tmp[5];
    while (h[0] && h[1] && n < 5) {
      int8_t hi = hexVal(h[0]), lo = hexVal(h[1]);
      if (hi < 0 || lo < 0) break;
      tmp[n++] = (hi << 4) | lo;
      h += 2;
    }
    if (n < 3) { Serial.println(F("ERR addr needs 3-5 bytes")); return; }
    memcpy(addr, tmp, n); awid = n;
    applyConfig(); printCfg();
  } else if (!strncmp(s, "CH ", 3)) {
    int v = atoi(s + 3);
    if (v < 0 || v > 125) { Serial.println(F("ERR ch")); return; }
    chan = v; applyConfig(); printCfg();
  } else if (!strncmp(s, "CAP", 3)) {
    doCap(atoi(s + 3) ? atoi(s + 3) : NCAP);
  } else if (!strncmp(s, "TALLY", 5)) {
    int v = atoi(s + 5);
    doTally(v ? v : 5);
  } else if (!strncmp(s, "CLR", 3)) {
    used = 0; total = 0; other = 0; Serial.println(F("OK cleared"));
  } else if (!strncmp(s, "DIAG", 4)) {
    Serial.print(F("DIAG connected=")); Serial.print(radio.isChipConnected() ? 1 : 0);
    Serial.print(F(" ch="));            Serial.print(radio.getChannel());
    Serial.print(F(" rate="));          Serial.print((int)radio.getDataRate());
    Serial.print(F(" pa="));            Serial.print((int)radio.getPALevel());
    Serial.print(F(" rpd="));           Serial.print(radio.testRPD() ? 1 : 0);
    uint16_t hits = 0;
    uint32_t t0 = millis();
    uint8_t tb[32];
    while (millis() - t0 < 100) if (radio.available()) { radio.read(tb, 32); hits++; }
    Serial.print(F(" pkts/100ms=")); Serial.println(hits);
  } else if (!strncmp(s, "REINIT", 6)) {
    radio.begin(); applyConfig();
    Serial.print(F("REINIT connected=")); Serial.println(radio.isChipConnected() ? 1 : 0);
  } else if (!strncmp(s, "CRC ", 4)) {
    crcMode = atoi(s + 4); if (crcMode > 2) crcMode = 2;
    applyConfig(); Serial.print(F("OK crc=")); Serial.println(crcMode);
  } else if (!strncmp(s, "WATCH ", 6)) {
    // Boyer-Moore 多数決で「連射されている本物のフレーム」だけを時刻つきで1行出す
    uint8_t cur[32]; memset(cur, 0, 32);
    uint16_t run = 0; bool shown = true; uint16_t got = 0, kept = 0;
    uint32_t until = millis() + (uint32_t)atoi(s + 6) * 1000;
    Serial.println(F("WATCH-START"));
    while (millis() < until) {
      if (!radio.available()) continue;
      uint8_t b[32]; radio.read(b, 32); got++;
      uint8_t same = 0;                     // 全バイト同一＝受信機のフリーラン。捨てる
      while (same < 32 && b[same] == b[0]) same++;
      if (same == 32) continue;
      kept++;
      if (!memcmp(b, cur, 25)) {          // パケットは25バイト目(frame[40]=CRC末尾)で終わる。以降はノイズ
        if (run < 60) run++;                // 上限。これが無いとノイズが票を独占する
        if (run == 20 && !shown) {
          shown = true;
          Serial.print(millis() / 100); Serial.print(F(" "));
          for (uint8_t i = 0; i < 25; i++) {
            if (b[i] < 16) Serial.print('0');
            Serial.print(b[i], HEX); Serial.print(' ');
          }
          Serial.println();
        }
      } else if (run) { run--; }
      else { memcpy(cur, b, 25); run = 1; shown = false; }
    }
    Serial.print(F("WATCH-END got=")); Serial.print(got);
    Serial.print(F(" kept=")); Serial.print(kept);
    Serial.print(F(" run=")); Serial.println(run);
  } else if (!strncmp(s, "SCAN ", 5)) {
    // 同期語に依存せず、RPD(>-64dBm)だけで全126chの電波の有無を数える
    uint16_t hits[126]; memset(hits, 0, sizeof(hits));
    uint32_t sweeps = 0, until = millis() + (uint32_t)atoi(s + 5) * 1000;
    radio.stopListening();
    while (millis() < until) {
      for (uint8_t c = 0; c < 126; c++) {
        radio.setChannel(c);
        radio.startListening();
        delayMicroseconds(300);            // RPDの確定に必要な時間
        if (radio.testRPD()) hits[c]++;
        radio.stopListening();
      }
      sweeps++;
    }
    Serial.print(F("SCAN-END sweeps=")); Serial.println(sweeps);
    for (uint8_t c = 0; c < 126; c++)
      if (hits[c]) {
        Serial.print(F("  ")); Serial.print(2400 + c); Serial.print(F("MHz ch"));
        Serial.print(c); Serial.print(F(" hits=")); Serial.println(hits[c]);
      }
    applyConfig();
  } else if (!strncmp(s, "CFG", 3)) {
    printCfg();
  } else if (*s) {
    Serial.println(F("ERR unknown"));
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  Serial.println(F("BOOT lucimo_sniff6"));
  if (!radio.begin() || !radio.isChipConnected()) {
    Serial.println(F("ERR nRF24 not found"));
    while (1) {}
  }
  applyConfig();
  printCfg();
  Serial.println(F("READY"));
}

void loop() {
  for (uint8_t i = 0; i < 8 && radio.available(); i++) { uint8_t t[32]; radio.read(t, 32); }
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (lineLen) { line[lineLen] = 0; handle(line); lineLen = 0; }
    } else if (lineLen < sizeof(line) - 1) {
      line[lineLen++] = c;
    }
  }
}
