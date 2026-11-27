#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <LiquidCrystal_I2C.h>
#include <esp_timer.h>
#include <math.h>
#include "wifi_secrets.h"
#include "web_page.h"

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  #define ESP32_CORE_V3 1
#else
  #define ESP32_CORE_V3 0
#endif

// ============================================================================
// 1. PIN MAP (Pump=GPIO14, Buzzer=GPIO32, Soil=GPIO34/35, Flow=GPIO27)
// ============================================================================
constexpr uint8_t PIN_SOIL_1     = 34;  // ADC1_CH6 (Wi-Fi safe)
constexpr uint8_t PIN_SOIL_2     = 35;  // ADC1_CH7 (Wi-Fi safe)
constexpr uint8_t PIN_FLOW       = 27;  // YF-S201 (via level shifter)
constexpr uint8_t PIN_LCD_SDA    = 21;  // LCD 16x2 I2C (via BSS138)
constexpr uint8_t PIN_LCD_SCL    = 22;  // LCD 16x2 I2C (via BSS138)
constexpr uint8_t PIN_ENC_CLK    = 18;  // KY-040 CLK
constexpr uint8_t PIN_ENC_DT     = 19;  // KY-040 DT
constexpr uint8_t PIN_ENC_SW     = 23;  // KY-040 SW
constexpr uint8_t PIN_POMPA_PWM  = 14;  // MOSFET TRIG/PWM
constexpr uint8_t PIN_BUZZER     = 32;  // NPN Base (1k) -> 5V Buzzer

// ============================================================================
// 2. CONFIGURATION & CALIBRATION
// ============================================================================
constexpr uint8_t  LCD_I2C_ADDR            = 0x27;
constexpr uint8_t  LCD_KOLOM               = 16;
constexpr uint8_t  LCD_BARIS               = 2;
constexpr uint32_t INTERVAL_HALAMAN_LCD_MS = 2500;

constexpr bool     POMPA_AKTIF_HIGH        = true;
constexpr bool     BUZZER_AKTIF_HIGH       = true;

constexpr uint32_t PWM_FREQ_HZ             = 1000;
constexpr uint8_t  PWM_RES_BITS            = 8;
constexpr uint8_t  PWM_CHANNEL_V2          = 0;
constexpr uint32_t PWM_MAX_RAW             = (1UL << PWM_RES_BITS) - 1UL;
constexpr float    PWM_DUTY_MIN_PCT        = 45.0f;
constexpr float    PWM_DUTY_MAX_PCT        = 100.0f;

// Calibrated 2-point linear endpoints from bench measurement
constexpr float    SOIL1_ADC_KERING        = 2545.37f;
constexpr float    SOIL1_ADC_BASAH         = 1011.15f;
constexpr float    SOIL2_ADC_KERING        = 2444.57f;
constexpr float    SOIL2_ADC_BASAH         = 1049.60f;
constexpr uint8_t  SOIL_OVERSAMPLE         = 8;
constexpr float    SOIL_EMA_ALPHA          = 0.25f;
constexpr uint32_t INTERVAL_BACA_SOIL_MS   = 400;

constexpr float    FLOW_PULSA_PER_LITER    = 450.0f;
constexpr int64_t  FLOW_GLITCH_MIN_US      = 2000;
constexpr uint32_t INTERVAL_FLOW_MS        = 1000;

constexpr uint32_t DURASI_SIKLUS_MAKS_MS     = 15000;
constexpr uint32_t WAKTU_PERESAPAN_MS        = 5000;
constexpr uint32_t BATAS_PROTEKSI_SIRAM_MS   = 45000;
constexpr uint32_t TOLERANSI_ALIRAN_AWAL_MS  = 4000;
constexpr uint32_t BATAS_ALIRAN_TERHENTI_MS  = 3000;

constexpr uint32_t DEBOUNCE_TOMBOL_MS      = 35;
constexpr uint32_t DURASI_TEKAN_LAMA_MS    = 1500;
constexpr uint32_t INTERVAL_SERIAL_MS      = 1500;

// Editable per-sensor plant config (persisted in NVS "plant_cfg")
struct SoilConfig {
  char  nama[24];
  float minPct;
  float maxPct;
  bool  enabled;
};

enum StateSistem : uint8_t {
  STANDBY = 0,
  PEMANTAUAN,
  PENYIRAMAN,
  PERESAPAN,
  GANGGUAN
};

enum JenisGangguan : uint8_t {
  GANGGUAN_TIDAK_ADA = 0,
  GANGGUAN_TIDAK_ADA_ALIRAN,
  GANGGUAN_BATAS_WAKTU
};

enum EventTombol : uint8_t {
  TOMBOL_TIDAK_ADA = 0,
  TOMBOL_TEKAN_SINGKAT,
  TOMBOL_TEKAN_LAMA
};

// ============================================================================
// 3. OBJECTS & STATE
// ============================================================================
LiquidCrystal_I2C* lcdPtr = nullptr;
WebServer server(80);
Preferences cfgPrefs;

