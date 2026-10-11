// lucimo 照明リモコン: M5Atom Lite を仮想リモコン化する
//   - 初回は AP "lucimo-setup" が立つので、スマホから繋いでWiFiを設定する
//   - 以後 http://lucimo.local/ でリモコンのWebページが開く
//   - HTTP API: GET /cmd?k=ON  （keyは / のページ or /list 参照）
//   - シリアルに S を送るとリモコンの電波の採取を始め、X で止める（README「電波を採取する」）
#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <esp_bt.h>
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "frames.h"

WebServer web(80);
static const uint8_t BTN = 39;   // M5Atom Lite 本体ボタン（押下でLOW）
static uint32_t btnMinMs = 80;  // 本体ボタンの最低送信時間（/btnms で変更可）
static const uint8_t LED = 27;   // M5Atom Lite のRGB LED（SK6812）

// 次に送る命令をLEDで示す: 緑=次はON / 赤=次はOFF / 青=送信中
static void led(uint8_t r, uint8_t g, uint8_t b) { neopixelWrite(LED, r, g, b); }
static void ledNext(bool nextIsOn) { if (nextIsOn) led(0, 24, 0); else led(24, 0, 0); }

// ---- BLEアドバタイズ送信（生HCI。bluedroidのAPIは今回のアドレス型を弾くため使わない） ----
// 送信は生HCIで行う。正規APIは送信間隔の下限が20msで、実機(約2.6ms)に対して
// 発射数が1/7しか出せず、連続操作で取りこぼす（実測: 150msの押下で8発 vs 実機57発）。
// コントローラは5msを受け付けるので、完了イベントを待つ実装で直接叩く。
static volatile uint16_t doneOp = 0;
static volatile uint8_t  doneSt = 0;
static void ctrlReady() {}
// ---- 採取: リモコンのアドバタイズを受けてシリアルに出す（S=開始 / X=停止） ----
// 照明が反応するのは Service Data(UUID 0x08F0) の方なので、それだけを拾う。
// 1押下で UUID の後ろが 10 00 のものと 11 00 のものが交互に届く。送り返して動くと
// 確かめているのは 10 00 の方なので、そちらだけを残す。
struct Cap { uint32_t t; uint8_t a[6], d[31]; };
static Cap capQ[16];
static volatile uint8_t capW = 0, capR = 0;
static void capPush(const uint8_t *r, uint16_t l) {  // r = LE Advertising Report の1件目
  if (l < 9) return;
  uint8_t n = r[8];                                  // r: 種別,アドレス型,アドレス6,長さ,データ
  const uint8_t *dt = r + 9;                         // データ: 02 01 02 1B 16 F0 08 ...
  if (n != 31 || l < 9u + n || dt[5] != 0xF0 || dt[6] != 0x08 || dt[7] != 0x10 || dt[8] != 0x00) return;
  Cap &c = capQ[capW % 16];
  c.t = millis(); memcpy(c.a, r + 2, 6); memcpy(c.d, dt, 31);
  capW++;
}

static int ctrlRecv(uint8_t *d, uint16_t l) {
  if (l >= 7 && d[0] == 0x04 && d[1] == 0x0E) {      // Command Complete
    doneOp = d[4] | (d[5] << 8); doneSt = d[6];
  }
  if (l >= 5 && d[0] == 0x04 && d[1] == 0x3E && d[3] == 0x02 && d[4] >= 1)  // LE Advertising Report
    capPush(d + 5, l - 5);
  return 0;
}
static esp_vhci_host_callback_t vhciCb = { ctrlReady, ctrlRecv };

static bool hci(uint16_t op, const uint8_t *p, uint8_t n) {
  uint8_t buf[64];
  buf[0] = 0x01; buf[1] = op & 0xFF; buf[2] = op >> 8; buf[3] = n;
  if (n) memcpy(buf + 4, p, n);
  uint32_t t0 = millis();
  while (!esp_vhci_host_check_send_available())
    if (millis() - t0 > 300) return false; else delay(1);
  doneOp = 0; doneSt = 0xFF;
  esp_vhci_host_send_packet(buf, n + 4);
  t0 = millis();
  while (doneOp != op && millis() - t0 < 300) delay(1);
  return doneOp == op && doneSt == 0;
}
static bool advEnable(bool on) { uint8_t e = on; return hci(0x200A, &e, 1); }

