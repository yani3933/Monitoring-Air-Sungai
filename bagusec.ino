#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include <WiFi.h>
#include "DFRobot_EC10.h"
#include "DFRobot_ESP_PH.h"
#include <Wire.h>
#include <HTTPClient.h>

// Deklarasi pin sensor
#define ONE_WIRE_BUS 4  // Pin suhu
#define DO_PIN 35       // Pin DO
#define EC_PIN 34       // Pin EC
#define PH_PIN 33       // Pin pH
#define TRIG_PIN 5      // Pin TRIG sensor level air
#define ECHO_PIN 18     // Pin ECHO sensor level air

#define VREF 5000    // VREF (mv)
#define ADC_RES 4095 // Resolusi ADC maksimum ESP32
#define ESPADC 4095.0       // Resolusi ADC ESP32
#define ESPVOLTAGE 3300.0   // Tegangan suplai ADC dalam milivolt
#define ESPVOLTAGEPH 2500.0 // Tegangan suplai ADC untuk pH

// Parameter Kalibrasi DO
#define CAL1_V (1600) // mv
#define CAL1_T (25)   // ℃

float duration_us, distance_cm, distance;
float Vref = 1650; // Tegangan pada pH 6.89 (dalam mV)
float m = -0.059;  // Faktor skala perubahan tegangan terhadap pH

// Setup sensor suhu
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Setup sensor EC10
DFRobot_EC10 ec;

// Setup sensor pH
DFRobot_ESP_PH ph;

// WiFi credentials
const char* ssid = "iPhone";
const char* pass = "cobainaja";

// Alamat server PHP
const char *host = "172.20.10.6";

float ccmeWQI, wqi_web;
String kategori = "";

void setup() {
  Serial.begin(115200);

  // Mulai koneksi WiFi
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  sensors.begin();
  ec.begin();
  EEPROM.begin(32);
  ph.begin();

// Kalibrasi EC dengan larutan standar
  float calibrationVoltage = analogRead(EC_PIN) / ESPADC * ESPVOLTAGE;
  ec.calibration(calibrationVoltage, 25.0); // Gunakan suhu sekitar 25°C
}

void loop() {
  // Baca data dari sensor
  float temperatureC = readTemperature();
  float nilaid = readDO();
  float ecValue = readEC();
  float distance = readLevel();
  float phValue = readPH();

  // Hitung CCME WQI dan tentukan kategori
  ccmeWQI = calculateCCMEWQI(temperatureC, phValue, nilaid, ecValue);
  wqi_web = ccmeWQI;

  // Menentukan kategori berdasarkan WQI
  if (ccmeWQI >= 95) {
    kategori = "Sangat Baik";
  } else if (ccmeWQI >= 80) {
    kategori = "Baik";
  } else if (ccmeWQI >= 65) {
    kategori = "Sedang";
  } else if (ccmeWQI >= 45) {
    kategori = "Buruk";
  } else {
    kategori = "Sangat Buruk";
  }

  // Tampilkan data ke Serial Monitor
  Serial.println("\n=== Sistem Monitoring Kualitas Air ===");
  Serial.printf("- Suhu      : %.2f °C\n", temperatureC);
  Serial.printf("- DO        : %.2f mg/L\n", nilaid);
  Serial.printf("- EC        : %.2f ms/cm\n", ecValue);
  Serial.printf("- pH        : %.2f\n", phValue);
  Serial.printf("- Level Air : %.2f cm\n", distance);
  Serial.printf("- CCME WQI  : %.2f\n", ccmeWQI);
  Serial.printf("- Kategori  : %s\n", kategori.c_str());

  // Kirim data ke server PHP
  kirim(temperatureC, distance, phValue, nilaid, ecValue, ccmeWQI);

  delay(2500);
}

// Fungsi untuk membaca suhu
float readTemperature() {
  sensors.requestTemperatures();
  return sensors.getTempCByIndex(0);
}

// Tabel saturasi DO (mg/L) berdasarkan suhu (0-40°C)
const uint16_t DO_Table[41] = {
  14460, 14220, 13820, 13440, 13090, 12740, 12420, 12110, 11810, 11530,
  11260, 11010, 10770, 10530, 10300, 10080, 9860, 9660, 9460, 9270,
  9080, 8900, 8730, 8570, 8410, 8250, 8110, 7960, 7820, 7690,
  7560, 7430, 7300, 7180, 7070, 6950, 6840, 6730, 6630, 6530, 6410
};


