/*
 * ============================================================================
 * PROYEK : Rancang Bangun Sistem Kendali Irigasi Presisi Adaptif
 *          berdasarkan Jenis Tanaman dan Kelembapan Tanah
 * TARGET : ESP32 dengan ESP32 Arduino Core 3.x (API LEDC Core 3.x)
 * CATATAN: Kode ini dibuat untuk Arduino IDE (Core 3.x) dan BELUM diuji
 *          langsung pada perangkat fisik. Verifikasi tiap komponen sebelum
 *          operasi otomatis.
 * ============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  #define ESP32_CORE_V3 1
#else
  #define ESP32_CORE_V3 0
#endif

// ============================================================================
// 1. KONFIGURASI PIN (SESUAI PEMETAAN PERANGKAT KERAS)
//    Catatan: Tidak ada LED indikator. GPIO13, GPIO14, GPIO33 tidak digunakan.
// ============================================================================
constexpr uint8_t PIN_SOIL_1     = 34;  // Sensor kelembapan tanah 1 (AO, 3.3V)
constexpr uint8_t PIN_SOIL_2     = 35;  // Sensor kelembapan tanah 2 (AO, 3.3V)
constexpr uint8_t PIN_FLOW       = 27;  // YF-S201 (via level shifter ke 3.3V)
constexpr uint8_t PIN_LCD_SDA    = 21;  // LCD 16x2 I2C (via BSS138)
constexpr uint8_t PIN_LCD_SCL    = 22;  // LCD 16x2 I2C (via BSS138)
constexpr uint8_t PIN_ENC_CLK    = 18;  // Rotary encoder KY-040 CLK (3.3V)
constexpr uint8_t PIN_ENC_DT     = 19;  // Rotary encoder KY-040 DT  (3.3V)
constexpr uint8_t PIN_ENC_SW     = 23;  // Rotary encoder KY-040 SW  (3.3V)
constexpr uint8_t PIN_POMPA_PWM  = 14;  // Modul MOSFET TRIG/PWM pompa 12V
constexpr uint8_t PIN_BUZZER     = 32;  // Basis NPN (via R 1k) -> Buzzer 5V

// ============================================================================
// 2. KONFIGURASI PERANGKAT KERAS, KALIBRASI, PWM, & PROTEKSI
//    Semua parameter penting ditempatkan di sini agar mudah dikalibrasi.
// ============================================================================

// --- LCD 16x2 I2C ---
constexpr uint8_t LCD_I2C_ADDR            = 0x27;  // Umumnya 0x27 atau 0x3F
constexpr uint8_t LCD_KOLOM               = 16;
constexpr uint8_t LCD_BARIS               = 2;
constexpr uint32_t INTERVAL_HALAMAN_LCD_MS = 2500; // Pergantian halaman LCD

// --- Polaritas Kontrol Aktuator ---
// PENTING: Kecocokan modul MOSFET terhadap logika 3,3 V ESP32 wajib
// diverifikasi dari datasheet/spesifikasi modul, tidak dapat diperbaiki kode.
constexpr bool POMPA_AKTIF_HIGH           = true;  // false jika modul aktif-LOW
constexpr bool BUZZER_AKTIF_HIGH          = true;  // NPN low-side: HIGH = ON

// --- Pengaturan PWM Pompa (Kompatibel ESP32 Arduino Core 3.x & 2.x) ---
constexpr uint32_t PWM_FREQ_HZ            = 1000;  // Frekuensi PWM (Hz)
constexpr uint8_t  PWM_RES_BITS           = 8;     // Resolusi 8-bit (0..255)
constexpr uint8_t  PWM_CHANNEL_V2         = 0;     // Channel LEDC (khusus Core 2.x)
constexpr uint32_t PWM_MAX_RAW            = (1UL << PWM_RES_BITS) - 1UL;
// Duty cycle minimum agar pompa mulai berputar & tetap mengalir WAJIB diuji
// pada pompa fisik. Duty cycle PWM TIDAK berbanding lurus dengan debit air.
constexpr float    PWM_DUTY_MIN_PCT       = 45.0f; // Contoh awal (%), uji fisik
constexpr float    PWM_DUTY_MAX_PCT       = 100.0f;// Duty cycle maksimum (%)

// --- Kalibrasi Sensor Kelembapan Tanah (Terpisah per Sensor) ---
// Nilai awal ini hanya contoh pengujian; WAJIB dikalibrasi pada tanah aktual.
// Hasil konversi adalah kelembapan relatif (0-100%), BUKAN kadar air volumetrik.
constexpr float SOIL1_ADC_KERING          = 2545.0f; // Contoh ADC kering S1
constexpr float SOIL1_ADC_BASAH           = 1011.0f; // Contoh ADC basah  S1
constexpr float SOIL2_ADC_KERING          = 2445.0f; // Contoh ADC kering S2
constexpr float SOIL2_ADC_BASAH           = 1050.0f; // Contoh ADC basah  S2
constexpr uint8_t  SOIL_OVERSAMPLE        = 8;       // Rata-rata baca ADC
constexpr float    SOIL_EMA_ALPHA         = 0.25f;   // Filter EMA (0.1..0.5)
constexpr uint32_t INTERVAL_BACA_SOIL_MS  = 500;     // Interval baca sensor

// --- Kalibrasi Water Flow Sensor YF-S201 ---
// Rumus debit  : Q (L/menit) = (deltaPulsa * 60000.0) / (PULSA_PER_LITER * deltaMs)
// Rumus volume : V (Liter)   = totalPulsa / PULSA_PER_LITER
// Nilai 450.0 hanya nilai uji awal; WAJIB diverifikasi dengan gelas ukur.
constexpr float    FLOW_PULSA_PER_LITER   = 450.0f;  // Konstanta kalibrasi
constexpr uint32_t INTERVAL_FLOW_MS       = 1000;    // Interval hitung debit

// --- Waktu Siklus Penyiraman, Peresapan, & Proteksi ---
constexpr uint32_t DURASI_SIKLUS_MAKS_MS     = 15000; // Batas 1 siklus normal
constexpr uint32_t WAKTU_PERESAPAN_MS        = 20000; // Jeda peresapan tanah
// Proteksi waktu penyiraman terus-menerus (terpisah dari durasi siklus normal).
// Memantau total waktu aktif pompa tanpa mencapai ambang atas.
constexpr uint32_t BATAS_PROTEKSI_SIRAM_MS   = 45000; // Batas proteksi total
constexpr uint32_t TOLERANSI_ALIRAN_AWAL_MS  = 4000;  // Toleransi awal nyala
constexpr uint32_t BATAS_ALIRAN_TERHENTI_MS  = 3000;  // Aliran berhenti di tengah

// --- Encoder, Tombol, Buzzer, & Serial ---
constexpr uint32_t DEBOUNCE_TOMBOL_MS     = 35;      // Debounce tombol SW
constexpr uint32_t DURASI_TEKAN_LAMA_MS   = 1500;    // Ambang tekan lama
constexpr uint32_t INTERVAL_SERIAL_MS     = 1000;    // Log Serial berkala

// --- Profil Tanaman (Nilai Contoh Pengujian, BUKAN Rekomendasi Agronomis) ---
// Sistem menyesuaikan kendali berdasarkan profil terpilih & kelembapan tanah,
// bukan melalui machine learning, fuzzy logic, atau data cuaca.
struct ProfilTanaman {
  const char* nama;            // Maks 10 karakter agar muat rapi di LCD 16x2
  float       ambangBawahPct;  // Mulai siram jika rata-rata < ambang ini
  float       ambangAtasPct;   // Berhenti siram jika rata-rata >= ambang ini
};

constexpr ProfilTanaman DAFTAR_PROFIL[] = {
  { "Profil 1", 30.0f, 60.0f },  // Contoh pengujian sementara
  { "Profil 2", 40.0f, 70.0f },  // Contoh pengujian sementara
  { "Profil 3", 50.0f, 80.0f }   // Contoh pengujian sementara
};
constexpr uint8_t JUMLAH_PROFIL = sizeof(DAFTAR_PROFIL) / sizeof(DAFTAR_PROFIL[0]);

// ============================================================================
// 3. DEFINISI STATE MACHINE & TIPE DATA
// ============================================================================
enum StateSistem : uint8_t {
  PILIH_PROFIL = 0,
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
// 4. OBJEK & VARIABEL GLOBAL
// ============================================================================
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_KOLOM, LCD_BARIS);

StateSistem   stateSaatIni         = PILIH_PROFIL;
JenisGangguan gangguanAktif        = GANGGUAN_TIDAK_ADA;
uint8_t       indeksProfilDipilih  = 0;

// --- Data Sensor Kelembapan Tanah ---
bool     soilSudahInisialisasi = false;
float    soil1AdcFilter        = 0.0f;
float    soil2AdcFilter        = 0.0f;
float    soil1Pct              = 0.0f;
float    soil2Pct              = 0.0f;
float    kelembapanRata2Pct    = 0.0f;
uint32_t waktuBacaSoilTerakhir = 0;

// --- Data Pompa & Timer Siklus ---
bool     pompaSedangNyala       = false;
float    dutyPompaAktifPct      = 0.0f;
uint32_t waktuMulaiSiklusMs     = 0;
uint32_t waktuMulaiPeresapanMs  = 0;
uint32_t akumulasiWaktuSiramMs  = 0;

// --- Data Aliran Air (YF-S201) dengan Penghitung Terpisah & Aman ISR ---
portMUX_TYPE      flowMux               = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t isrPulsaDebit         = 0;  // Khusus hitung debit periodik
volatile uint32_t isrPulsaMonitorAliran = 0;  // Khusus pantau waktu pulsa akhir
volatile uint64_t isrTotalPulsaVolume   = 0;  // Monotonik, tanpa pembulatan

float    debitLpm                = 0.0f;
float    volumeTotalLiter        = 0.0f;
uint32_t waktuHitungFlowTerakhir = 0;
uint32_t waktuPulsaTerakhirMs    = 0;
bool     aliranSudahTerdeteksi   = false;

// --- Encoder & Tombol ---
uint8_t  encPrevState          = 0;
int8_t   encAkumulator         = 0;
bool     encSudahInit          = false;
int      btnBacaTerakhir       = HIGH;
int      btnStabil             = HIGH;
uint32_t btnWaktuUbahMs        = 0;
uint32_t btnWaktuMulaiTekanMs  = 0;
bool     btnTekanLamaTerkirim  = false;

// --- Buzzer Alarm Non-Blocking ---
bool     buzzerSedangBunyi     = false;
uint8_t  alarmLangkah          = 0;
uint32_t alarmWaktuLangkahMs   = 0;

// --- LCD & Serial ---
bool     lcdTerdeteksi         = false;
uint8_t  halamanLcd            = 0;
uint32_t waktuGantiHalamanMs   = 0;
uint32_t waktuCetakSerialMs    = 0;
char     cacheBaris0[LCD_KOLOM + 1] = "";
char     cacheBaris1[LCD_KOLOM + 1] = "";

// ============================================================================
// 5. INTERRUPT SERVICE ROUTINE (FLOW SENSOR)
// ============================================================================
void IRAM_ATTR isrFlowPulse() {
  portENTER_CRITICAL_ISR(&flowMux);
  isrPulsaDebit++;
  isrPulsaMonitorAliran++;
  isrTotalPulsaVolume++;
  portEXIT_CRITICAL_ISR(&flowMux);
}

// ============================================================================
// 6. FUNGSI UTILITAS NAMA STATE & GANGGUAN
// ============================================================================
const char* namaState(StateSistem st) {
  switch (st) {
    case PILIH_PROFIL: return "PILIH_PROFIL";
    case PEMANTAUAN:   return "PEMANTAUAN";
    case PENYIRAMAN:   return "PENYIRAMAN";
    case PERESAPAN:    return "PERESAPAN";
    case GANGGUAN:     return "GANGGUAN";
    default:           return "TIDAK_DIKENAL";
  }
}

const char* teksGangguanLcd(JenisGangguan g) {
  switch (g) {
    case GANGGUAN_TIDAK_ADA_ALIRAN: return "Tidak ada aliran";  // 16 karakter
    case GANGGUAN_BATAS_WAKTU:      return "Batas waktu max";   // 15 karakter
    default:                        return "Gangguan sistem";
  }
}

// ============================================================================
// 7. FUNGSI KENDALI POMPA & BUZZER (SAFETY-FIRST)
// ============================================================================
void inisialisasiPwmPompa() {
#if ESP32_CORE_V3
  ledcAttach(PIN_POMPA_PWM, PWM_FREQ_HZ, PWM_RES_BITS);
#else
  ledcSetup(PWM_CHANNEL_V2, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttachPin(PIN_POMPA_PWM, PWM_CHANNEL_V2);
#endif
}

void tulisPwmPompa(float dutyPct) {
  float dutyTerbatas = constrain(dutyPct, 0.0f, 100.0f);
  uint32_t nilaiPwm = (uint32_t)lroundf((dutyTerbatas / 100.0f) * (float)PWM_MAX_RAW);
  if (!POMPA_AKTIF_HIGH) {
    nilaiPwm = PWM_MAX_RAW - nilaiPwm;
  }
#if ESP32_CORE_V3
  ledcWrite(PIN_POMPA_PWM, nilaiPwm);
#else
  ledcWrite(PWM_CHANNEL_V2, nilaiPwm);
#endif
}

void setPompaOff() {
  pompaSedangNyala  = false;
  dutyPompaAktifPct = 0.0f;
  tulisPwmPompa(0.0f);
}

void setPompaDutyPct(float dutyPct) {
  float minDuty = constrain(PWM_DUTY_MIN_PCT, 0.0f, 100.0f);
  float maxDuty = constrain(PWM_DUTY_MAX_PCT, minDuty, 100.0f);
  dutyPompaAktifPct = constrain(dutyPct, minDuty, maxDuty);
  pompaSedangNyala  = true;
  tulisPwmPompa(dutyPompaAktifPct);
}

// Perhitungan PWM adaptif sederhana (bukan fuzzy logic / machine learning):
// Semakin jauh kelembapan di bawah ambang atas, duty cycle semakin mendekati
// PWM_DUTY_MAX_PCT; mendekati ambang atas, turun menuju PWM_DUTY_MIN_PCT.
float hitungDutyAdaptifPct(float kelembapanPct, const ProfilTanaman& profil) {
  float minDuty = constrain(PWM_DUTY_MIN_PCT, 0.0f, 100.0f);
  float maxDuty = constrain(PWM_DUTY_MAX_PCT, minDuty, 100.0f);
  float pembagi = (profil.ambangAtasPct > 1.0f) ? profil.ambangAtasPct : 1.0f;
  float fraksi  = constrain((profil.ambangAtasPct - kelembapanPct) / pembagi, 0.0f, 1.0f);
  return minDuty + fraksi * (maxDuty - minDuty);
}

void setBuzzer(bool nyala) {
  buzzerSedangBunyi = nyala;
  bool levelPin = BUZZER_AKTIF_HIGH ? nyala : !nyala;
  digitalWrite(PIN_BUZZER, levelPin ? HIGH : LOW);
}

// ============================================================================
// 8. TRANSISI STATE & PENANGANAN GANGGUAN
// ============================================================================
void ubahState(StateSistem stateBaru, const char* alasan) {
  if (stateSaatIni != stateBaru) {
    Serial.printf("[STATE] %s -> %s (%s)\n",
                  namaState(stateSaatIni), namaState(stateBaru), alasan);
  }
  stateSaatIni = stateBaru;
  halamanLcd = 0;
  waktuGantiHalamanMs = millis();
}

void picuGangguan(JenisGangguan jenis, const char* detailSerial) {
  setPompaOff();
  gangguanAktif = jenis;
  alarmLangkah = 0;
  alarmWaktuLangkahMs = millis();
  setBuzzer(true);
  Serial.printf("[GANGGUAN] %s | %s\n", teksGangguanLcd(jenis), detailSerial);
  ubahState(GANGGUAN, teksGangguanLcd(jenis));
}

void mulaiSiklusPenyiraman(uint32_t now) {
  // Bersihkan pulsa sisa sebelum pompa dinyalakan agar proteksi aliran akurat
  portENTER_CRITICAL(&flowMux);
  isrPulsaMonitorAliran = 0;
  portEXIT_CRITICAL(&flowMux);

  waktuMulaiSiklusMs    = now;
  waktuPulsaTerakhirMs  = now;
  aliranSudahTerdeteksi = false;

  const ProfilTanaman& profil = DAFTAR_PROFIL[indeksProfilDipilih];
  float duty = hitungDutyAdaptifPct(kelembapanRata2Pct, profil);
  setPompaDutyPct(duty);
  ubahState(PENYIRAMAN, "Kelembapan di bawah ambang bawah");
}

void hentikanKePeresapan(uint32_t now, bool targetTercapai, const char* alasan) {
  if (pompaSedangNyala) {
    akumulasiWaktuSiramMs += (now - waktuMulaiSiklusMs);
  }
  setPompaOff();
  if (targetTercapai) {
    akumulasiWaktuSiramMs = 0;  // Target kelembapan tercapai, reset proteksi
  }
  waktuMulaiPeresapanMs = now;
  ubahState(PERESAPAN, alasan);
}

void kembaliKePilihProfil(const char* alasan) {
  setPompaOff();
  setBuzzer(false);
  gangguanAktif         = GANGGUAN_TIDAK_ADA;
  akumulasiWaktuSiramMs = 0;
  aliranSudahTerdeteksi = false;
  ubahState(PILIH_PROFIL, alasan);
}

// ============================================================================
// 9. PEMBACAAN & PENYARINGAN SENSOR KELEMBAPAN TANAH
// ============================================================================
float bacaAdcOversample(uint8_t pin) {
  uint32_t jumlah = 0;
  for (uint8_t i = 0; i < SOIL_OVERSAMPLE; ++i) {
    jumlah += analogRead(pin);
  }
  return (float)jumlah / (float)SOIL_OVERSAMPLE;
}

float konversiKelembapanRelatif(float adc, float adcKering, float adcBasah, const char* label) {
  float selisih = adcBasah - adcKering;
  if (fabsf(selisih) < 1.0f) {
    // Proteksi pembagian dengan nol jika kalibrasi kering == basah
    Serial.printf("[PERINGATAN] Kalibrasi %s tidak valid (ADC kering == basah).\n", label);
    return 0.0f;
  }
  float persen = 100.0f * (adc - adcKering) / selisih;
  return constrain(persen, 0.0f, 100.0f);
}

void bacaKelembapanTanah(uint32_t now, bool paksaBaca) {
  if (!paksaBaca && (now - waktuBacaSoilTerakhir < INTERVAL_BACA_SOIL_MS)) {
    return;
  }
  waktuBacaSoilTerakhir = now;

  float raw1 = bacaAdcOversample(PIN_SOIL_1);
  float raw2 = bacaAdcOversample(PIN_SOIL_2);

  if (!soilSudahInisialisasi) {
    soil1AdcFilter = raw1;
    soil2AdcFilter = raw2;
    soilSudahInisialisasi = true;
  } else {
    soil1AdcFilter = (SOIL_EMA_ALPHA * raw1) + ((1.0f - SOIL_EMA_ALPHA) * soil1AdcFilter);
    soil2AdcFilter = (SOIL_EMA_ALPHA * raw2) + ((1.0f - SOIL_EMA_ALPHA) * soil2AdcFilter);
  }

  soil1Pct = konversiKelembapanRelatif(soil1AdcFilter, SOIL1_ADC_KERING, SOIL1_ADC_BASAH, "S1");
  soil2Pct = konversiKelembapanRelatif(soil2AdcFilter, SOIL2_ADC_KERING, SOIL2_ADC_BASAH, "S2");
  // Kedua sensor memantau satu area penyiraman yang dilayani satu pompa.
  kelembapanRata2Pct = 0.5f * (soil1Pct + soil2Pct);
}

// ============================================================================
// 10. PEMANTAUAN ALIRAN AIR (YF-S201)
// ============================================================================
void perbaruiFlow(uint32_t now) {
  // 1) Pantau pulsa terbaru untuk deteksi no-flow (terpisah dari hitung debit)
  uint32_t pulsaMonitor = 0;
  uint64_t totalPulsaSnapshot = 0;

  portENTER_CRITICAL(&flowMux);
  pulsaMonitor = isrPulsaMonitorAliran;
  isrPulsaMonitorAliran = 0;
  totalPulsaSnapshot = isrTotalPulsaVolume;
  portEXIT_CRITICAL(&flowMux);

  if (pulsaMonitor > 0) {
    waktuPulsaTerakhirMs = now;
    if (stateSaatIni == PENYIRAMAN && pompaSedangNyala) {
      aliranSudahTerdeteksi = true;
    }
  }

  // Hitung volume langsung dari total pulsa agar tidak hilang karena pembulatan
  if (FLOW_PULSA_PER_LITER > 0.01f) {
    volumeTotalLiter = (float)((double)totalPulsaSnapshot / (double)FLOW_PULSA_PER_LITER);
  }

  // 2) Hitung debit periodik (liter/menit) menggunakan penghitung debit terpisah
  uint32_t selangMs = now - waktuHitungFlowTerakhir;
  if (selangMs >= INTERVAL_FLOW_MS) {
    uint32_t pulsaDebit = 0;
    portENTER_CRITICAL(&flowMux);
    pulsaDebit = isrPulsaDebit;
    isrPulsaDebit = 0;
    portEXIT_CRITICAL(&flowMux);

    waktuHitungFlowTerakhir = now;
    if (FLOW_PULSA_PER_LITER > 0.01f) {
      debitLpm = ((float)pulsaDebit * 60000.0f) / (FLOW_PULSA_PER_LITER * (float)selangMs);
    } else {
      debitLpm = 0.0f;
    }
  }
}

// ============================================================================
// 11. ROTARY ENCODER & TOMBOL (DEBOUNCE + TEKAN SINGKAT / LAMA)
// ============================================================================
int8_t bacaLangkahEncoder() {
  static const int8_t TABEL_TRANSISI[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
  };
  uint8_t state = ((uint8_t)digitalRead(PIN_ENC_CLK) << 1) | (uint8_t)digitalRead(PIN_ENC_DT);
  if (!encSudahInit) {
    encPrevState = state;
    encSudahInit = true;
    return 0;
  }
  encAkumulator += TABEL_TRANSISI[(encPrevState << 2) | state];
  encPrevState = state;

  // KY-040 umumnya 4 transisi per detent (ubah ke 2 jika encoder 2-step)
  if (encAkumulator >= 4) {
    encAkumulator = 0;
    return 1;
  }
  if (encAkumulator <= -4) {
    encAkumulator = 0;
    return -1;
  }
  return 0;
}

EventTombol bacaEventTombol(uint32_t now) {
  int baca = digitalRead(PIN_ENC_SW);
  if (baca != btnBacaTerakhir) {
    btnWaktuUbahMs = now;
    btnBacaTerakhir = baca;
  }

  if ((now - btnWaktuUbahMs) >= DEBOUNCE_TOMBOL_MS && baca != btnStabil) {
    btnStabil = baca;
    if (btnStabil == LOW) {
      btnWaktuMulaiTekanMs = now;
      btnTekanLamaTerkirim = false;
    } else {
      // Dilepas: picu tekan singkat HANYA jika tekan lama belum terjadi
      if (!btnTekanLamaTerkirim) {
        return TOMBOL_TEKAN_SINGKAT;
      }
    }
  }

  // Ditahan: picu tekan lama tepat satu kali saat durasi tercapai
  if (btnStabil == LOW && !btnTekanLamaTerkirim &&
      (now - btnWaktuMulaiTekanMs >= DURASI_TEKAN_LAMA_MS)) {
    btnTekanLamaTerkirim = true;
    return TOMBOL_TEKAN_LAMA;
  }

  return TOMBOL_TIDAK_ADA;
}

void prosesEncoderDanTombol(uint32_t now) {
  int8_t langkah = bacaLangkahEncoder();
  EventTombol ev = bacaEventTombol(now);

  // Putaran encoder hanya mengganti profil saat berada di menu PILIH_PROFIL
  if (stateSaatIni == PILIH_PROFIL && langkah != 0) {
    int baru = (int)indeksProfilDipilih + (int)langkah;
    if (baru < 0) baru = JUMLAH_PROFIL - 1;
    if (baru >= JUMLAH_PROFIL) baru = 0;
    indeksProfilDipilih = (uint8_t)baru;
    Serial.printf("[MENU] Pilih %s (%.0f%% - %.0f%%)\n",
                  DAFTAR_PROFIL[indeksProfilDipilih].nama,
                  DAFTAR_PROFIL[indeksProfilDipilih].ambangBawahPct,
                  DAFTAR_PROFIL[indeksProfilDipilih].ambangAtasPct);
  }

  if (ev == TOMBOL_TIDAK_ADA) {
    return;
  }

  if (stateSaatIni == GANGGUAN) {
    // Saat gangguan: HANYA tekan singkat yang mereset alarm.
    // Tekan lama tidak boleh melewati penguncian gangguan.
    if (ev == TOMBOL_TEKAN_SINGKAT) {
      Serial.println("[ALARM] Gangguan diakui & direset oleh pengguna.");
      kembaliKePilihProfil("Reset alarm gangguan");
    } else if (ev == TOMBOL_TEKAN_LAMA) {
      Serial.println("[ALARM] Tekan lama diabaikan; gunakan tekan singkat untuk reset.");
    }
    return;
  }

  if (stateSaatIni == PILIH_PROFIL) {
    if (ev == TOMBOL_TEKAN_SINGKAT) {
      bacaKelembapanTanah(now, true);
      akumulasiWaktuSiramMs = 0;
      Serial.printf("[KONFIRMASI] %s aktif (Bawah=%.0f%%, Atas=%.0f%%)\n",
                    DAFTAR_PROFIL[indeksProfilDipilih].nama,
                    DAFTAR_PROFIL[indeksProfilDipilih].ambangBawahPct,
                    DAFTAR_PROFIL[indeksProfilDipilih].ambangAtasPct);
      ubahState(PEMANTAUAN, "Profil dikonfirmasi pengguna");
    }
    return;
  }

  // Saat operasi normal (PEMANTAUAN, PENYIRAMAN, PERESAPAN):
  // Tekan lama menghentikan pompa dan kembali ke menu pemilihan profil.
  if (ev == TOMBOL_TEKAN_LAMA) {
    kembaliKePilihProfil("Tekan lama oleh pengguna");
  }
}

// ============================================================================
// 12. LOGIKA KENDALI IRIGASI & PROTEKSI (STATE MACHINE)
// ============================================================================
void jalankanKendaliIrigasi(uint32_t now) {
  const ProfilTanaman& profil = DAFTAR_PROFIL[indeksProfilDipilih];

  switch (stateSaatIni) {
    case PILIH_PROFIL:
      if (pompaSedangNyala) setPompaOff();
      break;

    case PEMANTAUAN:
      if (pompaSedangNyala) setPompaOff();
      // Hysteresis: mulai siram hanya jika rata-rata < ambang bawah
      if (kelembapanRata2Pct < profil.ambangBawahPct) {
        mulaiSiklusPenyiraman(now);
      }
      break;

    case PENYIRAMAN: {
      uint32_t durasiSiklusMs = now - waktuMulaiSiklusMs;
      uint32_t totalSiramMs   = akumulasiWaktuSiramMs + durasiSiklusMs;

      // 1) Proteksi batas waktu penyiraman terus-menerus
      if (totalSiramMs >= BATAS_PROTEKSI_SIRAM_MS) {
        picuGangguan(GANGGUAN_BATAS_WAKTU, "Durasi penyiraman melampaui batas proteksi");
        break;
      }

      // 2) Proteksi tidak ada aliran (baik saat awal nyala maupun berhenti di tengah)
      if (!aliranSudahTerdeteksi && durasiSiklusMs >= TOLERANSI_ALIRAN_AWAL_MS) {
        picuGangguan(GANGGUAN_TIDAK_ADA_ALIRAN, "Tidak ada pulsa aliran setelah pompa menyala");
        break;
      }
      if (aliranSudahTerdeteksi && (now - waktuPulsaTerakhirMs >= BATAS_ALIRAN_TERHENTI_MS)) {
        picuGangguan(GANGGUAN_TIDAK_ADA_ALIRAN, "Pulsa aliran berhenti di tengah penyiraman");
        break;
      }

      // 3) Hysteresis: hentikan penyiraman jika kelembapan >= ambang atas
      if (kelembapanRata2Pct >= profil.ambangAtasPct) {
        hentikanKePeresapan(now, true, "Kelembapan mencapai ambang atas");
        break;
      }

      // 4) Batas durasi satu siklus normal -> jeda peresapan sebelum evaluasi ulang
      if (durasiSiklusMs >= DURASI_SIKLUS_MAKS_MS) {
        hentikanKePeresapan(now, false, "Batas durasi 1 siklus tercapai");
        break;
      }

      // 5) Sesuaikan duty cycle PWM secara adaptif berdasarkan kelembapan terkini
      float dutyBaru = hitungDutyAdaptifPct(kelembapanRata2Pct, profil);
      if (fabsf(dutyBaru - dutyPompaAktifPct) >= 0.5f) {
        setPompaDutyPct(dutyBaru);
      }
      break;
    }

    case PERESAPAN:
      if (pompaSedangNyala) setPompaOff();
      if ((now - waktuMulaiPeresapanMs) >= WAKTU_PERESAPAN_MS) {
        // Evaluasi ulang kelembapan setelah waktu peresapan selesai
        bacaKelembapanTanah(now, true);
        if (kelembapanRata2Pct < profil.ambangBawahPct) {
          mulaiSiklusPenyiraman(now);
        } else {
          if (kelembapanRata2Pct >= profil.ambangAtasPct) {
            akumulasiWaktuSiramMs = 0;
          }
          ubahState(PEMANTAUAN, "Peresapan selesai, kelembapan cukup");
        }
      }
      break;

    case GANGGUAN:
      // Pastikan pompa tetap terkunci mati selama gangguan aktif
      if (pompaSedangNyala) setPompaOff();
      break;
  }
}

// ============================================================================
// 13. POLA ALARM BUZZER NON-BLOCKING
// ============================================================================
void perbaruiAlarmBuzzer(uint32_t now) {
  if (stateSaatIni != GANGGUAN) {
    if (buzzerSedangBunyi) setBuzzer(false);
    return;
  }

  // Pola 3 bunyi pendek (200 ms ON / 200 ms OFF) diikuti jeda 1000 ms
  static const uint16_t DURASI_POLA[6] = { 200, 200, 200, 200, 200, 1000 };
  static const bool     STATUS_POLA[6] = { true, false, true, false, true, false };

  if ((now - alarmWaktuLangkahMs) >= DURASI_POLA[alarmLangkah]) {
    alarmWaktuLangkahMs = now;
    alarmLangkah = (alarmLangkah + 1) % 6;
    setBuzzer(STATUS_POLA[alarmLangkah]);
  }
}

// ============================================================================
// 14. TAMPILAN LCD 16x2 TANPA KEDIPAN
// ============================================================================
void tulisBarisLcd(uint8_t baris, const char* teks) {
  if (!lcdTerdeteksi) {
    return;
  }
  char buf[LCD_KOLOM + 1];
  uint8_t len = 0;
  while (teks[len] != '\0' && len < LCD_KOLOM) {
    buf[len] = teks[len];
    len++;
  }
  while (len < LCD_KOLOM) {
    buf[len++] = ' ';  // Timpa sisa karakter lama agar tidak perlu lcd.clear()
  }
  buf[LCD_KOLOM] = '\0';

  char* cache = (baris == 0) ? cacheBaris0 : cacheBaris1;
  if (strcmp(cache, buf) != 0) {
    lcd.setCursor(0, baris);
    lcd.print(buf);
    strcpy(cache, buf);
  }
}

void perbaruiLCD(uint32_t now) {
  char b0[LCD_KOLOM + 1];
  char b1[LCD_KOLOM + 1];
  const ProfilTanaman& profil = DAFTAR_PROFIL[indeksProfilDipilih];

  if (stateSaatIni == PILIH_PROFIL) {
    snprintf(b0, sizeof(b0), "Pilih Profil:");
    snprintf(b1, sizeof(b1), ">%-8s %.0f-%.0f%%",
             profil.nama, profil.ambangBawahPct, profil.ambangAtasPct);
    tulisBarisLcd(0, b0);
    tulisBarisLcd(1, b1);
    return;
  }

  if (stateSaatIni == GANGGUAN) {
    snprintf(b0, sizeof(b0), "ALARM! Tekan SW");
    snprintf(b1, sizeof(b1), "%s", teksGangguanLcd(gangguanAktif));
    tulisBarisLcd(0, b0);
    tulisBarisLcd(1, b1);
    return;
  }

  // Halaman bergantian saat PEMANTAUAN, PENYIRAMAN, PERESAPAN
  if ((now - waktuGantiHalamanMs) >= INTERVAL_HALAMAN_LCD_MS) {
    waktuGantiHalamanMs = now;
    halamanLcd = (halamanLcd + 1) % 4;
  }

  switch (halamanLcd) {
    case 0:
      snprintf(b0, sizeof(b0), "%-10s", profil.nama);
      snprintf(b1, sizeof(b1), "Rata2: %5.1f%%", kelembapanRata2Pct);
      break;
    case 1:
      snprintf(b0, sizeof(b0), "S1:%5.1f%%", soil1Pct);
      snprintf(b1, sizeof(b1), "S2:%5.1f%%", soil2Pct);
      break;
    case 2: {
      const char* tagState = (stateSaatIni == PENYIRAMAN) ? "SIRAM"
                           : (stateSaatIni == PERESAPAN)  ? "RESAP"
                                                          : "PANTAU";
      snprintf(b0, sizeof(b0), "Pompa:%-3s %6s", pompaSedangNyala ? "ON" : "OFF", tagState);
      snprintf(b1, sizeof(b1), "PWM  : %5.1f%%", dutyPompaAktifPct);
      break;
    }
    case 3:
    default:
      snprintf(b0, sizeof(b0), "Flow:%5.2f L/m", debitLpm);
      snprintf(b1, sizeof(b1), "Vol :%5.2f L", volumeTotalLiter);
      break;
  }

  tulisBarisLcd(0, b0);
  tulisBarisLcd(1, b1);
}

// ============================================================================
// 15. LOG SERIAL BERKALA (115200 BAUD)
// ============================================================================
void cetakSerialBerkala(uint32_t now) {
  if ((now - waktuCetakSerialMs) < INTERVAL_SERIAL_MS) {
    return;
  }
  waktuCetakSerialMs = now;

  const ProfilTanaman& profil = DAFTAR_PROFIL[indeksProfilDipilih];
  Serial.printf("[DATA] State=%s | %s | S1=%.1f%%(%.0f) S2=%.1f%%(%.0f) Avg=%.1f%% | Pompa=%s(%.0f%%) | Flow=%.2f L/m Vol=%.3f L\n",
                namaState(stateSaatIni),
                profil.nama,
                soil1Pct, soil1AdcFilter,
                soil2Pct, soil2AdcFilter,
                kelembapanRata2Pct,
                pompaSedangNyala ? "ON" : "OFF",
                dutyPompaAktifPct,
                debitLpm,
                volumeTotalLiter);
}

// ============================================================================
// 16. SETUP & LOOP UTAMA
// ============================================================================
void setup() {
  // 1) Pastikan pompa dan buzzer MATI sejak instruksi paling awal
  digitalWrite(PIN_POMPA_PWM, POMPA_AKTIF_HIGH ? LOW : HIGH);
  pinMode(PIN_POMPA_PWM, OUTPUT);
  inisialisasiPwmPompa();
  setPompaOff();

  digitalWrite(PIN_BUZZER, BUZZER_AKTIF_HIGH ? LOW : HIGH);
  pinMode(PIN_BUZZER, OUTPUT);
  setBuzzer(false);

  // 2) Serial Monitor
  Serial.begin(115200);
  Serial.println("\n=== SISTEM KENDALI IRIGASI PRESISI ADAPTIF (ESP32 Core 3.x) ===");

  // 3) Konfigurasi ADC untuk kedua sensor kelembapan tanah
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_SOIL_1, ADC_11db);
  analogSetPinAttenuation(PIN_SOIL_2, ADC_11db);

  // 4) Inisialisasi Rotary Encoder KY-040
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT,  INPUT_PULLUP);
  pinMode(PIN_ENC_SW,  INPUT_PULLUP);

  // 5) Inisialisasi Water Flow Sensor YF-S201 (Interrupt)
  pinMode(PIN_FLOW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_FLOW), isrFlowPulse, FALLING);

  // 6) Cek & inisialisasi LCD 16x2 I2C (non-blocking jika LCD belum terhubung)
  Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
  Wire.setTimeOut(20);
  Wire.beginTransmission(LCD_I2C_ADDR);
  uint8_t i2cErr = Wire.endTransmission();
  if (i2cErr == 0) {
    lcdTerdeteksi = true;
    lcd.init();
    lcd.backlight();
    lcd.clear();
    Serial.printf("[LCD] Terdeteksi di 0x%02X.\n", LCD_I2C_ADDR);
  } else {
    lcdTerdeteksi = false;
    Serial.printf("[LCD] Tidak merespons di 0x%02X (err=%u); cek kabel SDA/SCL/BSS138/alamat. Lanjut tanpa LCD.\n",
                  LCD_I2C_ADDR, i2cErr);
  }

  // 7) Pembacaan awal sensor & masuk ke menu pemilihan profil (pompa tetap OFF)
  uint32_t now = millis();
  bacaKelembapanTanah(now, true);
  waktuHitungFlowTerakhir = now;
  waktuGantiHalamanMs     = now;
  waktuCetakSerialMs      = now;
  stateSaatIni            = PILIH_PROFIL;
  perbaruiLCD(now);

  Serial.println("[INIT] Selesai. Pompa OFF. Putar encoder & tekan SW untuk memilih profil.");
}

void loop() {
  uint32_t now = millis();

  prosesEncoderDanTombol(now);
  perbaruiFlow(now);
  bacaKelembapanTanah(now, false);
  jalankanKendaliIrigasi(now);
  perbaruiAlarmBuzzer(now);
  perbaruiLCD(now);
  cetakSerialBerkala(now);
}
