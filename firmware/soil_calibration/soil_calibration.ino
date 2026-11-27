#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

// Dual Soil Moisture 2-Point Calibration + ESP32 NVS Persistence
// Target: Classic ESP32 DevKit (esp32:esp32:esp32)
// Electrical safety: Power soil modules at 3.3 V so GPIO34/35 never exceed 3.3 V.
constexpr int SOIL1_PIN  = 34;  // ADC1_CH6 (input-only)
constexpr int SOIL2_PIN  = 35;  // ADC1_CH7 (input-only)
constexpr int PUMP_PIN   = 14;  // Held LOW during calibration
constexpr int BUZZER_PIN = 25;  // Held LOW during calibration

constexpr int ADC_OVERSAMPLE = 16;   // Hardware noise reduction per reading
constexpr int CAL_SAMPLES    = 20;   // Samples captured per dry/wet point
constexpr uint32_t STREAM_MS = 250;  // Live print interval

Preferences prefs;

struct SoilCal {
  float dryRaw;
  float wetRaw;
  bool hasDry;
  bool hasWet;
  bool valid;
};

// Measured 2-point linear calibration defaults (Dry Dirt 0% -> Wet 100%)
constexpr uint8_t CAL_SCHEMA_VER = 2;       // Bump to re-seed NVS on flash
constexpr float DEFAULT_S1_DRY = 2545.37f;  // Soil 1 (GPIO34) 0% DRY DIRT
constexpr float DEFAULT_S1_WET = 1011.15f;  // Soil 1 (GPIO34) 100% WET
constexpr float DEFAULT_S2_DRY = 2444.57f;  // Soil 2 (GPIO35) 0% DRY DIRT
constexpr float DEFAULT_S2_WET = 1049.60f;  // Soil 2 (GPIO35) 100% WET

SoilCal cal1 = {DEFAULT_S1_DRY, DEFAULT_S1_WET, true, true, true};
SoilCal cal2 = {DEFAULT_S2_DRY, DEFAULT_S2_WET, true, true, true};

bool liveStream = true;
uint32_t lastStream = 0;

int readOversampledRaw(int pin) {
  uint32_t sum = 0;
  for (int i = 0; i < ADC_OVERSAMPLE; ++i) {
    sum += analogRead(pin);
  }
  return (int)(sum / ADC_OVERSAMPLE);
}

uint32_t readOversampledMv(int pin) {
  uint32_t sum = 0;
  for (int i = 0; i < ADC_OVERSAMPLE; ++i) {
    sum += analogReadMilliVolts(pin);
  }
  return sum / ADC_OVERSAMPLE;
}

float computePercent(int raw, const SoilCal& c) {
  if (!c.valid || fabsf(c.wetRaw - c.dryRaw) < 1.0f) return -1.0f;
  float pct = 100.0f * ((float)raw - c.dryRaw) / (c.wetRaw - c.dryRaw);
  return constrain(pct, 0.0f, 100.0f);
}

// Capture CAL_SAMPLES readings over ~2 seconds and apply 2-sigma outlier rejection
void samplePoint(int pin, const char* label, float& outMean, float& outStd) {
  float buf[CAL_SAMPLES];
  double sum = 0.0;
  for (int i = 0; i < CAL_SAMPLES; ++i) {
    buf[i] = (float)readOversampledRaw(pin);
    sum += buf[i];
    delay(80);
  }
  double mean = sum / CAL_SAMPLES;
  double var = 0.0;
  for (int i = 0; i < CAL_SAMPLES; ++i) {
    double d = buf[i] - mean;
    var += d * d;
  }
  double sd = sqrt(var / CAL_SAMPLES);

  double fSum = 0.0;
  int fCount = 0;
  for (int i = 0; i < CAL_SAMPLES; ++i) {
    if (sd == 0.0 || fabs(buf[i] - mean) <= 2.0 * sd) {
      fSum += buf[i];
      ++fCount;
    }
  }
  outMean = (float)(fCount > 0 ? (fSum / fCount) : mean);
  outStd  = (float)sd;
  Serial.printf("[CAL] %s: mean=%.2f  stddev=%.2f  (kept %d/%d samples)\n",
                label, outMean, outStd, fCount, CAL_SAMPLES);
  if (outStd > 40.0f) {
    Serial.println("      [WARN] High variance detected — hold probe steady and retry if needed.");
  }
}

