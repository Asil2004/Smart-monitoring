#include <WiFi.h>
#include <WebServer.h>
#include <Firebase_ESP_Client.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- KONFIGURATSIYA ---
#define API_KEY "AIzaSyArkLOSoR0Wmsc6sZZFd_ptsbL_VPCvmI8"
#define DATABASE_URL "smart-monitoring-82bd2-default-rtdb.asia-southeast1.firebasedatabase.app"

#define DHTPIN 4
#define LDR_PIN 32
#define MQ135_PIN 34
#define MQ9_PIN 35
#define BUZZER_PIN 12
#define LED_RED 13

// OBYEKTLAR
Adafruit_SSD1306 display(128, 64, &Wire, -1);
DHT dht(DHTPIN, DHT22);
WebServer server(80);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// GLOBAL O'ZGARUVCHILAR
float t, h, lx, m135, m9;
String scanResultsHTML = "";
bool firebaseActive = false;
unsigned long prevMillis = 0;

// INTERAKTIV WIFI SETUP SAHIFASI
String getHTML() {
  String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:sans-serif; background:#0f172a; color:white; text-align:center; padding:20px;}";
  html += ".card{background:#1e293b; padding:25px; border-radius:15px; border:1px solid #38bdf8; max-width:400px; margin:auto;}";
  html += "input{padding:12px; margin:10px 0; width:100%; border-radius:8px; border:1px solid #334155; background:#0f172a; color:white; box-sizing:border-box;}";
  html += "button{padding:12px; width:100%; background:#38bdf8; border:none; color:white; border-radius:8px; font-weight:bold; cursor:pointer;}";
  html += ".wifi-list{text-align:left; margin:15px 0; max-height:180px; overflow-y:auto; background:#0b0e14; padding:10px; border-radius:8px;}";
  html += ".wifi-item{padding:10px; border-bottom:1px solid #1e293b; cursor:pointer; font-size:14px;} .wifi-item:hover{color:#38bdf8;}";
  html += "</style></head><body><div class='card'><h2>📡 Smart Setup</h2>";
  html += "<form action='/scan'><button type='submit'>SCAN NETWORKS</button></form>";
  html += "<div class='wifi-list'>" + (scanResultsHTML == "" ? "Skanerlang..." : scanResultsHTML) + "</div>";
  html += "<form action='/connect' method='POST'>";
  html += "<input type='text' name='ssid' id='ssid' placeholder='WiFi SSID' required>";
  html += "<input type='password' name='pass' placeholder='Password' required>";
  html += "<button type='submit' style='background:#10b981;'>CONNECT</button></form></div>";
  html += "<script>function setSSID(name){document.getElementById('ssid').value = name;}</script></body></html>";
  return html;
}

void setup() {
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) Serial.println("OLED Error");
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.display();

  dht.begin();
  Wire.begin();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("SmartCore_Config_AP", "12345678");
  
  server.on("/", []() { server.send(200, "text/html", getHTML()); });
  
  server.on("/scan", []() {
    int n = WiFi.scanNetworks();
    scanResultsHTML = "";
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      scanResultsHTML += "<div class='wifi-item' onclick='setSSID(\"" + ssid + "\")'>📶 " + ssid + "</div>";
    }
    server.send(200, "text/html", getHTML());
  });

  server.on("/connect", HTTP_POST, []() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    WiFi.begin(ssid.c_str(), pass.c_str());
    server.send(200, "text/html", "<h2>Connecting... Check OLED screen</h2>");
  });

  server.begin();
}

void loop() {
  server.handleClient();

  // Sensorlarni o'qish
  t = dht.readTemperature();
  h = dht.readHumidity();
  m135 = analogRead(MQ135_PIN);
  m9 = analogRead(MQ9_PIN);
  
  // LDR O'qish (0-100% formatida)
  int ldrRaw = analogRead(LDR_PIN);
  lx = map(ldrRaw, 4095, 0, 0, 100); 

  display.clearDisplay();
  display.setCursor(0, 0);
  display.printf("T:%0.1fC H:%0.1f%%", isnan(t) ? 0 : t, isnan(h) ? 0 : h);
  display.setCursor(0, 18);
  display.printf("M135:%0.0f M9:%0.0f", m135, m9);
  display.setCursor(0, 36);
  display.printf("LUX: %0.0f %%", lx);
  
  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(0, 54); display.print("Status: ONLINE");
    
    if (!firebaseActive) {
      configTime(5 * 3600, 0, "pool.ntp.org", "time.nist.gov");
      config.api_key = API_KEY;
      config.database_url = DATABASE_URL;
      config.signer.test_mode = true; // SSL xatolarini kamaytirish uchun

      Firebase.begin(&config, &auth);
      Firebase.reconnectWiFi(true);
      fbdo.setResponseSize(1024); // RAM tejash
      firebaseActive = true;
    }

    if (millis() - prevMillis > 5000) {
      prevMillis = millis();
      FirebaseJson json;
      json.set("temperature", isnan(t) ? 0 : t);
      json.set("humidity", isnan(h) ? 0 : h);
      json.set("lux", lx);
      json.set("mq135", m135);
      json.set("mq9", m9);
      
      if (!Firebase.RTDB.setJSON(&fbdo, "/sensors", &json)) {
        Serial.println("Firebase Error: " + fbdo.errorReason());
      } else {
        Firebase.RTDB.setTimestamp(&fbdo, "/last_seen");
      }
    }
  } else {
    display.setCursor(0, 54); display.print("Status: SETUP MODE");
  }

  // Xavfni tekshirish
  if (m9 > 1000 || m135 > 1500) {
    digitalWrite(LED_RED, HIGH);
    digitalWrite(BUZZER_PIN, (millis() % 400 < 200) ? HIGH : LOW);
  } else {
    digitalWrite(LED_RED, LOW);
    digitalWrite(BUZZER_PIN, LOW);
  }
  
  display.display();
}