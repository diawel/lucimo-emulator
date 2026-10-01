// ble_hci.ino - HCIを直接叩くBLEアドバタイザ (M5Atom Lite / ESP32)
//
// bluedroid を使わず、コントローラだけを起動して VHCI に生のHCIコマンドを流す。
//   * esp_ble_gap_set_rand_addr() はホスト側でアドレス種別を検査して弾くが、
//     HCI_LE_Set_Random_Address はコントローラが素直に受ける。
//     これで resolvable private 形（上位2bit=01）のAdvAも置ける。
//   * 2スロットを高速に交互送信できる。実機のリモコンはP/Qを交互に投げていた。
//
// 出している電波は標準のBLEアドバタイズそのもの（ADV_NONCONN_IND / ch37 / 20ms）。
// 変えているのはアドレスと広告データのバイト列だけ。
//
// コマンド（\n終端）:
//   A <rand12hex> <data62hex>   スロットAを設定
//   B <rand12hex> <data62hex>   スロットBを設定
//   MODE A | B | ALT            送信するスロット
//   PERIOD <ms>                 ALT時の切替周期（既定 40ms）
//   GO / HALT / INFO

#include <Arduino.h>
#include "esp_bt.h"
#include "esp_gap_ble_api.h"

struct Slot { uint8_t addr[6]; uint8_t data[31]; bool set; };
static Slot slotA = {}, slotB = {};
static char mode = 'A';
static uint16_t period = 40;
static bool running = false;
static volatile bool canSend = true;

static char line[160];
static uint8_t lineLen = 0;

static void ctrlReady(void)                        { canSend = true; }
static int  ctrlRecv(uint8_t *d, uint16_t l)       { (void)d; (void)l; return 0; }  // イベントは捨てる
static esp_vhci_host_callback_t vhciCb = { ctrlReady, ctrlRecv };

static void hci(uint16_t opcode, const uint8_t *p, uint8_t n) {
  uint8_t buf[64];
  buf[0] = 0x01;                       // HCI Command packet
  buf[1] = opcode & 0xFF;
  buf[2] = opcode >> 8;
  buf[3] = n;
  if (n) memcpy(buf + 4, p, n);
  uint32_t t0 = millis();
  while (!esp_vhci_host_check_send_available()) {      // 無限待ちにしない
    if (millis() - t0 > 300) { Serial.printf("HCI-TIMEOUT op=%04X\n", opcode); return; }
    delay(1);
  }
  esp_vhci_host_send_packet(buf, n + 4);
  delay(2);                            // Command Complete を待つ代わりの余裕
}

static void advEnable(bool on) { uint8_t v = on ? 1 : 0; hci(0x200A, &v, 1); }

static uint16_t advIvl = 0x20;      // 0.625ms 単位
static void advParams() {
  uint8_t p[15] = {
    (uint8_t)(advIvl & 0xFF), (uint8_t)(advIvl >> 8),
    (uint8_t)(advIvl & 0xFF), (uint8_t)(advIvl >> 8),
    0x03,              // ADV_NONCONN_IND
    0x01,              // own address type = random
    0x00,              // peer address type
    0, 0, 0, 0, 0, 0,  // peer address
    0x01,              // channel map = ch37 のみ（リモコンも2402MHz単波）
    0x00               // filter policy
  };
  hci(0x2006, p, sizeof(p));
}

static void sendSlot(const Slot &s) {
  advEnable(false);
  hci(0x2005, s.addr, 6);              // LE Set Random Address
  uint8_t d[32];
  d[0] = 31;
  memcpy(d + 1, s.data, 31);
  hci(0x2008, d, 32);                  // LE Set Advertising Data
  advParams();
  advEnable(true);
}

static int8_t hv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
static uint8_t parseHex(const char *h, uint8_t *out, uint8_t max) {
  uint8_t n = 0;
  while (h[0] && h[1] && n < max) {
    int8_t a = hv(h[0]), b = hv(h[1]);
    if (a < 0 || b < 0) break;
    out[n++] = (a << 4) | b;
    h += 2;
  }
  return n;
}

