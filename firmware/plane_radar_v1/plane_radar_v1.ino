/*
  plane_radar_v1.ino — LIVE ADS-B plane radar for the Freising desk device.

  Hardware: ESP32-C3 Super Mini + GC9A01 1.28" round display
  Wiring:   see docs/display_wiring_guide.html (same pins as display_test_v1)

  Data:     https://api.adsb.lol (free, no API key)
  Setup:    copy config.example.h to config.h, enter your Wi-Fi name+password.

  Libraries: Adafruit GC9A01A, ArduinoJson (install via Library Manager)
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>
#include "config.h"

// ---------- Display ----------
#define TFT_SCLK 4
#define TFT_MOSI 3
#define TFT_DC   10
#define TFT_CS   1
#define TFT_RST  0

Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);

// Colors
#define C_BG      tft.color565(0, 8, 24)      // dark navy
#define C_RING    tft.color565(0, 90, 50)     // dim green
#define C_CROSS   tft.color565(0, 60, 35)
#define C_TEXT    GC9A01A_WHITE
#define C_PLANE   GC9A01A_RED
#define C_NEAR    GC9A01A_YELLOW
#define C_INFO    GC9A01A_CYAN
#define C_DIM     tft.color565(90, 100, 110)

// ---------- Aircraft data ----------
struct Aircraft {
  char  flight[10];
  float distKm;
  float bearingDeg;
  float trackDeg;
  int   altFt;
  int   gsKt;
};
#define MAX_AC 30
Aircraft planes[MAX_AC];
int      planeCount = 0;
uint32_t lastFetch  = 0;
int      failCount  = 0;

// ---------- Helpers ----------
float deg2rad(float d) { return d * 0.017453293f; }

float haversineKm(float lat1, float lon1, float lat2, float lon2) {
  float dLat = deg2rad(lat2 - lat1);
  float dLon = deg2rad(lon2 - lon1);
  float a = sinf(dLat / 2) * sinf(dLat / 2) +
            cosf(deg2rad(lat1)) * cosf(deg2rad(lat2)) *
            sinf(dLon / 2) * sinf(dLon / 2);
  return 6371.0f * 2 * atan2f(sqrtf(a), sqrtf(1 - a));
}

float bearingDeg(float lat1, float lon1, float lat2, float lon2) {
  float dLon = deg2rad(lon2 - lon1);
  float y = sinf(dLon) * cosf(deg2rad(lat2));
  float x = cosf(deg2rad(lat1)) * sinf(deg2rad(lat2)) -
            sinf(deg2rad(lat1)) * cosf(deg2rad(lat2)) * cosf(dLon);
  float b = atan2f(y, x) * 57.29578f;
  return fmodf(b + 360.0f, 360.0f);
}

// ---------- Screens ----------
void splash(const char *line1, const char *line2, uint16_t color) {
  tft.fillScreen(C_BG);
  tft.drawCircle(120, 120, 118, C_RING);
  tft.setTextColor(C_NEAR); tft.setTextSize(2);
  tft.setCursor(52, 90);  tft.print("PLANE RADAR");
  tft.setTextColor(color); tft.setTextSize(1);
  int w = strlen(line2) * 6;
  tft.setCursor(120 - w / 2, 130); tft.print(line2);
  tft.setTextColor(C_DIM);
  w = strlen(line1) * 6;
  tft.setCursor(120 - w / 2, 116); tft.print(line1);
}

void connectWifi() {
  splash("connecting to", WIFI_SSID, C_INFO);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 25000) {
    delay(400);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi OK, IP: %s\n", WiFi.localIP().toString().c_str());
    splash("wifi connected", WiFi.localIP().toString().c_str(), GC9A01A_GREEN);
    delay(1200);
  } else {
    splash("WIFI FAILED", "check config.h + reboot", GC9A01A_RED);
    Serial.println("\nWiFi failed. Check credentials in config.h");
    while (true) delay(1000);   // stay on error screen
  }
}

// ---------- ADS-B fetch ----------
bool fetchPlanes() {
  WiFiClientSecure client;
  client.setInsecure();                 // skip cert validation (public data)
  HTTPClient http;
  http.setTimeout(15000);

  int radiusNm = (int)(RADAR_RANGE_KM / 1.852f) + 4;   // fetch a bit past the rim
  char url[120];
  snprintf(url, sizeof(url), "https://api.adsb.lol/v2/point/%.4f/%.4f/%d",
           (double)HOME_LAT, (double)HOME_LON, radiusNm);

  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != 200) {
    Serial.printf("HTTP error: %d\n", code);
    http.end();
    return false;
  }

  // Only parse the fields we need (keeps memory small)
  JsonDocument filter;
  filter["ac"][0]["flight"]   = true;
  filter["ac"][0]["lat"]      = true;
  filter["ac"][0]["lon"]      = true;
  filter["ac"][0]["alt_baro"] = true;
  filter["ac"][0]["gs"]       = true;
  filter["ac"][0]["track"]    = true;

  String payload = http.getString();    // buffer whole body (handles chunked)
  http.end();
  Serial.printf("Payload size: %u bytes, free heap: %u\n",
                payload.length(), ESP.getFreeHeap());

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  payload = String();                   // free the buffer early
  if (err) {
    Serial.printf("JSON error: %s\n", err.c_str());
    return false;
  }

  planeCount = 0;
  for (JsonObject ac : doc["ac"].as<JsonArray>()) {
    if (planeCount >= MAX_AC) break;
    if (!ac["lat"].is<float>() || !ac["lon"].is<float>()) continue;
    if (ac["alt_baro"].is<const char *>()) continue;          // "ground" -> skip

    Aircraft &p = planes[planeCount];
    const char *fl = ac["flight"] | "";
    strncpy(p.flight, fl, sizeof(p.flight) - 1);
    p.flight[sizeof(p.flight) - 1] = 0;
    // trim trailing spaces the API pads with
    for (int i = strlen(p.flight) - 1; i >= 0 && p.flight[i] == ' '; i--) p.flight[i] = 0;
    if (p.flight[0] == 0) strcpy(p.flight, "------");

    float lat = ac["lat"], lon = ac["lon"];
    p.distKm     = haversineKm(HOME_LAT, HOME_LON, lat, lon);
    p.bearingDeg = bearingDeg(HOME_LAT, HOME_LON, lat, lon);
    p.trackDeg   = ac["track"] | 0.0f;
    p.altFt      = ac["alt_baro"] | 0;
    p.gsKt       = (int)(ac["gs"] | 0.0f);
    planeCount++;
  }

  // sort nearest-first (small N, simple sort is fine)
  for (int i = 0; i < planeCount - 1; i++)
    for (int j = i + 1; j < planeCount; j++)
      if (planes[j].distKm < planes[i].distKm) {
        Aircraft t = planes[i]; planes[i] = planes[j]; planes[j] = t;
      }

  Serial.printf("Aircraft airborne nearby: %d\n", planeCount);
  return true;
}

// ---------- Radar drawing ----------
void planeTriangle(int cx, int cy, float trackDeg, int size, uint16_t color) {
  float t = deg2rad(trackDeg);
  float ct = cosf(t), st = sinf(t);
  // template points (up-facing): tip(0,-s), left(-s*0.7,s*0.8), right(s*0.7,s*0.8)
  float px[3] = {0, -0.7f * size, 0.7f * size};
  float py[3] = {(float)-size, 0.8f * size, 0.8f * size};
  int x[3], y[3];
  for (int i = 0; i < 3; i++) {
    x[i] = cx + (int)(px[i] * ct - py[i] * st);
    y[i] = cy + (int)(px[i] * st + py[i] * ct);
  }
  tft.fillTriangle(x[0], y[0], x[1], y[1], x[2], y[2], color);
}

void drawRadar() {
  tft.fillScreen(C_BG);

  // rings + crosshair
  tft.drawLine(120, 10, 120, 230, C_CROSS);
  tft.drawLine(10, 120, 230, 120, C_CROSS);
  tft.drawCircle(120, 120, 37,  C_RING);
  tft.drawCircle(120, 120, 74,  C_RING);
  tft.drawCircle(120, 120, 110, C_RING);
  tft.fillCircle(120, 120, 2, C_TEXT);

  tft.setTextSize(1);
  tft.setTextColor(C_TEXT);
  tft.setCursor(117, 3);   tft.print("N");
  tft.setCursor(117, 231); tft.print("S");
  tft.setCursor(2, 117);   tft.print("W");
  tft.setCursor(232, 117); tft.print("E");
  // range label on east spoke
  tft.setTextColor(C_DIM);
  tft.setCursor(178, 124);
  tft.printf("%.0fkm", (double)(RADAR_RANGE_KM * 2 / 3));

  // aircraft
  for (int i = planeCount - 1; i >= 0; i--) {     // draw nearest last (on top)
    Aircraft &p = planes[i];
    float b = deg2rad(p.bearingDeg);
    if (p.distKm <= RADAR_RANGE_KM) {
      float r = (p.distKm / RADAR_RANGE_KM) * 110.0f;
      int x = 120 + (int)(sinf(b) * r);
      int y = 120 - (int)(cosf(b) * r);
      planeTriangle(x, y, p.trackDeg, 6, i == 0 ? C_NEAR : C_PLANE);
    } else {
      int x = 120 + (int)(sinf(b) * 117.0f);
      int y = 120 - (int)(cosf(b) * 117.0f);
      tft.fillCircle(x, y, 2, C_PLANE);           // rim dot: bearing cue only
    }
  }

  // nearest aircraft info block (inside lower half of the circle)
  if (planeCount > 0) {
    Aircraft &n = planes[0];
    tft.setTextSize(2);
    tft.setTextColor(C_NEAR);
    int w = strlen(n.flight) * 12;
    tft.setCursor(120 - w / 2, 150); tft.print(n.flight);
    tft.setTextSize(1);
    tft.setTextColor(C_INFO);
    char info[48];
    snprintf(info, sizeof(info), "%.1fkm  %dm  %dkm/h",
             (double)n.distKm, (int)(n.altFt * 0.3048f), (int)(n.gsKt * 1.852f));
    w = strlen(info) * 6;
    tft.setCursor(120 - w / 2, 172); tft.print(info);
  } else {
    tft.setTextSize(1);
    tft.setTextColor(C_DIM);
    tft.setCursor(84, 160); tft.print("no aircraft");
  }

  // status line: count + wifi
  tft.setTextSize(1);
  tft.setTextColor(C_DIM);
  char st[24];
  snprintf(st, sizeof(st), "%d planes", planeCount);
  int w = strlen(st) * 6;
  tft.setCursor(120 - w / 2, 68); tft.print(st);
}

// ---------- Main ----------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Plane Radar v1 booting...");
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(0);
  connectWifi();
  splash("fetching", "aircraft data...", C_INFO);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    splash("wifi lost", "reconnecting...", GC9A01A_ORANGE);
    WiFi.reconnect();
    delay(4000);
    return;
  }
  if (millis() - lastFetch >= FETCH_INTERVAL_MS || lastFetch == 0) {
    lastFetch = millis();
    if (fetchPlanes()) {
      failCount = 0;
      drawRadar();
    } else {
      failCount++;
      if (failCount >= 3) splash("API error", "retrying...", GC9A01A_ORANGE);
    }
  }
  delay(50);
}
