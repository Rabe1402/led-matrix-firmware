// ESP32 LED-Matrix-Client (16x16, SSE)
// Aus dem Arduino-Sketch ESP32_Matrix_v2.ino fuer PlatformIO uebernommen.
// Zugangsdaten und Server-Adresse stehen in include/secrets.h (nicht im Git).
#include <Arduino.h>
#include <Adafruit_GFX.h>

#include <FastLED.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include "secrets.h"


// ─── Config ──────────────────────────────────────────────────────────────────
#define LED_PIN      1
#define NUM_LEDS     256        // 16x16
#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB
uint8_t bri = 64;

const char* SSID       = WIFI_SSID;
const char* PASSWORD   = WIFI_PASSWORD;

const char* BACKEND    = BACKEND_URL;  // ohne / am Ende
const char* MATRIX_ID  = MATRIX_ID_STR;

uint8_t TextR;
uint8_t TextG;
uint8_t TextB;

char ScrollTextBuf[32] = "Booting...";
char* ScrollText = ScrollTextBuf;

int16_t scrollX = 16; 
int16_t textBreite = 0;

//int to char for write text 
char puffer[32];

CRGB leds[NUM_LEDS];

GFXcanvas16 matrix(16, 16);

TaskHandle_t MatrixTaskHandle;

// Vorwaertsdeklarationen (der Arduino-Praeprozessor hat die frueher automatisch erzeugt)
void applyCanvasToPixels(uint8_t bri);
uint16_t getTileMatrixIndex(uint8_t x, uint8_t y);

//---helper----

uint16_t RGB565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3)| (b >> 3); //GRB
}

void TextToCanvas(char* text, uint8_t R = 255, uint8_t G = 255, uint8_t B = 255) {
  TextR = R;
  TextG = G;
  TextB = B;
  ScrollText = text;
}

// Die Funktion für deine Pixel-Matrix
void matrixTask(void * pvParameters) {
  for(;;) { // Endlosschleife für diesen Task
  // 1. Canvas im Hintergrund löschen und Text neu zeichnen mit dem aktuellen Versatz (scrollX)
    matrix.fillScreen(0);
    matrix.setCursor(scrollX, 0); // Die X-Position wandert nach links
    matrix.setTextColor(RGB565(TextR, TextG, TextB), RGB565(0, 0, 0));
    matrix.setTextSize(2);
    matrix.print(ScrollText);

    // 2. Deine Funktion aufrufen, um das Canvas auf die LEDs zu bringen
    // (Wir übergeben z.B. Helligkeit 64)
    applyCanvasToPixels(constrain(bri, 1, 255)); 

    // 3. X-Position für den nächsten Frame verringern (nach links schieben)
    scrollX--;

    // 4. Prüfen, ob der Text komplett durchgelaufen ist
    // Wenn scrollX kleiner ist als minus die Textbreite, ist er ganz links verschwunden
    if (scrollX < -textBreite) {
      scrollX = 16; // Wieder ganz rechts starten (Reset)
    }

    // 5. Scroll-Geschwindigkeit festlegen (z.B. alle 50ms ein Pixel weiter = 20 FPS)
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}


CRGB canvasToFastLEDColor(uint16_t color565) {
  
  uint8_t r = ((color565 >> 11) & 0x1F) * 255 / 31;
  uint8_t g = ((color565 >> 5)  & 0x3F) * 255 / 63;
  uint8_t b = (color565         & 0x1F) * 255 / 31;
  
  return CRGB(g, r, b);
}

void applyCanvasToPixels(uint8_t bri) {
  FastLED.setBrightness(constrain(bri, 1, 255));
  
  for (uint8_t y = 0; y < 16; y++) {
    for (uint8_t x = 0; x < 16; x++) {
      
      uint16_t ledIdx = getTileMatrixIndex(x, y);
      
      if (ledIdx < NUM_LEDS) {
        // 1. Hol die Farbe aus der Canvas
        uint16_t canvasColor = matrix.getPixel(x, y);
        
        // 2. Nutze den Helfer von oben zur Umwandlung
        leds[ledIdx] = canvasToFastLEDColor(canvasColor);
      }
    }
  }
  
  FastLED.show();
}


void showStartupAnimation() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB(0, 80, 255);
    FastLED.show();
    delay(4);
  }
  delay(200);
  FastLED.clear();
  FastLED.show();
  delay(1000);
}