SoilConfig cfgS1 = { "Soil 1", 40.0f, 60.0f, true };
SoilConfig cfgS2 = { "Soil 2", 60.0f, 78.0f, true };

bool buzzerSaatSiram     = true;   // When soil < min: BOTH Pump + Buzzer ON
bool proteksiFlowAktif   = false;  // Toggleable ON when YF-S201 has water

StateSistem   stateSaatIni        = STANDBY;
JenisGangguan gangguanAktif       = GANGGUAN_TIDAK_ADA;
bool          manualOverride      = false;

bool     soilSudahInit         = false;
float    soil1AdcFilter        = 0.0f;
float    soil2AdcFilter        = 0.0f;
float    soil1Pct              = 0.0f;
float    soil2Pct              = 0.0f;
uint32_t waktuBacaSoilTerakhir = 0;

bool     pompaSedangNyala      = false;
float    dutyPompaAktifPct     = 0.0f;
uint32_t waktuMulaiSiklusMs    = 0;
uint32_t waktuMulaiPeresapanMs = 0;
uint32_t akumulasiWaktuSiramMs = 0;

portMUX_TYPE      flowMux               = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t isrPulsaDebit         = 0;
volatile uint32_t isrPulsaMonitorAliran = 0;
volatile uint64_t isrTotalPulsaVolume   = 0;
volatile int64_t  isrLastPulseUs        = 0;

float    debitLpm                = 0.0f;
float    volumeTotalLiter        = 0.0f;
uint64_t totalPulsaTampil        = 0;
uint32_t waktuHitungFlowTerakhir = 0;
uint32_t waktuPulsaTerakhirMs    = 0;
bool     aliranSudahTerdeteksi   = false;

int      btnBacaTerakhir      = HIGH;
int      btnStabil            = HIGH;
uint32_t btnWaktuUbahMs       = 0;
uint32_t btnWaktuMulaiTekanMs = 0;
bool     btnTekanLamaTerkirim = false;

bool     buzzerSedangBunyi    = false;
bool     buzzerManualBeep     = false;
bool     buzzerManualHold     = false;
uint32_t buzzerBeepMulaiMs    = 0;
uint8_t  alarmLangkah         = 0;
uint32_t alarmWaktuLangkahMs  = 0;

bool     lcdTerdeteksi        = false;
uint8_t  lcdAddrAktif         = 0;
uint8_t  halamanLcd           = 0;
uint32_t waktuGantiHalamanMs  = 0;
uint32_t waktuCetakSerialMs   = 0;
char     cacheBaris0[LCD_KOLOM + 1] = "";
char     cacheBaris1[LCD_KOLOM + 1] = "";

// ============================================================================
// 4. FLOW ISR
// ============================================================================
void IRAM_ATTR isrFlowPulse() {
  int64_t nowUs = esp_timer_get_time();
  portENTER_CRITICAL_ISR(&flowMux);
  if ((nowUs - isrLastPulseUs) >= FLOW_GLITCH_MIN_US) {
    isrLastPulseUs = nowUs;
    isrPulsaDebit++;
    isrPulsaMonitorAliran++;
    isrTotalPulsaVolume++;
  }
  portEXIT_CRITICAL_ISR(&flowMux);
}

// ============================================================================
// 5. NVS CONFIG PERSISTENCE
// ============================================================================
void loadPlantConfig() {
  cfgPrefs.begin("plant_cfg", true);
  String n1 = cfgPrefs.getString("s1_n", "Soil 1");
  n1.toCharArray(cfgS1.nama, sizeof(cfgS1.nama));
  cfgS1.minPct  = constrain(cfgPrefs.getFloat("s1_min", 40.0f), 0.0f, 100.0f);
  cfgS1.maxPct  = constrain(cfgPrefs.getFloat("s1_max", 60.0f), cfgS1.minPct, 100.0f);
  cfgS1.enabled = cfgPrefs.getBool("s1_en", true);

  String n2 = cfgPrefs.getString("s2_n", "Soil 2");
  n2.toCharArray(cfgS2.nama, sizeof(cfgS2.nama));
  cfgS2.minPct  = constrain(cfgPrefs.getFloat("s2_min", 60.0f), 0.0f, 100.0f);
  cfgS2.maxPct  = constrain(cfgPrefs.getFloat("s2_max", 78.0f), cfgS2.minPct, 100.0f);
  cfgS2.enabled = cfgPrefs.getBool("s2_en", true);

  buzzerSaatSiram   = cfgPrefs.getBool("bz_pump", true);
  proteksiFlowAktif = cfgPrefs.getBool("fl_prot", false);
  cfgPrefs.end();
}

