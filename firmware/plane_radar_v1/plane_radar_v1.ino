/*
  plane_radar_v1.ino — LIVE ADS-B plane radar for the Freising desk device.

  Hardware: ESP32-C3 Super Mini + GC9A01 1.28" round display
  Wiring:   see docs/display_wiring_guide.html

  Data:     https://api.adsb.lol   (aircraft positions, free, no key)
            https://api.adsbdb.com (airline + route lookup by callsign)
  Setup:    copy config.example.h to config.h, enter Wi-Fi name+password.

  Pages (auto-cycle):
    1. RADAR   — sweep, all airborne aircraft around home
    2. FLIGHT  — FlightRadar-style card for the nearest aircraft
    3. TRACK   — nearest aircraft's path + closest approach
    4. MUC     — Munich Airport: runways, arrivals & departures
    5. SPOTTER — coolest aircraft in the sky right now

  Libraries: Adafruit GC9A01A, ArduinoJson
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
#define PAGE_INTERVAL_MS 12000
#endif

// Munich Airport reference point + runway geometry (schematic)
#define MUC_LAT 48.3538
#define MUC_LON 11.7861
#define RWY_HDG 82.0f          // both runways 08/26
#define RWY_LEN 4.0f           // km
#define RWY_SEP 1.15f          // km, half separation from centreline
#define RWY_STAGGER 0.75f      // km, half stagger

// ---------- Display ----------
#define TFT_SCLK 4
#define TFT_MOSI 3
#define TFT_DC   10
#define TFT_CS   1
#define TFT_RST  0

Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);

// ---------- Glass-cockpit palette ----------
#define C_BG      tft.color565(2, 4, 14)
#define C_GRID    tft.color565(44, 76, 130)
#define C_GRIDDIM tft.color565(16, 28, 58)
#define C_BRIGHT  tft.color565(0, 220, 255)
#define C_SWEEP   tft.color565(0, 110, 190)
#define C_AMBER   tft.color565(255, 176, 0)
#define C_CYAN    tft.color565(120, 230, 255)
#define C_WHITE   GC9A01A_WHITE
#define C_DIM     tft.color565(120, 130, 145)
#define C_RED     tft.color565(255, 64, 48)
#define C_ORANGE  GC9A01A_ORANGE
#define C_GREY    tft.color565(130, 140, 150)

// ---------- Aircraft ----------
struct Aircraft {
  char  flight[10], reg[10], typ[6], sqk[6], op[28];
  float distKm, bearingDeg, trackDeg;
  float eastKm, northKm;                 // relative to home
  int   altFt, gsKt, vrFpm;
  bool  ground;
};
#define MAX_AC 40
Aircraft planes[MAX_AC];
int      planeCount = 0;

uint32_t lastFetch = 0, lastPageSwitch = 0;
int      failCount = 0;
uint8_t  page      = 0;   // 0 radar, 1 flight, 2 track, 3 airport, 4 spotter
#define PAGE_COUNT 5
const uint32_t pageDur[PAGE_COUNT] = {15000, 12000, 12000, 12000, 12000};

// MUC offset from home (computed in setup)
float mucE = 0, mucN = 0;

// two-slot route cache: slot 0 = nearest, slot 1 = spotter
struct RouteCache {
  char forCs[10], codes[24], cities[30], airline[24];
};
RouteCache rc[2];

// track history of nearest airborne aircraft
#define TRAIL_MAX 20
float  trailE[TRAIL_MAX], trailN[TRAIL_MAX];
int    trailLen = 0;
char   trailFor[10] = "";

// continuous sweep state
int      sweepR = 8;
uint32_t lastSweepStep = 0;

// dynamic pixels on radar page (low-flicker erase)
struct Blip { int16_t x, y; uint8_t r; };
Blip prevBlips[MAX_AC + 1];
int  prevBlipCount = 0;

// ---------- Math ----------
float deg2rad(float d) { return d * 0.017453293f; }

float haversineKm(float la1, float lo1, float la2, float lo2) {
  float dLa = deg2rad(la2 - la1), dLo = deg2rad(lo2 - lo1);
  float a = sinf(dLa/2)*sinf(dLa/2) +
            cosf(deg2rad(la1))*cosf(deg2rad(la2))*sinf(dLo/2)*sinf(dLo/2);
  return 6371.0f * 2 * atan2f(sqrtf(a), sqrtf(1 - a));
}

float bearingDegF(float la1, float lo1, float la2, float lo2) {
  float dLo = deg2rad(lo2 - lo1);
  float y = sinf(dLo) * cosf(deg2rad(la2));
  float x = cosf(deg2rad(la1))*sinf(deg2rad(la2)) -
            sinf(deg2rad(la1))*cosf(deg2rad(la2))*cosf(dLo);
  return fmodf(atan2f(y, x) * 57.29578f + 360.0f, 360.0f);
}

const char *compass8(float deg) {
  static const char *n[8] = {"N","NE","E","SE","S","SW","W","NW"};
  return n[((int)((deg + 22.5f) / 45.0f)) % 8];
}

int nearestAirborne() {
  for (int i = 0; i < planeCount; i++)
    if (!planes[i].ground) return i;
  return -1;
}

void centerText(const char *s, int y, uint8_t size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color);
  tft.setCursor(120 - (int)strlen(s) * 3 * size, y);
  tft.print(s);
}

// ---------- Coolness scoring ----------
struct CoolRule { const char *pfx; uint8_t score; const char *label; };

const CoolRule typeRules[] = {
  {"A388", 100, "SUPERJUMBO A380"},
  {"A124",  99, "ANTONOV AN-124"},
  {"B52",   99, "B-52 BOMBER"},
  {"C5M",   97, "C-5 GALAXY"},
  {"C17",   96, "C-17 GLOBEMASTER"},
  {"EUFI",  96, "EUROFIGHTER"},
  {"F16",   95, "FIGHTER JET"},
  {"F35",   95, "FIGHTER JET"},
  {"A400",  93, "A400M ATLAS"},
  {"C130",  90, "C-130 HERCULES"},
  {"IL76",  90, "ILYUSHIN IL-76"},
  {"B744",  92, "QUEEN B747"},
  {"B748",  92, "QUEEN B747-8"},
  {"MD11",  86, "RARE TRIJET MD-11"},
  {"A346",  72, "RARE QUAD A340"},
  {"A345",  72, "RARE QUAD A340"},
  {"A343",  68, "RARE QUAD A340"},
  {"B77W",  55, "BIG TWIN B777"},
  {"B773",  53, "BIG TWIN B777"},
  {"B772",  50, "BIG TWIN B777"},
  {"A35K",  52, "AIRBUS A350-1000"},
  {"A359",  48, "AIRBUS A350"},
  {"B78",   45, "DREAMLINER B787"},
  {"A339",  42, "AIRBUS A330neo"},
  {"A33",   38, "AIRBUS A330"},
  {"B76",   40, "BOEING 767"},
};
const CoolRule csRules[] = {
  {"AF1", 100, "AIR FORCE ONE?!"},
  {"SAM",  98, "US GOVT SPECIAL"},
  {"GAF",  95, "GERMAN AIR FORCE"},
  {"NATO", 95, "NATO FLIGHT"},
  {"RRR",  92, "ROYAL AIR FORCE"},
  {"CTM",  88, "FRENCH AIR FORCE"},
  {"MMF",  88, "NATO TANKER"},
  {"RCH",  86, "US AF TRANSPORT"},
  {"DUKE", 85, "US ARMY"},
};

int coolScore(const Aircraft &p, const char **label) {
  int best = 0; *label = "HEAVY TRAFFIC";
  if (strcmp(p.sqk, "7700") == 0 || strcmp(p.sqk, "7600") == 0 ||
      strcmp(p.sqk, "7500") == 0) { *label = "EMERGENCY SQUAWK"; return 100; }
  for (unsigned i = 0; i < sizeof(typeRules)/sizeof(typeRules[0]); i++)
    if (strncmp(p.typ, typeRules[i].pfx, strlen(typeRules[i].pfx)) == 0 &&
        typeRules[i].score > best) { best = typeRules[i].score; *label = typeRules[i].label; }
  for (unsigned i = 0; i < sizeof(csRules)/sizeof(csRules[0]); i++)
    if (strncmp(p.flight, csRules[i].pfx, strlen(csRules[i].pfx)) == 0 &&
        csRules[i].score > best) { best = csRules[i].score; *label = csRules[i].label; }
  return best;
}

int coolestIdx() {
  int best = -1, bestScore = -1;
  const char *l;
  for (int i = 0; i < planeCount; i++) {
    int s = coolScore(planes[i], &l);
    if (s > bestScore) { bestScore = s; best = i; }
  }
  return best;
}

// ---------- Shared chrome ----------
void drawBezel() {
  tft.drawCircle(120, 120, 119, C_GRID);
  tft.drawCircle(120, 120, 118, C_GRIDDIM);
  for (int a = 0; a < 360; a += 10) {
    float r = deg2rad((float)a);
    float s = sinf(r), c = cosf(r);
    int   l = (a % 90 == 0) ? 9 : (a % 30 == 0 ? 6 : 3);
    tft.drawLine(120 + (int)(s*118), 120 - (int)(c*118),
                 120 + (int)(s*(118-l)), 120 - (int)(c*(118-l)),
                 (a % 30 == 0) ? C_GRID : C_GRIDDIM);
  }
  tft.setTextSize(1);
  tft.setTextColor(C_BRIGHT);
  tft.setCursor(117, 14);  tft.print("N");
  tft.setTextColor(C_GRID);
  tft.setCursor(117, 219); tft.print("S");
  tft.setCursor(14, 117);  tft.print("W");
  tft.setCursor(220, 117); tft.print("E");
}

void pageDots() {
  for (int i = 0; i < PAGE_COUNT; i++)
    tft.fillCircle(96 + i * 12, 228, 2, i == page ? C_BRIGHT : C_GRIDDIM);
}

// ---------- Boot ----------
void splash(const char *l1, const char *l2, uint16_t color) {
  tft.fillScreen(C_BG);
  drawBezel();
  centerText("PLANE RADAR", 88, 2, C_AMBER);
  centerText("F R E I S I N G", 108, 1, C_GRID);
  centerText(l1, 130, 1, C_DIM);
  centerText(l2, 144, 1, color);
}

void bootAnimation() {
  tft.fillScreen(C_BG);
  for (int r = 8; r <= 116; r += 4) {
    tft.drawCircle(120, 120, r, C_SWEEP);
    if (r > 12) tft.drawCircle(120, 120, r - 4, C_BG);
    delay(14);
  }
  tft.drawCircle(120, 120, 112, C_BG);
  splash("", "", C_DIM);
}

void connectWifi() {
  splash("connecting to", WIFI_SSID, C_CYAN);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 25000) {
    delay(400);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi OK, IP: %s\n", WiFi.localIP().toString().c_str());
    splash("wifi connected", WiFi.localIP().toString().c_str(), C_BRIGHT);
    delay(1000);
  } else {
    splash("WIFI FAILED", "check config.h + reboot", C_RED);
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
  if (http.GET() != 200) { http.end(); return false; }

  JsonDocument filter;
  JsonObject f = filter["ac"].add<JsonObject>();
  f["flight"] = f["lat"] = f["lon"] = f["alt_baro"] = f["gs"] = f["track"] = true;
  f["r"] = f["t"] = f["squawk"] = f["baro_rate"] = f["ownOp"] = f["desc"] = true;

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  payload = String();
  if (err) { Serial.printf("JSON error: %s\n", err.c_str()); return false; }

  planeCount = 0;
  for (JsonObject ac : doc["ac"].as<JsonArray>()) {
    if (planeCount >= MAX_AC) break;
    if (!ac["lat"].is<float>() || !ac["lon"].is<float>()) continue;

    bool onGround = ac["alt_baro"].is<const char *>();
    const char *cs = ac["flight"] | "";
    if (onGround && cs[0] == 0) continue;          // skip ground vehicles

    Aircraft &p = planes[planeCount];
    p.ground = onGround;
    copyStr(p.flight, sizeof(p.flight), cs);
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
    p.eastKm     = (lon - HOME_LON) * 111.32f * cosf(deg2rad(lat));
    p.northKm    = (lat - HOME_LAT) * 110.57f;
    p.trackDeg   = ac["track"] | 0.0f;
    p.altFt      = onGround ? 0 : (int)(ac["alt_baro"] | 0);
    p.gsKt       = (int)(ac["gs"] | 0.0f);
    p.vrFpm      = ac["baro_rate"] | 0;
    planeCount++;
  }

  for (int i = 0; i < planeCount - 1; i++)
    for (int j = i + 1; j < planeCount; j++)
      if (planes[j].distKm < planes[i].distKm) {
        Aircraft t = planes[i]; planes[i] = planes[j]; planes[j] = t;
      }

  int na = nearestAirborne();
  if (na >= 0) {
    if (strcmp(trailFor, planes[na].flight) != 0) {
      strcpy(trailFor, planes[na].flight);
      trailLen = 0;
    }
    if (trailLen == TRAIL_MAX) {
      memmove(trailE, trailE + 1, sizeof(float) * (TRAIL_MAX - 1));
      memmove(trailN, trailN + 1, sizeof(float) * (TRAIL_MAX - 1));
      trailLen--;
    }
    trailE[trailLen] = planes[na].eastKm;
    trailN[trailLen] = planes[na].northKm;
    trailLen++;
  }

  Serial.printf("Aircraft: %d (air+gnd), heap: %u\n", planeCount, ESP.getFreeHeap());
  return true;
}

// ---------- Route lookup (two cache slots) ----------
void fetchRoute(const char *callsign, int slot) {
  RouteCache &r = rc[slot];
  if (strcmp(r.forCs, callsign) == 0) return;
  strcpy(r.forCs, callsign);
  r.codes[0] = r.cities[0] = r.airline[0] = 0;
  if (callsign[0] == '-' || callsign[0] == 0) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  char url[80];
  snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/callsign/%s", callsign);
  if (!http.begin(client, url)) return;
  if (http.GET() == 200) {
    JsonDocument doc;
    if (deserializeJson(doc, http.getString()) == DeserializationError::Ok) {
      JsonObject fr = doc["response"]["flightroute"];
      if (!fr.isNull()) {
        const char *o  = fr["origin"]["iata_code"]         | "";
        const char *d  = fr["destination"]["iata_code"]    | "";
        const char *oc = fr["origin"]["municipality"]      | "";
        const char *dc = fr["destination"]["municipality"] | "";
        strncpy(r.airline, fr["airline"]["name"] | "", sizeof(r.airline)-1);
        if (o[0] && d[0]) {
          snprintf(r.codes, sizeof(r.codes), "%s > %s", o, d);
          snprintf(r.cities, sizeof(r.cities), "%.12s > %.12s", oc, dc);
        }
      }
    }
  }
  http.end();
  Serial.printf("Route[%d] %s: '%s' %s\n", slot, callsign, r.codes, r.airline);
}

// ---------- Drawing primitives ----------
void planeTriangle(int cx, int cy, float trackDeg, int size, uint16_t color) {
  float t = deg2rad(trackDeg), ct = cosf(t), st = sinf(t);
  float px[3] = {0, -0.7f * size, 0.7f * size};
  float py[3] = {(float)-size, 0.8f * size, 0.8f * size};
  int x[3], y[3];
  for (int i = 0; i < 3; i++) {
    x[i] = cx + (int)(px[i] * ct - py[i] * st);
    y[i] = cy + (int)(px[i] * st + py[i] * ct);
  }
  tft.fillTriangle(x[0], y[0], x[1], y[1], x[2], y[2], color);
  tft.drawTriangle(x[0], y[0], x[1], y[1], x[2], y[2], C_BG);
}

void dashedLine(int x0, int y0, float dirDeg, int len, uint16_t color) {
  float t = deg2rad(dirDeg), s = sinf(t), c = cosf(t);
  for (int d = 0; d < len; d += 8) {
    int xa = x0 + (int)(s * d),       ya = y0 - (int)(c * d);
    int xb = x0 + (int)(s * (d + 4)), yb = y0 - (int)(c * (d + 4));
    if ((xa-120)*(xa-120) + (ya-120)*(ya-120) > 108*108) break;
    tft.drawLine(xa, ya, xb, yb, color);
  }
}

// ---------- Page 1: RADAR ----------
void radarGrid() {
  tft.drawCircle(120, 120, 36,  C_GRIDDIM);
  tft.drawCircle(120, 120, 72,  C_GRIDDIM);
  tft.drawCircle(120, 120, 108, C_GRID);
  tft.drawLine(120, 14, 120, 226, C_GRIDDIM);
  tft.drawLine(14, 120, 226, 120, C_GRIDDIM);
  tft.fillCircle(120, 120, 2, C_BRIGHT);
  tft.setTextSize(1);
  tft.setTextColor(C_GRID);
  tft.setCursor(158, 124); tft.printf("%.0f", (double)(RADAR_RANGE_KM * 2 / 3));
  tft.setCursor(194, 124); tft.print("km");
}

void radarStatic() {
  tft.fillScreen(C_BG);
  drawBezel();
  radarGrid();
  pageDots();
}

void eraseBlips() {
  for (int i = 0; i < prevBlipCount; i++)
    tft.fillCircle(prevBlips[i].x, prevBlips[i].y, prevBlips[i].r, C_BG);
  prevBlipCount = 0;
  tft.fillRect(36, 146, 168, 38, C_BG);
  tft.fillRect(78, 60, 84, 10, C_BG);
}

void radarPaint(bool record) {
  radarGrid();
  if (record) prevBlipCount = 0;

  for (int i = planeCount - 1; i >= 0; i--) {
    Aircraft &p = planes[i];
    if (p.ground) continue;
    float b = deg2rad(p.bearingDeg);
    int x, y; uint8_t r;
    if (p.distKm <= RADAR_RANGE_KM) {
      float rr = (p.distKm / RADAR_RANGE_KM) * 106.0f;
      x = 120 + (int)(sinf(b) * rr);
      y = 120 - (int)(cosf(b) * rr);
      planeTriangle(x, y, p.trackDeg, 6, i == nearestAirborne() ? C_AMBER : C_BRIGHT);
      r = 9;
    } else {
      x = 120 + (int)(sinf(b) * 111.0f);
      y = 120 - (int)(cosf(b) * 111.0f);
      tft.fillCircle(x, y, 3, C_RED);
      tft.drawCircle(x, y, 4, C_BG);
      r = 5;
    }
    if (record && prevBlipCount < MAX_AC + 1) {
      prevBlips[prevBlipCount].x = x;
      prevBlips[prevBlipCount].y = y;
      prevBlips[prevBlipCount].r = r;
      prevBlipCount++;
    }
  }

  int na = nearestAirborne();
  if (na >= 0) {
    Aircraft &n = planes[na];
    centerText(n.flight, 148, 2, C_AMBER);
    char info[48];
    snprintf(info, sizeof(info), "%.1fkm %s  %dm  %dkm/h",
             (double)n.distKm, compass8(n.bearingDeg),
             (int)(n.altFt * 0.3048f), (int)(n.gsKt * 1.852f));
    centerText(info, 170, 1, C_CYAN);
  } else {
    centerText("scanning...", 160, 1, C_DIM);
  }
  char st[20];
  snprintf(st, sizeof(st), "%d CONTACTS", planeCount);
  centerText(st, 61, 1, C_GRID);
}

void radarDynamic() { eraseBlips(); radarPaint(true); }

void sweepStep() {
  tft.drawCircle(120, 120, sweepR, C_SWEEP);
  if (sweepR > 10) tft.drawCircle(120, 120, sweepR - 4, C_BG);
  sweepR += 4;
  if (sweepR > 104) {
    tft.drawCircle(120, 120, sweepR - 4, C_BG);
    tft.drawCircle(120, 120, sweepR - 8, C_BG);
    sweepR = 8;
    radarPaint(false);
  }
}

// ---------- Flight card (shared by page 2 + 5) ----------
void statRow(int y, const char *l1, const char *v1,
             const char *l2, const char *v2, uint16_t vc1, uint16_t vc2) {
  tft.setTextSize(1);
  tft.setTextColor(C_GRID);  tft.setCursor(44, y);  tft.print(l1);
  tft.setTextColor(vc1);     tft.setCursor(74, y);  tft.print(v1);
  tft.setTextColor(C_GRID);  tft.setCursor(132, y); tft.print(l2);
  tft.setTextColor(vc2);     tft.setCursor(160, y); tft.print(v2);
}

void flightCard(Aircraft &n, RouteCache &r, const char *topBadge, uint16_t badgeColor) {
  tft.fillRect(28, 36, 184, 84, C_BG);
  tft.fillRect(28, 128, 184, 64, C_BG);
  tft.fillRect(40, 196, 160, 16, C_BG);

  if (topBadge[0]) centerText(topBadge, 38, 1, badgeColor);
  centerText(n.flight, 50, 3, C_AMBER);
  if (r.codes[0]) {
    centerText(r.codes, 78, 2, C_CYAN);
    if (r.cities[0]) centerText(r.cities, 96, 1, C_DIM);
  } else {
    centerText("route unknown", 84, 1, C_DIM);
  }
  const char *who = r.airline[0] ? r.airline : n.op;
  if (who[0]) centerText(who, 108, 1, C_WHITE);

  char v1[16], v2[16];
  uint16_t distC = n.distKm < 5 ? C_RED : (n.distKm < 12 ? C_AMBER : C_WHITE);
  snprintf(v1, sizeof(v1), "%.1f km", (double)n.distKm);
  snprintf(v2, sizeof(v2), "%.0f %s", (double)n.bearingDeg, compass8(n.bearingDeg));
  statRow(132, "DIST", v1, "BRG", v2, distC, C_WHITE);

  uint16_t vsC = n.vrFpm > 300 ? C_BRIGHT : (n.vrFpm < -300 ? C_AMBER : C_WHITE);
  if (n.ground) snprintf(v1, sizeof(v1), "GROUND");
  else          snprintf(v1, sizeof(v1), "%d m", (int)(n.altFt * 0.3048f));
  snprintf(v2, sizeof(v2), "%+d m/s", (int)(n.vrFpm * 0.00508f));
  statRow(148, "ALT", v1, "V/S", v2, C_WHITE, vsC);

  snprintf(v1, sizeof(v1), "%d km/h", (int)(n.gsKt * 1.852f));
  snprintf(v2, sizeof(v2), "%.0f %s", (double)n.trackDeg, compass8(n.trackDeg));
  statRow(164, "SPD", v1, "TRK", v2, C_WHITE, C_WHITE);

  char tr[16];
  snprintf(tr, sizeof(tr), "%s %s", n.typ[0] ? n.typ : "----", n.reg);
  snprintf(v2, sizeof(v2), "%s", n.sqk[0] ? n.sqk : "----");
  statRow(180, "A/C", tr, "SQK", v2, C_WHITE, C_WHITE);

  char look[20];
  snprintf(look, sizeof(look), "LOOK %s", compass8(n.bearingDeg));
  centerText(look, 200, 1, C_AMBER);
  float b = deg2rad(n.bearingDeg);
  int ax = 120 - (int)(strlen(look) * 3) - 12, ay = 203;
  tft.fillTriangle(ax + (int)(sinf(b)*6),      ay - (int)(cosf(b)*6),
                   ax + (int)(sinf(b+2.6f)*5), ay - (int)(cosf(b+2.6f)*5),
                   ax + (int)(sinf(b-2.6f)*5), ay - (int)(cosf(b-2.6f)*5), C_AMBER);
}

// ---------- Page 2: FLIGHT ----------
void detailStatic() {
  tft.fillScreen(C_BG);
  drawBezel();
  centerText("NEAREST FLIGHT", 26, 1, C_GRID);
  tft.drawFastHLine(52, 124, 136, C_GRIDDIM);
  pageDots();
}

void detailDynamic() {
  int na = nearestAirborne();
  if (na < 0) { centerText("no contacts", 110, 2, C_DIM); return; }
  flightCard(planes[na], rc[0], "", C_DIM);
}

// ---------- Page 3: TRACK ----------
void trackStatic() {
  tft.fillScreen(C_BG);
  drawBezel();
  centerText("FLIGHT TRACK", 26, 1, C_GRID);
  pageDots();
}

void trackDynamic() {
  tft.fillRect(24, 36, 192, 180, C_BG);
  int na = nearestAirborne();
  if (na < 0) { centerText("no contacts", 110, 2, C_DIM); return; }
  Aircraft &n = planes[na];

  float range = n.distKm * 1.5f;
  if (range < 8) range = 8;
  float scale = 86.0f / range;

  tft.drawCircle(120, 126, 43, C_GRIDDIM);
  tft.drawCircle(120, 126, 86, C_GRIDDIM);
  tft.setTextSize(1); tft.setTextColor(C_GRID);
  tft.setCursor(166, 130); tft.printf("%.0fkm", (double)range);

  tft.fillCircle(120, 126, 3, C_CYAN);
  tft.drawCircle(120, 126, 6, C_CYAN);
  tft.setTextColor(C_CYAN);
  tft.setCursor(111, 136); tft.print("YOU");

  for (int i = 0; i < trailLen; i++) {
    int x = 120 + (int)(trailE[i] * scale);
    int y = 126 - (int)(trailN[i] * scale);
    if ((x-120)*(x-120) + (y-126)*(y-126) > 88*88) continue;
    uint16_t c = (i > trailLen - 4) ? C_BRIGHT : (i > trailLen - 10 ? C_SWEEP : C_GRIDDIM);
    tft.fillCircle(x, y, (i == trailLen - 1) ? 2 : 1, c);
  }

  int px = 120 + (int)(n.eastKm * scale);
  int py = 126 - (int)(n.northKm * scale);
  if ((px-120)*(px-120) + (py-126)*(py-126) <= 88*88) {
    dashedLine(px, py, n.trackDeg, 150, C_GRID);
    dashedLine(px, py, n.trackDeg + 180.0f, 150, C_GRIDDIM);
    planeTriangle(px, py, n.trackDeg, 7, C_AMBER);
  }

  centerText(n.flight, 40, 2, C_AMBER);

  float spdKms = n.gsKt * 1.852f / 3600.0f;
  float vx = sinf(deg2rad(n.trackDeg)) * spdKms;
  float vy = cosf(deg2rad(n.trackDeg)) * spdKms;
  char cpa[36];
  float v2 = vx * vx + vy * vy;
  if (v2 > 1e-8f) {
    float tSec = -(n.eastKm * vx + n.northKm * vy) / v2;
    if (tSec > 0 && tSec < 3600) {
      float ce = n.eastKm + vx * tSec, cn = n.northKm + vy * tSec;
      float cd = sqrtf(ce * ce + cn * cn);
      snprintf(cpa, sizeof(cpa), "closest %.1fkm in %d:%02d",
               (double)cd, (int)tSec / 60, (int)tSec % 60);
    } else {
      snprintf(cpa, sizeof(cpa), "moving away  %.1fkm", (double)n.distKm);
    }
  } else {
    snprintf(cpa, sizeof(cpa), "%.1fkm", (double)n.distKm);
  }
  centerText(cpa, 200, 1, C_CYAN);
}

// ---------- Page 4: MUC AIRPORT ----------
#define MUC_SCALE 8.0f          // px per km (view radius ~12km)

void mucToScreen(float e, float n, int *x, int *y) {
  *x = 120 + (int)((e - mucE) * MUC_SCALE);
  *y = 126 - (int)((n - mucN) * MUC_SCALE);
}

void drawRunway(float ce, float cn) {   // centre offset from MUC ref (km)
  float u_e = sinf(deg2rad(RWY_HDG)), u_n = cosf(deg2rad(RWY_HDG));
  int x0, y0, x1, y1;
  mucToScreen(mucE + ce - u_e * RWY_LEN/2, mucN + cn - u_n * RWY_LEN/2, &x0, &y0);
  mucToScreen(mucE + ce + u_e * RWY_LEN/2, mucN + cn + u_n * RWY_LEN/2, &x1, &y1);
  for (int o = -1; o <= 1; o++)
    tft.drawLine(x0, y0 + o, x1, y1 + o, C_GREY);
  // threshold marks
  tft.fillCircle(x0, y0, 2, C_WHITE);
  tft.fillCircle(x1, y1, 2, C_WHITE);
}

void airportStatic() {
  tft.fillScreen(C_BG);
  drawBezel();
  centerText("MUC  MUNICH", 26, 1, C_AMBER);
  pageDots();
}

void airportDynamic() {
  tft.fillRect(24, 36, 192, 180, C_BG);

  // runways 08L/26R (north) and 08R/26L (south), schematic
  drawRunway(-RWY_STAGGER, +RWY_SEP);
  drawRunway(+RWY_STAGGER, -RWY_SEP);
  // terminal area blob between runways
  int tx, ty;
  mucToScreen(mucE, mucN, &tx, &ty);
  tft.fillRoundRect(tx - 8, ty - 3, 16, 6, 2, C_GRIDDIM);

  // your home position
  int hx, hy;
  mucToScreen(0, 0, &hx, &hy);
  if ((hx-120)*(hx-120) + (hy-126)*(hy-126) <= 88*88) {
    tft.fillCircle(hx, hy, 2, C_CYAN);
    tft.setTextSize(1); tft.setTextColor(C_CYAN);
    tft.setCursor(hx - 8, hy + 6); tft.print("YOU");
  }

  int arr = 0, dep = 0;
  for (int i = planeCount - 1; i >= 0; i--) {
    Aircraft &p = planes[i];
    float dMucE = p.eastKm - mucE, dMucN = p.northKm - mucN;
    float dMuc  = sqrtf(dMucE * dMucE + dMucN * dMucN);
    if (dMuc > 25) continue;

    bool low = !p.ground && p.altFt < 8000;
    if (low && p.vrFpm < -300) arr++;
    if (low && p.vrFpm >  300) dep++;

    int x, y;
    mucToScreen(p.eastKm, p.northKm, &x, &y);
    if ((x-120)*(x-120) + (y-126)*(y-126) > 86*86) continue;

    if (p.ground) {
      tft.fillCircle(x, y, 2, C_GREY);
    } else if (low) {
      planeTriangle(x, y, p.trackDeg, 6,
                    p.vrFpm < -300 ? C_BRIGHT : (p.vrFpm > 300 ? C_AMBER : C_WHITE));
    } else {
      tft.fillCircle(x, y, 2, C_GRIDDIM);   // high overflight
    }
  }

  // legend + counts
  char b1[20], b2[20];
  snprintf(b1, sizeof(b1), "ARR %d", arr);
  snprintf(b2, sizeof(b2), "DEP %d", dep);
  tft.setTextSize(2);
  tft.setTextColor(C_BRIGHT); tft.setCursor(58, 196);  tft.print(b1);
  tft.setTextColor(C_AMBER);  tft.setCursor(134, 196); tft.print(b2);
}

// ---------- Page 5: SPOTTER ----------
void spotterStatic() {
  tft.fillScreen(C_BG);
  drawBezel();
  centerText("PLANE SPOTTER", 26, 1, C_GRID);
  tft.drawFastHLine(52, 124, 136, C_GRIDDIM);
  pageDots();
}

void spotterDynamic() {
  int ci = coolestIdx();
  if (ci < 0) { centerText("no contacts", 110, 2, C_DIM); return; }
  const char *label;
  int score = coolScore(planes[ci], &label);
  uint16_t badgeC = score >= 85 ? C_RED : (score >= 50 ? C_AMBER : C_DIM);
  char badge[28];
  if (score >= 40) snprintf(badge, sizeof(badge), "* %s *", label);
  else             snprintf(badge, sizeof(badge), "biggest around");
  flightCard(planes[ci], rc[1], badge, badgeC);
}

// ---------- Page management ----------
bool skipPage(uint8_t p) {
  if ((p == 1 || p == 2) && nearestAirborne() < 0) return true;
  if (p == 4 && planeCount == 0) return true;
  return false;
}

void drawPageFull() {
  prevBlipCount = 0;
  sweepR = 8;
  switch (page) {
    case 0: radarStatic();   radarDynamic();   break;
    case 1: detailStatic();  detailDynamic();  break;
    case 2: trackStatic();   trackDynamic();   break;
    case 3: airportStatic(); airportDynamic(); break;
    case 4: spotterStatic(); spotterDynamic(); break;
  }
}

void drawPageUpdate() {
  switch (page) {
    case 0: radarDynamic();   break;
    case 1: detailDynamic();  break;
    case 2: trackDynamic();   break;
    case 3: airportDynamic(); break;
    case 4: spotterDynamic(); break;
  }
}

void prefetchRoutes() {
  int na = nearestAirborne();
  if ((page == 1 || page == 2) && na >= 0) fetchRoute(planes[na].flight, 0);
  if (page == 4) {
    int ci = coolestIdx();
    if (ci >= 0) fetchRoute(planes[ci].flight, 1);
  }
}

// ---------- Main ----------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Plane Radar v3 (5 pages) booting...");
  mucE = (MUC_LON - HOME_LON) * 111.32f * cosf(deg2rad(MUC_LAT));
  mucN = (MUC_LAT - HOME_LAT) * 110.57f;
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.begin();
  tft.setRotation(0);
  bootAnimation();
  connectWifi();
  splash("fetching", "aircraft data...", C_CYAN);
  lastPageSwitch = millis();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    splash("wifi lost", "reconnecting...", C_ORANGE);
    WiFi.reconnect();
    delay(4000);
    drawPageFull();
    return;
  }

  uint32_t now = millis();

  if (now - lastFetch >= FETCH_INTERVAL_MS || lastFetch == 0) {
    bool first = (lastFetch == 0);
    lastFetch = now;
    if (fetchPlanes()) {
      failCount = 0;
      prefetchRoutes();
      if (first) drawPageFull(); else drawPageUpdate();
    } else if (++failCount >= 3) {
      splash("API error", "retrying...", C_ORANGE);
    }
  }

  if (page == 0 && now - lastSweepStep >= 35) {
    lastSweepStep = now;
    sweepStep();
  }

  if (now - lastPageSwitch >= pageDur[page]) {
    lastPageSwitch = now;
    do { page = (page + 1) % PAGE_COUNT; } while (skipPage(page));
    prefetchRoutes();
    drawPageFull();
  }

  delay(40);
}