uint16_t getTileMatrixIndex(uint8_t x, uint8_t y) {
  uint8_t tile = 0;
  uint8_t localX = x % 8;
  uint8_t localY = y % 8;

  if (x >= 8) {
      // ── Rechte Seite: unverändert ──────────────────────────
      uint8_t localIdx = (localY * 8) + localX;
      if (y < 8) return 128 + localIdx;  // Oben Rechts
      else        return 192 + localIdx;  // Unten Rechts

    } else {
      // ── Linke Seite: 180° gedreht + Tiles vertauscht ───────
      uint8_t flippedX = 7 - localX;
      uint8_t flippedY = 7 - localY;
      uint8_t localIdx = (flippedY * 8) + flippedX;

      if (y < 8)  return 64  + localIdx;  // Oben Links  → war Tile-Offset 0
      else         return 0   + localIdx;  // Unten Links → war Tile-Offset 64
  }
  
}


void applyPixels(JsonArray pixArr, uint8_t bri) {
  FastLED.setBrightness(constrain(bri, 1, 255));
  
  int arrayIdx = 0;
  for (uint8_t y = 0; y < 16; y++) {
    for (uint8_t x = 0; x < 16; x++) {
      
      if (arrayIdx < (int)pixArr.size()) {
        JsonArray p = pixArr[arrayIdx];
        if (p.size() >= 3) {
          
          uint16_t ledIdx = getTileMatrixIndex(x, y);
          
          if (ledIdx < NUM_LEDS) {
            leds[ledIdx] = CRGB(
              p[0].as<uint8_t>(),
              p[1].as<uint8_t>(),
              p[2].as<uint8_t>()
            );
          }
        }
      }
      arrayIdx++;
    }
  }
  FastLED.show();
}

// ─── Register with backend ───────────────────────────────────────────────────
bool doRegister() {
  HTTPClient http;
  String url = String(BACKEND) + "/api/esp/register";
  
  // ignore sles signed ssl 
  
  //WiFiClientSecure secureClient;
  //secureClient.setInsecure();
  WiFiClient client;
  
  http.begin(client, url); /*secureClient*/ 
  http.addHeader("Content-Type", "application/json");

  JsonDocument body;
  body["matrixId"] = MATRIX_ID;
  body["ip"]       = WiFi.localIP().toString();
  body["firmware"] = "v2.2";
  String bodyStr;
  serializeJson(body, bodyStr);

  int code = http.POST(bodyStr);
  if (code != 200) {
    Serial.printf("[REG] Failed: %d\n", code);
    http.end();
    return false;
  }

  String resp = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, resp)) return true; 

  Serial.printf("[REG] OK – name: %s, nightMode: %d\n",
    doc["name"] | "?", (bool)(doc["nightMode"] | false));

  if (!doc["nightMode"] && doc["lastState"]["pixels"].is<JsonArray>()) {
    JsonArray pixArr = doc["lastState"]["pixels"];
    uint8_t bri = doc["lastState"]["brightness"] | 30;
    applyPixels(pixArr, bri);
    Serial.println("[REG] Restored last state");
  }

  return true;
}

