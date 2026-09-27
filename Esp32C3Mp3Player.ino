// ========================================================================================
//      Meine Welt in meinem Kopf
// ========================================================================================
// Projekt:       ESP32 MP3 Player
// Author:        Johannes P. Langner
// Controller:    ESP32-C3 DEV Module (Variante mit I2C: SDA = GPIO8, SCL = GPIO9)
// Description:   Einfacher MP3 Player
//                In den Ordner sind Kategorisch die MP3 nach Musik richtung angelegt.
//                - Chillout
//                - Work
//                - Dokumentation
//                - Lofi
//                Für den Aufbau werden weitere Module benötigt:
//                - PCM5102 I2S IIS (Lossless Digital Audio DAC Decoder)
//                - OLED Display 128x32 (SSD1306, I2C)
//                - SD Karten Leser (SPI)
//                - 2 Taster, 1 Potentiometer (10k), Spannungsteiler für den Akku
//                Die komplette Verdrahtung steht in der README.md
//
// Bibliotheken:  - Adafruit GFX Library
//                - Adafruit SSD1306
//                - arduino-audio-tools  (https://github.com/pschatzmann/arduino-audio-tools)
//                - arduino-libhelix     (https://github.com/pschatzmann/arduino-libhelix)
// Stand:         27.09.2026
// ========================================================================================

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <vector>
#include <algorithm>
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "esp_sleep.h"

// ========================================================================================
// Pin Belegung
// ========================================================================================
// I2C (OLED Display)
#define PIN_I2C_SDA        8
#define PIN_I2C_SCL        9

// SPI (SD Karten Leser)
#define PIN_SD_SCK         4
#define PIN_SD_MISO        5
#define PIN_SD_MOSI        6
#define PIN_SD_CS          7

// I2S (PCM5102 DAC)
#define PIN_DAC_BCK       10  // PCM5102: BCK
#define PIN_DAC_LCK       21  // PCM5102: LCK (Word Select)
#define PIN_DAC_DIN        2  // PCM5102: DIN

// Taster (gegen GND, interner Pull-Up)
#define PIN_BUTTON_PLAY    3  // Taster 1: Play / nächster Titel
#define PIN_BUTTON_CAT    20  // Taster 2: Stop / nächste Kategorie

// Analog Eingänge (ADC1)
#define PIN_POTI           0  // Schleifer des Potentiometers (Lautstärke)
#define PIN_BATTERY        1  // Akku über Spannungsteiler 100k / 100k

// ========================================================================================
// Einstellungen
// ========================================================================================
#define SCREEN_WIDTH 128 // OLED Display Breite, in Pixel
#define SCREEN_HEIGHT 32 // OLED Display Höhe, in Pixel
#define OLED_RESET     -1 // Reset-Pin (wird nicht benötigt, daher -1)
#define OLED_ADDRESS 0x3C // Adresse des Displays ist meist 0x3C oder 0x3D

#define SD_SPI_FREQUENCY      10000000UL  // bei Lesefehlern auf 4000000 reduzieren
#define DEFAULT_CATEGORY      "Chillout"

#define MAX_TEXT_CHARS        20     // Zeichen pro Zeile links neben der Akku Anzeige
#define VOLUME_BAR_MAX_WIDTH  118    // Lautstärke Balken endet vor der Akku Anzeige
#define VOLUME_BAR_Y          30     // Pixel Zeile 31 und 32 (0-basiert 30 und 31)

#define BUTTON_DEBOUNCE_MS    30
#define POTI_INTERVAL_MS      50
#define BATTERY_INTERVAL_MS   30000UL  // Akku alle 30 Sekunden messen (zusätzlich bei Titelwechsel)
#define SCROLL_INTERVAL_MS    350
#define SCROLL_PAUSE_MS       1500
#define SHUTDOWN_SECONDS      30

#define BATTERY_DIVIDER       2.0f   // Spannungsteiler 100k / 100k
#define BATTERY_MIN_VALID_MV  2500   // darunter ist kein Akku angeschlossen (z.B. nur USB)
#define BATTERY_SHUTDOWN_PCT  1      // bei 1% wird die Abschaltung eingeleitet