void savePlantConfig() {
  cfgS1.minPct = constrain(cfgS1.minPct, 0.0f, 100.0f);
  cfgS1.maxPct = constrain(cfgS1.maxPct, cfgS1.minPct, 100.0f);
  cfgS2.minPct = constrain(cfgS2.minPct, 0.0f, 100.0f);
  cfgS2.maxPct = constrain(cfgS2.maxPct, cfgS2.minPct, 100.0f);

  cfgPrefs.begin("plant_cfg", false);
  cfgPrefs.putString("s1_n", cfgS1.nama);
  cfgPrefs.putFloat("s1_min", cfgS1.minPct);
  cfgPrefs.putFloat("s1_max", cfgS1.maxPct);
  cfgPrefs.putBool("s1_en", cfgS1.enabled);

  cfgPrefs.putString("s2_n", cfgS2.nama);
  cfgPrefs.putFloat("s2_min", cfgS2.minPct);
  cfgPrefs.putFloat("s2_max", cfgS2.maxPct);
  cfgPrefs.putBool("s2_en", cfgS2.enabled);

  cfgPrefs.putBool("bz_pump", buzzerSaatSiram);
  cfgPrefs.putBool("fl_prot", proteksiFlowAktif);
  cfgPrefs.end();
}

// ============================================================================
// 6. HELPERS & ACTUATORS
// ============================================================================
const char* namaState(StateSistem st) {
  switch (st) {
    case STANDBY:    return "STANDBY";
    case PEMANTAUAN: return "PEMANTAUAN";
    case PENYIRAMAN: return "PENYIRAMAN";
    case PERESAPAN:  return "PERESAPAN";
    case GANGGUAN:   return "GANGGUAN";
    default:         return "UNKNOWN";
  }
}

const char* teksGangguanLcd(JenisGangguan g) {
  switch (g) {
    case GANGGUAN_TIDAK_ADA_ALIRAN: return "Tidak ada aliran";
    case GANGGUAN_BATAS_WAKTU:      return "Batas waktu max";
    default:                        return "Tidak ada";
  }
}

void inisialisasiPwmPompa() {
#if ESP32_CORE_V3
  ledcAttach(PIN_POMPA_PWM, PWM_FREQ_HZ, PWM_RES_BITS);
#else
  ledcSetup(PWM_CHANNEL_V2, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttachPin(PIN_POMPA_PWM, PWM_CHANNEL_V2);
#endif
}

void tulisPwmPompa(float dutyPct) {
  float duty = constrain(dutyPct, 0.0f, 100.0f);
  uint32_t raw = (uint32_t)lroundf((duty / 100.0f) * (float)PWM_MAX_RAW);
  if (!POMPA_AKTIF_HIGH) raw = PWM_MAX_RAW - raw;
#if ESP32_CORE_V3
  ledcWrite(PIN_POMPA_PWM, raw);
#else
  ledcWrite(PWM_CHANNEL_V2, raw);
#endif
}

void setPompaOff() {
  pompaSedangNyala  = false;
  dutyPompaAktifPct = 0.0f;
  tulisPwmPompa(0.0f);
}

void setPompaDutyPct(float dutyPct, bool enforceMin) {
  float lo = enforceMin ? constrain(PWM_DUTY_MIN_PCT, 0.0f, 100.0f) : 0.0f;
  float hi = constrain(PWM_DUTY_MAX_PCT, lo, 100.0f);
  dutyPompaAktifPct = constrain(dutyPct, lo, hi);
  pompaSedangNyala  = (dutyPompaAktifPct > 0.0f);
  tulisPwmPompa(dutyPompaAktifPct);
}

void setBuzzer(bool nyala) {
  buzzerSedangBunyi = nyala;
  bool level = BUZZER_AKTIF_HIGH ? nyala : !nyala;
  digitalWrite(PIN_BUZZER, level ? HIGH : LOW);
}

// Compute adaptive duty from the driest enabled sensor's deficit to its maxPct
float hitungDutyAdaptifPerSensor() {
  float maxDeficitFrac = 0.0f;
  if (cfgS1.enabled && soil1Pct < cfgS1.maxPct) {
    float div = (cfgS1.maxPct > 1.0f) ? cfgS1.maxPct : 1.0f;
    float f = constrain((cfgS1.maxPct - soil1Pct) / div, 0.0f, 1.0f);
    if (f > maxDeficitFrac) maxDeficitFrac = f;
  }
  if (cfgS2.enabled && soil2Pct < cfgS2.maxPct) {
    float div = (cfgS2.maxPct > 1.0f) ? cfgS2.maxPct : 1.0f;
    float f = constrain((cfgS2.maxPct - soil2Pct) / div, 0.0f, 1.0f);
    if (f > maxDeficitFrac) maxDeficitFrac = f;
  }
  float minDuty = constrain(PWM_DUTY_MIN_PCT, 0.0f, 100.0f);
  float maxDuty = constrain(PWM_DUTY_MAX_PCT, minDuty, 100.0f);
  return minDuty + maxDeficitFrac * (maxDuty - minDuty);
}

bool adaSensorDiBawahMin() {
  bool dry1 = cfgS1.enabled && (soil1Pct < cfgS1.minPct);
  bool dry2 = cfgS2.enabled && (soil2Pct < cfgS2.minPct);
  return dry1 || dry2;
}

bool semuaSensorMencapaiMax() {
  bool ok1 = !cfgS1.enabled || (soil1Pct >= cfgS1.maxPct);
  bool ok2 = !cfgS2.enabled || (soil2Pct >= cfgS2.maxPct);
  return ok1 && ok2;
}

void ubahState(StateSistem stBaru, const char* alasan) {
  if (stateSaatIni != stBaru) {
    Serial.printf("[STATE] %s -> %s (%s)\n", namaState(stateSaatIni), namaState(stBaru), alasan);
  }
  stateSaatIni = stBaru;
  halamanLcd = 0;
  waktuGantiHalamanMs = millis();
}

void picuGangguan(JenisGangguan jenis, const char* detail) {
  manualOverride = false;
  buzzerManualBeep = false;
  buzzerManualHold = false;
  setPompaOff();
  gangguanAktif = jenis;
  alarmLangkah = 0;
  alarmWaktuLangkahMs = millis();
  setBuzzer(true);
  Serial.printf("[GANGGUAN] %s | %s\n", teksGangguanLcd(jenis), detail);
  ubahState(GANGGUAN, teksGangguanLcd(jenis));
}

void mulaiSiklusPenyiraman(uint32_t now) {
  portENTER_CRITICAL(&flowMux);
  isrPulsaMonitorAliran = 0;
  portEXIT_CRITICAL(&flowMux);

  waktuMulaiSiklusMs    = now;
  waktuPulsaTerakhirMs  = now;
  aliranSudahTerdeteksi = false;

  setPompaDutyPct(hitungDutyAdaptifPerSensor(), true);
  if (buzzerSaatSiram) setBuzzer(true);
  ubahState(PENYIRAMAN, "Sensor tanah < batas minimal");
}

void hentikanKePeresapan(uint32_t now, bool targetTercapai, const char* alasan) {
  if (pompaSedangNyala) akumulasiWaktuSiramMs += (now - waktuMulaiSiklusMs);
  setPompaOff();
  if (!buzzerManualHold && !buzzerManualBeep) setBuzzer(false);
  if (targetTercapai) akumulasiWaktuSiramMs = 0;
  waktuMulaiPeresapanMs = now;
  ubahState(PERESAPAN, alasan);
}

void masukStandby(const char* alasan) {
  manualOverride        = false;
  buzzerManualBeep      = false;
  buzzerManualHold      = false;
  setPompaOff();
  setBuzzer(false);
  gangguanAktif         = GANGGUAN_TIDAK_ADA;
  akumulasiWaktuSiramMs = 0;
  aliranSudahTerdeteksi = false;
  ubahState(STANDBY, alasan);
}

void resetFlowCounters() {
  portENTER_CRITICAL(&flowMux);
  isrPulsaDebit = 0;
  isrPulsaMonitorAliran = 0;
  isrTotalPulsaVolume = 0;
  portEXIT_CRITICAL(&flowMux);
  debitLpm = 0.0f;
  volumeTotalLiter = 0.0f;
  totalPulsaTampil = 0;
}

// ============================================================================
// 7. SENSORS, BUTTON, CONTROL, BUZZER, LCD
// ============================================================================
float bacaAdcOversample(uint8_t pin) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < SOIL_OVERSAMPLE; ++i) sum += analogRead(pin);
  return (float)sum / (float)SOIL_OVERSAMPLE;
}

