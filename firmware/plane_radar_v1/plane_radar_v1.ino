/*
  plane_radar_v1.ino — LIVE ADS-B plane radar for the Freising desk device.

  Hardware: ESP32-C3 Super Mini + GC9A01 1.28" round display
  Wiring:   see docs/display_wiring_guide.html (same pins as display_test_v1)

  Data:     https://api.adsb.lol (free, no API key)
            + route lookup via /api/0/routeset (origin > destination)
  Setup:    copy config.example.h to config.h, enter your Wi-Fi name+password.

  Pages (auto-cycle):
    1. Radar — all aircraft around home position
    2. Nearest flight detail — FlightRadar-style card

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

#ifndef PAGE_INTERVAL_MS
#define PAGE_INTERVAL_MS 10000        // how long each page is shown
#endif

// ---------- Display ----------
#define TFT_SCLK 4
#define TFT_MOSI 3
#define TFT_DC   10
#define TFT_CS   1
#define TFT_RST  0

Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);

// Colors
#define C_BG      tft.color565(0, 8, 24)
#define C_RING    tft.color565(0, 90, 50)
#define C_CROSS   tft.color565(0, 60, 35)
#define C_TEXT    GC9A01A_WHITE
#define C_PLANE   GC9A01A_RED
#define C_NEAR    GC9A01A_YELLOW
#define C_INFO    GC9A01A_CYAN
#define C_DIM     tft.color565(90, 100, 110)
#define C_GREEN   GC9A01A_GREEN
#define C_ORANGE  GC9A01A_ORANGE

// ---------- Aircraft data ----------
struct Aircraft {
  char  flight[10];   // callsign
  char  reg[10];      // registration e.g. D-AIZQ
  char  typ[6];       // ICAO type e.g. A320
  char  sqk[6];       // squawk
  char  op[28];       // operator / aircraft description
  float distKm;
  float bearingDeg;
  float trackDeg;
  int   altFt;
  int   gsKt;
  int   vrFpm;        // vertical rate ft/min
};
#define MAX_AC 30
Aircraft planes[MAX_AC];
int      planeCount = 0;

uint32_t lastFetch      = 0;
uint32_t lastPageSwitch = 0;
int      failCount      = 0;
uint8_t  page           = 0;          // 0 = radar, 1 = nearest detail

// route cache (one extra API call, only when nearest callsign changes)
char routeFor[10]     = "";
char routeStr[24]     = "";   // "MUC > VNO"
char routeCities[30]  = "";   // "Munich > Vilnius"
char routeAirline[24] = "";   // "Air Baltic"

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

float bearingDegF(float lat1, float lon1, float lat2, float lon2) {
  float dLon = deg2rad(lon2 - lon1);
  float y = sinf(dLon) * cosf(deg2rad(lat2));
  float x = cosf(deg2rad(lat1)) * sinf(deg2rad(lat2)) -
            sinf(deg2rad(lat1)) * cosf(deg2rad(lat2)) * cosf(dLon);
  float b = atan2f(y, x) * 57.29578f;
  return fmodf(b + 360.0f, 360.0f);
}

const char *compass8(float deg) {
  static const char *names[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  return names[((int)((deg + 22.5f) / 45.0f)) % 8];
}

void centerText(const char *s, int y, uint8_t size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color);
  int w = strlen(s) * 6 * size;
  tft.setCursor(120 - w / 2, y);
  tft.print(s);
}

void pageDots() {
  tft.fillCircle(113, 222, 3, page == 0 ? C_TEXT : C_DIM);
  tft.fillCircle(127, 222, 3, page == 1 ? C_TEXT : C_DIM);
}

// ---------- Screens ----------
void splash(const char *line1, const char *line2, uint16_t color) {
  tft.fillScreen(C_BG);
  tft.drawCircle(120, 120, 118, C_RING);
  centerText("PLANE RADAR", 90, 2, C_NEAR);
  centerText(line1, 116, 1, C_DIM);
  centerText(line2, 130, 1, color);
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
    splash("wifi connected", WiFi.localIP().toString().c_str(), C_GREEN);
    delay(1200);
  } else {
    splash("WIFI FAILED", "check config.h + reboot", GC9A01A_RED);
    Serial.println("\nWiFi failed. Check credentials in config.h");
    while (true) delay(1000);
  }
}

// ---------- ADS-B fetch ----------
void copyStr(char *dst, size_t n, const char *src) {
  if (!src) src = "";
  strncpy(dst, src, n - 1);
  dst[n - 1] = 0;
  for (int i = strlen(dst) - 1; i >= 0 && dst[i] == ' '; i--) dst[i] = 0;
}

bool fetchPlanes() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);

  int radiusNm = (int)(RADAR_RANGE_KM / 1.852f) + 4;
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

  JsonDocument filter;
  filter["ac"][0]["flight"]    = true;
  filter["ac"][0]["lat"]       = true;
  filter["ac"][0]["lon"]       = true;
  filter["ac"][0]["alt_baro"]  = true;
  filter["ac"][0]["gs"]        = true;
  filter["ac"][0]["track"]     = true;
  filter["ac"][0]["r"]         = true;
  filter["ac"][0]["t"]         = true;
  filter["ac"][0]["squawk"]    = true;
  filter["ac"][0]["baro_rate"] = true;
  filter["ac"][0]["ownOp"]     = true;
  filter["ac"][0]["desc"]      = true;

  String payload = http.getString();
  http.end();
  Serial.printf("Payload size: %u bytes, free heap: %u\n",
                payload.length(), ESP.getFreeHeap());

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  payload = String();
  if (err) {
    Serial.printf("JSON error: %s\n", err.c_str());
    return false;
  }

  planeCount = 0;
  for (JsonObject ac : doc["ac"].as<JsonArray>()) {
    if (planeCount >= MAX_AC) break;
    if (!ac["lat"].is<float>() || !ac["lon"].is<float>()) continue;
    if (ac["alt_baro"].is<const char *>()) continue;   // on ground

    Aircraft &p = planes[planeCount];
    copyStr(p.flight, sizeof(p.flight), ac["flight"] | "");
    if (p.flight[0] == 0) strcpy(p.flight, "------");
    copyStr(p.reg, sizeof(p.reg), ac["r"] | "");
    copyStr(p.typ, sizeof(p.typ), ac["t"] | "");
    copyStr(p.sqk, sizeof(p.sqk), ac["squawk"] | "");
    const char *opName = ac["ownOp"] | (const char *)nullptr;
    if (!opName) opName = ac["desc"] | "";
    copyStr(p.op, sizeof(p.op), opName);

    float lat = ac["lat"], lon = ac["lon"];
    p.distKm     = haversineKm(HOME_LAT, HOME_LON, lat, lon);
    p.bearingDeg = bearingDegF(HOME_LAT, HOME_LON, lat, lon);
    p.trackDeg   = ac["track"] | 0.0f;
    p.altFt      = ac["alt_baro"] | 0;
    p.gsKt       = (int)(ac["gs"] | 0.0f);
    p.vrFpm      = ac["baro_rate"] | 0;
    planeCount++;
  }

  for (int i = 0; i < planeCount - 1; i++)
    for (int j = i + 1; j < planeCount; j++)
      if (planes[j].distKm < planes[i].distKm) {
        Aircraft t = planes[i]; planes[i] = planes[j]; planes[j] = t;
      }

  Serial.printf("Aircraft airborne nearby: %d\n", planeCount);
  return true;
}

// ---------- Route lookup (adsbdb.com: airline + origin > destination) ----------
void fetchRoute(const char *callsign) {
  if (strcmp(routeFor, callsign) == 0) return;    // cached
  strcpy(routeFor, callsign);
  routeStr[0] = routeCities[0] = routeAirline[0] = 0;

  if (callsign[0] == '-' || callsign[0] == 0) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  char url[80];
  snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/callsign/%s", callsign);
  if (!http.begin(client, url)) return;

  int code = http.GET();
  if (code == 200) {
    JsonDocument doc;
    if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
      JsonObject fr = doc["response"]["flightroute"];
      if (!fr.isNull()) {
        const char *o  = fr["origin"]["iata_code"]        | "";
        const char *d  = fr["destination"]["iata_code"]   | "";
        const char *oc = fr["origin"]["municipality"]     | "";
        const char *dc = fr["destination"]["municipality"]| "";
        const char *al = fr["airline"]["name"]            | "";
        if (o[0] && d[0]) {
          snprintf(routeStr, sizeof(routeStr), "%s > %s", o, d);
          snprintf(routeCities, sizeof(routeCities), "%.12s > %.12s", oc, dc);
        }
        strncpy(routeAirline, al, sizeof(routeAirline) - 1);
      }
    }
  }
  http.end();
  Serial.printf("Route for %s: '%s' (%s, %s)\n",
                callsign, routeStr, routeCities, routeAirline);
}

// ---------- Page 1: radar ----------
void planeTriangle(int cx, int cy, float trackDeg, int size, uint16_t color) {
  float t = deg2rad(trackDeg);
  float ct = cosf(t), st = sinf(t);
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
  tft.drawLine(120, 10, 120, 230, C_CROSS);
  tft.drawLine(10, 120, 230, 120, C_CROSS);
  tft.drawCircle(120, 120, 37,  C_RING);
  tft.drawCircle(120, 120, 74,  C_RING);
  tft.drawCircle(120, 120, 110, C_RING);
  tft.fillCircle(120, 120, 2, C_TEXT);

  tft.setTextSize(1); tft.setTextColor(C_TEXT);
  tft.setCursor(117, 3);   tft.print("N");
  tft.setCursor(117, 231); tft.print("S");
  tft.setCursor(2, 117);   tft.print("W");
  tft.setCursor(232, 117); tft.print("E");
  tft.setTextColor(C_DIM);
  tft.setCursor(178, 124);
  tft.printf("%.0fkm", (double)(RADAR_RANGE_KM * 2 / 3));

  for (int i = planeCount - 1; i >= 0; i--) {
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
      tft.fillCircle(x, y, 2, C_PLANE);
    }
  }

  if (planeCount > 0) {
    Aircraft &n = planes[0];
    centerText(n.flight, 150, 2, C_NEAR);
    char info[48];
    snprintf(info, sizeof(info), "%.1fkm  %dm  %dkm/h",
             (double)n.distKm, (int)(n.altFt * 0.3048f), (int)(n.gsKt * 1.852f));
    centerText(info, 172, 1, C_INFO);
  } else {
    centerText("no aircraft", 160, 1, C_DIM);
  }

  char st[24];
  snprintf(st, sizeof(st), "%d planes", planeCount);
  centerText(st, 68, 1, C_DIM);
  pageDots();
}

// ---------- Page 2: nearest flight detail ----------
void statRow(int y, const char *lab1, const char *val1,
                    const char *lab2, const char *val2) {
  tft.setTextSize(1);
  tft.setTextColor(C_DIM);  tft.setCursor(44, y);  tft.print(lab1);
  tft.setTextColor(C_TEXT); tft.setCursor(76, y);  tft.print(val1);
  tft.setTextColor(C_DIM);  tft.setCursor(134, y); tft.print(lab2);
  tft.setTextColor(C_TEXT); tft.setCursor(166, y); tft.print(val2);
}

void drawDetail() {
  tft.fillScreen(C_BG);
  tft.drawCircle(120, 120, 118, C_RING);

  if (planeCount == 0) {
    centerText("NEAREST FLIGHT", 60, 1, C_DIM);
    centerText("no aircraft", 115, 2, C_DIM);
    pageDots();
    return;
  }

  Aircraft &n = planes[0];
  centerText("NEAREST FLIGHT", 26, 1, C_DIM);
  centerText(n.flight, 42, 3, C_NEAR);

  // route (origin > destination) + cities
  if (routeStr[0]) {
    centerText(routeStr, 70, 2, C_INFO);
    if (routeCities[0]) centerText(routeCities, 88, 1, C_DIM);
  } else {
    centerText("route unknown", 76, 1, C_DIM);
  }

  // airline (fallback: operator from ADS-B) + type/registration
  const char *who = routeAirline[0] ? routeAirline : n.op;
  if (who[0]) centerText(who, 100, 1, C_TEXT);
  char line[32];
  if (n.typ[0] || n.reg[0]) {
    snprintf(line, sizeof(line), "%s  %s", n.typ, n.reg);
    centerText(line, 112, 1, C_DIM);
  }

  tft.drawFastHLine(48, 124, 144, C_CROSS);

  // stats grid
  char v1[16], v2[16];
  snprintf(v1, sizeof(v1), "%.1f km", (double)n.distKm);
  snprintf(v2, sizeof(v2), "%.0f %s", (double)n.bearingDeg, compass8(n.bearingDeg));
  statRow(134, "DIST", v1, "BRG", v2);

  const char *trend = n.vrFpm > 300 ? "+" : (n.vrFpm < -300 ? "-" : "=");
  snprintf(v1, sizeof(v1), "%dm %s", (int)(n.altFt * 0.3048f), trend);
  snprintf(v2, sizeof(v2), "%d m/s", (int)(n.vrFpm * 0.00508f));
  statRow(150, "ALT", v1, "V/S", v2);

  snprintf(v1, sizeof(v1), "%dkm/h", (int)(n.gsKt * 1.852f));
  snprintf(v2, sizeof(v2), "%.0f %s", (double)n.trackDeg, compass8(n.trackDeg));
  statRow(166, "SPD", v1, "TRK", v2);

  snprintf(v1, sizeof(v1), "%s", n.sqk[0] ? n.sqk : "----");
  snprintf(v2, sizeof(v2), "%d nearby", planeCount);
  statRow(182, "SQK", v1, "AC", v2);

  // little arrow showing where to look (bearing) at top-right of card
  float b = deg2rad(planes[0].bearingDeg);
  int ax = 120 + (int)(sinf(b) * 104.0f);
  int ay = 120 - (int)(cosf(b) * 104.0f);
  tft.fillCircle(ax, ay, 4, C_NEAR);

  pageDots();
}

void drawPage() {
  if (page == 0) drawRadar();
  else           drawDetail();
}

// ---------- Main ----------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Plane Radar v1 (2 pages) booting...");
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(0);
  connectWifi();
  splash("fetching", "aircraft data...", C_INFO);
  lastPageSwitch = millis();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    splash("wifi lost", "reconnecting...", C_ORANGE);
    WiFi.reconnect();
    delay(4000);
    return;
  }

  uint32_t now = millis();

  if (now - lastFetch >= FETCH_INTERVAL_MS || lastFetch == 0) {
    lastFetch = now;
    if (fetchPlanes()) {
      failCount = 0;
      if (page == 1 && planeCount > 0) fetchRoute(planes[0].flight);
      drawPage();
    } else {
      failCount++;
      if (failCount >= 3) splash("API error", "retrying...", C_ORANGE);
    }
  }

  if (now - lastPageSwitch >= PAGE_INTERVAL_MS) {
    lastPageSwitch = now;
    page ^= 1;
    if (page == 1 && planeCount > 0) fetchRoute(planes[0].flight);
    drawPage();
  }

  delay(50);
}