static uint32_t txUntil = 0;
static uint32_t lastAct = 0;
static uint32_t waitReady() { return 0; }          // 保留は無し

static void txStop() { advEnable(false); txUntil = 0; }

// 任意のAdvDataをそのまま送る（解析用）。アドレスは全トークン共通のものを使う。
static void txRaw(const uint8_t *data) {
  advEnable(false);
  uint8_t a[6]; memcpy_P(a, TOKS[0].addr, 6);
  hci(0x2005, a, 6);
  uint8_t d[32]; d[0] = 31; memcpy(d + 1, data, 31);
  hci(0x2008, d, 32);
  advEnable(true);
  txUntil = millis() + 8000;
}

// アドレスもパラメータも全ボタン共通なので起動時に一度だけ設定する。
// 命令ごとに変えるのはアドバタイズデータだけ。
// 照明は一度受けたトークンを約16秒間は弾く（実測: 再利用間隔4秒=2/6、8秒=3/6、16秒以上=5/5）。
// そこで単純な順番待ちではなく「最も長く休んでいるトークン」を選び、
// それでも足りなければ不足分だけ待つ。取りこぼす代わりに、稀に短い待ちが入る。
static const uint32_t MAXWAIT = 1500;     // 待ちの上限。超える場合は諦めて撃つ
static const uint32_t REST = 15000;       // 実測: 再利用11.0秒で4/10、11.5秒で10/10。閾値11秒台に25%の余裕
static uint32_t lastUsed[NTOK];           // 0 = 未使用
static int8_t forceTok = -1;              // 検証用: 使うトークンを固定する

// 任意のAdvDataをそのまま送る（解析用）。照明がどのバイトを検証しているかを調べる

static uint32_t txBusy = 0;               // 送信を見送ったとき、あと何ms待てば使えるか

static void txStart(uint8_t ci) {
  Cmd c; memcpy_P(&c, &CMDS[ci], sizeof(Cmd));
  uint8_t t;
  if (forceTok >= 0) t = c.first + (forceTok % c.n);
  else {
    t = c.first;                          // 最も長く休んでいるものを選ぶ
    for (uint8_t i = 1; i < c.n; i++) {
      uint8_t k = c.first + i;
      if (!lastUsed[k]) { t = k; break; }              // 未使用が最優先
      if (lastUsed[t] && lastUsed[k] < lastUsed[t]) t = k;
    }
    // 休みが足りなければ待つ。ただし上限を設ける（単スレッドなのでUIが固まるため）。
    // 上限を超える場合は送信しない。無駄撃ちは照明側の記憶を更新してしまい、
    // かえって回復を遅らせる（連打するほど復活が遠のく）。
    txBusy = 0;
    if (lastUsed[t]) {
      uint32_t e = millis() - lastUsed[t];
      if (e < REST) {
        uint32_t need = REST - e;
        if (need > MAXWAIT) {
          txBusy = need;
          Serial.printf("  送信見送り: あと %lums 必要 (%d個中)\n", need, c.n);
          return;
        }
        delay(need);
      }
    }
    lastUsed[t] = millis() ? millis() : 1;
  }
  uint8_t addr[6], data[31];
  memcpy_P(addr, TOKS[t].addr, 6);
  memcpy_P(data, TOKS[t].data, 31);
  advEnable(false);
  hci(0x2005, addr, 6);    // アドレスはトークン共通だが念のため毎回設定する
  uint8_t d[32]; d[0] = 31; memcpy(d + 1, data, 31);
  bool ok = hci(0x2008, d, 32);
  ok &= advEnable(true);
  if (!ok) Serial.println("  送信失敗");
  txUntil = millis() + 8000;
}

