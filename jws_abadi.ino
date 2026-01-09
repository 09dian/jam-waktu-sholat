#include <SPI.h>
#include <DMD.h>
#include <TimerOne.h>
#include "SystemFont5x7.h"
#include "Arial_Black_16_ISO_8859_1.h"
#include <Wire.h>
#include <RTClib.h>
#include <PrayerTimes.h>
#include <string.h>

#define DISPLAYS_ACROSS 3
#define DISPLAYS_DOWN 1
DMD dmd(DISPLAYS_ACROSS, DISPLAYS_DOWN);

const int buzzerPin = 4;
RTC_DS3231 rtc;
PrayerTimes pt(-7.258163014836239, 107.84373863655642, 7); // kordinat smk 9 garut GMT+7

void ScanDMD() { dmd.scanDisplayBySPI(); }

const char* namaSholat[] = {"IMSYAK","SUBUH","SYURUQ","DUHA","DZUHUR","ASHAR","MAGHRIB","ISYA"};

struct Jadwal { int h; int m; } jadwal[8];
bool sudahAdzan[8] = {false,false,false,false,false,false,false,false};

// ===== Adzan =====
bool sedangAdzan = false;
unsigned long adzanStart = 0;
const int durasiAdzan = 10000; // 10 detik

// ===== Iqomah =====
bool sedangHitungIqomah = false;
unsigned long iqomahStart = 0;
int iqomahDurasi[8] = {0,300,0,0,300,300,300,300};
int iqomahSholatIndex = -1;

// ===== Titik-titik =====
bool sedangTitik = false;
unsigned long titikStart = 0;
const int durasiTitik = 600; // 10 menit

// ===== Reset harian =====
int lastPrintedDay = -1;

// ===== Mode tampilan bergantian =====
enum ModeTampil { MODE_SCROLLINFO, MODE_KHOTIB, MODE_JADWAL, MODE_TANGGAL, MODE_JAM_BESAR };
ModeTampil modeSekarang = MODE_SCROLLINFO;
unsigned long modeStart = 0;
const unsigned long durasiMode = 30000; // 30 detik per mode

// ===== Jadwal sholat bergantian =====
int sholatIndexSekarang = 0;
unsigned long jadwalSholatStart = 0;
const unsigned long durasiPerSholat = 3000; // 3 detik per sholat

// ===== Khotib index =====
int indexKhotib = 0;

// ===== Utility font kecil =====
int approxWidth5x7(const char* s){ return 6 * strlen(s); }

void hitungJadwalSholat(DateTime now){
    int fajrH,fajrM,sunriseH,sunriseM,dhuhrH,dhuhrM,
        asrH,asrM,maghribH,maghribM,ishaH,ishaM;

    // PrayerTimes library: calculate(day, month, year, &fajrH, &fajrM, ...)
    pt.calculate(now.day(), now.month(), now.year(),
                 fajrH,fajrM,
                 sunriseH,sunriseM,
                 dhuhrH,dhuhrM,
                 asrH,asrM,
                 maghribH,maghribM,
                 ishaH,ishaM);

    int imsyakH = fajrH;
    int imsyakM = fajrM - 10;
    if(imsyakM < 0){ imsyakH--; imsyakM += 60; if(imsyakH < 0) imsyakH += 24; }

    int duhaTotal = sunriseM + 30;
    int duhaH = sunriseH + duhaTotal / 60;
    int duhaM = duhaTotal % 60;
    if(duhaH >= 24) duhaH -= 24;

    jadwal[0] = {imsyakH, imsyakM};
    jadwal[1] = {fajrH, fajrM};
    jadwal[2] = {sunriseH, sunriseM};
    jadwal[3] = {duhaH, duhaM};
    jadwal[4] = {dhuhrH, dhuhrM};
    jadwal[5] = {asrH, asrM};
    jadwal[6] = {maghribH, maghribM};
    jadwal[7] = {ishaH, ishaM};
}

void printJadwal(DateTime now){
    Serial.print("Tanggal: "); Serial.print(now.day()); Serial.print("/"); Serial.print(now.month()); Serial.print("/"); Serial.println(now.year());
    Serial.println("Jadwal Sholat Hari Ini:");
    for(int i=0;i<8;i++){
        char waktu[6]; sprintf(waktu,"%02d:%02d",jadwal[i].h,jadwal[i].m);
        Serial.print(" - "); Serial.print(namaSholat[i]); Serial.print(" : "); Serial.println(waktu);
    }
    Serial.println("============================");
}