float konversiKelembapan(float adc, float kering, float basah) {
  float selisih = basah - kering;
  if (fabsf(selisih) < 1.0f) return 0.0f;
  return constrain(100.0f * (adc - kering) / selisih, 0.0f, 100.0f);
}

void bacaKelembapanTanah(uint32_t now, bool paksa) {
  if (!paksa && (now - waktuBacaSoilTerakhir < INTERVAL_BACA_SOIL_MS)) return;
  waktuBacaSoilTerakhir = now;

  float r1 = bacaAdcOversample(PIN_SOIL_1);
  float r2 = bacaAdcOversample(PIN_SOIL_2);
  if (!soilSudahInit) {
    soil1AdcFilter = r1;
    soil2AdcFilter = r2;
    soilSudahInit  = true;
  } else {
    soil1AdcFilter = SOIL_EMA_ALPHA * r1 + (1.0f - SOIL_EMA_ALPHA) * soil1AdcFilter;
    soil2AdcFilter = SOIL_EMA_ALPHA * r2 + (1.0f - SOIL_EMA_ALPHA) * soil2AdcFilter;
  }
  soil1Pct = konversiKelembapan(soil1AdcFilter, SOIL1_ADC_KERING, SOIL1_ADC_BASAH);
  soil2Pct = konversiKelembapan(soil2AdcFilter, SOIL2_ADC_KERING, SOIL2_ADC_BASAH);
}

