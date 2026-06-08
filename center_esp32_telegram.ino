#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>

// =========================
// WIFI
// =========================
const char* WIFI_SSID = "QuannT2";
const char* WIFI_PASS = "66668888";

// =========================
// TELEGRAM
// =========================
const char* TG_TOKEN   = "7540750464:AAHmm7jSLbRNe_JuoJifV9CL-mFm2p7lmtg";   // <-- Thay BOT TOKEN cua ban
const char* TG_CHAT_ID = "7849432822";     // <-- Thay CHAT ID cua ban

// Cooldown chong spam: 120 giay moi gui lai
const unsigned long TG_COOLDOWN_MS = 120000UL;

// Luu muc canh bao truoc do de phat hien thay doi
int prevAlarmLevel1 = -1;
int prevAlarmLevel2 = -1;

// Thoi diem gui telegram gan nhat moi tram
unsigned long lastTgSent1 = 0;
unsigned long lastTgSent2 = 0;

// =========================
// LCD I2C
// =========================
LiquidCrystal_I2C lcd(0x27, 16, 2);

// =========================
// WEB SERVER
// =========================
WebServer server(80);

// =========================
// CHAN TRUNG TAM
// =========================
#define CENTER_LED      26
#define CENTER_BUZZER   25

// =========================
// THOI GIAN
// =========================
const unsigned long OFFLINE_TIMEOUT_MS = 10000;
const unsigned long LCD_UPDATE_MS      = 2000;
const unsigned long MUTE_TIME_MS       = 10000;
const unsigned long WIFI_RETRY_MS      = 10000;

// Nhay LED/coi
const unsigned long WARNING_BLINK_MS   = 500;
const unsigned long DANGER_BLINK_MS    = 150;

// =========================
// MUTE
// =========================
bool centerBuzzerMuted = false;
unsigned long centerMuteStartTime = 0;

bool muteStation1Request = false;
bool muteStation2Request = false;

unsigned long lastLCDUpdate = 0;
unsigned long lastWiFiRetry = 0;

// =========================
// CAU TRUC DU LIEU
// =========================
struct StationData {
  int station = 0;
  int mq2 = 0;
  int mq135 = 0;
  float temperature = 0;
  int fire = 0;
  int smokeDetected = 0;
  int toxicDetected = 0;
  int tempHighDetected = 0;
  int alarmLevel = 0;
  unsigned long lastUpdate = 0;
  bool online = false;
};

StationData station1;
StationData station2;