// ---- グローバル連番列の順送り再生（ローリングコード対応） ----
// 照明は「後退・同一=拒否、前方のトークンを受理」。実測した2つの時間:
//   - カウンタ再同期 約2.5秒: 直前の送信から2.5秒以上あくと、後退(先頭)トークンも受理する
//   - リプレイ拒否 約11〜16秒: 同じトークンの再送はこの間弾かれる
// 連番列(SEQ, カウンタ順。SEQCMD[i]=命令ID)を前にだけ進めて撃つ。前方が尽きたら先頭へ
// 巻き戻すが、巻き戻しトークンが通る条件（先頭トークンがリプレイ拒否を抜け、かつ照明が
// 再同期済み）が揃うまでは撃たずに待つ。空撃ちで照明の記憶を乱さないため。
static const uint32_t REPLAY_MS = 16000;     // 同一トークンの再利用はこれだけ空ける（リプレイ拒否）
static const uint32_t RESYNC_MS = 3500;      // 最後の送信からこれだけ空けば照明は再同期済み（実測~2.5s+余裕）
static int seqPos = -1;                       // 直近に撃った連番位置。-1 = 未同期
static uint32_t seqLast = 0;                   // 最後にSEQを撃った時刻
static uint32_t seqUsed[NSEQ];                 // 各トークンを最後に撃った時刻（0=未使用）

// 戻り値: 1=撃った / 0=見送り(巻き戻せる条件が未達、待つ) / -1=SEQに無い(呼び元がtxStartへ)
static int seqPlay(uint8_t cmdId) {
  uint32_t now = millis();
  int front = -1;                                        // このコマンドの先頭トークン
  for (int i = 0; i < NSEQ; i++)
    if (pgm_read_byte(&SEQCMD[i]) == cmdId) { front = i; break; }
  if (front < 0) return -1;                              // SEQに無い → txStartへ

  int sel = -1;                                          // 前方の次の該当トークン
  for (int i = seqPos + 1; i < NSEQ && sel < 0; i++)
    if (pgm_read_byte(&SEQCMD[i]) == cmdId) sel = i;
  if (sel < 0) {                                         // 前方が尽きた → 巻き戻しを検討
    bool rested   = (seqUsed[front] == 0) || (now - seqUsed[front] >= REPLAY_MS);  // 先頭が16s以上前
    bool resynced = (now - seqLast >= RESYNC_MS);                                   // 最後の送信が3.5s以上前
    if (rested && resynced) sel = front;                // 両方満たせば巻き戻して撃つ
    else return 0;                                       // 揃うまで撃たずに待つ
  }

  txBusy = 0;
  uint8_t addr[6], data[31];
  memcpy_P(addr, SEQ[sel].addr, 6);
  memcpy_P(data, SEQ[sel].data, 31);
  advEnable(false);
  hci(0x2005, addr, 6);
  uint8_t d[32]; d[0] = 31; memcpy(d + 1, data, 31);
  advEnable(hci(0x2008, d, 32));
  txUntil = millis() + 8000;
  seqPos = sel; seqLast = now; seqUsed[sel] = now ? now : 1;
  return 1;
}

static int findCmd(const String &k) {
  for (uint8_t i = 0; i < NCMD; i++) {
    char b[16]; strncpy_P(b, (const char *)pgm_read_ptr(&CMDS[i].key), sizeof(b));
    if (k.equalsIgnoreCase(b)) return i;
  }
  return -1;
}

// ---- Webページ。リモコンの配列をそのまま並べる ----
static const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang=ja><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1,user-scalable=no">
<meta name=apple-mobile-web-app-capable content=yes><title>lucimo</title><style>
:root{--bg:#1c1c1e;--k:#2c2c2e;--t:#f2f2f7;--a:#0a84ff}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent;
 -webkit-user-select:none;user-select:none;-webkit-touch-callout:none;touch-action:manipulation}