void perbaruiFlow(uint32_t now) {
  uint32_t pulsaMonitor = 0;
  uint64_t totalSnap = 0;
  portENTER_CRITICAL(&flowMux);
  pulsaMonitor = isrPulsaMonitorAliran;
  isrPulsaMonitorAliran = 0;
  totalSnap = isrTotalPulsaVolume;
  portEXIT_CRITICAL(&flowMux);

  totalPulsaTampil = totalSnap;
  if (pulsaMonitor > 0) {
    waktuPulsaTerakhirMs = now;
    if (stateSaatIni == PENYIRAMAN && pompaSedangNyala) aliranSudahTerdeteksi = true;
  }
  if (FLOW_PULSA_PER_LITER > 0.01f) {
    volumeTotalLiter = (float)((double)totalSnap / (double)FLOW_PULSA_PER_LITER);
  }

  uint32_t selangMs = now - waktuHitungFlowTerakhir;
  if (selangMs >= INTERVAL_FLOW_MS) {
    uint32_t pulsaDebit = 0;
    portENTER_CRITICAL(&flowMux);
    pulsaDebit = isrPulsaDebit;
    isrPulsaDebit = 0;
    portEXIT_CRITICAL(&flowMux);
    waktuHitungFlowTerakhir = now;
    debitLpm = (FLOW_PULSA_PER_LITER > 0.01f)
             ? ((float)pulsaDebit * 60000.0f) / (FLOW_PULSA_PER_LITER * (float)selangMs)
             : 0.0f;
  }
}

EventTombol bacaEventTombol(uint32_t now) {
  int baca = digitalRead(PIN_ENC_SW);
  if (baca != btnBacaTerakhir) { btnWaktuUbahMs = now; btnBacaTerakhir = baca; }
  if ((now - btnWaktuUbahMs) >= DEBOUNCE_TOMBOL_MS && baca != btnStabil) {
    btnStabil = baca;
    if (btnStabil == LOW) {
      btnWaktuMulaiTekanMs = now;
      btnTekanLamaTerkirim = false;
    } else if (!btnTekanLamaTerkirim) {
      return TOMBOL_TEKAN_SINGKAT;
    }
  }
  if (btnStabil == LOW && !btnTekanLamaTerkirim && (now - btnWaktuMulaiTekanMs >= DURASI_TEKAN_LAMA_MS)) {
    btnTekanLamaTerkirim = true;
    return TOMBOL_TEKAN_LAMA;
  }
  return TOMBOL_TIDAK_ADA;
}

void prosesTombol(uint32_t now) {
  EventTombol ev = bacaEventTombol(now);
  if (ev == TOMBOL_TIDAK_ADA) return;
  if (stateSaatIni == GANGGUAN) {
    if (ev == TOMBOL_TEKAN_SINGKAT) masukStandby("Reset alarm via SW");
    return;
  }
  if (manualOverride) {
    masukStandby("Keluar manual via SW");
    return;
  }
  if (stateSaatIni == STANDBY && ev == TOMBOL_TEKAN_SINGKAT) {
    bacaKelembapanTanah(now, true);
    akumulasiWaktuSiramMs = 0;
    ubahState(PEMANTAUAN, "Mulai Auto via SW");
  } else if (ev == TOMBOL_TEKAN_LAMA) {
    masukStandby("Tekan lama via SW");
  }
}

void jalankanKendaliIrigasi(uint32_t now) {
  if (manualOverride) return;

  switch (stateSaatIni) {
    case STANDBY:
      if (pompaSedangNyala) setPompaOff();
      break;

    case PEMANTAUAN:
      if (pompaSedangNyala) setPompaOff();
      if (adaSensorDiBawahMin()) {
        mulaiSiklusPenyiraman(now);
      }
      break;

    case PENYIRAMAN: {
      uint32_t durSiklus = now - waktuMulaiSiklusMs;
      uint32_t totalMs   = akumulasiWaktuSiramMs + durSiklus;

      // Proteksi no-flow (hanya jika diaktifkan di UI)
      if (proteksiFlowAktif) {
        if (!aliranSudahTerdeteksi && durSiklus >= TOLERANSI_ALIRAN_AWAL_MS) {
          picuGangguan(GANGGUAN_TIDAK_ADA_ALIRAN, "Tidak ada pulsa aliran saat start");
          break;
        }
        if (aliranSudahTerdeteksi && (now - waktuPulsaTerakhirMs >= BATAS_ALIRAN_TERHENTI_MS)) {
          picuGangguan(GANGGUAN_TIDAK_ADA_ALIRAN, "Aliran berhenti di tengah penyiraman");
          break;
        }
        if (totalMs >= BATAS_PROTEKSI_SIRAM_MS) {
          picuGangguan(GANGGUAN_BATAS_WAKTU, "Batas waktu penyiraman terlampaui");
          break;
        }
      }

      // Stop when all enabled sensors reach >= maxPct
      if (semuaSensorMencapaiMax()) {
        hentikanKePeresapan(now, true, "Semua sensor mencapai ambang atas");
        break;
      }

      // Keep pump adaptive PWM & buzzer ON while below threshold
      float duty = hitungDutyAdaptifPerSensor();
      if (fabsf(duty - dutyPompaAktifPct) >= 0.5f) setPompaDutyPct(duty, true);
      if (buzzerSaatSiram && !buzzerSedangBunyi) setBuzzer(true);
      if (!buzzerSaatSiram && !buzzerManualHold && !buzzerManualBeep && buzzerSedangBunyi) setBuzzer(false);
      break;
    }

    case PERESAPAN:
      if (pompaSedangNyala) setPompaOff();
      if ((now - waktuMulaiPeresapanMs) >= WAKTU_PERESAPAN_MS) {
        bacaKelembapanTanah(now, true);
        if (adaSensorDiBawahMin()) mulaiSiklusPenyiraman(now);
        else {
          if (semuaSensorMencapaiMax()) akumulasiWaktuSiramMs = 0;
          ubahState(PEMANTAUAN, "Peresapan selesai");
        }
      }
      break;

    case GANGGUAN:
      if (pompaSedangNyala) setPompaOff();
      break;
  }
}