// =========================
// HTML
// =========================
String getHTML() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>He thong canh bao</title>
  <style>
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: Arial, sans-serif;
      background: linear-gradient(135deg, #0f172a, #1e293b);
      color: #e2e8f0;
    }
    .container {
      max-width: 1200px;
      margin: 0 auto;
      padding: 24px;
    }
    .header {
      text-align: center;
      margin-bottom: 24px;
    }
    .header h1 {
      margin: 0;
      font-size: 32px;
      color: #f8fafc;
    }
    .header p {
      color: #cbd5e1;
      margin-top: 8px;
    }
    .header img {
      display: block;
      margin: 0 auto 12px auto;
    }
    .toolbar {
      display: flex;
      justify-content: center;
      gap: 12px;
      margin-bottom: 24px;
      flex-wrap: wrap;
    }
    button {
      border: none;
      border-radius: 12px;
      padding: 12px 18px;
      font-size: 15px;
      font-weight: bold;
      cursor: pointer;
      background: #2563eb;
      color: white;
    }
    button:hover { opacity: 0.9; }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
      gap: 20px;
    }
    .card {
      background: rgba(255,255,255,0.06);
      border: 1px solid rgba(255,255,255,0.08);
      border-radius: 20px;
      padding: 20px;
      box-shadow: 0 10px 30px rgba(0,0,0,0.2);
      backdrop-filter: blur(10px);
    }
    .card h2 {
      margin-top: 0;
      margin-bottom: 16px;
      font-size: 22px;
    }
    .status {
      display: inline-block;
      padding: 6px 12px;
      border-radius: 999px;
      font-weight: bold;
      margin-bottom: 16px;
    }
    .safe { background: #166534; color: #dcfce7; }
    .warn { background: #92400e; color: #fef3c7; }
    .danger { background: #991b1b; color: #fee2e2; }
    .offline { background: #334155; color: #e2e8f0; }
    .item {
      display: flex;
      justify-content: space-between;
      padding: 10px 0;
      border-bottom: 1px solid rgba(255,255,255,0.08);
    }
    .item:last-child { border-bottom: none; }
    .big-alert {
      margin-top: 24px;
      padding: 18px;
      border-radius: 18px;
      text-align: center;
      font-size: 24px;
      font-weight: bold;
    }
    .footer {
      text-align: center;
      margin-top: 24px;
      color: #94a3b8;
      font-size: 14px;
    }
    .pulse {
      animation: pulse 1s infinite;
    }
    @keyframes pulse {
      0% { transform: scale(1); opacity: 1; }
      50% { transform: scale(1.02); opacity: 0.85; }
      100% { transform: scale(1); opacity: 1; }
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <img src="data:image/png;base64,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" alt="UTC Logo" style="width:80px;height:80px;border-radius:50%;box-shadow:0 4px 12px rgba(0,0,0,0.4);">
      <h1>HE THONG CANH BAO HOA HOAN VA KHI DOC</h1>
      <p>Truong Dai hoc Giao thong Van tai &bull; Giam sat 2 tram xu ly theo thoi gian thuc</p>
    </div>

    <div class="toolbar">
      <button onclick="muteCenter()">Tat coi trung tam</button>
      <button onclick="muteStation(1)">Tat coi tram 1</button>
      <button onclick="muteStation(2)">Tat coi tram 2</button>
      <button onclick="location.reload()">Tai lai trang</button>
    </div>

    <div class="grid">
      <div class="card">
        <h2>TRAM XU LY 1</h2>
        <div id="status1" class="status offline">Dang cho du lieu</div>
        <div class="item"><span>Nhiet do</span><strong id="temp1">--</strong></div>
        <div class="item"><span>MQ-2</span><strong id="mq21">--</strong></div>
        <div class="item"><span>MQ-135</span><strong id="mq1351">--</strong></div>
        <div class="item"><span>Lua</span><strong id="fire1">--</strong></div>
        <div class="item"><span>Khoi</span><strong id="smoke1">--</strong></div>
        <div class="item"><span>Khi doc</span><strong id="toxic1">--</strong></div>
        <div class="item"><span>Cap nhat</span><strong id="time1">--</strong></div>
      </div>

      <div class="card">
        <h2>TRAM XU LY 2</h2>
        <div id="status2" class="status offline">Dang cho du lieu</div>
        <div class="item"><span>Nhiet do</span><strong id="temp2">--</strong></div>
        <div class="item"><span>MQ-2</span><strong id="mq22">--</strong></div>
        <div class="item"><span>MQ-135</span><strong id="mq1352">--</strong></div>
        <div class="item"><span>Lua</span><strong id="fire2">--</strong></div>
        <div class="item"><span>Khoi</span><strong id="smoke2">--</strong></div>
        <div class="item"><span>Khi doc</span><strong id="toxic2">--</strong></div>
        <div class="item"><span>Cap nhat</span><strong id="time2">--</strong></div>
      </div>
    </div>

    <div id="globalAlert" class="big-alert offline">HE THONG DANG CHO DU LIEU...</div>

    <div class="footer">ESP32 Center Dashboard &bull; Truong Dai hoc Giao thong Van tai</div>
  </div>

  <script>
    function setStation(prefix, data) {
      document.getElementById("temp" + prefix).textContent = data.temperature + " \u00b0C";
      document.getElementById("mq2" + prefix).textContent = data.mq2;
      document.getElementById("mq135" + prefix).textContent = data.mq135;
      document.getElementById("fire" + prefix).textContent = data.fire ? "CO" : "KHONG";
      document.getElementById("smoke" + prefix).textContent = data.smokeDetected ? "VUOT NGUONG" : "BINH THUONG";
      document.getElementById("toxic" + prefix).textContent = data.toxicDetected ? "VUOT NGUONG" : "BINH THUONG";
      document.getElementById("time" + prefix).textContent = data.lastSeen;

      const statusEl = document.getElementById("status" + prefix);
      statusEl.className = "status";

      if (!data.online) {
        statusEl.classList.add("offline");
        statusEl.textContent = "MAT KET NOI";
      } else if (data.alarmLevel == 0) {
        statusEl.classList.add("safe");
        statusEl.textContent = "AN TOAN";
      } else if (data.alarmLevel == 1) {
        statusEl.classList.add("warn");
        statusEl.textContent = "CANH BAO";
      } else {
        statusEl.classList.add("danger");
        statusEl.textContent = "NGUY HIEM";
      }
    }

    function setGlobalAlert(text, cls, pulse=false) {
      const el = document.getElementById("globalAlert");
      el.textContent = text;
      el.className = "big-alert " + cls + (pulse ? " pulse" : "");
    }

    async function loadData() {
      try {
        const res = await fetch("/api/status");
        const data = await res.json();

        setStation("1", data.station1);
        setStation("2", data.station2);

        const s1 = data.station1;
        const s2 = data.station2;

        if ((s1.online && s1.alarmLevel == 2) || (s2.online && s2.alarmLevel == 2)) {
          setGlobalAlert("NGUY HIEM! CO TRAM DANG BAO DONG MUC CAO", "danger", true);
        } else if ((s1.online && s1.alarmLevel == 1) || (s2.online && s2.alarmLevel == 1)) {
          setGlobalAlert("HE THONG DANG O MUC CANH BAO", "warn");
        } else if (s1.online || s2.online) {
          setGlobalAlert("HE THONG DANG HOAT DONG BINH THUONG", "safe");
        } else {
          setGlobalAlert("CHUA NHAN DUOC DU LIEU TU CAC TRAM", "offline");
        }
      } catch (e) {
        console.log(e);
        setGlobalAlert("LOI KET NOI DEN TRUNG TAM", "danger");
      }
    }

    async function muteCenter() {
      await fetch("/api/muteCenter", { method: "POST" });
    }

    async function muteStation(id) {
      await fetch("/api/muteStation?station=" + id, { method: "POST" });
    }

    loadData();
    setInterval(loadData, 2000);
  </script>
</body>
</html>
)rawliteral";

  return html;
}

// =========================
// WIFI
// =========================
void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Dang ket noi WiFi");

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 30) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi OK");
    Serial.print("IP Trung tam: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Khong ket noi duoc WiFi");
  }
}