// ─── SSE Stream Handler ───────────────────────────────────────────────────────
void runSSEStream() {
  WiFiClient client;
  //client.setInsecure(); // SSL-Zertifikatsprüfung umgehen

  String host = String(BACKEND);
  host.replace("http://", "");
  host.replace("https://", "");
  
  int port = 443;
  int portSep = host.indexOf(':');
  if (portSep != -1) {
    port = host.substring(portSep + 1).toInt();
    host = host.substring(0, portSep);
  }

  Serial.printf("[SSE] Verbinde mit %s:%d...\n", host.c_str(), port);

  if (!client.connect(host.c_str(), port)) {
    Serial.println("[SSE] Verbindung fehlgeschlagen.");
    return;
  }

  String path = "/api/esp/stream/" + String(MATRIX_ID);
  client.printf(
    "GET %s HTTP/1.1\r\n"
    "Host: %s\r\n"
    "User-Agent: ESP32-Matrix-Client\r\n"
    "Accept: text/event-stream\r\n"
    "Cache-Control: no-cache\r\n"
    "Connection: keep-alive\r\n\r\n",
    path.c_str(), host.c_str()
  );

  Serial.println("[SSE] Verbunden, warte auf Events...");

  String eventType = "";
  String dataLine  = "";
  String currentLine = ""; 
  
  eventType.reserve(32);
  dataLine.reserve(16384);
  currentLine.reserve(256);

  unsigned long lastActivity = millis();
  bool headersPassed = false;

  while (client.connected()) {
    // Watchdog: Wenn 10 Sek gar nichts kommt (auch kein Keepalive) -> Reconnect
    if (millis() - lastActivity > 10000) {
      Serial.println("[SSE] Timeout im Stream, starte neu...");
      break;
    }

    if (!client.available()) {
      delay(1); 
      continue;
    }

    char c = client.read();
    lastActivity = millis(); 

    if (c == '\n') {
      currentLine.trim(); 

      if (!headersPassed) {
        if (currentLine.length() == 0) {
          headersPassed = true;
          Serial.println("[SSE] HTTP-Header übersprungen. Stream läuft LIVE!");
        }
        currentLine = "";
        continue;
      }

      if (currentLine.startsWith(":")) {
        currentLine = "";
        continue;
      }

      // SSE Felder parsen
      if (currentLine.startsWith("event:")) {
        eventType = currentLine.substring(6);
        eventType.trim();
      } else if (currentLine.startsWith("data:")) {
        dataLine = currentLine.substring(5);
        dataLine.trim();
      } else if (currentLine.length() == 0) {
        // Eine leere Zeile beendet den SSE-Event-Block
        if (eventType.length() > 0 && dataLine.length() > 0) {
          Serial.printf("[SSE] Live-Event erhalten: %s (%d Bytes)\n", eventType.c_str(), dataLine.length());

          if (eventType == "pixels") {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, dataLine);
            
            if (!err) {
              JsonArray pixArr = doc["pixels"];
              uint8_t bri = doc["brightness"] | 30;
              applyPixels(pixArr, bri);
              Serial.println("[SSE] Pixel gezeichnet.");
            } else {
              Serial.printf("[JSON] Fehler: %s\n", err.c_str());
            }

          } else if (eventType == "brightness") {
            JsonDocument doc;
            deserializeJson(doc, dataLine);
            uint8_t bri = doc["brightness"] | 30;
            FastLED.setBrightness(constrain(bri, 1, 255));
            FastLED.show();
            Serial.println("[SSE] Helligkeit angepasst.");

          } else if (eventType == "off") {
            FastLED.clear();
            FastLED.show();
            Serial.println("[SSE] Matrix aus.");
          }
        }
        
        // Reset für das nächste Event
        eventType = "";
        dataLine = "";
      }

      // Zeile zurücksetzen für die nächsten Zeichen
      currentLine = "";
    } 
    else if (c != '\r') {
      // Zeichen an die aktuelle Zeile hängen (außer Carriage Return)
      currentLine += c;
    }
  }

  client.stop();
  Serial.println("[SSE] Stream-Verbindung getrennt.");
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
  int16_t x1, y1; uint16_t w, h;
  Serial.begin(115200);
  delay(100);

  matrix.setTextWrap(false);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(bri);
  FastLED.clear();
  FastLED.show();

  xTaskCreate(matrixTask, "MatrixTask", 4096, NULL, 1, &MatrixTaskHandle);
  delay(1);
  vTaskSuspend(MatrixTaskHandle);
  
  showStartupAnimation();

  int tries = 0;




  WiFi.disconnect(true);
  delay(1000);           

  WiFi.begin(SSID, PASSWORD);
  Serial.print("[WiFi] Verbinde");
  while (WiFi.status() != WL_CONNECTED && tries < 60) {
    delay(500);
    Serial.print(".");
    scrollX = 16;
    vTaskResume(MatrixTaskHandle);
    snprintf(puffer, sizeof(puffer), "connecting  ");
    TextToCanvas(puffer, 20, 230, 210);
    matrix.getTextBounds(ScrollText, 0, 0, &x1, &y1, &w, &h);
    textBreite = w;
    delay(50* w);
    vTaskSuspend(MatrixTaskHandle);
    tries++;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[WiFi] Fehlgeschlagen. Neustart in 5s...");

    scrollX = 16;
    vTaskResume(MatrixTaskHandle);
    snprintf(puffer, sizeof(puffer), "rebooting  ");
    TextToCanvas(puffer, 20, 230, 210);
    matrix.getTextBounds(ScrollText, 0, 0, &x1, &y1, &w, &h);
    textBreite = w;
    delay(50* w);
    vTaskSuspend(MatrixTaskHandle);

    delay(5000);
    ESP.restart();
  }
  Serial.printf("\n[WiFi] Verbunden! IP: %s\n", WiFi.localIP().toString().c_str());


  scrollX = 16;
  vTaskResume(MatrixTaskHandle);
  snprintf(puffer, sizeof(puffer), "Ready! IP: %s", WiFi.localIP().toString().c_str());
  TextToCanvas(puffer, 20, 230, 210);
  matrix.getTextBounds(ScrollText, 0, 0, &x1, &y1, &w, &h);
  textBreite = w;
  delay(50* w);
  vTaskSuspend(MatrixTaskHandle);

}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
  static bool registered = false;
  
  if (!registered) {
    if (!doRegister()) {
      Serial.println("[LOOP] Registrierung fehlgeschlagen, neuer Versuch in 5s...");
      delay(5000);
      return;
    }
    registered = true;
  }

  runSSEStream();

  Serial.println("[LOOP] Stream abgebrochen. Reconnect in 0.5s...");
  delay(500);
  // registered bleibt true, nur Stream wird neu aufgebaut
}