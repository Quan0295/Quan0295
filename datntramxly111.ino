// ============================================================
// TRAM XU LY - HE THONG CANH BAO HOA HOAN & KHI DOC
// Version: 3.0 - Canh bao nhanh, huy nhanh, khong nut
// ============================================================
#include <WiFi.h>
#include <HTTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>

// ============================================================
// CAU HINH
// ============================================================
#define STATION_ID       2          // 1 hoac 2

const char* WIFI_SSID  = "QuannT2";
const char* WIFI_PASS  = "66668888";
const char* CENTER_URL = "http://192.168.1.175/api/update";

// -- PIN --
#define PIN_MQ2          34
#define PIN_MQ135        35
#define PIN_FLAME        27
#define PIN_DS18B20       4
#define PIN_LED          26
#define PIN_BUZZER       25

// -- NGUONG CAM BIEN --
// Cach chinh: De cam bien 5 phut trong khong khi sach,
// doc gia tri MQ2_raw va MQ135_raw tren Serial Monitor,
// dat THRESHOLD = gia_tri_do_duoc x 1.8
const int   MQ2_THRESHOLD   = 1800;
const int   MQ135_THRESHOLD = 600;
const float TEMP_THRESHOLD  = 50.0;
#define     FLAME_ACTIVE    LOW     // Cam bien lua: LOW = co lua

// -- THOI GIAN --
const unsigned long READ_INTERVAL_MS = 2000;  // Doc cam bien moi 2 giay
const unsigned long MUTE_TIME_MS     = 10000; // Mute tu lenh web
const unsigned long WIFI_RETRY_MS    = 10000;
const unsigned long HTTP_TIMEOUT_MS  = 3000;

// PREHEAT: Chi ap dung cho MQ (can thoi gian on dinh)
// KHONG ap dung cho cam bien lua va nhiet do
const unsigned long PREHEAT_MS = 120000; // 2 phut

// -- NHAY LED/COI --
const unsigned long WARN_BLINK_MS   = 500;  // Muc 1: nhay 0.5s
const unsigned long DANGER_BLINK_MS = 150;  // Muc 2: nhay 0.15s

// ============================================================
// THAM SO PHAT HIEN & HUY CANH BAO
//
// LUA    : Bat NGAY (>= 1 lan doc)  | Tat sau 2 lan = 4 giay
// KHOI   : Bat NGAY (>= 1 lan doc)  | Tat sau 2 lan = 4 giay
// KHI DOC: Bat NGAY (>= 1 lan doc)  | Tat sau 2 lan = 4 giay
// NHIET DO: Bat NGAY                | Tat sau 2 lan = 4 giay
//
// CANCEL_COUNT = 2: Phai doc DUOI nguong 2 lan lien tiep (4 giay)
// moi tat canh bao. Tranh tat nham khi nong do dao dong quanh nguong
// ============================================================
const int CANCEL_COUNT = 2;   // 2 × 2s = 4 giay la tat

// Loc nhieu: trung binh 3 mau ADC (chi dung cho hien thi, KHONG dung cho trigger)
const int MA_SAMPLES = 3;

// Den giu them sau khi tat canh bao (thong bao truc quan)
const unsigned long LED_HOLD_MS = 3000; // 3 giay

// ============================================================
// BIEN NOI BO
// ============================================================
OneWire oneWire(PIN_DS18B20);
DallasTemperature ds18b20(&oneWire);

int   mq2Value     = 0;
int   mq135Value   = 0;
int   flameValue   = HIGH;
float temperatureC = 0.0;

bool smokeDetected    = false;
bool toxicDetected    = false;
bool tempHighDetected = false;
bool fireDetected     = false;
int  alarmLevel       = 0;

// Moving average buffer (loc nhieu ADC, CHI dung cho hien thi)
int mq2Buf[MA_SAMPLES]   = {};
int mq135Buf[MA_SAMPLES] = {};
int maBufIdx = 0;

// Counter cho tung loai canh bao
// Bat: tang len CANCEL_COUNT ngay lap tuc
// Tat: giam 1 moi lan doc → can CANCEL_COUNT lan moi ve 0
int smokeTrigger = 0;
int toxicTrigger = 0;
int tempTrigger  = 0;
int fireTrigger  = 0;

// Den hoat dong doc lap voi coi
int  ledLevel      = 0;
unsigned long ledHoldStart = 0;

bool buzzerMuted = false;
unsigned long muteStartTime = 0;

unsigned long lastReadTime  = 0;
unsigned long lastWiFiRetry = 0;

// ============================================================
// DOC ADC - TRUNG BINH 10 MAU (khong block loop)
// ============================================================
int readADC(int pin) {
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(pin);
    delayMicroseconds(200);
  }
  return (int)(sum / 10);
}