// Fungsi untuk membaca DO
float readDO() {
  uint16_t ADC_Raw = analogRead(DO_PIN);
  uint32_t ADC_Voltage = uint32_t(VREF) * ADC_Raw / ADC_RES;
  return (float)readDOConcentration(ADC_Voltage, readTemperature()) / 1000.0;
}

uint16_t readDOConcentration(uint32_t voltage_mv, uint8_t temperature_c) {
  // Pastikan suhu tidak melebihi indeks tabel
  if (temperature_c > 40) {
    temperature_c = 40;
  }

  uint16_t V_saturation = CAL1_V + 35 * temperature_c - CAL1_T * 35;
  return voltage_mv * DO_Table[temperature_c] / V_saturation;
}


// Fungsi untuk membaca EC
float readEC() {
  int rawEC = analogRead(EC_PIN);
  float voltageEC = rawEC / ESPADC * ESPVOLTAGE;
  Serial.printf("Raw ADC: %d, Voltage EC: %.2f mV\n", rawEC, voltageEC);

  // Hitung nilai EC
  float ecValue = ec.readEC(voltageEC, readTemperature());
  ecValue = ecValue / 10.0; // Penyesuaian jika nilai terlalu besar

  Serial.printf("EC Value: %.2f mS/cm\n", ecValue);

  return ecValue;
}

// Fungsi untuk membaca level air
float readLevel() {
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  duration_us = pulseIn(ECHO_PIN, HIGH);
  return 0.017 * duration_us;
}

// Fungsi untuk membaca pH
float readPH() {
  float voltage = analogRead(PH_PIN) / ESPADC * ESPVOLTAGEPH;
  Serial.printf("Tegangan pH (filtered): %.2f mV\n", voltage);

  float pH_1 = 6.89, V_1 = 1235.04;
  float pH_2 = 4.5, V_2 = 552.81;

  return pH_1 + (voltage - V_1) * (pH_2 - pH_1) / (V_2 - V_1);
}

// Fungsi untuk menghitung CCME WQI
float calculateCCMEWQI(float temperature, float ph, float doValue, float ec) {
  float tempMin = 20.0, tempMax = 30.0;
  float phMin = 6.5, phMax = 9.0;
  float doStandard = 5.0;
  float ecStandard = 0.25;

  int failedParams = 0, failedTests = 0, totalParams = 4;
  float excursions[4] = {0};

  if (temperature < tempMin || temperature > tempMax) {
    failedParams++;
    failedTests++;
    excursions[0] = (temperature < tempMin) ? ((tempMin / temperature) - 1) : ((temperature / tempMax) - 1);
  }

  if (ph < phMin || ph > phMax) {
    failedParams++;
    failedTests++;
    excursions[1] = (ph < phMin) ? ((phMin / ph) - 1) : ((ph / phMax) - 1);
  }

  if (doValue < doStandard) {
    failedParams++;
    failedTests++;
    excursions[2] = (doStandard / doValue) - 1;
  }

  if (ec > ecStandard) {
    failedParams++;
    failedTests++;
    excursions[3] = (ec / ecStandard) - 1;
  }

  float F1 = (float(failedParams) / totalParams) * 100.0;
  float F2 = (float(failedTests) / totalParams) * 100.0;

  float totalExcursion = 0;
  for (int i = 0; i < totalParams; i++) {
    totalExcursion += excursions[i];
  }
  float NSE = (failedTests == 0) ? 0 : totalExcursion / failedTests;

  float F3 = (NSE == 0) ? 0 : NSE / (0.01 * NSE + 0.01);

  float WQI = 100.0 - sqrt((pow(F1, 2) + pow(F2, 2) + pow(F3, 2)) / 1.732);
  return constrain(WQI, 0, 100);
}

// Fungsi untuk mengirim data ke server
void kirim(float suhu, float level, float pH, float DO, float EC, float WQI) {
  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect(host, httpPort)) {
    Serial.println("Koneksi Gagal");
    return;
  }

  String url = "/TugasAkhirRahma/save.php?ph=" + String(pH) +
               "&oksigen=" + String(DO) +
               "&elektrik=" + String(EC) +
               "&suhu=" + String(suhu) +
               "&level=" + String(level) +
               "&kategori=" + String(WQI);

  client.print("GET " + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Connection: close\r\n\r\n");

  while (client.available() == 0) {
    if (millis() - millis() > 1000) {
      Serial.println("Client Timeout !");
      client.stop();
      return;
    }
  }

  while (client.available()) {
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }

  Serial.println("Selesai");
}