void perbaruiBuzzer(uint32_t now) {
  if (stateSaatIni == GANGGUAN) {
    static const uint16_t DUR[6] = { 200, 200, 200, 200, 200, 1000 };
    static const bool     ST[6]  = { true, false, true, false, true, false };
    if ((now - alarmWaktuLangkahMs) >= DUR[alarmLangkah]) {
      alarmWaktuLangkahMs = now;
      alarmLangkah = (alarmLangkah + 1) % 6;
      setBuzzer(ST[alarmLangkah]);
    }
    return;
  }
  if (buzzerManualBeep && (now - buzzerBeepMulaiMs >= 500)) {
    buzzerManualBeep = false;
    bool keepOn = buzzerManualHold || (!manualOverride && stateSaatIni == PENYIRAMAN && buzzerSaatSiram);
    setBuzzer(keepOn);
  }
}

void tulisBarisLcd(uint8_t baris, const char* teks) {
  if (!lcdTerdeteksi || !lcdPtr) return;
  char buf[LCD_KOLOM + 1];
  uint8_t n = 0;
  while (teks[n] != '\0' && n < LCD_KOLOM) { buf[n] = teks[n]; n++; }
  while (n < LCD_KOLOM) buf[n++] = ' ';
  buf[LCD_KOLOM] = '\0';
  char* cache = (baris == 0) ? cacheBaris0 : cacheBaris1;
  if (strcmp(cache, buf) != 0) {
    lcdPtr->setCursor(0, baris);
    lcdPtr->print(buf);
    strcpy(cache, buf);
  }
}

void perbaruiLCD(uint32_t now) {
  (void)now;
  if (!lcdTerdeteksi || !lcdPtr) return;
  char b0[LCD_KOLOM + 1], b1[LCD_KOLOM + 1];
  if (stateSaatIni == GANGGUAN) {
    snprintf(b0, sizeof(b0), "ALARM! Tekan SW");
    snprintf(b1, sizeof(b1), "%s", teksGangguanLcd(gangguanAktif));
  } else {
    // Row 0: Soil 1 Plant Name (max 11 chars) + Humidity %
    // Row 1: Soil 2 Plant Name (max 11 chars) + Humidity %
    snprintf(b0, sizeof(b0), "%-11.11s %3.0f%%", cfgS1.nama, soil1Pct);
    snprintf(b1, sizeof(b1), "%-11.11s %3.0f%%", cfgS2.nama, soil2Pct);
  }
  tulisBarisLcd(0, b0);
  tulisBarisLcd(1, b1);
}

void initLcdAutoScan() {
  Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
  Wire.setClock(100000);
  Wire.setTimeOut(25);

  Serial.println("[I2C] Scanning SDA=GPIO21, SCL=GPIO22...");
  uint8_t foundAddr = 0;
  int count = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[I2C] Found device at 0x%02X\n", addr);
      ++count;
      // Prefer standard PCF8574/PCF8574A LCD backpack addresses (0x20-0x27, 0x38-0x3F)
      if (foundAddr == 0 || addr == LCD_I2C_ADDR || addr == 0x3F) {
        if ((addr >= 0x20 && addr <= 0x27) || (addr >= 0x38 && addr <= 0x3F) || foundAddr == 0) {
          foundAddr = addr;
        }
      }
    }
  }
  if (count == 0) {
    lcdTerdeteksi = false;
    lcdAddrAktif = 0;
    Serial.println("[LCD] No I2C device found on GPIO21/22 (check VCC 5V, GND, SDA=21, SCL=22, BSS138).");
    return;
  }

  lcdAddrAktif = foundAddr;
  if (lcdPtr) { delete lcdPtr; lcdPtr = nullptr; }
  lcdPtr = new LiquidCrystal_I2C(lcdAddrAktif, LCD_KOLOM, LCD_BARIS);
  lcdPtr->init();
  lcdPtr->backlight();
  lcdPtr->clear();
  cacheBaris0[0] = '\0';
  cacheBaris1[0] = '\0';
  lcdTerdeteksi = true;
  Serial.printf("[LCD] Initialized 16x2 I2C at 0x%02X.\n", lcdAddrAktif);
}

