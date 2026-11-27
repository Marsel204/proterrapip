#include <Arduino.h>
#include "soc/gpio_reg.h"

// Combined Pump (GPIO14 / MOSFET) + Buzzer (GPIO25 / S8050) bench-test sketch
// Target: Classic ESP32 DevKit (esp32:esp32:esp32)
constexpr int PUMP_PIN   = 14;  // PWM MOSFET TRIG
constexpr int BUZZER_PIN = 25;  // S8050 Base via 1 kOhm resistor

constexpr uint32_t PULSE_MS    = 2000;  // HIGH/LOW duration for pump 't' pulse test
constexpr uint32_t BEEP_MS     = 500;   // Short beep duration for 'b'
constexpr uint32_t STREAM_MS   = 500;   // Live raw pin readout interval

bool pumpOn = false;
bool buzzerOn = false;
bool buzzerTimed = false;
bool adcProbeMode = false;
bool liveStream = true;

uint32_t buzzerOnSince = 0;
uint32_t lastStream = 0;

bool cycleActive = false;
int cycleStep = 0;
uint32_t cycleTimer = 0;

void printRawStatus() {
  uint32_t outReg = REG_READ(GPIO_OUT_REG);
  uint32_t inReg  = REG_READ(GPIO_IN_REG);
  uint32_t enReg  = REG_READ(GPIO_ENABLE_REG);

  uint32_t pOut = (outReg >> PUMP_PIN) & 1U;
  uint32_t pIn  = (inReg  >> PUMP_PIN) & 1U;
  uint32_t pEn  = (enReg  >> PUMP_PIN) & 1U;
  int pDig = digitalRead(PUMP_PIN);

  uint32_t bOut = (outReg >> BUZZER_PIN) & 1U;
  uint32_t bIn  = (inReg  >> BUZZER_PIN) & 1U;
  int bDig = digitalRead(BUZZER_PIN);

  if (adcProbeMode) {
    int rawAdc = analogRead(PUMP_PIN);
    uint32_t mv = analogReadMilliVolts(PUMP_PIN);
    Serial.printf("[RAW] D14(ADC): dig=%d adc=%4d/4095 %4lu mV (IN=%lu EN=%lu) | D25(BZ): cmd=%s dig=%d OUT=%lu IN=%lu\n",
                  pDig, rawAdc, (unsigned long)mv, (unsigned long)pIn, (unsigned long)pEn,
                  buzzerOn ? "HIGH" : "LOW ", bDig, (unsigned long)bOut, (unsigned long)bIn);
  } else {
    Serial.printf("[RAW] D14(PUMP): cmd=%s dig=%d OUT=%lu IN=%lu | D25(BZ): cmd=%s dig=%d OUT=%lu IN=%lu\n",
                  pumpOn ? "HIGH" : "LOW ", pDig, (unsigned long)pOut, (unsigned long)pIn,
                  buzzerOn ? "HIGH" : "LOW ", bDig, (unsigned long)bOut, (unsigned long)bIn);
  }
}

void setPump(bool high, const char* reason) {
  if (adcProbeMode) {
    adcProbeMode = false;
    digitalWrite(PUMP_PIN, high ? HIGH : LOW);
    pinMode(PUMP_PIN, OUTPUT);
  } else {
    digitalWrite(PUMP_PIN, high ? HIGH : LOW);
  }
  pumpOn = high;
  Serial.printf("[PUMP D%d] %s (%s)\n", PUMP_PIN, high ? "HIGH (ON)" : "LOW (OFF)", reason);
  printRawStatus();
}

void setBuzzer(bool high, bool timed, const char* reason) {
  noTone(BUZZER_PIN);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, high ? HIGH : LOW);
  buzzerOn = high;
  buzzerTimed = high && timed;
  if (high) buzzerOnSince = millis();
  Serial.printf("[BUZZER D%d] %s (%s)\n", BUZZER_PIN, high ? "HIGH (ON)" : "LOW (OFF)", reason);
  printRawStatus();
}