static void printSlot(const char *nm, const Slot &s) {
  Serial.printf("%s set=%d addr=", nm, s.set);
  for (int i = 0; i < 6; i++) Serial.printf("%02X", s.addr[i]);
  Serial.print(" data=");
  for (int i = 0; i < 31; i++) Serial.printf("%02X", s.data[i]);
  Serial.println();
}

static void handle(char *s) {
  while (*s == ' ') s++;
  if ((s[0] == 'A' || s[0] == 'B') && s[1] == ' ') {
    Slot &sl = (s[0] == 'A') ? slotA : slotB;
    char *p = s + 2;
    while (*p == ' ') p++;
    if (parseHex(p, sl.addr, 6) != 6) { Serial.println("ERR addr"); return; }
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    uint8_t n = parseHex(p, sl.data, 31);
    if (n < 31) memset(sl.data + n, 0, 31 - n);
    sl.set = true;
    Serial.printf("OK slot %c\n", s[0]);
  } else if (!strncmp(s, "MODE ", 5)) {
    if (!strncmp(s + 5, "ALT", 3)) { mode = 'X'; Serial.println("OK mode ALT"); }
    else if (s[5] == 'A' || s[5] == 'B') { mode = s[5]; Serial.printf("OK mode %c\n", s[5]); }
    else Serial.println("ERR mode");
  } else if (!strncmp(s, "PERIOD ", 7)) {
    period = atoi(s + 7); if (period < 10) period = 10;
    Serial.printf("OK period %u\n", period);
  } else if (!strncmp(s, "GO", 2)) {
    running = true; Serial.println("OK go");
  } else if (!strncmp(s, "HALT", 4)) {
    running = false; advEnable(false); Serial.println("OK halt");
  } else if (!strncmp(s, "IVL ", 4)) {
    advIvl = atoi(s + 4); advParams();
    Serial.printf("OK ivl=%u (%.2fms)\n", advIvl, advIvl * 0.625);
  } else if (!strncmp(s, "PWR ", 4)) {
    int lv = atoi(s + 4);                 // 0..7 -> ESP_PWR_LVL_N12 .. P9
    if (lv < 0) lv = 0; if (lv > 7) lv = 7;
    esp_err_t e = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, (esp_power_level_t)lv);
    Serial.printf("OK pwr lv=%d err=%d now=%d\n", lv, e,
                  (int)esp_ble_tx_power_get(ESP_BLE_PWR_TYPE_ADV));
  } else if (!strncmp(s, "INFO", 4)) {
    Serial.printf("mode=%c period=%u running=%d\n", mode, period, running);
    printSlot("A", slotA); printSlot("B", slotB);
  } else if (*s) Serial.println("ERR unknown");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("BOOT ble_hci");

  if (!btStart()) { Serial.println("FATAL btStart"); while (1) delay(1000); }
  Serial.printf("ctrl_status=%d\n", (int)esp_bt_controller_get_status());
  esp_err_t e = esp_vhci_host_register_callback(&vhciCb);
  Serial.printf("vhci_cb=%d (%s)\n", e, esp_err_to_name(e));
  delay(200);

  hci(0x0C03, NULL, 0);                // HCI Reset
  advEnable(false);
  Serial.println("READY");
}

void loop() {
  static uint32_t hb = 0;
  if (millis() - hb > 3000) { hb = millis(); Serial.printf("ALIVE run=%d mode=%c\n", running, mode); }
  static uint32_t last = 0;
  static bool useA = true;
  if (running) {
    uint32_t now = millis();
    if (mode == 'X') {
      if (now - last >= period) {
        last = now;
        const Slot &s = useA ? slotA : slotB;
        if (s.set) sendSlot(s);
        useA = !useA;
      }
    } else if (now - last >= 500) {     // 単一スロットは定期的に打ち直すだけ
      last = now;
      const Slot &s = (mode == 'A') ? slotA : slotB;
      if (s.set) sendSlot(s);
    }
  }
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { if (lineLen) { line[lineLen] = 0; handle(line); lineLen = 0; } }
    else if (lineLen < sizeof(line) - 1) line[lineLen++] = c;
  }
}