// Objekt für das Display erstellen
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ========================================================================================
// Audio
// ========================================================================================
I2SStream i2s;                                   // Ausgabe zum PCM5102
VolumeStream volume(i2s);                        // Lautstärke Regelung
MP3DecoderHelix mp3;                             // MP3 Decoder
EncodedAudioStream decoder(&volume, &mp3);       // MP3 -> PCM -> Lautstärke -> I2S
StreamCopy copier;                               // Kopiert die Datei in den Decoder
File audioFile;

// ========================================================================================
// Zustand
// ========================================================================================
enum class PlayerState {
  Idle,        // Start Text "Press Play"
  Playing,     // Titel wird abgespielt
  NoMp3,       // Keine Ordner / MP3s auf der SD Karte
  SdError,     // SD Karte nicht lesbar
  LowBattery,  // Akku leer, Abschaltung läuft
  Off          // Display aus
};

PlayerState mState = PlayerState::Idle;

std::vector<String> mCategories;   // Ordner auf der SD Karte
std::vector<String> mTitles;       // MP3 Dateien im aktuellen Ordner
int mCategoryIndex = 0;
int mTitleIndex = 0;

int mVolumePercent = -1;           // 0..100, -1 = noch nicht gelesen
int mBatteryPercent = -1;          // 0..100, -1 = kein Akku gemessen
uint32_t mLastBatteryMs = 0;
uint32_t mLastPotiMs = 0;

bool mRedraw = true;
uint32_t mScrollLastMs = 0;
int mScrollOffset = 0;

uint32_t mShutdownStartMs = 0;
int mShutdownSecondsLeft = SHUTDOWN_SECONDS;

String mHintText = "";             // kurzer Hinweis in Zeile 2 (z.B. "Keine Titel!")
uint32_t mHintUntilMs = 0;

// ========================================================================================
// Taster mit Entprellung
// ========================================================================================
struct Button {
  uint8_t pin;
  bool stableState;      // true = gedrückt
  bool lastRead;
  uint32_t lastChangeMs;

  void begin(uint8_t p) {
    pin = p;
    pinMode(pin, INPUT_PULLUP);
    stableState = false;
    lastRead = false;
    lastChangeMs = 0;
  }

  // liefert true, genau einmal pro Tastendruck
  bool pressed() {
    bool reading = digitalRead(pin) == LOW;
    uint32_t now = millis();
    if (reading != lastRead) {
      lastRead = reading;
      lastChangeMs = now;
    }
    if ((now - lastChangeMs) > BUTTON_DEBOUNCE_MS && reading != stableState) {
      stableState = reading;
      return stableState;
    }
    return false;
  }
};

Button mButtonPlay;
Button mButtonCategory;

// ========================================================================================
// Hilfsfunktionen Text
// ========================================================================================

// Der Standard Font kennt kein UTF-8, daher Umlaute ersetzen.
String toDisplayText(const String &text) {
  String result;
  result.reserve(text.length() + 4);
  for (size_t i = 0; i < text.length(); i++) {
    uint8_t c = (uint8_t)text[i];
    if (c == 0xC3 && i + 1 < text.length()) {
      uint8_t n = (uint8_t)text[++i];
      switch (n) {
        case 0xA4: result += "ae"; break;
        case 0xB6: result += "oe"; break;
        case 0xBC: result += "ue"; break;
        case 0x84: result += "Ae"; break;
        case 0x96: result += "Oe"; break;
        case 0x9C: result += "Ue"; break;
        case 0x9F: result += "ss"; break;
        default:   result += '?'; break;
      }
    }
    else if (c >= 0x80) {
      // sonstige Sonderzeichen überspringen
      if (c >= 0xC0) result += '?';
    }
    else {
      result += (char)c;
    }
  }
  return result;
}

String titleName(const String &fileName) {
  String name = fileName;
  int dot = name.lastIndexOf('.');
  if (dot > 0) {
    name = name.substring(0, dot);
  }
  return toDisplayText(name);
}

bool isMp3File(const String &name) {
  if (name.startsWith(".")) return false;  // z.B. "._titel.mp3" von macOS
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".mp3");
}

bool lessIgnoreCase(const String &a, const String &b) {
  return strcasecmp(a.c_str(), b.c_str()) < 0;
}

// ========================================================================================
// SD Karte
// ========================================================================================