void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastWiFiRetry < WIFI_RETRY_MS) return;

  lastWiFiRetry = millis();
  Serial.println("WiFi mat ket noi, dang thu ket noi lai...");
  WiFi.disconnect(true);
  delay(500);
  connectWiFi();
}

// =========================
String formatLastSeen(unsigned long msAgo) {
  if (msAgo < 1000) return "vua xong";
  if (msAgo < 60000) return String(msAgo / 1000) + " giay truoc";
  return String(msAgo / 60000) + " phut truoc";
}

void updateOnlineState() {
  unsigned long now = millis();

  if (station1.lastUpdate > 0 && (now - station1.lastUpdate > OFFLINE_TIMEOUT_MS)) {
    station1.online = false;
  }
  if (station2.lastUpdate > 0 && (now - station2.lastUpdate > OFFLINE_TIMEOUT_MS)) {
    station2.online = false;
  }
}

void updateCenterMuteState() {
  if (centerBuzzerMuted && (millis() - centerMuteStartTime >= MUTE_TIME_MS)) {
    centerBuzzerMuted = false;
  }
}

// =========================
// GUI TELEGRAM
// =========================
void sendTelegram(String message) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClientSecure client;
  client.setInsecure(); // Khong kiem tra SSL cert

  HTTPClient http;
  String url = "https://api.telegram.org/bot";
  url += TG_TOKEN;
  url += "/sendMessage";

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  // Escape newline cho JSON
  message.replace("\n", "\\n");

  StaticJsonDocument<512> doc;
  doc["chat_id"] = TG_CHAT_ID;
  doc["text"] = message;
  doc["parse_mode"] = "HTML";

  String body;
  serializeJson(doc, body);

  int code = http.POST(body);
  Serial.print("[Telegram] HTTP: ");
  Serial.println(code);
  http.end();
}

