// ========================================================================================
//      Meine Welt in meinem Kopf
// ========================================================================================
// Projekt:       ESP32 MP3 Player
// Author:        Johannes P. Langner
// Controller:    ESP32-C3 DEV Module
// Description:   Einfacher MP3 Player
//                In den Ordner sind Kategorisch die MP3 nach Musik richtung angelegt.
//                - Chillout
//                - Work
//                - Dokumentation
//                - Lofi
//                Für den Aufbau werden weitere Module benötigt:
//                - PCM5102 I2S IIS (Lossless Digital Audio DAC Decoder)
//                - OLED Display 128x32
//                -  
// Stand:         27.09.2026
// ========================================================================================

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED Display Breite, in Pixel
#define SCREEN_HEIGHT 32 // OLED Display Höhe, in Pixel
#define OLED_RESET     -1 // Reset-Pin (wird nicht benötigt, daher -1)

// Objekt für das Display erstellen
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==================================================
void setup() {
    // Adresse des Displays ist meist 0x3C oder 0x3D
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    for(;;); // Endlosschleife, wenn das Display nicht gefunden wird
  }
  
  display.clearDisplay(); // Puffer leeren
  
  // Text konfigurieren
  display.setTextSize(1);      // Schriftgröße (1 ist Standard)
  display.setTextColor(WHITE); // Textfarbe (bei monochromen Displays immer weiß/hell)
  display.setCursor(0,0);      // Startposition oben links (X=0, Y=0)
  display.println("ESP32 MP3 Player");
  display.setCursor(0,10);
  display.println("Press Play");
  display.setCursor(0,20);
  display.println("Catogorie: Chillout");
  
  display.display(); // Puffer auf dem Bildschirm anzeigen

}

// only for test
int mTitleTest = 0;
int mAccuState = 1; // 1 für leer, 26 für Voll;

// ==================================================
void loop() {

  display.clearDisplay();

  display.setCursor(0,0);
  display.print("Titel "); display.println(mTitleTest, DEC);
  display.setCursor(0,10);
  display.println("Anzahl Titel: 1");
  display.setCursor(0,20);
  display.println("Catogorie: Chillout");

  // accu stand
  display.drawRect(120, 0, 8, 32, WHITE);
  for (int i = 3; i < 29; i++) {
    if (i < 26 - mAccuState) {
      display.drawFastHLine(122, i, 4, BLACK);
    }
    else {
      display.drawFastHLine(122, i, 4, WHITE);
    }
  }
  
  display.display(); // Puffer auf dem Bildschirm anzeigen

  if(mAccuState < 26){
    mAccuState++;
  }
  else {
    mAccuState = 1;
  }
  mTitleTest++;
  delay(1000);
}