void loadCategories() {
  mCategories.clear();
  File root = SD.open("/");
  if (!root) return;

  File entry = root.openNextFile();
  while (entry) {
    if (entry.isDirectory()) {
      String name = entry.name();
      if (!name.startsWith(".") && name != "System Volume Information") {
        mCategories.push_back(name);
      }
    }
    entry.close();
    entry = root.openNextFile();
  }
  root.close();

  std::sort(mCategories.begin(), mCategories.end(), lessIgnoreCase);
}

void createDefaultCategories() {
  const char *defaults[] = { "Chillout", "Work", "Dokumentation", "Lofi" };
  for (const char *name : defaults) {
    String path = String("/") + name;
    SD.mkdir(path);
  }
  loadCategories();
}

void loadTitles() {
  mTitles.clear();
  mTitleIndex = 0;
  if (mCategories.empty()) return;

  File dir = SD.open("/" + mCategories[mCategoryIndex]);
  if (!dir) return;

  File entry = dir.openNextFile();
  while (entry) {
    if (!entry.isDirectory()) {
      String name = entry.name();
      if (isMp3File(name)) {
        mTitles.push_back(name);
      }
    }
    entry.close();
    entry = dir.openNextFile();
  }
  dir.close();

  std::sort(mTitles.begin(), mTitles.end(), lessIgnoreCase);
}

// Liest die SD Karte ein und legt ggf. die Standard Ordner an.
void initLibrary() {
  loadCategories();

  if (mCategories.empty()) {
    createDefaultCategories();
    mState = PlayerState::NoMp3;
    return;
  }

  mCategoryIndex = 0;
  for (size_t i = 0; i < mCategories.size(); i++) {
    if (mCategories[i].equalsIgnoreCase(DEFAULT_CATEGORY)) {
      mCategoryIndex = i;
      break;
    }
  }
  loadTitles();
  mState = PlayerState::Idle;
}

// ========================================================================================
// Audio Steuerung
// ========================================================================================

void setupAudio() {
  auto cfg = i2s.defaultConfig(TX_MODE);
  cfg.pin_bck = PIN_DAC_BCK;
  cfg.pin_ws = PIN_DAC_LCK;
  cfg.pin_data = PIN_DAC_DIN;
  cfg.pin_mck = -1;              // PCM5102 erzeugt den Systemtakt selbst (SCK auf GND)
  cfg.sample_rate = 44100;
  cfg.channels = 2;
  cfg.bits_per_sample = 16;
  cfg.buffer_count = 12;         // etwas mehr Puffer, damit das Display Update nicht stört
  cfg.buffer_size = 512;
  i2s.begin(cfg);

  volume.begin(cfg);
  volume.setVolume(0.0f);
}

void stopPlayback() {
  copier.end();
  decoder.end();
  if (audioFile) {
    audioFile.close();
  }
}

// Öffnet den Titel mit dem Index; bei Fehlern wird der nächste Titel versucht.
bool startTrack(int index) {
  stopPlayback();
  if (mTitles.empty()) return false;

  for (size_t attempt = 0; attempt < mTitles.size(); attempt++) {
    mTitleIndex = (index + attempt) % mTitles.size();
    String path = "/" + mCategories[mCategoryIndex] + "/" + mTitles[mTitleIndex];
    audioFile = SD.open(path);
    if (audioFile) {
      decoder.begin();
      copier.begin(decoder, audioFile);
      mScrollOffset = 0;
      mScrollLastMs = millis();
      mRedraw = true;
      return true;
    }
  }
  return false;
}

void measureBattery();

void playNextTrack() {
  int next = mTitleIndex + 1;
  if (next >= (int)mTitles.size()) {
    next = 0;  // nach dem letzten Titel wieder von vorne
  }
  measureBattery();  // Akku zusätzlich bei jedem Titelwechsel prüfen
  if (mState != PlayerState::Playing) return;  // Akku leer
  if (!startTrack(next)) {
    mState = PlayerState::Idle;
    mRedraw = true;
  }
}

void processAudio() {
  copier.copy();
  if (!audioFile || audioFile.available() == 0) {
    // Titel zu Ende -> nächster Titel
    playNextTrack();
  }
}

// ========================================================================================
// Eingaben
// ========================================================================================

void showHint(const String &text, uint32_t durationMs) {
  mHintText = text;
  mHintUntilMs = millis() + durationMs;
  mRedraw = true;
}