// ============================================================================
// 8. WEB SERVER REST API
// ============================================================================
String jsonEscape(const char* s) {
  String out;
  while (*s) {
    if (*s == '"' || *s == '\\') out += '\\';
    out += *s++;
  }
  return out;
}

String buildStatusJson() {
  String n1 = jsonEscape(cfgS1.nama);
  String n2 = jsonEscape(cfgS2.nama);
  String out = "{";
  out += "\"state\":\"" + String(namaState(stateSaatIni)) + "\",";
  out += "\"fault\":\"" + String(teksGangguanLcd(gangguanAktif)) + "\",";
  out += "\"manual\":" + String(manualOverride ? "true" : "false") + ",";
  out += "\"s1Name\":\"" + n1 + "\",\"s1Min\":" + String(cfgS1.minPct, 0) + ",\"s1Max\":" + String(cfgS1.maxPct, 0) + ",\"s1En\":" + String(cfgS1.enabled ? "true" : "false") + ",";
  out += "\"s2Name\":\"" + n2 + "\",\"s2Min\":" + String(cfgS2.minPct, 0) + ",\"s2Max\":" + String(cfgS2.maxPct, 0) + ",\"s2En\":" + String(cfgS2.enabled ? "true" : "false") + ",";
  out += "\"bzOnPump\":" + String(buzzerSaatSiram ? "true" : "false") + ",";
  out += "\"flowProt\":" + String(proteksiFlowAktif ? "true" : "false") + ",";
  out += "\"s1Pct\":" + String(soil1Pct, 1) + ",\"s1Raw\":" + String(soil1AdcFilter, 0) + ",";
  out += "\"s2Pct\":" + String(soil2Pct, 1) + ",\"s2Raw\":" + String(soil2AdcFilter, 0) + ",";
  out += "\"pumpOn\":" + String(pompaSedangNyala ? "true" : "false") + ",\"dutyPct\":" + String(dutyPompaAktifPct, 1) + ",";
  out += "\"buzzerOn\":" + String(buzzerSedangBunyi ? "true" : "false") + ",";
  out += "\"flowLpm\":" + String(debitLpm, 2) + ",\"volL\":" + String(volumeTotalLiter, 3) + ",\"pulses\":" + String((unsigned long)totalPulsaTampil) + ",";
  char addrHex[8];
  snprintf(addrHex, sizeof(addrHex), "0x%02X", lcdAddrAktif);
  out += "\"lcdOk\":" + String(lcdTerdeteksi ? "true" : "false") + ",\"lcdAddr\":\"" + String(addrHex) + "\",";
  out += "\"ip\":\"" + (WiFi.isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) + "\",";
  out += "\"rssi\":" + String(WiFi.isConnected() ? WiFi.RSSI() : 0) + ",";
  out += "\"uptimeS\":" + String((unsigned long)(millis() / 1000UL));
  out += "}";
  return out;
}

void sendOkWithStatus() {
  server.send(200, "application/json", "{\"ok\":true,\"status\":" + buildStatusJson() + "}");
}

void setupWebRoutes() {
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/api/status", HTTP_GET, []() {
    server.send(200, "application/json", buildStatusJson());
  });

  server.on("/api/config", HTTP_POST, []() {
    if (server.hasArg("s1n")) {
      String n = server.arg("s1n"); n.trim();
      if (n.length() > 0) n.toCharArray(cfgS1.nama, sizeof(cfgS1.nama));
    }
    if (server.hasArg("s1min")) cfgS1.minPct = server.arg("s1min").toFloat();
    if (server.hasArg("s1max")) cfgS1.maxPct = server.arg("s1max").toFloat();
    if (server.hasArg("s1en"))  cfgS1.enabled = (server.arg("s1en") == "1");

    if (server.hasArg("s2n")) {
      String n = server.arg("s2n"); n.trim();
      if (n.length() > 0) n.toCharArray(cfgS2.nama, sizeof(cfgS2.nama));
    }
    if (server.hasArg("s2min")) cfgS2.minPct = server.arg("s2min").toFloat();
    if (server.hasArg("s2max")) cfgS2.maxPct = server.arg("s2max").toFloat();
    if (server.hasArg("s2en"))  cfgS2.enabled = (server.arg("s2en") == "1");

    savePlantConfig();
    Serial.printf("[CFG] S1=%s (%.0f-%.0f%% en=%d) | S2=%s (%.0f-%.0f%% en=%d)\n",
                  cfgS1.nama, cfgS1.minPct, cfgS1.maxPct, cfgS1.enabled,
                  cfgS2.nama, cfgS2.minPct, cfgS2.maxPct, cfgS2.enabled);
    sendOkWithStatus();
  });

  server.on("/api/options", HTTP_POST, []() {
    if (server.hasArg("bzOnPump")) buzzerSaatSiram = (server.arg("bzOnPump") == "1");
    if (server.hasArg("flowProt")) proteksiFlowAktif = (server.arg("flowProt") == "1");
    savePlantConfig();
    sendOkWithStatus();
  });

  server.on("/api/pump", HTTP_POST, []() {
    if (stateSaatIni == GANGGUAN) { sendOkWithStatus(); return; }
    float duty = server.hasArg("duty") ? server.arg("duty").toFloat() : 0.0f;
    manualOverride = true;
    if (duty <= 0.0f) setPompaOff();
    else setPompaDutyPct(duty, false);
    Serial.printf("[WEB] Manual Pump D14 PWM = %.0f%%\n", dutyPompaAktifPct);
    sendOkWithStatus();
  });

  server.on("/api/buzzer", HTTP_POST, []() {
    String mode = server.hasArg("mode") ? server.arg("mode") : "off";
    if (mode == "beep") {
      buzzerManualBeep = true;
      buzzerBeepMulaiMs = millis();
      setBuzzer(true);
    } else if (mode == "on") {
      buzzerManualBeep = false;
      buzzerManualHold = true;
      setBuzzer(true);
    } else {
      buzzerManualBeep = false;
      buzzerManualHold = false;
      setBuzzer(false);
    }
    sendOkWithStatus();
  });

  server.on("/api/action", HTTP_POST, []() {
    String cmd = server.hasArg("cmd") ? server.arg("cmd") : "";
    if (cmd == "stop" || cmd == "standby") {
      masukStandby("STOP/Standby via Web UI");
    } else if (cmd == "start_auto") {
      if (stateSaatIni != GANGGUAN) {
        manualOverride = false;
        buzzerManualHold = false;
        bacaKelembapanTanah(millis(), true);
        akumulasiWaktuSiramMs = 0;
        ubahState(PEMANTAUAN, "Mulai Auto via Web UI");
      }
    } else if (cmd == "reset_alarm") {
      if (stateSaatIni == GANGGUAN) masukStandby("Reset alarm via Web UI");
    } else if (cmd == "reset_vol") {
      resetFlowCounters();
    }
    sendOkWithStatus();
  });

  server.begin();
}