body{margin:0;padding:16px;background:var(--bg);color:var(--t);
 font:15px/1.3 -apple-system,BlinkMacSystemFont,"Hiragino Sans",sans-serif;
 max-width:380px;margin:0 auto;padding-bottom:40px}
h1{font-size:13px;letter-spacing:.2em;text-align:center;color:#8e8e93;font-weight:500;margin:8px 0 20px}
.g{display:grid;gap:10px;margin-bottom:10px}
.c2{grid-template-columns:1fr 1fr}.c4{grid-template-columns:repeat(4,1fr)}
button{touch-action:none;background:var(--k);color:var(--t);border:0;border-radius:14px;padding:16px 4px;
 font:inherit;font-weight:600;cursor:pointer;transition:transform .08s,background .15s}
button:active{transform:scale(.94);background:var(--a)}
button.on{background:#30d158;color:#000}button.off{background:#3a3a3c}
.pad{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:10px}
.pad button{aspect-ratio:1;border-radius:50%;padding:0;font-size:13px}
.pad .sp{visibility:hidden}
hr{border:0;border-top:1px solid #38383a;margin:18px 0}
#t{position:fixed;left:50%;bottom:24px;transform:translateX(-50%);background:#48484a;
 padding:8px 18px;border-radius:20px;opacity:0;transition:opacity .2s;pointer-events:none;font-size:13px}
</style><h1>LUCIMO</h1>
<div class="g c2"><button class=on data-k="ON">ON</button><button class=off data-k="OFF">OFF</button></div>
<div class="g c2"><button data-k="FAV1">お好み ①</button><button data-k="FAV2">お好み ②</button></div>
<div class="g c2"><button data-k="COLOR">色調切替</button><button data-k="BRIGHTNESS">明るさ切替</button></div>
<div class=pad>
 <span class=sp></span><button data-k="BRIGHTER" data-step>明るく</button><span class=sp></span>
 <button data-k="WHITE" data-step>白色</button><button data-k="NIGHT">ナイト</button><button data-k="WARM" data-step>暖色</button>
 <span class=sp></span><button data-k="DIMMER" data-step>暗く</button><span class=sp></span>
</div>
<div class="g c2"><button data-k="TIMER30">30分</button><button data-k="TIMER60">60分</button></div>
<hr>
<div class="g c4">
 <button data-k="G1_ON">①入</button><button data-k="G2_ON">②入</button>
 <button data-k="G3_ON">③入</button><button data-k="G4_ON">④入</button>
 <button data-k="G1_OFF">①切</button><button data-k="G2_OFF">②切</button>
 <button data-k="G3_OFF">③切</button><button data-k="G4_OFF">④切</button>
</div>
<div id=t></div><script>
const ONCE_MIN=80;   // 1タップの送信時間（ms）
let t0=0,cur=null,step=0,busy=0,lastOnce=0;
function down(b,e){ e.preventDefault(); if(cur||busy)return;
  cur=b.dataset.k; step=b.hasAttribute('data-step'); t0=Date.now();
  if(navigator.vibrate)navigator.vibrate(12); toast(cur);
}
async function up(e){ if(!cur)return; if(e)e.preventDefault();
  const k=cur; cur=null; busy=1;
  // 実機は長押し中0.2秒ごとに新トークンを作るが、採取済みトークンでは追随できない
  // （長押し1秒で5個消費してしまう）。よって 1タップ = 1段階 に統一する。
  try{
    await fetch(`/cmd?k=${k}&ms=${ONCE_MIN}`);
    const w=+(await (await fetch('/busy')).text());   // 見送られていたら残り時間が返る
    if(w>0) toast(`${k} あと${Math.ceil(w/1000)}秒`);
  }catch(e){ toast('送信できません'); }
  busy=0; }
addEventListener('pointerup',up); addEventListener('pointercancel',up);
for(const b of document.querySelectorAll('button'))
  b.addEventListener('pointerdown',e=>down(b,e));
function toast(m){ const t=document.getElementById('t');
  t.textContent=m; t.style.opacity=1; clearTimeout(t._h);
  t._h=setTimeout(()=>t.style.opacity=0,900); }
</script></html>)HTML";

void setup() {
  Serial.begin(115200);
  btStart();
  esp_vhci_host_register_callback(&vhciCb);
  hci(0x0C03, nullptr, 0);                          // HCI Reset
  {
    uint8_t a[6]; memcpy_P(a, TOKS[0].addr, 6);     // アドレスは全トークン共通
    bool k1 = hci(0x2005, a, 6);                    // LE Set Random Address（LSByte先）
    uint8_t p[15] = { 0x08,0x00, 0x08,0x00,         // 送信間隔 5ms
                      0x03,                          // ADV_NONCONN_IND
                      0x01, 0x00, 0,0,0,0,0,0,
                      0x01,                          // ch37 のみ
                      0x00 };
    bool k2 = hci(0x2006, p, sizeof(p));            // パラメータも一度だけ
    Serial.printf("初期化 アドレス=%d パラメータ=%d\n", k1, k2);
  }

  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  if (!wm.autoConnect("lucimo-setup")) ESP.restart();
  MDNS.begin("lucimo");

  web.on("/", []{ web.send_P(200, "text/html; charset=utf-8", PAGE); });
  web.on("/down", []{                       // 押している間だけ送る（段階ボタン用）
    int i = findCmd(web.arg("k"));
    if (i < 0) { web.send(404, "text/plain", "unknown"); return; }
    txStart(i);                             // 離すまで送り続ける
    lastAct = millis();
    web.send(200, "text/plain", "ok");
  });
  web.on("/up", []{ txStop(); web.send(200, "text/plain", "ok"); });
  web.on("/cmd", []{
    int i = findCmd(web.arg("k"));
    if (i < 0) { web.send(404, "text/plain", "unknown"); return; }
    uint16_t ms = web.arg("ms").toInt(); if (!ms) ms = 80;
    web.send(200, "text/plain", "ok");     // 先に応答。ブラウザを待たせない（見送りは /busy で分かる）
    if (!web.hasArg("t")) {                 // 連番列で順送り再生（検証用の &t 指定時は従来路）
      int r = seqPlay(i);
      if (r == 1) { lastAct = millis(); delay(ms); txStop(); return; }
      if (r == 0) return;                   // 見送り。/busy で分かる
    }                                       // r == -1（未採取）は下の従来路へ落ちる
    forceTok = web.hasArg("t") ? web.arg("t").toInt() : -1;   // 検証用
    txStart(i);
    if (txBusy) return;                   // 見送り。応答は送信済み
    lastAct = millis(); delay(ms); txStop();
  });
  web.on("/wifioff", []{                    // WiFiを完全に止める（復帰は電源再投入）
    web.send(200, "text/plain", "wifi off");
    delay(200);
    WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
    Serial.println("WiFi停止。以後は本体ボタンのみ");
  });
  web.on("/raw", []{                        // /raw?d=<AdvData 31バイトのhex 62文字>
    String h = web.arg("d");
    if (h.length() != 62) { web.send(400, "text/plain", "need 62 hex"); return; }
    uint8_t d[31];
    for (int i = 0; i < 31; i++)
      d[i] = strtoul(h.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
    uint16_t ms = web.arg("ms").toInt(); if (!ms) ms = 500;
    web.send(200, "text/plain", "ok");
    txRaw(d); delay(ms); txStop();
  });
  web.on("/busy", []{ web.send(200, "text/plain", String(txBusy)); });
  web.on("/btnms", []{                      // 本体ボタンの最低送信時間を変える
    if (web.hasArg("v")) btnMinMs = web.arg("v").toInt();
    web.send(200, "text/plain", String(btnMinMs));
  });
  web.on("/list", []{
    String s;
    for (uint8_t i = 0; i < NCMD; i++) {
      Cmd c; memcpy_P(&c, &CMDS[i], sizeof(Cmd));
      char k[16], j[24];
      strncpy_P(k, c.key, sizeof(k)); strncpy_P(j, c.jp, sizeof(j));
      s += String(k) + "\t" + j + "\t" + c.n + "個\n";
    }
    web.send(200, "text/plain; charset=utf-8", s);
  });
  pinMode(BTN, INPUT);
  for (int i = 0; i < 3; i++) {                  // 起動時の自己診断: 赤→緑→青
    led(i == 0 ? 60 : 0, i == 1 ? 60 : 0, i == 2 ? 60 : 0);
    Serial.printf("LED %s\n", i == 0 ? "赤" : i == 1 ? "緑" : "青");
    delay(400);
  }
  ledNext(true);                                 // 起動時は「次はON」（緑）
  web.begin();
  Serial.printf("READY http://%s/  http://lucimo.local/\n", WiFi.localIP().toString().c_str());
}

static bool btnDown = false, btnIsOn = false;
static uint32_t prevEnd = 0;
static uint32_t btnT0 = 0;

static void pollButton() {
  bool down = digitalRead(BTN) == LOW;
  if (down == btnDown) return;
  delay(20);                                   // チャタリング除去
  if ((digitalRead(BTN) == LOW) != down) return;
  btnDown = down;
  if (down) {
    btnIsOn = !btnIsOn;
    // ON/OFF は連番列を順送り再生する（無ければ従来路）。
    const char *key = btnIsOn ? "ON" : "OFF";
    int i = findCmd(key);
    btnT0 = millis();
    led(0, 0, 40);                             // 送信中は青
    if (seqPlay(i) < 0) txStart(i);             // 最低時間なし
    Serial.printf("[%7lu] 押下 %-6s 前の送信終了から %lums 無音\n",
                  millis(), key, prevEnd ? millis() - prevEnd : 0);
  } else {
    // ON/OFF は段階の概念が無いので、最低限の発射数を確保する。
    // 照明はバーストの先頭で反応するため、長く撃っても体感の遅延は増えない。
    uint32_t held = millis() - btnT0;
    if (held < btnMinMs) delay(btnMinMs - held);
    txStop();
    ledNext(!btnIsOn);                         // 次の押下で送る命令を表示
    Serial.printf("[%7lu] 解放   押下%lums 送信%lums\n", millis(), held, millis() - btnT0);
    prevEnd = millis();
  }
}

// 1押下で同じアドバタイズが数十発届くので、直前と違うものだけを出す。
// 形式: <時刻(0.1秒単位)> ADV <アドレス 送出順> <AdvData 31バイト>
static void capPoll() {
  static uint8_t last[31];
  while (capR != capW) {
    Cap c = capQ[capR % 16]; capR++;
    if (!memcmp(c.d, last, 31)) continue;
    memcpy(last, c.d, 31);
    Serial.printf("%lu ADV ", c.t / 100);
    for (int i = 0; i < 6; i++) Serial.printf("%02X", c.a[i]);
    Serial.print(' ');
    for (int i = 0; i < 31; i++) Serial.printf("%02X", c.d[i]);
    Serial.println();
  }
}

static void capCmd(char c) {
  if (c == 'S') {
    txStop();
    uint8_t m[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x3F };  // LE Meta イベントは既定で無効なので有効にする
    hci(0x0C01, m, 8);
    uint8_t p[7] = { 0x00, 0x10,0x00, 0x10,0x00, 0x00, 0x00 };  // パッシブ、間隔=窓=10ms
    uint8_t e[2] = { 0x01, 0x00 };                              // 開始、重複を省かない
    bool ok = hci(0x200B, p, 7) && hci(0x200C, e, 2);
    Serial.printf("CAPTURE-START %d\n", ok);
  } else if (c == 'X') {
    uint8_t e[2] = { 0x00, 0x00 };
    hci(0x200C, e, 2);
    capPoll();
    Serial.println("CAPTURE-END");
  }
}

void loop() {
  if (Serial.available()) capCmd(Serial.read());
  capPoll();
  pollButton();
  web.handleClient();
  if (txUntil && millis() > txUntil) txStop();
}