void handleButtons() {
  bool playPressed = mButtonPlay.pressed();
  bool categoryPressed = mButtonCategory.pressed();

  if (mState == PlayerState::LowBattery || mState == PlayerState::Off
      || mState == PlayerState::NoMp3 || mState == PlayerState::SdError) {
    return;
  }

  if (playPressed) {
    if (mState == PlayerState::Idle) {
      if (mTitles.empty()) {
        showHint("Keine Titel!", 2000);
      }
      else if (startTrack(mTitleIndex)) {
        mState = PlayerState::Playing;
        measureBattery();
      }
      else {
        showHint("Lesefehler!", 2000);
      }
    }
    else if (mState == PlayerState::Playing) {
      playNextTrack();
    }
  }

  if (categoryPressed && !mCategories.empty()) {
    stopPlayback();
    mCategoryIndex = (mCategoryIndex + 1) % mCategories.size();
    loadTitles();
    mState = PlayerState::Idle;
    mHintUntilMs = 0;
    mRedraw = true;
  }
}

int readAnalogAverage(uint8_t pin, int samples) {
  uint32_t sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
  }
  return sum / samples;
}

void updateVolume(uint32_t now) {
  if (now - mLastPotiMs < POTI_INTERVAL_MS) return;
  mLastPotiMs = now;

  int raw = readAnalogAverage(PIN_POTI, 8);         // 0..4095
  int percent = (raw * 100L + 2047) / 4095;

  // Hysterese gegen Flackern, Endwerte werden immer übernommen
  if (mVolumePercent < 0 || abs(percent - mVolumePercent) >= 2
      || (percent == 0 && mVolumePercent != 0)
      || (percent == 100 && mVolumePercent != 100)) {
    mVolumePercent = percent;
    // quadratische Kurve, damit die Lautstärke gleichmäßiger empfunden wird
    float v = mVolumePercent / 100.0f;
    volume.setVolume(v * v);
    mRedraw = true;
  }
}

// ========================================================================================
// Akku
// ========================================================================================

// Entladekurve LiPo / Li-Ion (1 Zelle), Spannung in mV -> Prozent
int batteryPercentFromMillivolts(int mv) {
  static const int table[][2] = {
    { 4200, 100 }, { 4100, 90 }, { 4000, 80 }, { 3900, 65 }, { 3800, 50 },
    { 3750, 40 },  { 3700, 30 }, { 3650, 20 }, { 3600, 12 }, { 3500, 6 },
    { 3400, 3 },   { 3300, 1 },  { 3200, 0 }
  };
  const int count = sizeof(table) / sizeof(table[0]);
  if (mv >= table[0][0]) return 100;
  if (mv <= table[count - 1][0]) return 0;
  for (int i = 1; i < count; i++) {
    if (mv >= table[i][0]) {
      int mvHigh = table[i - 1][0], pctHigh = table[i - 1][1];
      int mvLow = table[i][0], pctLow = table[i][1];
      return pctLow + (long)(mv - mvLow) * (pctHigh - pctLow) / (mvHigh - mvLow);
    }
  }
  return 0;
}

int readBatteryMillivolts() {
  uint32_t sum = 0;
  const int samples = 16;
  for (int i = 0; i < samples; i++) {
    sum += analogReadMilliVolts(PIN_BATTERY);
  }
  return (int)((sum / samples) * BATTERY_DIVIDER);
}

void startShutdown() {
  stopPlayback();
  mState = PlayerState::LowBattery;
  mShutdownStartMs = millis();
  mShutdownSecondsLeft = SHUTDOWN_SECONDS;
  mRedraw = true;
}

void measureBattery() {
  mLastBatteryMs = millis();
  int mv = readBatteryMillivolts();

  int percent;
  if (mv < BATTERY_MIN_VALID_MV) {
    percent = -1;  // kein Akku angeschlossen (Betrieb über USB)
  }
  else {
    percent = batteryPercentFromMillivolts(mv);
    if (percent <= BATTERY_SHUTDOWN_PCT) {
      // zur Sicherheit nochmal messen, bevor abgeschaltet wird
      int mv2 = readBatteryMillivolts();
      int percent2 = batteryPercentFromMillivolts(mv2);
      if (mv2 >= BATTERY_MIN_VALID_MV && percent2 <= BATTERY_SHUTDOWN_PCT) {
        percent = percent2;
      }
      else {
        percent = BATTERY_SHUTDOWN_PCT + 1;
      }
    }
  }

  if (percent != mBatteryPercent) {
    mBatteryPercent = percent;
    mRedraw = true;
  }

  if (mBatteryPercent >= 0 && mBatteryPercent <= BATTERY_SHUTDOWN_PCT
      && mState != PlayerState::LowBattery && mState != PlayerState::Off) {
    startShutdown();
  }
}