// ============================================================================
// 9. SETUP & LOOP
// ============================================================================
void setup() {
  digitalWrite(PIN_POMPA_PWM, POMPA_AKTIF_HIGH ? LOW : HIGH);
  pinMode(PIN_POMPA_PWM, OUTPUT);
  inisialisasiPwmPompa();
  setPompaOff();

  digitalWrite(PIN_BUZZER, BUZZER_AKTIF_HIGH ? LOW : HIGH);
  pinMode(PIN_BUZZER, OUTPUT);
  setBuzzer(false);

  Serial.begin(115200);
  Serial.println("\n=== IRIGASI PRESISI ADAPTIF + CUSTOM PLANT WEB UI ===");

  loadPlantConfig();
  Serial.printf("[CFG] S1=%s (%.0f-%.0f%%) | S2=%s (%.0f-%.0f%%)\n",
                cfgS1.nama, cfgS1.minPct, cfgS1.maxPct,
                cfgS2.nama, cfgS2.minPct, cfgS2.maxPct);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_SOIL_1, ADC_11db);
  analogSetPinAttenuation(PIN_SOIL_2, ADC_11db);

  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT,  INPUT_PULLUP);
  pinMode(PIN_ENC_SW,  INPUT_PULLUP);

  pinMode(PIN_FLOW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_FLOW), isrFlowPulse, FALLING);

  initLcdAutoScan();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("Irigasi-ESP32", "irigasi123");
  Serial.printf("[AP  ] Hotspot: Irigasi-ESP32 (pass: irigasi123) -> http://%s/\n",
                WiFi.softAPIP().toString().c_str());

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WIFI] Connecting to %s", WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0 < 10000)) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WIFI] CONNECTED! STA Web UI: http://%s/\n", WiFi.localIP().toString().c_str());
  }

  setupWebRoutes();

  resetFlowCounters();
  uint32_t now = millis();
  bacaKelembapanTanah(now, true);
  waktuHitungFlowTerakhir = now;
  waktuGantiHalamanMs     = now;
  waktuCetakSerialMs      = now;
  perbaruiLCD(now);
}

void loop() {
  uint32_t now = millis();

  server.handleClient();
  prosesTombol(now);
  perbaruiFlow(now);
  bacaKelembapanTanah(now, false);
  jalankanKendaliIrigasi(now);
  perbaruiBuzzer(now);
  perbaruiLCD(now);

  if (now - waktuCetakSerialMs >= INTERVAL_SERIAL_MS) {
    waktuCetakSerialMs = now;
    Serial.printf("[DATA] %s | S1(%s)=%.1f%%(min %.0f) S2(%s)=%.1f%%(min %.0f) | Pump(D14)=%s(%.0f%%) Bz(D32)=%s\n",
                  manualOverride ? "MANUAL" : namaState(stateSaatIni),
                  cfgS1.nama, soil1Pct, cfgS1.minPct,
                  cfgS2.nama, soil2Pct, cfgS2.minPct,
                  pompaSedangNyala ? "ON" : "OFF", dutyPompaAktifPct,
                  buzzerSedangBunyi ? "ON" : "OFF");
  }
}