void updateValidity(SoilCal& c) {
  c.valid = c.hasDry && c.hasWet && (fabsf(c.wetRaw - c.dryRaw) >= 20.0f);
}

void loadNvs() {
  prefs.begin("soil_cal", true);
  uint8_t ver = prefs.getUChar("ver", 0);
  bool s1Ok = prefs.getBool("s1_ok", false);
  bool s2Ok = prefs.getBool("s2_ok", false);
  if (ver == CAL_SCHEMA_VER && s1Ok) {
    cal1.dryRaw = prefs.getFloat("s1_dry", DEFAULT_S1_DRY);
    cal1.wetRaw = prefs.getFloat("s1_wet", DEFAULT_S1_WET);
    cal1.hasDry = cal1.hasWet = cal1.valid = true;
  }
  if (ver == CAL_SCHEMA_VER && s2Ok) {
    cal2.dryRaw = prefs.getFloat("s2_dry", DEFAULT_S2_DRY);
    cal2.wetRaw = prefs.getFloat("s2_wet", DEFAULT_S2_WET);
    cal2.hasDry = cal2.hasWet = cal2.valid = true;
  }
  prefs.end();

  // Seed/update NVS when schema version changes or uncalibrated
  if (ver != CAL_SCHEMA_VER || !s1Ok || !s2Ok) {
    cal1 = {DEFAULT_S1_DRY, DEFAULT_S1_WET, true, true, true};
    cal2 = {DEFAULT_S2_DRY, DEFAULT_S2_WET, true, true, true};
    saveNvs();
  }
}

void saveNvs() {
  updateValidity(cal1);
  updateValidity(cal2);
  prefs.begin("soil_cal", false);
  prefs.putUChar("ver", CAL_SCHEMA_VER);
  prefs.putBool("s1_ok", cal1.valid);
  prefs.putFloat("s1_dry", cal1.dryRaw);
  prefs.putFloat("s1_wet", cal1.wetRaw);
  prefs.putBool("s2_ok", cal2.valid);
  prefs.putFloat("s2_dry", cal2.dryRaw);
  prefs.putFloat("s2_wet", cal2.wetRaw);
  prefs.end();
  Serial.println("[NVS] Saved soil calibration to ESP32 flash (namespace: soil_cal).");
}

void clearNvs() {
  prefs.begin("soil_cal", false);
  prefs.clear();
  prefs.end();
  cal1 = {0, 0, false, false, false};
  cal2 = {0, 0, false, false, false};
  Serial.println("[NVS] Cleared saved soil calibration.");
}

void printCalSummary() {
  Serial.println("\n=== SOIL CALIBRATION PARAMETERS ===");
  auto showOne = [](const char* name, int pin, const SoilCal& c) {
    Serial.printf("%s (GPIO%d): dry=%.2f  wet=%.2f  status=%s\n",
                  name, pin, c.dryRaw, c.wetRaw, c.valid ? "CALIBRATED" : "INCOMPLETE");
    if (c.valid) {
      float m = 100.0f / (c.wetRaw - c.dryRaw);
      float b = -m * c.dryRaw;
      Serial.printf("   Formula: pct = constrain((%.6f * raw) + (%.4f), 0, 100)\n", m, b);
    }
  };
  showOne("Soil 1", SOIL1_PIN, cal1);
  showOne("Soil 2", SOIL2_PIN, cal2);
}

void printMenu() {
  Serial.println("\n=== DUAL SOIL CALIBRATION (GPIO34 & GPIO35) ===");
  Serial.println("d  : Capture DRY (0%)  for BOTH S1(D34) & S2(D35) [20 samples]");
  Serial.println("w  : Capture WET (100%) for BOTH S1(D34) & S2(D35) [20 samples]");
  Serial.println("1d / 1w : Capture DRY / WET for Soil 1 (GPIO34) only");
  Serial.println("2d / 2w : Capture DRY / WET for Soil 2 (GPIO35) only");
  Serial.println("s  : Save calibration to ESP32 NVS flash");
  Serial.println("p  : Print calibration summary & linear formulas");
  Serial.println("c  : Clear saved NVS calibration");
  Serial.println("m  : Pause/resume live readout");
  Serial.println("?  : Show this menu");
}