void updateBattery(uint32_t now) {
  if (mState == PlayerState::LowBattery || mState == PlayerState::Off) return;
  if (now - mLastBatteryMs >= BATTERY_INTERVAL_MS) {
    measureBattery();
  }
}

void powerOff() {
  mState = PlayerState::Off;
  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  i2s.end();
  // Deep Sleep ohne Weckquelle: Neustart nur über Reset oder Aus-/Einschalten
  esp_deep_sleep_start();
}

void updateShutdown(uint32_t now) {
  if (mState != PlayerState::LowBattery) return;
  int left = SHUTDOWN_SECONDS - (int)((now - mShutdownStartMs) / 1000);
  if (left < 0) left = 0;
  if (left != mShutdownSecondsLeft) {
    mShutdownSecondsLeft = left;
    mRedraw = true;
  }
  if (left == 0) {
    powerOff();
  }
}

// ========================================================================================
// Display
// ========================================================================================

void drawBattery() {
  // Bereich rechts neben dem Text frei machen (lange Texte werden hier abgeschnitten)
  display.fillRect(119, 0, 9, SCREEN_HEIGHT, BLACK);
  display.drawRect(120, 0, 8, 32, WHITE);
  if (mBatteryPercent < 0) {
    return;  // kein Akku -> nur der Rahmen
  }

  // mAccuState: 1 für leer, 26 für Voll
  int mAccuState = 1 + (mBatteryPercent * 25 + 50) / 100;
  for (int i = 3; i < 29; i++) {
    if (i < 26 - mAccuState) {
      display.drawFastHLine(122, i, 4, BLACK);
    }
    else {
      display.drawFastHLine(122, i, 4, WHITE);
    }
  }
}

void drawVolumeBar() {
  if (mVolumePercent < 0) return;
  int width = (mVolumePercent * VOLUME_BAR_MAX_WIDTH + 50) / 100;
  if (width > 0) {
    display.fillRect(0, VOLUME_BAR_Y, width, 2, WHITE);
  }
}

void drawTextLine(int row, const String &text) {
  display.setCursor(0, row * 10);
  display.print(text);
}

// Titel, die zu lang für das Display sind, laufen als Lauftext durch
String scrollingText(const String &text) {
  if ((int)text.length() <= MAX_TEXT_CHARS) return text;
  String loop = text + "   " + text;
  int start = mScrollOffset % (text.length() + 3);
  return loop.substring(start, start + MAX_TEXT_CHARS);
}

void updateScroll(uint32_t now) {
  if (mState != PlayerState::Playing || mTitles.empty()) return;
  String title = titleName(mTitles[mTitleIndex]);
  if ((int)title.length() <= MAX_TEXT_CHARS) return;

  uint32_t wait = (mScrollOffset % (title.length() + 3) == 0) ? SCROLL_PAUSE_MS : SCROLL_INTERVAL_MS;
  if (now - mScrollLastMs >= wait) {
    mScrollLastMs = now;
    mScrollOffset++;
    mRedraw = true;
  }
}

String categoryLine() {
  if (mCategories.empty()) return "Kat: -";
  return "Kat: " + toDisplayText(mCategories[mCategoryIndex]);
}

// Zeichnet den aktuellen Inhalt in den Puffer (ohne display())
void renderScreen() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setTextWrap(false);

  bool hintActive = mHintUntilMs != 0 && (int32_t)(millis() - mHintUntilMs) < 0;

  switch (mState) {
    case PlayerState::Idle: {
      drawTextLine(0, "ESP32 MP3 Player");
      if (hintActive) {
        drawTextLine(1, mHintText);
      }
      else {
        drawTextLine(1, "Press Play (" + String(mTitles.size()) + ")");
      }
      drawTextLine(2, categoryLine());
      drawVolumeBar();
      drawBattery();
      break;
    }

    case PlayerState::Playing: {
      drawTextLine(0, scrollingText(titleName(mTitles[mTitleIndex])));
      drawTextLine(1, "Anzahl Titel: " + String(mTitles.size()));
      drawTextLine(2, categoryLine());
      drawVolumeBar();
      drawBattery();
      break;
    }

    case PlayerState::NoMp3: {
      drawTextLine(0, "Noch keine MP3s");
      drawTextLine(1, "auf der SD Karte");
      drawTextLine(2, "gespeichert!");
      break;
    }

    case PlayerState::SdError: {
      drawTextLine(0, "SD Karte Fehler!");
      drawTextLine(1, "Karte einlegen");
      drawTextLine(2, "und neu starten");
      break;
    }

    case PlayerState::LowBattery: {
      drawTextLine(0, "Batteriestand: " + String(mBatteryPercent < 0 ? 0 : mBatteryPercent) + "%");
      drawTextLine(1, "Bitte Aufladen");
      drawTextLine(2, "Abschaltung in " + String(mShutdownSecondsLeft) + "s");
      drawBattery();
      break;
    }

    case PlayerState::Off:
      break;
  }
}

