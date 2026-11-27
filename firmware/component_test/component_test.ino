#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <Preferences.h>

// Classic ESP32 DevKit. Never apply 5 V to a GPIO.
const char* WIFI_SSID = "";
const char* WIFI_PASSWORD = "";
constexpr int SOIL1_PIN = 34, SOIL2_PIN = 35;
constexpr int ROTARY_CLK = 18, ROTARY_DT = 19, ROTARY_SW = 23;
constexpr int LCD_SDA = 21, LCD_SCL = 22;
constexpr int FLOW_PIN = 27, PUMP_PIN = 25;
constexpr int LED_GREEN = 13, LED_YELLOW = 14, LED_RED = 32;
constexpr int BUZZER_PIN = 33;
constexpr uint8_t LCD_ADDRESS = 0x27; // Change to scanner result if needed.
constexpr float PULSES_PER_LITER = 450.0f; // Starting value for YF-S201 only.
constexpr uint32_t PUMP_MAX_MS = 5000;
// Calibrated 2-point linear soil endpoints (Dry Dirt 0% -> Wet 100%, also stored in NVS "soil_cal")
constexpr float SOIL1_DRY_DEFAULT = 2545.37f, SOIL1_WET_DEFAULT = 1011.15f;
constexpr float SOIL2_DRY_DEFAULT = 2444.57f, SOIL2_WET_DEFAULT = 1049.60f;

LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);
Preferences soilPrefs;
const char* plants[] = {"Cabai", "Tomat", "Selada"};
int selectedPlant = 0;
int soil1 = 0, soil2 = 0;
float soil1Pct = 0.0f, soil2Pct = 0.0f;
float soil1Dry = SOIL1_DRY_DEFAULT, soil1Wet = SOIL1_WET_DEFAULT;
float soil2Dry = SOIL2_DRY_DEFAULT, soil2Wet = SOIL2_WET_DEFAULT;
volatile uint32_t flowPulses = 0;
portMUX_TYPE flowMux = portMUX_INITIALIZER_UNLOCKED;
float flowRate = 0, totalLiter = 0;
bool pumpState = false, buzzerActive = false, ledTestActive = false;
bool lcdReady = false, wifiReported = false;
uint32_t pumpStart = 0, buzzerStart = 0, buzzerDuration = 0, ledStart = 0;
uint32_t lastSensor = 0, lastFlow = 0, lastLCD = 0, lastPage = 0;
uint8_t lcdPage = 0;

void IRAM_ATTR flowISR() {
  portENTER_CRITICAL_ISR(&flowMux);
  ++flowPulses;
  portEXIT_CRITICAL_ISR(&flowMux);
}

void pumpControl(bool on) {
  if (on == pumpState) return; // Repeated '1' does not extend the timer.
  pumpState = on;
  digitalWrite(PUMP_PIN, on ? HIGH : LOW); // Active-HIGH driver assumed.
  if (on) pumpStart = millis();
  Serial.println(on ? "POMPA ON (maks 5 detik)" : "POMPA OFF");
}

void serviceOutputs() {
  uint32_t now = millis();
  if (pumpState && now - pumpStart >= PUMP_MAX_MS) {
    pumpControl(false);
    Serial.println("PROTEKSI: BATAS WAKTU POMPA");
  }
  if (buzzerActive && now - buzzerStart >= buzzerDuration) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerActive = false;
  }
  if (ledTestActive && now - ledStart >= 500) ledTestActive = false;
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_YELLOW, ledTestActive || flowRate > 0.1f);
  digitalWrite(LED_RED, ledTestActive || pumpState);
}

void beep(uint32_t duration) {
  buzzerStart = millis();
  buzzerDuration = duration;
  buzzerActive = true;
  digitalWrite(BUZZER_PIN, HIGH); // Active buzzer through transistor.
}