// ============================================================
// WIFI
// ============================================================
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[WiFi] Dang ket noi");
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] OK - IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[WiFi] Khong ket noi duoc, thu lai sau...");
  }
}

void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastWiFiRetry < WIFI_RETRY_MS) return;
  lastWiFiRetry = millis();
  WiFi.disconnect(true);
  delay(300);
  connectWiFi();
}

// ============================================================
// DOC CAM BIEN
// ============================================================
void readSensors() {
  int rawMQ2   = readADC(PIN_MQ2);
  int rawMQ135 = readADC(PIN_MQ135);
  flameValue   = digitalRead(PIN_FLAME);

  // Cap nhat buffer trung binh dong (chi dung hien thi)
  mq2Buf[maBufIdx % MA_SAMPLES]   = rawMQ2;
  mq135Buf[maBufIdx % MA_SAMPLES] = rawMQ135;
  maBufIdx++;

  long s2 = 0, s135 = 0;
  for (int i = 0; i < MA_SAMPLES; i++) { s2 += mq2Buf[i]; s135 += mq135Buf[i]; }
  mq2Value   = (int)(s2   / MA_SAMPLES);
  mq135Value = (int)(s135 / MA_SAMPLES);

  ds18b20.requestTemperatures();
  temperatureC = ds18b20.getTempCByIndex(0);
  if (temperatureC == DEVICE_DISCONNECTED_C) temperatureC = -127.0;
}

// ============================================================
// DANH GIA CANH BAO
//
// DIEM KHAC BIET so v2:
//
// [FIX 1] LUA: fireDetected = (fireTrigger > 0) thay vi >= 2
//         → Bat NGAY khi doc duoc 1 lan (khong phai doi 4 giay nua)
//
// [FIX 2] LUA: Tang 2 buoc moi lan phat hien, giam 1 buoc moi lan khong co
//         → Tat sau 2 lan doc = 4 giay (nhanh, on dinh)
//
// [FIX 3] KHOI/KHI: Dung gia tri RAW (khong qua avg) de kich hoat
//         → Phat hien trong cung lan doc dau tien vuot nguong (2 giay)
//
// [FIX 4] NHIET DO: Khong bi anh huong boi PREHEAT
//         → Bao ngay khi nhiet do cao bat ke thoi gian khoi dong
// ============================================================
void evaluateAlarm() {
  bool inPreheat = (millis() < PREHEAT_MS);

  // Lay gia tri RAW moi nhat - KHONG qua moving average
  // → Phat hien nhanh nhat co the, khong co do tre
  int latestIdx = (maBufIdx - 1 + MA_SAMPLES) % MA_SAMPLES;
  int rawMQ2    = (maBufIdx > 0) ? mq2Buf[latestIdx]   : 0;
  int rawMQ135  = (maBufIdx > 0) ? mq135Buf[latestIdx] : 0;

  // Dieu kien kich hoat
  // Preheat chi ap dung cho MQ-2 va MQ-135 (can thoi gian on dinh)
  // Lua va nhiet do: BAO NGAY, khong co preheat
  bool rawSmoke = (!inPreheat) && (rawMQ2   > MQ2_THRESHOLD);
  bool rawToxic = (!inPreheat) && (rawMQ135 > MQ135_THRESHOLD);
  bool rawTemp  = (temperatureC > -50.0) && (temperatureC > TEMP_THRESHOLD);
  bool rawFire  = (flameValue == FLAME_ACTIVE);

  // ── LUA ──────────────────────────────────────────────────
  // [FIX 1+2] Bat NGAY khi co lua, tat sau 2 lan khong co lua
  // Tang 2 buoc: dam bao > 0 ngay lap tuc → fireDetected = true ngay
  // Giam 1 buoc: can 2 lan doc duoi nguong → tat sau 4 giay
  if (rawFire) {
    fireTrigger = min(fireTrigger + 2, 4);
  } else {
    fireTrigger = max(fireTrigger - 1, 0);
  }
  fireDetected = (fireTrigger > 0); // [FIX] > 0 thay vi >= 2

  // ── KHOI (MQ-2) ──────────────────────────────────────────
  // Bat NGAY: smokeTrigger = CANCEL_COUNT ngay lap tuc → smokeDetected = true
  // Tat sau CANCEL_COUNT lan doc duoi nguong = 4 giay
  if (rawSmoke) {
    smokeTrigger = CANCEL_COUNT;
  } else {
    smokeTrigger = max(smokeTrigger - 1, 0);
  }
  smokeDetected = (smokeTrigger > 0);

  // ── KHI DOC (MQ-135) ─────────────────────────────────────
  if (rawToxic) {
    toxicTrigger = CANCEL_COUNT;
  } else {
    toxicTrigger = max(toxicTrigger - 1, 0);
  }
  toxicDetected = (toxicTrigger > 0);

  // ── NHIET DO ─────────────────────────────────────────────
  // [FIX 4] Khong co preheat cho nhiet do
  if (rawTemp) {
    tempTrigger = CANCEL_COUNT;
  } else {
    tempTrigger = max(tempTrigger - 1, 0);
  }
  tempHighDetected = (tempTrigger > 0);

  // ── TINH MUC CANH BAO ────────────────────────────────────
  int prevLevel = alarmLevel;
  int count = (smokeDetected   ? 1 : 0)
            + (toxicDetected   ? 1 : 0)
            + (tempHighDetected? 1 : 0)
            + (fireDetected    ? 1 : 0);

  // Lua hoac 2+ nguon nguy hiem → muc 2 (nguy hiem)
  // 1 nguon → muc 1 (canh bao)
  if      (fireDetected || count >= 2) alarmLevel = 2;
  else if (count == 1)                 alarmLevel = 1;
  else                                 alarmLevel = 0;

  // ── QUAN LY DEN (doc lap voi coi) ────────────────────────
  if (alarmLevel > 0) {
    ledLevel     = alarmLevel;
    ledHoldStart = 0;
  } else if (prevLevel > 0 && alarmLevel == 0) {
    // Vua tat canh bao: coi tat ngay, den giu them 3 giay
    ledHoldStart = millis();
    Serial.println("[ALARM] Het canh bao → COI TAT NGAY | Den con 3s");
  }

  if (ledHoldStart > 0 && millis() - ledHoldStart >= LED_HOLD_MS) {
    ledLevel     = 0;
    ledHoldStart = 0;
    Serial.println("[ALARM] Den tat - He thong binh thuong");
  }

  // Log preheat (neu dang trong thoi gian on dinh)
  if (inPreheat) {
    Serial.printf("[PREHEAT] Con lai %lus | MQ2=%d (nguong %d) | MQ135=%d (nguong %d)\n",
      (PREHEAT_MS - millis()) / 1000,
      rawMQ2, MQ2_THRESHOLD, rawMQ135, MQ135_THRESHOLD);
  }
}