void checkAndSendTelegramAlert(StationData& st, int& prevLevel, unsigned long& lastSent, int stationId) {
  if (!st.online) return;

  int curLevel = st.alarmLevel;
  unsigned long now = millis();

  // Khong gui neu chua co thay doi va chua het cooldown
  bool levelChanged = (curLevel != prevLevel && prevLevel != -1);
  bool cooldownOk   = (now - lastSent >= TG_COOLDOWN_MS);

  if (!levelChanged && !cooldownOk) {
    prevLevel = curLevel;
    return;
  }

  // Chi gui khi muc tang len (canh bao/nguy hiem) hoac ve an toan
  if (!levelChanged && curLevel == 0) { prevLevel = curLevel; return; }
  if (!levelChanged) return;

  prevLevel = curLevel;
  lastSent = now;

  String msg = "";
  String tramName = "Tram xu ly " + String(stationId);

  if (curLevel == 2) {
    msg += "<b>🚨 NGUY HIEM - " + tramName + "</b>\n";
    msg += "━━━━━━━━━━━━━━━━━━━━\n";
    if (st.fire)          msg += "🔥 Phat hien LUA\n";
    if (st.smokeDetected) msg += "💨 Khoi VUOT NGUONG (MQ-2: " + String(st.mq2) + ")\n";
    if (st.toxicDetected) msg += "☣️  Khi doc VUOT NGUONG (MQ-135: " + String(st.mq135) + ")\n";
    if (st.tempHighDetected) msg += "🌡️ Nhiet do cao: " + String(st.temperature, 1) + " C\n";
    msg += "━━━━━━━━━━━━━━━━━━━━\n";
    msg += "<b>Kiem tra va xu ly ngay lap tuc!</b>";
  } else if (curLevel == 1) {
    msg += "<b>⚠️ CANH BAO - " + tramName + "</b>\n";
    msg += "━━━━━━━━━━━━━━━━━━━━\n";
    if (st.smokeDetected) msg += "💨 Khoi bat thuong (MQ-2: " + String(st.mq2) + ")\n";
    if (st.toxicDetected) msg += "☣️  Khi doc bat thuong (MQ-135: " + String(st.mq135) + ")\n";
    if (st.tempHighDetected) msg += "🌡️ Nhiet do tang: " + String(st.temperature, 1) + " C\n";
    msg += "━━━━━━━━━━━━━━━━━━━━\n";
    msg += "Theo doi va kiem tra tinh trang.";
  } else {
    msg += "<b>✅ AN TOAN - " + tramName + "</b>\n";
    msg += "He thong tro ve trang thai binh thuong.";
  }

  sendTelegram(msg);
}

// =========================
// NHAN DU LIEU TU TRAM
// =========================
void handleUpdate() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"No body\"}");
    return;
  }

  String body = server.arg("plain");
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, body);

  if (err) {
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"JSON error\"}");
    return;
  }

  int st = doc["station"] | 0;
  StationData* target = nullptr;

  if (st == 1) target = &station1;
  else if (st == 2) target = &station2;
  else {
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"Invalid station\"}");
    return;
  }

  target->station = st;
  target->mq2 = doc["mq2"] | 0;
  target->mq135 = doc["mq135"] | 0;
  target->temperature = doc["temperature"] | 0.0;
  target->fire = doc["fire"] | 0;
  target->smokeDetected = doc["smokeDetected"] | 0;
  target->toxicDetected = doc["toxicDetected"] | 0;
  target->tempHighDetected = doc["tempHighDetected"] | 0;
  target->alarmLevel = doc["alarmLevel"] | 0;
  target->lastUpdate = millis();
  target->online = true;

  // Kiem tra va gui canh bao Telegram
  if (st == 1) checkAndSendTelegramAlert(station1, prevAlarmLevel1, lastTgSent1, 1);
  if (st == 2) checkAndSendTelegramAlert(station2, prevAlarmLevel2, lastTgSent2, 2);

  bool muteForThisStation = false;
  if (st == 1 && muteStation1Request) {
    muteForThisStation = true;
    muteStation1Request = false;
  }
  if (st == 2 && muteStation2Request) {
    muteForThisStation = true;
    muteStation2Request = false;
  }

  StaticJsonDocument<64> res;
  res["ok"] = true;
  res["mute"] = muteForThisStation ? 1 : 0;

  String response;
  serializeJson(res, response);
  server.send(200, "application/json", response);
}