void enterAdcProbe() {
  cycleActive = false;
  pumpOn = false;
  adcProbeMode = true;
  pinMode(PUMP_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PUMP_PIN, ADC_11db);
  Serial.println("[PUMP D14] Switched to ADC INPUT probe (not driving MOSFET; send 0 or 1 to drive again)");
  printRawStatus();
}

void printMenu() {
  Serial.println("\n=== PUMP (D14) + BUZZER (D25) TEST ===");
  Serial.println("1 : Pump   D14 HIGH (stays ON until 0 or x)");
  Serial.println("0 : Pump   D14 LOW  (OFF immediately)");
  Serial.println("t : Pump   D14 3x pulse test (2 s HIGH / 2 s LOW)");
  Serial.println("b : Buzzer D25 short beep (0.5 s HIGH)");
  Serial.println("2 : Buzzer D25 HIGH (steady ON)");
  Serial.println("3 : Buzzer D25 LOW  (OFF)");
  Serial.println("p : Buzzer D25 2 kHz tone (0.5 s, passive buzzer check)");
  Serial.println("x : ALL OFF (Pump D14 LOW + Buzzer D25 LOW)");
  Serial.println("a : D14 ADC probe mode (releases D14 to INPUT, prints raw 0..4095 + mV)");
  Serial.println("m : Toggle live 500 ms raw stream (currently ON)");
  Serial.println("s : Single raw snapshot of D14 + D25");
  Serial.println("? : Show this menu");
}

void setup() {
  // Pre-load LOW latches before enabling outputs
  digitalWrite(PUMP_PIN, LOW);
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(BUZZER_PIN, OUTPUT);

  Serial.begin(115200);
  delay(300);
  Serial.println("\nESP32 Pump (D14) + Buzzer (D25) Test Ready (both LOW)");
  printMenu();
  printRawStatus();
  lastStream = millis();
}

void loop() {
  uint32_t now = millis();

  // Auto-off for short timed beep
  if (buzzerOn && buzzerTimed && (now - buzzerOnSince >= BEEP_MS)) {
    setBuzzer(false, false, "beep done");
  }

  // Non-blocking 3x Pump HIGH/LOW pulse test
  if (cycleActive && (now - cycleTimer >= PULSE_MS)) {
    cycleTimer = now;
    cycleStep++;
    if (cycleStep >= 6) {
      cycleActive = false;
      setPump(false, "pulse test complete");
    } else {
      bool nextHigh = (cycleStep % 2 == 0);
      setPump(nextHigh, "pulse test");
    }
  }

  // Periodic live raw D14 + D25 readout
  if (liveStream && (now - lastStream >= STREAM_MS)) {
    lastStream = now;
    printRawStatus();
  }

  // Serial commands
  while (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == '\r' || cmd == '\n') continue;

    switch (cmd) {
      case '1':
        cycleActive = false;
        setPump(true, "manual");
        break;
      case '0':
        cycleActive = false;
        setPump(false, "manual");
        break;
      case 't':
      case 'T':
        cycleActive = true;
        cycleStep = 0;
        cycleTimer = now;
        setPump(true, "pulse test 1/3");
        break;
      case 'b':
      case 'B':
        setBuzzer(true, true, "0.5s beep");
        break;
      case '2':
        setBuzzer(true, false, "manual ON");
        break;
      case '3':
        setBuzzer(false, false, "manual OFF");
        break;
      case 'p':
      case 'P':
        buzzerOn = false;
        buzzerTimed = false;
        Serial.println("[BUZZER D25] 2 kHz tone (0.5 s)");
        tone(BUZZER_PIN, 2000, 500);
        break;
      case 'x':
      case 'X':
        cycleActive = false;
        setPump(false, "ALL OFF");
        setBuzzer(false, false, "ALL OFF");
        break;
      case 'a':
      case 'A':
        enterAdcProbe();
        break;
      case 'm':
      case 'M':
        liveStream = !liveStream;
        Serial.printf("[STREAM] Live 500 ms raw stream %s\n", liveStream ? "ON" : "OFF");
        break;
      case 's':
      case 'S':
        printRawStatus();
        break;
      case '?':
        printMenu();
        break;
      default:
        Serial.printf("Unknown command '%c' (send '?' for help)\n", cmd);
        break;
    }
  }
}