// ============================================================
// DIEU KHIEN LED VA COI
// Chay moi vong loop → nhay dung tan so (500ms/150ms)
//
// COI: Theo alarmLevel → tat NGAY khi alarmLevel = 0
// DEN: Theo ledLevel   → giu them 3 giay sau khi tat canh bao
// ============================================================
void controlAlarm() {
  static bool warnBlink = false, dangerBlink = false;
  static unsigned long lastWarn = 0, lastDanger = 0;
  unsigned long now = millis();

  // Tinh toan trang thai nhay (dung chung cho ca den va coi)
  if (now - lastWarn   >= WARN_BLINK_MS)   { lastWarn   = now; warnBlink   = !warnBlink;   }
  if (now - lastDanger >= DANGER_BLINK_MS) { lastDanger = now; dangerBlink = !dangerBlink; }

  // -- DEN (ledLevel) --
  if      (ledLevel == 0) digitalWrite(PIN_LED, LOW);
  else if (ledLevel == 1) digitalWrite(PIN_LED, warnBlink);
  else                    digitalWrite(PIN_LED, dangerBlink);

  // -- COI (alarmLevel) - tat NGAY khi het canh bao --
  if (alarmLevel == 0 || buzzerMuted) {
    digitalWrite(PIN_BUZZER, LOW);
  } else if (alarmLevel == 1) {
    digitalWrite(PIN_BUZZER, warnBlink);
  } else {
    digitalWrite(PIN_BUZZER, HIGH); // Muc 2: keu lien tuc
  }
}

// ============================================================
// MUTE COI (tu lenh web / trung tam)
// ============================================================
void updateMuteState() {
  if (alarmLevel == 0) { buzzerMuted = false; return; }
  if (buzzerMuted && millis() - muteStartTime >= MUTE_TIME_MS) {
    buzzerMuted = false;
    Serial.println("[MUTE] Het han - bat lai coi");
  }
}