// =========================
// TRA VE TRANG THAI WEB
// =========================
void handleStatus() {
  updateOnlineState();

  unsigned long now = millis();
  StaticJsonDocument<512> doc;

  JsonObject s1 = doc.createNestedObject("station1");
  s1["mq2"] = station1.mq2;
  s1["mq135"] = station1.mq135;
  s1["temperature"] = station1.temperature;
  s1["fire"] = station1.fire;
  s1["smokeDetected"] = station1.smokeDetected;
  s1["toxicDetected"] = station1.toxicDetected;
  s1["alarmLevel"] = station1.alarmLevel;
  s1["online"] = station1.online;
  s1["lastSeen"] = station1.lastUpdate > 0 ? formatLastSeen(now - station1.lastUpdate) : "chua co";

  JsonObject s2 = doc.createNestedObject("station2");
  s2["mq2"] = station2.mq2;
  s2["mq135"] = station2.mq135;
  s2["temperature"] = station2.temperature;
  s2["fire"] = station2.fire;
  s2["smokeDetected"] = station2.smokeDetected;
  s2["toxicDetected"] = station2.toxicDetected;
  s2["alarmLevel"] = station2.alarmLevel;
  s2["online"] = station2.online;
  s2["lastSeen"] = station2.lastUpdate > 0 ? formatLastSeen(now - station2.lastUpdate) : "chua co";

  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// =========================
// API MUTE TRUNG TAM
// =========================
void handleMuteCenter() {
  centerBuzzerMuted = true;
  centerMuteStartTime = millis();
  server.send(200, "application/json", "{\"ok\":true}");
}

// =========================
// API MUTE TRAM
// =========================
void handleMuteStation() {
  if (!server.hasArg("station")) {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }

  int st = server.arg("station").toInt();

  if (st == 1) muteStation1Request = true;
  else if (st == 2) muteStation2Request = true;
  else {
    server.send(400, "application/json", "{\"ok\":false}");
    return;
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

void handleRoot() {
  server.send(200, "text/html", getHTML());
}

// =========================
// LCD
// =========================
void updateLCD() {
  updateOnlineState();

  lcd.clear();

  if (station1.online) {
    lcd.setCursor(0, 0);
    lcd.print("T1:");
    lcd.print(station1.temperature, 1);
    lcd.print("C ");
    lcd.print(station1.alarmLevel);
  } else {
    lcd.setCursor(0, 0);
    lcd.print("T1: OFFLINE");
  }

  if (station2.online) {
    lcd.setCursor(0, 1);
    lcd.print("T2:");
    lcd.print(station2.temperature, 1);
    lcd.print("C ");
    lcd.print(station2.alarmLevel);
  } else {
    lcd.setCursor(0, 1);
    lcd.print("T2: OFFLINE");
  }
}

// =========================
// CANH BAO TRUNG TAM
// =========================
void updateCenterAlarm() {
  updateOnlineState();
  updateCenterMuteState();

  bool danger = (station1.online && station1.alarmLevel == 2) ||
                (station2.online && station2.alarmLevel == 2);

  bool warning = (station1.online && station1.alarmLevel == 1) ||
                 (station2.online && station2.alarmLevel == 1);

  static bool warningBlinkState = false;
  static bool dangerBlinkState = false;
  static unsigned long lastWarningBlink = 0;
  static unsigned long lastDangerBlink = 0;

  unsigned long now = millis();

  if (!danger && !warning) {
    digitalWrite(CENTER_LED, LOW);
    digitalWrite(CENTER_BUZZER, LOW);
    centerBuzzerMuted = false;
    return;
  }

  if (warning && !danger) {
    if (now - lastWarningBlink >= WARNING_BLINK_MS) {
      lastWarningBlink = now;
      warningBlinkState = !warningBlinkState;
    }

    digitalWrite(CENTER_LED, warningBlinkState);
    if (centerBuzzerMuted) {
      digitalWrite(CENTER_BUZZER, LOW);
    } else {
      digitalWrite(CENTER_BUZZER, warningBlinkState);
    }
    return;
  }

  if (danger) {
    if (now - lastDangerBlink >= DANGER_BLINK_MS) {
      lastDangerBlink = now;
      dangerBlinkState = !dangerBlinkState;
    }

    digitalWrite(CENTER_LED, dangerBlinkState);
    if (centerBuzzerMuted) {
      digitalWrite(CENTER_BUZZER, LOW);
    } else {
      digitalWrite(CENTER_BUZZER, HIGH);
    }
  }
}

// =========================
// SETUP
// =========================
void setup() {
  Serial.begin(115200);

  pinMode(CENTER_LED, OUTPUT);
  pinMode(CENTER_BUZZER, OUTPUT);

  digitalWrite(CENTER_LED, LOW);
  digitalWrite(CENTER_BUZZER, LOW);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Dang ket noi...");

  connectWiFi();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("IP Trung tam:");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/update", HTTP_POST, handleUpdate);
  server.on("/api/muteCenter", HTTP_POST, handleMuteCenter);
  server.on("/api/muteStation", HTTP_POST, handleMuteStation);

  server.begin();
  Serial.println("Web server da khoi dong");

  // Thong bao khoi dong qua Telegram
  delay(1000);
  sendTelegram("<b>🟢 He thong da khoi dong</b>\nTrung tam dang hoat dong, san sang nhan du lieu tu cac tram.");
}

// =========================
// LOOP
// =========================
void loop() {
  ensureWiFi();
  server.handleClient();
  updateCenterAlarm();

  if (millis() - lastLCDUpdate >= LCD_UPDATE_MS) {
    lastLCDUpdate = millis();
    updateLCD();
  }
}