// ===== Fungsi tampilan =====
void showIqomahCenter(const char* line1,const char* line2){
    dmd.clearScreen(true);
    dmd.selectFont(SystemFont5x7);
    int fontH = 7, spacing = 2;
    int totalH = fontH + spacing + fontH;
    int y0 = (16 - totalH) / 2; if(y0 < 0) y0 = 0;
    int y1 = y0 + fontH + spacing;

    int w1 = approxWidth5x7(line1), w2 = approxWidth5x7(line2);
    int x1 = (96 - w1) / 2; if(x1 < 0) x1 = 0;
    int x2 = (96 - w2) / 2; if(x2 < 0) x2 = 0;

    dmd.drawString(x1, y0, (char*)line1, strlen(line1), GRAPHICS_NORMAL);
    dmd.drawString(x2, y1, (char*)line2, strlen(line2), GRAPHICS_NORMAL);
}

void showTextCenterSmall(const char* text){
    dmd.clearScreen(true);
    dmd.selectFont(SystemFont5x7);
    int w = approxWidth5x7(text), x = (96 - w) / 2; if(x < 0) x = 0;
    int y = (16 - 7) / 2;
    dmd.drawString(x, y, (char*)text, strlen(text), GRAPHICS_NORMAL);
}

void showDotsCenter(){
    static bool dotsVisible = false;
    static unsigned long lastBlink = 0;
    const unsigned long blinkInterval = 300;
    unsigned long now = millis();
    if(now - lastBlink >= blinkInterval){
        lastBlink = now; dotsVisible = !dotsVisible;
        dmd.clearScreen(true);
        if(dotsVisible) showTextCenterSmall(".");
    }
}

void showScrollTextBoldCenter(const char* text, int speedDelay){
    dmd.clearScreen(true);
    dmd.selectFont(Arial_Black_16_ISO_8859_1);
    int startX = 32 * DISPLAYS_ACROSS; // 96
    int y = 0;
    dmd.drawMarquee((char*)text, strlen(text), startX, y);

    unsigned long timer = millis();
    boolean finished = false;
    
    while(!finished){
        if((timer + (unsigned long)speedDelay) < millis()){
            finished = dmd.stepMarquee(-1,0);
            timer = millis();
        }
        // Allow the DMD scanning interrupt to keep running (Timer1 handles it).
        // Keep this loop blocking intentionally while marquee runs.
    }
}

