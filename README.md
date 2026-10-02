# Esp32C3Mp3Player
 A simple MP3 Player with ESP32-C3.

Einfacher MP3 Player auf Basis eines **ESP32-C3 Dev Moduls** (Variante mit I²C auf
GPIO8 = SDA und GPIO9 = SCL, z.B. ESP32-C3 SuperMini). Die MP3s liegen nach Kategorien
sortiert in Ordnern auf einer SD Karte und werden über einen PCM5102 I2S DAC ausgegeben.

## Benötigte Bauteile

| Bauteil | Hinweis |
|---|---|
| ESP32-C3 Dev Modul | Variante SDA = GPIO8, SCL = GPIO9 |
| OLED Display 128x32 | SSD1306, I²C, Adresse 0x3C |
| SD Karten Leser | SPI, FAT32 formatierte SD Karte |
| PCM5102 I2S DAC Modul | "PCM5102 I2S IIS Lossless Digital Audio DAC Decoder" |
| 2 Taster | Schließer, gegen GND |
| Potentiometer 10 kΩ | linear, für die Lautstärke |
| 2 Widerstände 100 kΩ | Spannungsteiler für die Akku Messung |
| LiPo / Li-Ion Akku (1 Zelle) | mit Lade-/Schutzschaltung (z.B. TP4056) |

## Pin Belegung

| GPIO | Funktion | Anschluss am Modul |
|---|---|---|
| GPIO0 | ADC – Lautstärke | Potentiometer Schleifer (Mitte) |
| GPIO1 | ADC – Akku Messung | Mitte Spannungsteiler 100k/100k |
| GPIO2 | I2S Daten | PCM5102 **DIN** |
| GPIO3 | Taster 1 (Play / nächster Titel) | Taster gegen GND |
| GPIO4 | SPI SCK | SD Karte **SCK** |
| GPIO5 | SPI MISO | SD Karte **MISO** |
| GPIO6 | SPI MOSI | SD Karte **MOSI** |
| GPIO7 | SPI CS | SD Karte **CS** |
| GPIO8 | I²C SDA | OLED **SDA** |
| GPIO9 | I²C SCL | OLED **SCL** |
| GPIO10 | I2S Bit Clock | PCM5102 **BCK** |
| GPIO20 | Taster 2 (Stop / nächste Kategorie) | Taster gegen GND |
| GPIO21 | I2S Word Select | PCM5102 **LCK** |

### OLED Display 128x32

| OLED | ESP32-C3 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO8 |
| SCL | GPIO9 |

### SD Karten Leser

| SD Modul | ESP32-C3 |
|---|---|
| VCC | 3V3 (bzw. 5V, wenn das Modul einen eigenen Spannungsregler hat) |
| GND | GND |
| SCK | GPIO4 |
| MISO | GPIO5 |
| MOSI | GPIO6 |
| CS | GPIO7 |

### PCM5102 I2S DAC

| PCM5102 | ESP32-C3 / Anschluss |
|---|---|
| VIN | 3V3 (oder 5V) |
| GND | GND |
| BCK | GPIO10 |
| DIN | GPIO2 |
| LCK | GPIO21 |
| SCK | **GND** (der DAC erzeugt den Systemtakt dann selbst) |
| FLT | GND (normaler Filter) |
| DEMP | GND (De-Emphasis aus) |
| XSMT | **3V3** (Soft-Mute aus, sonst bleibt der Ausgang stumm) |
| FMT | GND (I2S Format) |

Bei vielen PCM5102 Modulen sind FLT, DEMP, XSMT und FMT über Lötbrücken (H/L) auf der
Rückseite eingestellt und SCK ist schon mit GND verbunden – dann müssen diese Pins nicht
extra verdrahtet werden. Der Kopfhörer bzw. Verstärker kommt an den Line-Out (3,5 mm Klinke
oder L / G / R).

### Taster

Beide Taster schalten den GPIO gegen GND. Es werden die internen Pull-Up Widerstände
verwendet, externe Widerstände sind nicht nötig.

| Taster | ESP32-C3 | Funktion |
|---|---|---|
| Taster 1 | GPIO3 ↔ GND | Play, bei erneutem Drücken nächster Titel |
| Taster 2 | GPIO20 ↔ GND | Titel anhalten und zur nächsten Kategorie wechseln |

### Potentiometer (Lautstärke)

| Potentiometer | ESP32-C3 |
|---|---|
| Äußerer Pin 1 | GND |
| Schleifer (Mitte) | GPIO0 |
| Äußerer Pin 2 | 3V3 |

### Akku Messung

Der Akku wird über einen Spannungsteiler (2 × 100 kΩ) gemessen, damit am ADC maximal
ca. 2,1 V anliegen:

```
Akku + ──[100k]──┬──[100k]── GND
                 │
               GPIO1
```

Wird keine Akkuspannung erkannt (Betrieb nur über USB), zeigt die Akku Anzeige nur den
Rahmen und es findet keine Abschaltung statt.

### Hinweise zu den Pins

- GPIO20/21 sind beim ESP32-C3 die Pins der UART0 (RX/TX). Da sie hier für Taster 2 und
  den DAC verwendet werden, in der Arduino IDE **"USB CDC On Boot: Enabled"** einstellen,
  damit die serielle Konsole über den nativen USB Anschluss läuft.
- GPIO2, GPIO8 und GPIO9 sind Strapping Pins. Sie werden hier nur als Ausgang (DIN) bzw.
  für I²C genutzt, die Taster liegen bewusst nicht auf diesen Pins.

## Platine (PCB)

Im Ordner [`hardware/`](hardware/) liegt ein KiCad-Projekt (Schaltplan und geroutete Platine,
88 × 52 mm, 2 Lagen). Die Module werden auf beiden Seiten der Platine gesteckt: SuperMini,
OLED, Taster, Poti und Ein/Aus-Schalter auf der Oberseite, SD-Modul, PCM5102, HW-104
(PAM8403) und TP4056 auf der Unterseite. Die Pinbelegung ist dieselbe wie oben. Zusätzlich
sind der Akku-Pfad mit Lademodul, Schalter und Schottky-Diode sowie Kondensatoren gegen
Störgeräusche am Verstärker vorgesehen.

Einige Modulmaße sind geschätzt – vor der Bestellung bitte die Hinweise in der
[Platinen-README](hardware/README.md) beachten.

![Platine Oberseite](hardware/bilder/platine_oberseite.png)

## SD Karte

Die SD Karte muss FAT32 formatiert sein. Jeder Ordner im Hauptverzeichnis ist eine
Kategorie, die MP3s liegen direkt in diesen Ordnern:

```
/Chillout/Titel 1.mp3
/Dokumentation/...
/Lofi/...
/Work/...
```

Werden auf der SD Karte keine Ordner gefunden, legt der Player die Ordner **Chillout**,
**Work**, **Dokumentation** und **Lofi** an und zeigt den Hinweis, dass noch keine MP3s
gespeichert sind. Standard Kategorie beim Einschalten ist *Chillout*.

## Bedienung / Anzeige

1. **Einschalten:** Schiebetüren Animation, danach der Start Text
   - Zeile 1: `ESP32 MP3 Player`
   - Zeile 2: `Press Play (Anzahl Titel)`
   - Zeile 3: `Kat: <Ordner>`
2. **Taster 1:** Wiedergabe startet
   - Zeile 1: Titel der MP3 (lange Titel laufen als Lauftext durch)
   - Zeile 2: `Anzahl Titel: <Anzahl>`
   - Erneutes Drücken spielt den nächsten Titel, nach dem letzten Titel geht es wieder
     beim ersten los. Ist ein Titel zu Ende, wird automatisch der nächste gespielt.
3. **Taster 2:** Wiedergabe wird angehalten und die nächste Kategorie (Ordner) gewählt.
   Es wird wieder der Start Text mit der neuen Kategorie angezeigt.
4. **Potentiometer:** Lautstärke, angezeigt als 2 Pixel hoher Balken in den unteren beiden
   Pixelzeilen (31 und 32).
5. **Akku:** Wird alle 30 Sekunden und bei jedem Titelwechsel gemessen und rechts als
   Balken angezeigt. Bei 1 % wird die Wiedergabe gestoppt und angezeigt:
   - Zeile 1: `Batteriestand: 1%`
   - Zeile 2: `Bitte Aufladen`
   - Zeile 3: `Abschaltung in 30s` (zählt herunter)

   Bei 0 Sekunden wird das Display abgeschaltet und der ESP32 geht in den Deep Sleep.
   Neustart über Reset bzw. Aus- und wieder Einschalten.

## Software

Arduino IDE mit dem **esp32** Board Paket von Espressif (Version 3.x), Board
**"ESP32C3 Dev Module"**, *USB CDC On Boot: Enabled*.

Benötigte Bibliotheken:

- Adafruit GFX Library
- Adafruit SSD1306
- [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools)
- [arduino-libhelix](https://github.com/pschatzmann/arduino-libhelix) (MP3 Decoder)

Die beiden Audio Bibliotheken sind nicht im Bibliotheksverwalter enthalten und werden als
ZIP von GitHub heruntergeladen und über *Sketch → Bibliothek einbinden → .ZIP-Bibliothek
hinzufügen* installiert.