void printLiveLine() {
  int r1 = readOversampledRaw(SOIL1_PIN);
  int r2 = readOversampledRaw(SOIL2_PIN);
  uint32_t mv1 = readOversampledMv(SOIL1_PIN);
  uint32_t mv2 = readOversampledMv(SOIL2_PIN);

  Serial.printf("S1(D34): raw=%4d (%4lu mV)", r1, (unsigned long)mv1);
  if (cal1.valid) Serial.printf(" %5.1f%%", computePercent(r1, cal1));
  else Serial.print("  [raw]");

  Serial.printf("  |  S2(D35): raw=%4d (%4lu mV)", r2, (unsigned long)mv2);
  if (cal2.valid) Serial.printf(" %5.1f%%\n", computePercent(r2, cal2));
  else Serial.println("  [raw]");
}

void handleCommand(String cmd) {
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.isEmpty()) return;

  float mean = 0, sd = 0;

  if (cmd == "d") {
    Serial.println("[CAL] Sampling DRY (0%) for BOTH sensors — keep probes dry & still...");
    samplePoint(SOIL1_PIN, "S1(D34) DRY", mean, sd);
    cal1.dryRaw = mean; cal1.hasDry = true; updateValidity(cal1);
    samplePoint(SOIL2_PIN, "S2(D35) DRY", mean, sd);
    cal2.dryRaw = mean; cal2.hasDry = true; updateValidity(cal2);
    printCalSummary();
  } else if (cmd == "w") {
    Serial.println("[CAL] Sampling WET (100%) for BOTH sensors — dip sensing part only...");
    samplePoint(SOIL1_PIN, "S1(D34) WET", mean, sd);
    cal1.wetRaw = mean; cal1.hasWet = true; updateValidity(cal1);
    samplePoint(SOIL2_PIN, "S2(D35) WET", mean, sd);
    cal2.wetRaw = mean; cal2.hasWet = true; updateValidity(cal2);
    printCalSummary();
  } else if (cmd == "1d") {
    samplePoint(SOIL1_PIN, "S1(D34) DRY", mean, sd);
    cal1.dryRaw = mean; cal1.hasDry = true; updateValidity(cal1);
  } else if (cmd == "1w") {
    samplePoint(SOIL1_PIN, "S1(D34) WET", mean, sd);
    cal1.wetRaw = mean; cal1.hasWet = true; updateValidity(cal1);
  } else if (cmd == "2d") {
    samplePoint(SOIL2_PIN, "S2(D35) DRY", mean, sd);
    cal2.dryRaw = mean; cal2.hasDry = true; updateValidity(cal2);
  } else if (cmd == "2w") {
    samplePoint(SOIL2_PIN, "S2(D35) WET", mean, sd);
    cal2.wetRaw = mean; cal2.hasWet = true; updateValidity(cal2);
  } else if (cmd == "s") {
    saveNvs();
    printCalSummary();
  } else if (cmd == "p") {
    printCalSummary();
  } else if (cmd == "c") {
    clearNvs();
  } else if (cmd == "m") {
    liveStream = !liveStream;
    Serial.printf("[STREAM] Live readout %s\n", liveStream ? "ON" : "OFF");
  } else if (cmd == "?") {
    printMenu();
  } else {
    Serial.printf("Unknown command '%s' (send '?' for menu)\n", cmd.c_str());
  }
}

void setup() {
  // Keep pump and buzzer safely OFF during calibration
  digitalWrite(PUMP_PIN, LOW);   pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); pinMode(BUZZER_PIN, OUTPUT);

  analogReadResolution(12);
  analogSetPinAttenuation(SOIL1_PIN, ADC_11db);
  analogSetPinAttenuation(SOIL2_PIN, ADC_11db);

  Serial.begin(115200);
  delay(300);
  loadNvs();
  printMenu();
  printCalSummary();
  lastStream = millis();
}

void loop() {
  uint32_t now = millis();
  if (liveStream && (now - lastStream >= STREAM_MS)) {
    lastStream = now;
    printLiveLine();
  }

  static String lineBuf;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      if (lineBuf.length() > 0) {
        handleCommand(lineBuf);
        lineBuf = "";
      }
    } else {
      lineBuf += c;
      // Single-char shortcuts when no digit prefix is buffered
      if (lineBuf.length() == 1 && (c == 'd' || c == 'w' || c == 's' || c == 'p' || c == 'c' || c == 'm' || c == '?')) {
        handleCommand(lineBuf);
        lineBuf = "";
      } else if (lineBuf.length() == 2 && (lineBuf[0] == '1' || lineBuf[0] == '2')) {
        handleCommand(lineBuf);
        lineBuf = "";
      }
    }
  }
}