char lastTime[9] = "";
void showClockDMD(DateTime now){
    char buf[9]; sprintf(buf, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    if(strcmp(buf, lastTime) != 0){
        strcpy(lastTime, buf);
        dmd.clearScreen(true);
        dmd.selectFont(Arial_Black_16_ISO_8859_1);
        int textWidth = 8 * strlen(buf);
        int x = (96 - textWidth) / 2 + 6; if(x < 0) x = 0;
        dmd.drawString(x, 0, buf, strlen(buf), GRAPHICS_NORMAL);
    }
}

void showTanggalJam(){
    DateTime now = rtc.now();
    char tanggal[20], jam[20];
    sprintf(tanggal, "%02d:%02d:%04d", now.day(), now.month(), now.year());
    sprintf(jam,     "%02d:%02d:%02d", now.hour(), now.minute(), now.second());

    dmd.clearScreen(true);
    dmd.selectFont(SystemFont5x7);

    int totalWidth = 32 * DISPLAYS_ACROSS;
    int tWidth = strlen(tanggal) * 6, jWidth = strlen(jam) * 6;
    int xTanggal = (totalWidth - tWidth) / 2, xJam = (totalWidth - jWidth) / 2;
    dmd.drawString(xTanggal, 0, tanggal, strlen(tanggal), GRAPHICS_NORMAL);
    dmd.drawString(xJam, 8, jam, strlen(jam), GRAPHICS_NORMAL);
}

void showJadwalSekarang(int sholatIndex){
    if(sholatIndex < 0 || sholatIndex > 7) return;

    dmd.clearScreen(true);
    dmd.selectFont(SystemFont5x7);

    char buf1[40]; sprintf(buf1, "%s : %02d:%02d", namaSholat[sholatIndex], jadwal[sholatIndex].h, jadwal[sholatIndex].m);
    int w1 = approxWidth5x7(buf1);
    int x1 = (96 - w1) / 2; if(x1 < 0) x1 = 0;
    dmd.drawString(x1, 0, buf1, strlen(buf1), GRAPHICS_NORMAL);

    DateTime now = rtc.now();
    char buf2[16]; sprintf(buf2, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    int w2 = approxWidth5x7(buf2);
    int x2 = (96 - w2) / 2; if(x2 < 0) x2 = 0;
    dmd.drawString(x2, 8, buf2, strlen(buf2), GRAPHICS_NORMAL);
}

// ==================== Jadwal Khotib ====================
const char* khotibJumat[] = {
  "Bapak Hendi Hidayat, S.Pd., M.Pd",
  "Bapak Atep Idrus, SHi.",
  "Bapak Cecep Saepul Rohmat, M.Pdi",
  "Bapak Aa Saaduddin Akbar, S.Pdi"
};
const int jumlahKhotib = sizeof(khotibJumat) / sizeof(khotibJumat[0]);

void setup(){
    pinMode(buzzerPin, OUTPUT); digitalWrite(buzzerPin, LOW);
    Serial.begin(115200); delay(50);
    if(!rtc.begin()){ Serial.println("RTC tidak terdeteksi!"); while(1); }
    if(rtc.lostPower()){ rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); }
    
//rtc.adjust(DateTime(2025, 9, 11, 11, 48, 10)); //cek manual untuk cek waktu sholat

  pt.setCalculationMethod(ISNA);   // Sudah kamu modif: 20° / 18° / Syafi’i
  pt.setAdjustments(0,0,0,0,0,0);  // TANPA offset


    Timer1.initialize(5000);
    Timer1.attachInterrupt(ScanDMD);
    dmd.clearScreen(true);
    Serial.println("Sistem Adzan & Iqomah siap");

    // Hitung jadwal awal
    DateTime now = rtc.now();
    hitungJadwalSholat(now);
    printJadwal(now);

    // Awal langsung MODE_SCROLLINFO
    modeSekarang = MODE_SCROLLINFO;
    modeStart = millis();
    sholatIndexSekarang = 0;
    jadwalSholatStart = millis();
    lastPrintedDay = now.day(); // set awal supaya tidak double-recalculate di detik pertama
}
bool isWaktuSholat(DateTime now) {
  int jam = now.hour();
  int menit = now.minute();

  // IMSYAK 04:00
  if (jam == 4 && menit == 0) return true;
  // SUBUH 04:00
  if (jam == 4 && menit == 0) return true;
  // DZUHUR 11:40
  if (jam == 11 && menit == 40) return true;
  // ASHAR 14:40
  if (jam == 14 && menit == 40) return true;
  // MAGHRIB 17:30
  if (jam == 17 && menit == 30) return true;
  // ISYA 18:40
  if (jam == 18 && menit == 40) return true;

  return false;
}
void loop(){

    static unsigned long prevMillis = 0;
    unsigned long nowMillis = millis();
    DateTime now = rtc.now();

    if(nowMillis - prevMillis >= 1000){
        prevMillis = nowMillis;

        // Reset harian (cek jika tanggal berubah)
        if(now.day() != lastPrintedDay){
            for(int i = 0; i < 8; i++) sudahAdzan[i] = false;
            lastPrintedDay = now.day();
            hitungJadwalSholat(now);
            printJadwal(now);
            modeSekarang = MODE_SCROLLINFO; // reset harian mulai dari scrollinfo lagi
            modeStart = nowMillis;
            sholatIndexSekarang = 0;
            jadwalSholatStart = nowMillis;
        }

        // ===== Prioritas tinggi: adzan / iqomah / titik =====
        if(sedangAdzan){
            if(millis() - adzanStart >= durasiAdzan) sedangAdzan = false;
        }
        else if(sedangHitungIqomah){
            unsigned long elapsed = (millis() - iqomahStart) / 1000;
            unsigned long sisa = (iqomahSholatIndex >= 0 && iqomahSholatIndex < 8) ?
                                  max((unsigned long)iqomahDurasi[iqomahSholatIndex] - elapsed, 0UL) : 0UL;
            if(sisa == 0){
                sedangHitungIqomah = false;
                digitalWrite(buzzerPin,HIGH);
                showScrollTextBoldCenter("RAPATKAN BARISAN", 40);
                sedangTitik = true; titikStart = millis();
                digitalWrite(buzzerPin,LOW);
            } else {
                char timebuf[6]; sprintf(timebuf, "%02lu:%02lu", sisa/60, sisa%60);
                showIqomahCenter("IQOMAH", timebuf);
                if(sisa <= 6){
                    static unsigned long lastBeep = 0;
                    if(millis() - lastBeep >= 1000){ lastBeep = millis(); digitalWrite(buzzerPin, HIGH); delay(100); digitalWrite(buzzerPin, LOW); }
                } else digitalWrite(buzzerPin, LOW);
            }
        }
        else if(sedangTitik){
            if(millis() - titikStart >= (unsigned long)durasiTitik * 1000UL) sedangTitik = false;
            else showDotsCenter();
        }
        else {
            // ===== Prioritas normal: tampil bergantian =====
            if(nowMillis - modeStart >= durasiMode){ // ganti mode setiap 30 detik
                modeSekarang = (ModeTampil)((modeSekarang + 1) % 5); // 5 mode total
                modeStart = nowMillis;
                sholatIndexSekarang = 0;
                jadwalSholatStart = nowMillis;
            }
//==============================


switch (modeSekarang) {

  case MODE_SCROLLINFO:
      if (isWaktuSholat(now)) {
        // kalau pas waktu sholat → langsung loncat ke JADWAL
        modeSekarang = MODE_JADWAL;
      } else {
        showScrollTextBoldCenter("MASJID IZHARUL HAQ SMKN 9 GARUT", 30);
        modeSekarang = MODE_KHOTIB;
        modeStart = millis();
      }
      break;

    case MODE_KHOTIB:
      if (isWaktuSholat(now)) {
        // kalau pas waktu sholat → langsung loncat ke JADWAL
        modeSekarang = MODE_JADWAL;
      } else {
        char buf[128];
        int mingguKe = (now.day() - 1) / 7;
        if (mingguKe >= jumlahKhotib) mingguKe = jumlahKhotib - 1;
        sprintf(buf, "Khotib Jum'at %s", khotibJumat[mingguKe]);
        showScrollTextBoldCenter(buf, 30);

        modeSekarang = MODE_JADWAL;
        modeStart = millis();
      }
      break;


  case MODE_JADWAL:
    showJadwalSekarang(sholatIndexSekarang);
    if (nowMillis - jadwalSholatStart >= durasiPerSholat) {
      sholatIndexSekarang = (sholatIndexSekarang + 1) % 8;
      jadwalSholatStart = nowMillis;
    }
    break;

  case MODE_TANGGAL:
    showTanggalJam();
    break;

  case MODE_JAM_BESAR:
    showClockDMD(now);
    break;
}


//==============================

           
        }

        // ===== Cek Adzan =====
        for(int i = 0; i < 8; i++){
            static unsigned long buzzerStart = 0; static bool buzzerOn = false;
            if(now.hour() == jadwal[i].h && now.minute() == jadwal[i].m && now.second() == 0){
                if(!sudahAdzan[i]){
                    Serial.print("SAATNYA ADZAN "); Serial.println(namaSholat[i]);
                    digitalWrite(buzzerPin, HIGH); buzzerStart = millis(); buzzerOn = true;

                    char buf[32]; sprintf(buf, "ADZAN %s", namaSholat[i]);
                    showTextCenterSmall(buf);

                    sedangAdzan = true; adzanStart = millis(); sudahAdzan[i] = true;

                    if(iqomahDurasi[i] > 0){
                        sedangHitungIqomah = true; iqomahStart = millis(); iqomahSholatIndex = i;
                    }
                }
            }
            if(buzzerOn && millis() - buzzerStart >= 5000){ digitalWrite(buzzerPin, LOW); buzzerOn = false; }
        }
    } // end if 1s
}