void scanI2C() {
  Serial.println("=== I2C SCAN ===");
  int found = 0;
  lcdReady = false;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    serviceOutputs();
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("I2C ACK: 0x%02X\n", addr);
      ++found;
      if (addr == LCD_ADDRESS) lcdReady = true;
    }
  }
  if (!found) Serial.println("Tidak ada perangkat I2C terdeteksi.");
  if (!lcdReady) Serial.println("Alamat LCD tidak merespons; periksa kabel/alamat.");
}

void lcdLine(uint8_t row, const String& text) {
  lcd.setCursor(0, row);
  for (int i = 0; i < 16; ++i) lcd.print(i < text.length() ? text[i] : ' ');
}

void loadSoilCalibration() {
  soilPrefs.begin("soil_cal", true);
  if (soilPrefs.getBool("s1_ok", false)) {
    soil1Dry = soilPrefs.getFloat("s1_dry", SOIL1_DRY_DEFAULT);
    soil1Wet = soilPrefs.getFloat("s1_wet", SOIL1_WET_DEFAULT);
  }
  if (soilPrefs.getBool("s2_ok", false)) {
    soil2Dry = soilPrefs.getFloat("s2_dry", SOIL2_DRY_DEFAULT);
    soil2Wet = soilPrefs.getFloat("s2_wet", SOIL2_WET_DEFAULT);
  }
  soilPrefs.end();
}

float soilToPercent(int raw, float dryRaw, float wetRaw) {
  if (fabsf(wetRaw - dryRaw) < 1.0f) return 0.0f;
  return constrain(100.0f * ((float)raw - dryRaw) / (wetRaw - dryRaw), 0.0f, 100.0f);
}

void readSoil() {
  uint32_t sum1 = 0, sum2 = 0;
  for (int i = 0; i < 16; ++i) {
    sum1 += analogRead(SOIL1_PIN);
    sum2 += analogRead(SOIL2_PIN);
  }
  soil1 = sum1 / 16;
  soil2 = sum2 / 16;
  soil1Pct = soilToPercent(soil1, soil1Dry, soil1Wet);
  soil2Pct = soilToPercent(soil2, soil2Dry, soil2Wet);
  Serial.printf("SOIL: S1=%.1f%% (raw=%d) | S2=%.1f%% (raw=%d)\n", soil1Pct, soil1, soil2Pct, soil2);
}

void readFlow() {
  uint32_t now = millis(), elapsed = now - lastFlow;
  if (elapsed < 1000) return;
  portENTER_CRITICAL(&flowMux);
  uint32_t count = flowPulses;
  flowPulses = 0;
  portEXIT_CRITICAL(&flowMux);
  lastFlow = now;
  flowRate = count * 60000.0f / (elapsed * PULSES_PER_LITER);
  totalLiter += count / PULSES_PER_LITER;
  Serial.printf("FLOW: pulsa=%lu, %.2f L/min, total=%.3f L\n",
                (unsigned long)count, flowRate, totalLiter);
}

void readRotary() {
  // Full quadrature sequence rejects contact bounce without a blocking delay.
  static const int8_t transitions[16] = {
    0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0
  };
  static uint8_t previous = 0;
  static int8_t movement = 0;
  static bool initialized = false;
  uint8_t state = (digitalRead(ROTARY_CLK) << 1) | digitalRead(ROTARY_DT);
  if (!initialized) { previous = state; initialized = true; }
  movement += transitions[(previous << 2) | state];
  previous = state;
  if (movement >= 4 || movement <= -4) {
    selectedPlant = (selectedPlant + (movement > 0 ? 1 : 2)) % 3;
    movement = 0;
    Serial.printf("ROTARY: %s\n", plants[selectedPlant]);
    beep(50);
  }
  static int lastRaw = HIGH, stable = HIGH;
  static uint32_t changedAt = 0;
  int raw = digitalRead(ROTARY_SW);
  if (raw != lastRaw) { changedAt = millis(); lastRaw = raw; }
  if (millis() - changedAt >= 30 && stable != raw) {
    stable = raw;
    if (stable == LOW) {
      Serial.printf("TOMBOL: konfirmasi %s\n", plants[selectedPlant]);
      beep(150);
    }
  }
}

