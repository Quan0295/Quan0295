#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "DHT.h"
#include <TinyGPS++.h>
#include <SoftwareSerial.h>
#include <HTTPClient.h>

// ----- Cấu hình WiFi -----
const char* ssid = "Lan Anh_plus";
const char* password = "12345678";

// ----- Cấu hình Telegram -----
const char* botToken = "7540750464:AAHmm7jSLbRNe_JuoJifV9CL-mFm2p7lmtg";  // Thay bằng token bot Telegram 
const char* chatID = "7849432822";      // Thay bằng chat ID Telegram 

// ----- Cấu hình cảm biến DHT11 -----
#define DHTPIN 18
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ----- Cấu hình màn hình OLED -----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ----- Cấu hình GPS -----
TinyGPSPlus gps;
SoftwareSerial neo6M(16, 17); // TX GPS -> D16, RX GPS -> D17
const uint32_t GPSBaud = 9600;

// ----- Biến toàn cục -----
float temperature, humidity;
String Latitude = "N/A", Longitude = "N/A", gpsDate = "N/A";
WebServer server(80);

// ----- Hàm gửi tin nhắn Telegram -----
void sendTelegramMessage(String message) {
  HTTPClient http;
  String url = "https://api.telegram.org/bot" + String(botToken) +
               "/sendMessage?chat_id=" + String(chatID) +
               "&text=" + message;

  http.begin(url);
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    Serial.println(" Gửi cảnh báo thành công!");
  } else {
    Serial.print(" Lỗi khi gửi: ");
    Serial.println(httpResponseCode);
  }
  
  http.end();
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected.");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.begin();
  
  neo6M.begin(GPSBaud);
  dht.begin();
  Wire.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(10, 10);
  display.print("Welcome!");
  display.display();
  delay(2000);
}

void loop() {
  server.handleClient();
  smartDelay(1000);

  humidity = dht.readHumidity();
  temperature = dht.readTemperature();

  if (gps.location.isValid()) {
    Latitude = String(gps.location.lat(), 6);
    Longitude = String(gps.location.lng(), 6);
  } else {
    Latitude = "N/A";
    Longitude = "N/A";
  }

  if (gps.date.isValid()) {
    char sz[16];
    sprintf(sz, "%02d/%02d/%04d", gps.date.day(), gps.date.month(), gps.date.year());
    gpsDate = String(sz);
  } else {
    gpsDate = "N/A";
  }

  // Hiển thị dữ liệu lên OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Temp: "); display.print(temperature); display.print(" C");

  display.setCursor(0, 10);
  display.print("Humi: "); display.print(humidity); display.print(" %");

  display.setCursor(0, 20);
  display.print("Lat: "); display.print(Latitude);

  display.setCursor(0, 30);
  display.print("Long: "); display.print(Longitude);

  display.setCursor(0, 40);
  display.print("Date: "); display.print(gpsDate);

  display.display();
  delay(3000);

  // Kiểm tra và gửi cảnh báo qua Telegram
  if (temperature > 30.0) {
    sendTelegramMessage("Cảnh báo! Nhiệt độ : " + String(temperature) + " C");
  }

  if (humidity > 80.0) {
    sendTelegramMessage("Cảnh báo! Độ ẩm : " + String(humidity) + " %");
  }
}

// ----- Hiển thị WebServer -----
void handleRoot() {
  String html = "<html><head><meta http-equiv='refresh' content='5'>";
  html += "<title>ESP32 Web Server</title>";
  html += "<style>";
  html += "body { font-family: Arial; text-align: center; background-color: #f4f4f4;}";
  html += ".data-container { display: flex; justify-content: center; align-items: center; flex-direction: column;}";
  html += ".data-box { background: white; padding: 15px; margin: 10px; border-radius: 10px; box-shadow: 2px 2px 10px rgba(0,0,0,0.1); width: 300px;}";
  html += ".icon { font-size: 40px; color: #007BFF; }";
  html += "</style>";
  html += "<link rel='stylesheet' href='https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.2/css/all.min.css'>";
  html += "</head><body>";
  html += "<h1>ESP32 Sensor Data</h1>";
  html += "<div class='data-container'>";

  html += "<div class='data-box'><i class='fas fa-thermometer-half icon'></i>";
  html += "<h2>Temperature: " + String(temperature) + " &deg;C</h2></div>";

  html += "<div class='data-box'><i class='fas fa-tint icon'></i>";
  html += "<h2>Humidity: " + String(humidity) + " %</h2></div>";

  html += "<div class='data-box'><i class='fas fa-map-marker-alt icon'></i>";
  html += "<h2>Latitude: " + Latitude + "</h2><h2>Longitude: " + Longitude + "</h2></div>";

  html += "<div class='data-box'><i class='fas fa-calendar-alt icon'></i>";
  html += "<h2>Date: " + gpsDate + "</h2></div>";

  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

// ----- Đọc dữ liệu GPS -----
void smartDelay(unsigned long ms) {
  unsigned long start = millis();
  do {
    while (neo6M.available())
      gps.encode(neo6M.read());
  } while (millis() - start < ms);
}
