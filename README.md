# LED Matrix ESP32

Firmware für eine 16×16-WS2812B-Matrix (vier 8×8-Kacheln) auf einem ESP32-C6 Super Mini.
Der ESP meldet sich beim LED-Matrix-Server an, hält einen SSE-Stream offen und zeigt die
Bilder an, die der Server pusht.

## Ablauf

1. WLAN verbinden, IP als Lauftext anzeigen.
2. `POST /api/esp/register` mit `{ matrixId, ip, firmware }`. Ist kein Nachtmodus aktiv, wird das gespeicherte Bild gezeigt.
3. `GET /api/esp/stream/<matrixId>` (Server-Sent Events) offen halten. Bei Abbruch nach 0,5 s neu verbinden.

| Event        | Daten                          | Wirkung                          |
| ------------ | ------------------------------ | -------------------------------- |
| `pixels`     | `{ pixels: [[r,g,b] × 256], brightness }` | Bild anzeigen         |
| `brightness` | `{ brightness }`               | Helligkeit setzen                |
| `off`        | `{}`                           | Matrix ausschalten               |

Das Pixel-Array kommt zeilenweise vom Server. `getTileMatrixIndex()` in `src/main.cpp`
rechnet es in die LED-Reihenfolge der Kachelkette um:

| Position im Bild | LED-Nummern | Drehung |
| ---------------- | ----------- | ------- |
| unten links      | 0–63        | 180°    |
| oben links       | 64–127      | 180°    |
| oben rechts      | 128–191     | keine   |
| unten rechts     | 192–255     | keine   |

## Setup

Voraussetzung: [PlatformIO](https://platformio.org) (VS Code Extension oder CLI).

```bash
cp include/secrets.h.example include/secrets.h
# WLAN, Server-Adresse und Matrix-ID in include/secrets.h eintragen

pio run                  # bauen
pio run -t upload        # flashen
pio device monitor       # serielle Ausgabe (115200)
```

`include/secrets.h` steht in der `.gitignore` und wird nicht eingecheckt.

## Konfiguration

| Wo | Was |
| --- | --- |
| `include/secrets.h` | `WIFI_SSID`, `WIFI_PASSWORD`, `BACKEND_URL`, `MATRIX_ID_STR` |
| `src/main.cpp` | `LED_PIN` (1), `NUM_LEDS` (256), Farbreihenfolge (GRB) |
| `platformio.ini` | Board, Bibliotheken, USB-Serial-Flags |

`BACKEND_URL` ist die Adresse des API-Ports des Servers, ohne `/` am Ende. Der Sketch spricht
unverschlüsseltes HTTP, `https://` geht derzeit nicht.

## Hinweise zum Board

- Es gibt keine eigene Board-Definition für den C6 Super Mini, deshalb `esp32-c6-devkitm-1`.
- Der Super Mini hat keinen UART-Chip, `Serial` läuft über USB (`ARDUINO_USB_CDC_ON_BOOT=1`).
  Zum Flashen kann es nötig sein, die BOOT-Taste beim Einstecken zu halten.
- Der C6 braucht Arduino-ESP32 3.x, deshalb die pioarduino-Platform statt der offiziellen `espressif32`.

## Bekannte Punkte

- Der Watchdog im Stream (10 s ohne Daten) ist kürzer als der Keepalive des Servers (20 s). Das führt zu häufigen Reconnects.
- Kein TLS, kein Prüfen des HTTP-Statuscodes beim Stream.
- Matrixgröße und Kachel-Layout sind fest einkompiliert.

## Herkunft

Übernommen aus dem Arduino-Sketch `ESP32_Matrix_v2.ino`. Änderungen für PlatformIO:
`#include <Arduino.h>`, Vorwärtsdeklarationen, Zugangsdaten nach `include/secrets.h`,
drei wirkungslose Zeilen (`x1, y1; w, h;`) entfernt. Das Verhalten ist unverändert.