void updateDisplay(uint32_t now) {
  if (mState == PlayerState::Off) return;

  if (mHintUntilMs != 0 && (int32_t)(now - mHintUntilMs) >= 0) {
    mHintUntilMs = 0;
    mRedraw = true;
  }

  if (!mRedraw) return;
  mRedraw = false;
  renderScreen();
  display.display();
}

// Schiebetüren, die nach links und rechts aufgehen und den Start Text freigeben
void drawDoors(int opening) {
  const int half = SCREEN_WIDTH / 2;
  int doorWidth = half - opening;
  if (doorWidth <= 0) return;

  // linke Tür
  display.fillRect(0, 0, doorWidth, SCREEN_HEIGHT, WHITE);
  if (doorWidth > 8) {
    display.drawRect(3, 3, doorWidth - 6, SCREEN_HEIGHT - 6, BLACK);   // Türfüllung
    display.drawFastVLine(doorWidth - 6, 11, 10, BLACK);               // Griff
  }

  // rechte Tür
  int rightX = half + opening;
  display.fillRect(rightX, 0, doorWidth, SCREEN_HEIGHT, WHITE);
  if (doorWidth > 8) {
    display.drawRect(rightX + 3, 3, doorWidth - 6, SCREEN_HEIGHT - 6, BLACK);
    display.drawFastVLine(rightX + 5, 11, 10, BLACK);
  }

  // Spalt zwischen den geschlossenen Türen
  if (opening == 0) {
    display.drawFastVLine(half, 0, SCREEN_HEIGHT, BLACK);
  }
}

void playDoorAnimation() {
  const int frames = 24;
  const int half = SCREEN_WIDTH / 2;

  // Türen geschlossen
  renderScreen();
  drawDoors(0);
  display.display();
  delay(600);

  for (int f = 1; f <= frames; f++) {
    float t = (float)f / frames;
    float eased = t * t * (3.0f - 2.0f * t);   // sanft anfahren und abbremsen
    renderScreen();
    drawDoors((int)(eased * half + 0.5f));
    display.display();
    delay(25);
  }

  renderScreen();
  display.display();
}

// ==================================================
void setup() {
  // Taster
  mButtonPlay.begin(PIN_BUTTON_PLAY);
  mButtonCategory.begin(PIN_BUTTON_CAT);

  // ADC
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_POTI, ADC_11db);
  analogSetPinAttenuation(PIN_BATTERY, ADC_11db);

  // Display
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, true, false)) {
    for (;;); // Endlosschleife, wenn das Display nicht gefunden wird
  }
  display.clearDisplay();
  display.display();

  // SD Karte
  SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (SD.begin(PIN_SD_CS, SPI, SD_SPI_FREQUENCY)) {
    initLibrary();
  }
  else {
    mState = PlayerState::SdError;
  }

  // Audio
  setupAudio();

  // erste Messungen, damit Lautstärke und Akku direkt angezeigt werden
  updateVolume(millis() + POTI_INTERVAL_MS);
  measureBattery();

  if (mState == PlayerState::LowBattery) {
    // Akku schon beim Einschalten leer -> keine Animation
    mRedraw = true;
    return;
  }

  playDoorAnimation();
  mRedraw = false;
}

// ==================================================
void loop() {
  uint32_t now = millis();

  if (mState == PlayerState::Playing) {
    processAudio();
  }

  handleButtons();
  updateVolume(now);
  updateBattery(now);
  updateShutdown(now);
  updateScroll(now);
  updateDisplay(now);
}