void updateLCD() {
  if (!lcdReady) return;
  uint32_t now = millis();
  if (now - lastPage >= 3000) { lcdPage = (lcdPage + 1) % 4; lastPage = now; }
  if (now - lastLCD < 500) return;
  lastLCD = now;
  switch (lcdPage) {
    case 0: lcdLine(0, "IRIGASI PRESISI"); lcdLine(1, plants[selectedPlant]); break;
    case 1: lcdLine(0, "S1:" + String(soil1Pct, 0) + "% (" + String(soil1) + ")");
      lcdLine(1, "S2:" + String(soil2Pct, 0) + "% (" + String(soil2) + ")"); break;
    case 2: lcdLine(0, "Flow:" + String(flowRate, 1) + " L/m"); lcdLine(1, "Air:" + String(totalLiter, 3) + " L"); break;
    case 3: lcdLine(0, pumpState ? "Pompa: ON" : "Pompa: OFF");
      lcdLine(1, WiFi.status() == WL_CONNECTED ? "WiFi: TERHUBUNG" : "WiFi: OFFLINE"); break;
  }
}

void printHelp() {
  Serial.println("=== MENU PENGUJIAN ===");
  Serial.println("s: soil sekarang | f: debit terbaru | i: scan I2C (pompa OFF)");
  Serial.println("1: pompa ON maks 5s | 0: pompa OFF | b: buzzer | l: LED | ?: bantuan");
}

void serialCommand() {
  // Bound work so a continuous serial stream cannot starve pump protection.
  for (int n = 0; n < 16 && Serial.available(); ++n) {
    serviceOutputs();
    switch (Serial.read()) {
      case '1': pumpControl(true); break;
      case '0': pumpControl(false); break;
      case 'b': beep(500); break;
      case 'l': ledTestActive = true; ledStart = millis(); break;
      case 's': readSoil(); break;
      case 'f': Serial.printf("FLOW terakhir: %.2f L/min, %.3f L\n", flowRate, totalLiter); break;
      case 'i': pumpControl(false); scanI2C();
        if (lcdReady) { lcd.init(); lcd.backlight(); } break;
      case '?': printHelp(); break;
    }
  }
}

void setup() {
  // Set the inactive output latch before enabling the output.
  digitalWrite(PUMP_PIN, LOW); pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_GREEN, OUTPUT); pinMode(LED_YELLOW, OUTPUT); pinMode(LED_RED, OUTPUT);
  pinMode(ROTARY_CLK, INPUT_PULLUP); pinMode(ROTARY_DT, INPUT_PULLUP);
  pinMode(ROTARY_SW, INPUT_PULLUP); pinMode(FLOW_PIN, INPUT_PULLUP);
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetPinAttenuation(SOIL1_PIN, ADC_11db);
  analogSetPinAttenuation(SOIL2_PIN, ADC_11db);
  loadSoilCalibration();
  Wire.begin(LCD_SDA, LCD_SCL);
  Wire.setTimeOut(20);
  Serial.println("\nIRIGASI PRESISI - COMPONENT TEST (ESP32 DevKit)");
  scanI2C();
  if (lcdReady) { lcd.init(); lcd.backlight(); lcdLine(0, "IRIGASI PRESISI"); lcdLine(1, "COMPONENT TEST"); }
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), flowISR, FALLING);
  lastFlow = lastPage = millis();
  ledTestActive = true; ledStart = millis(); beep(200);
  if (strlen(WIFI_SSID)) {
    WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.println("WiFi: menghubungkan ke jaringan 2.4 GHz...");
  } else Serial.println("WiFi: dilewati (SSID kosong).");
  printHelp();
}

void loop() {
  serviceOutputs();
  readRotary();
  if (millis() - lastSensor >= 1000) { lastSensor = millis(); readSoil(); }
  readFlow();
  updateLCD();
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiReported) { Serial.print("WiFi terhubung: "); Serial.println(WiFi.localIP()); wifiReported = true; }
  } else wifiReported = false;
  serialCommand();
  serviceOutputs();
}
