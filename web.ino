#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <LiquidCrystal_I2C.h>
#include "time.h"
#include <ArduinoJson.h> // WAJIB INSTALL LIBRARY INI

// ===== KONFIGURASI WIFI =====
#define WIFI_SSID "H"
#define WIFI_PASSWORD "11111111"

// ===== KONFIGURASI API (SERVER) =====
String baseUrl = "https://c228eccacc4f.ngrok-free.app";
String logEndpoint = baseUrl + "/api/feeder/log";
String scheduleEndpoint = baseUrl + "/api/feeder/schedule";

// ===== KONFIGURASI SENSOR =====
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ===== KONFIGURASI SERVO =====
#define SERVOPIN 18
Servo servoMotor;

// ===== LED DAN BUZZER =====
#define LED_PIN 2
#define BUZZER_PIN 15

// ===== LCD I2C =====
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ===== VARIABEL JADWAL =====
String daftarJadwal[20];
int jumlahJadwalAktif = 0;
String lastFedTime = "";

// ===== BATAS SUHU =====
float BATAS_ATAS = 32.0;
float BATAS_BAWAH = 18.0;

// ===== NTP TIME =====
const char* ntpServer = "id.pool.ntp.org";
const long gmtOffset_sec = 25200;
const int daylightOffset_sec = 0;

int statusServo = 0;

unsigned long prevSensorMillis = 0;
unsigned long prevUploadMillis = 0;
unsigned long prevScheduleMillis = 0;

// === LED BLINK VARIABLES ===
bool ledBlinkState = false;
unsigned long prevLedMillis = 0;
const long intervalLedBlink = 100; // blink tiap 0.5 detik

// Timer interval
const long intervalSensor = 2000;
const long intervalUpload = 15000;
const long intervalCekJadwal = 60000;

// --- FUNGSI BUNYIKAN BUZZER ---
void beep(int n = 1, int dur = 100) {
  for (int i = 0; i < n; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(dur);
    digitalWrite(BUZZER_PIN, LOW);
    delay(80);
  }
}

// --- FUNGSI AMBIL JADWAL DARI SERVER ---
void syncJadwal() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(scheduleEndpoint);
    int httpCode = http.GET();

    if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, payload);

      if (!error) {
        JsonArray arrayJadwal = doc["schedules"];
        jumlahJadwalAktif = 0;

        Serial.println("=== UPDATE JADWAL ===");
        for (String jam : arrayJadwal) {
          if (jumlahJadwalAktif < 20) {
            daftarJadwal[jumlahJadwalAktif] = jam;
            Serial.println("Jadwal: " + jam);
            jumlahJadwalAktif++;
          }
        }
        Serial.printf("Total Jadwal: %d\n", jumlahJadwalAktif);
      }
    }
    http.end();
  }
}

// --- FUNGSI SYSTEM CHECK ---
void systemCheck() {
  lcd.clear();
  lcd.print("System Check...");

  digitalWrite(LED_PIN, HIGH); delay(300); digitalWrite(LED_PIN, LOW);
  beep(1, 100);

  lcd.setCursor(0, 1); lcd.print("Servo...");
  servoMotor.write(90); delay(500);
  servoMotor.write(0);  delay(500);

  lcd.clear();
  lcd.print("DAKM Ready!");
  delay(1500);
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  servoMotor.attach(SERVOPIN);
  servoMotor.write(0);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  lcd.init();
  lcd.backlight();
  lcd.print("Connecting WiFi");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  lcd.clear();
  lcd.print("WiFi Connected");
  delay(1000);

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  systemCheck();
  syncJadwal();
}

void loop() {
  unsigned long currentMillis = millis();

  // CEK WAKTU
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo)) return;

  char timeStrBuff[6];
  strftime(timeStrBuff, sizeof(timeStrBuff), "%H:%M", &timeInfo);
  String currentTimeStr = String(timeStrBuff);
  int currentSec = timeInfo.tm_sec;

  // SENSOR UPDATE LCD
  if (currentMillis - prevSensorMillis >= intervalSensor) {
    prevSensorMillis = currentMillis;

    float suhu = dht.readTemperature();
    float kelembaban = dht.readHumidity();

    if (!isnan(suhu) && !isnan(kelembaban)) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.printf("Time:%s:%02d", currentTimeStr.c_str(), currentSec);
      lcd.setCursor(0, 1);
      lcd.printf("T:%.1fC H:%.0f%%", suhu, kelembaban);

      if (suhu >= BATAS_ATAS || suhu <= BATAS_BAWAH) {
        beep(2);
      }
    }
  }

  // === LOGIKA LED INDIKATOR ===
  float suhuNow = dht.readTemperature();
  if (suhuNow >= BATAS_ATAS || suhuNow <= BATAS_BAWAH) {
    digitalWrite(LED_PIN, LOW);
  }
  else if (statusServo == 1) {
    digitalWrite(LED_PIN, HIGH);
  }
  else {
    if (currentMillis - prevLedMillis >= intervalLedBlink) {
      prevLedMillis = currentMillis;
      ledBlinkState = !ledBlinkState;
      digitalWrite(LED_PIN, ledBlinkState);
    }
  }

  // === CEK JADWAL ===
  bool matchFound = false;
  for (int i = 0; i < jumlahJadwalAktif; i++) {
    if (currentTimeStr == daftarJadwal[i]) {
      matchFound = true;
      break;
    }
  }

  if (matchFound && currentTimeStr != lastFedTime) {
    Serial.println("Feeding: " + currentTimeStr);

    lcd.clear();
    lcd.print("Feeding Time!");
    lcd.setCursor(0, 1);
    lcd.print(currentTimeStr);

    statusServo = 1;
    beep(3, 200);

    for (int a = 0; a < 5; a++) {
      servoMotor.write(90);
      delay(600);
      servoMotor.write(0);
      delay(400);
    }

    statusServo = 0;
    lastFedTime = currentTimeStr;
    prevUploadMillis = 0;
  }

  // === KIRIM DATA KE SERVER ===
  if (currentMillis - prevUploadMillis >= intervalUpload) {
    prevUploadMillis = currentMillis;
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin(logEndpoint);
      http.addHeader("Content-Type", "application/json");

      float t = dht.readTemperature();
      float h = dht.readHumidity();
      String statusStr = (statusServo == 1) ? "ON" : "OFF";

      String jsonPayload = "{\"temperature\":" + String(t, 1) +
                           ",\"humidity\":" + String(h, 1) +
                           ",\"servo_status\":\"" + statusStr + "\"}";

      int httpCode = http.POST(jsonPayload);
      if (httpCode > 0) {
        Serial.printf("Upload OK: %s\n", jsonPayload.c_str());
      }
      http.end();
    }
  }

  // === SYNC JADWAL ===
  if (currentMillis - prevScheduleMillis >= intervalCekJadwal) {
    prevScheduleMillis = currentMillis;
    syncJadwal();
  }
}