// ============================================================
// GUI DU LIEU VE TRUNG TAM
// ============================================================
void sendData() {
  if (WiFi.status() != WL_CONNECTED) return;

  StaticJsonDocument<256> doc;
  doc["station"]          = STATION_ID;
  doc["mq2"]              = mq2Value;
  doc["mq135"]            = mq135Value;
  doc["temperature"]      = (temperatureC > -50.0) ? temperatureC : 0.0;
  doc["fire"]             = fireDetected     ? 1 : 0;
  doc["smokeDetected"]    = smokeDetected    ? 1 : 0;
  doc["toxicDetected"]    = toxicDetected    ? 1 : 0;
  doc["tempHighDetected"] = tempHighDetected ? 1 : 0;
  doc["alarmLevel"]       = alarmLevel;

  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  http.begin(CENTER_URL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(HTTP_TIMEOUT_MS);

  int code = http.POST(payload);
  if (code > 0) {
    StaticJsonDocument<128> res;
    if (!deserializeJson(res, http.getString())) {
      if ((res["mute"] | 0) == 1) {
        buzzerMuted   = true;
        muteStartTime = millis();
        Serial.println("[HTTP] Nhan lenh TAT COI tu trung tam");
      }
    }
  } else {
    Serial.printf("[HTTP] Loi: %d\n", code);
  }
  http.end();
}

// ============================================================
// HIEN THI SERIAL - Ro rang de debug va chon nguong
// ============================================================
void printStatus() {
  bool inPreheat = (millis() < PREHEAT_MS);
  int  latestIdx = (maBufIdx - 1 + MA_SAMPLES) % MA_SAMPLES;
  int  rawMQ2    = (maBufIdx > 0) ? mq2Buf[latestIdx]   : 0;
  int  rawMQ135  = (maBufIdx > 0) ? mq135Buf[latestIdx] : 0;
  unsigned long sec = millis() / 1000;

  Serial.printf("\n===== TRAM %d | %02lu:%02lu | %s =====\n",
    STATION_ID, sec/60, sec%60,
    inPreheat ? "PREHEAT (MQ chua on dinh)" : "HOAT DONG");

  // MQ2 - hien thi ca raw, avg, nguong va trigger de de chinh
  Serial.printf("MQ-2  : raw=%-4d avg=%-4d nguong=%-4d trigger=%d → %s\n",
    rawMQ2, mq2Value, MQ2_THRESHOLD, smokeTrigger,
    inPreheat ? "PREHEAT" : (rawMQ2 > MQ2_THRESHOLD ? "!!! VUOT NGUONG !!!" : "binh thuong"));

  Serial.printf("MQ-135: raw=%-4d avg=%-4d nguong=%-4d trigger=%d → %s\n",
    rawMQ135, mq135Value, MQ135_THRESHOLD, toxicTrigger,
    inPreheat ? "PREHEAT" : (rawMQ135 > MQ135_THRESHOLD ? "!!! VUOT NGUONG !!!" : "binh thuong"));

  Serial.printf("Nhiet : %.1f C  nguong=%.0f C  trigger=%d → %s\n",
    temperatureC, TEMP_THRESHOLD, tempTrigger,
    tempHighDetected ? "!!! CAO !!!" : "binh thuong");

  Serial.printf("Lua   : %s (pin=%s trigger=%d)\n",
    fireDetected ? "!!! CO LUA !!!" : "khong",
    flameValue == LOW ? "LOW=co lua" : "HIGH=khong co",
    fireTrigger);

  const char* lvStr = alarmLevel == 0 ? "BINH THUONG" : alarmLevel == 1 ? "CANH BAO" : "NGUY HIEM";
  Serial.printf("Alarm : Lv%d [%s] | Coi: %s | Den: Lv%d\n",
    alarmLevel, lvStr,
    alarmLevel == 0 ? "tat" : buzzerMuted ? "MUTE" : "KEU",
    ledLevel);
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n[BOOT] Khoi dong he thong...");

  pinMode(PIN_FLAME,  INPUT);
  pinMode(PIN_LED,    OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_LED,    LOW);
  digitalWrite(PIN_BUZZER, LOW);

  analogReadResolution(12); // 12-bit ADC: 0 → 4095
  ds18b20.begin();
  connectWiFi();

  Serial.println("[BOOT] =============================================");
  Serial.printf("[BOOT] Tram ID    : %d\n", STATION_ID);
  Serial.printf("[BOOT] Nguong MQ2 : %d\n", MQ2_THRESHOLD);
  Serial.printf("[BOOT] Nguong MQ135: %d\n", MQ135_THRESHOLD);
  Serial.printf("[BOOT] Nguong Nhiet: %.0f C\n", TEMP_THRESHOLD);
  Serial.printf("[BOOT] CANCEL_COUNT: %d (tat sau %ds)\n", CANCEL_COUNT, CANCEL_COUNT*2);
  Serial.printf("[BOOT] Preheat MQ  : %lu giay\n", PREHEAT_MS/1000);
  Serial.println("[BOOT] Lua/NhietDo : BAO NGAY (khong preheat)");
  Serial.println("[BOOT] =============================================");
  Serial.println("[BOOT] San sang!\n");
}

// ============================================================
// LOOP
// controlAlarm() NGOAI interval: nhay dung 500ms/150ms
// readSensors + evaluateAlarm: chay moi 2 giay
// ============================================================
void loop() {
  ensureWiFi();
  updateMuteState();
  controlAlarm();   // Chay moi vong lap → den/coi nhay chinh xac

  if (millis() - lastReadTime >= READ_INTERVAL_MS) {
    lastReadTime = millis();
    readSensors();
    evaluateAlarm();
    printStatus();
    sendData();
  }
}
