/*
  plane_radar_v1.ino — LIVE ADS-B plane radar for the Freising desk device.

  Hardware: ESP32-C3 Super Mini + GC9A01 1.28" round display
  Wiring:   see docs/display_wiring_guide.html

  Data:     https://api.adsb.lol          (aircraft positions, free, no key)
            https://api.adsbdb.com        (airline + route lookup by callsign)
            https://aviationweather.gov   (MUC METAR weather, free, no key)
  Setup:    copy config.example.h to config.h, enter Wi-Fi name+password.

  Pages (auto-cycle; rare/emergency traffic force-jumps to SPOTTER):
    1. RADAR   — rotating beam, all airborne aircraft around home
    2. FLIGHT  — FlightRadar-style card for the nearest aircraft
    3. TRACK   — nearest aircraft's path + closest approach (CPA)
    4. MUC MAP — Munich Airport runway close-up with live traffic
    5. MUC OPS — next likely arrivals/departures board + cool visitor
    6. MUC WX  — latest EDDM METAR summary
    7. SPOTTER — coolest aircraft in the sky right now, and why

  Removed in the v6 refactor (kept the codebase honest and the cycle short):
    - OpenSky page: duplicated counts we already derive from adsb.lol data,
      anonymous access is heavily rate-limited, and its unfiltered JSON parse
      of a whole MUC bounding box risked large heap spikes on the C3.
    - aviationstack page: needs a paid-tier key to be useful; the free tier is
      ~100 calls/month, so the page mostly displayed "ADD API KEY".
    - MUC FIELD page: static facts that never change; dead weight in the
      carousel after the first viewing.

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
#define RWY_HDG 82.0f          // MUC's parallel runways are approximately 08/26.
#define RWY_LEN 4.0f           // Real-world runway length in km; used for the scaled map.
#define RWY_SEP 1.15f          // Half separation from airport centerline to each runway.
#define RWY_STAGGER 0.75f      // Small schematic stagger so the two runway labels do not collide.

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
#define C_GREEN   tft.color565(60, 240, 120)
#define C_BLUE    tft.color565(72, 150, 255)
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
uint8_t  page      = 0;   // 0 radar, 1 flight, 2 track, 3 MUC map, 4 MUC ops, 5 weather, 6 spotter
#define PAGE_COUNT 7
// The radar and nearest-flight pages carry the most live value, so they hold
// the screen longer; the reference/board pages rotate through more quickly.
const uint32_t pageDur[PAGE_COUNT] = {26000, 22000, 12000, 12000, 13000, 12000, 12000};

// One-shot alert latch: when a very rare aircraft or an emergency squawk shows
// up (cool score >= ALERT_SCORE), the carousel jumps straight to the SPOTTER
// page once per callsign, so the user cannot miss it while another page idles.
#define ALERT_SCORE 95
char lastAlertCs[10] = "";

// MUC offset from home (computed in setup)
float mucE = 0, mucN = 0;

// Route cache slots.
//
// Slot 0 is the nearest-flight card, slot 1 is the global spotter card, and
// slots 2-6 are temporary MUC board rows. Keeping a small fixed cache avoids
// heap churn on ESP32 while still letting the airport page show origin >
// destination information for several live flights.
struct RouteCache {
  char forCs[10], codes[24], cities[30], airline[24];
};
#define ROUTE_SLOT_NEAREST 0
#define ROUTE_SLOT_SPOTTER 1
#define ROUTE_SLOT_MUC_BASE 2
#define ROUTE_CACHE_COUNT 7
RouteCache rc[ROUTE_CACHE_COUNT];

// Extra airport-data pages.
//
// These are intentionally small caches. Each page fetches rarely and stores only
// the values that fit on the round TFT. That keeps the ESP32 responsive and
// avoids turning one tiny display into a general-purpose flight dashboard.
struct MucWeather {
  bool ok = false;
  char raw[96] = "";
  char wind[16] = "--";
  char vis[12] = "--";
  char temp[12] = "--";
  char qnh[12] = "--";
  uint32_t lastFetch = 0;
};
MucWeather mucWx;

// The METAR page refreshes rarely: aviationweather.gov asks clients to stay
// well under one request per minute, and airport weather only changes on the
// half-hour anyway. The page itself redraws whenever it is opened; this timer
// only controls actual network I/O.
#define MUC_WX_REFRESH_MS 300000UL

// track history of nearest airborne aircraft
#define TRAIL_MAX 20
float  trailE[TRAIL_MAX], trailN[TRAIL_MAX];
int    trailLen = 0;
char   trailFor[10] = "";

// Continuous animation state.
//
// The aircraft API updates in bursts, but the screen looks much better when
// traffic keeps moving between fetches. lastLiveStep controls those light
// animation redraws, while sweepDeg stores the rotating radar beam angle.
int      sweepDeg = 0;
uint32_t lastSweepStep = 0, lastLiveStep = 0;

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

float angleDiffDeg(float a, float b) {
  float d = fabsf(a - b);
  return d > 180.0f ? 360.0f - d : d;
}

float groundSpeedKmh(const Aircraft &p) {
  return p.gsKt * 1.852f;
}

float etaMinForDistance(float distKm, const Aircraft &p) {
  float kmh = groundSpeedKmh(p);
  if (kmh < 40.0f) return -1.0f;
  return distKm / kmh * 60.0f;
}

float distanceToMucKm(const Aircraft &p) {
  float e = p.eastKm - mucE, n = p.northKm - mucN;
  return sqrtf(e * e + n * n);
}

void liveAircraftOffsetKm(const Aircraft &p, float *eastKm, float *northKm) {
  *eastKm = p.eastKm;
  *northKm = p.northKm;
  if (lastFetch == 0 || p.gsKt < 5) return;

  // ADS-B positions arrive every FETCH_INTERVAL_MS, so the aircraft would look
  // frozen between network updates if we only drew the raw API coordinates. This
  // short dead-reckoning step projects the plane forward using its current track
  // and speed. The cap keeps stale data from sliding across the screen forever
  // if Wi-Fi/API fetches pause.
  float ageSec = (millis() - lastFetch) / 1000.0f;
  float maxAgeSec = FETCH_INTERVAL_MS / 1000.0f;
  if (ageSec > maxAgeSec) ageSec = maxAgeSec;

  float km = groundSpeedKmh(p) / 3600.0f * ageSec;
  *eastKm  += sinf(deg2rad(p.trackDeg)) * km;
  *northKm += cosf(deg2rad(p.trackDeg)) * km;
}

bool isMovingTowardMuc(const Aircraft &p) {
  float toMuc = fmodf(atan2f(mucE - p.eastKm, mucN - p.northKm) * 57.29578f + 360.0f, 360.0f);
  return angleDiffDeg(p.trackDeg, toMuc) < 70.0f;
}

bool isMovingAwayFromMuc(const Aircraft &p) {
  float fromMuc = fmodf(atan2f(p.eastKm - mucE, p.northKm - mucN) * 57.29578f + 360.0f, 360.0f);
  return angleDiffDeg(p.trackDeg, fromMuc) < 70.0f;
}

int nearestAirborne() {
  for (int i = 0; i < planeCount; i++)
    if (!planes[i].ground) return i;
  return -1;
}

void fitCopy(char *dst, size_t dstN, const char *src, int maxChars) {
  // Tiny TFT layout rule: text never gets to decide how wide the UI is.
  // Everything that reaches the display is clipped to a caller-owned text box,
  // so a long airline/route/callsign cannot spill into neighboring numbers.
  if (!src) src = "";
  if (maxChars < 1) maxChars = 1;
  int n = 0;
  while (src[n] && n < maxChars && n < (int)dstN - 1) {
    dst[n] = src[n];
    n++;
  }
  dst[n] = 0;
}

void printFit(int x, int y, const char *s, uint8_t size, uint16_t color, int boxW) {
  tft.setTextSize(size);
  tft.setTextColor(color);
  int maxChars = boxW / (6 * size);
  char b[48];
  fitCopy(b, sizeof(b), s, maxChars);
  tft.setCursor(x, y);
  tft.print(b);
}

void centerText(const char *s, int y, uint8_t size, uint16_t color) {
  char b[48];
  fitCopy(b, sizeof(b), s, 208 / (6 * size));
  tft.setTextSize(size);
  tft.setTextColor(color);
  int x = 120 - (int)strlen(b) * 3 * size;
  if (x < 16) x = 16;
  tft.setCursor(x, y);
  tft.print(b);
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
  int best = 0; *label = "REGULAR TRAFFIC";
  if (strcmp(p.sqk, "7700") == 0 || strcmp(p.sqk, "7600") == 0 ||
      strcmp(p.sqk, "7500") == 0) { *label = "EMERGENCY SQUAWK"; return 100; }

  // First pass: aircraft type and callsign prefixes. These are the strongest
  // "plane spotter" signals because they identify rare airframes, government
  // flights, military transports, and other traffic worth calling out even when
  // it is not the closest aircraft to the desk.
  for (unsigned i = 0; i < sizeof(typeRules)/sizeof(typeRules[0]); i++)
    if (strncmp(p.typ, typeRules[i].pfx, strlen(typeRules[i].pfx)) == 0 &&
        typeRules[i].score > best) { best = typeRules[i].score; *label = typeRules[i].label; }
  for (unsigned i = 0; i < sizeof(csRules)/sizeof(csRules[0]); i++)
    if (strncmp(p.flight, csRules[i].pfx, strlen(csRules[i].pfx)) == 0 &&
        csRules[i].score > best) { best = csRules[i].score; *label = csRules[i].label; }

  // Second pass: live situational interest. This lets the spotter page change
  // dynamically when something nearby becomes more exciting than a merely large
  // aircraft: very low traffic, a fast jet-like pass, or obvious MUC arrival /
  // departure behavior. The thresholds are deliberately conservative so common
  // cruise traffic does not constantly steal the page.
  float mucD = distanceToMucKm(p);
  if (!p.ground && p.distKm < 4.0f && p.altFt < 3500 && best < 82) {
    best = 82; *label = "LOW OVERHEAD PASS";
  } else if (!p.ground && p.gsKt > 430 && p.altFt < 20000 && best < 78) {
    best = 78; *label = "FAST LOW TRAFFIC";
  } else if (!p.ground && mucD < 16.0f && p.altFt < 9000 && p.vrFpm < -350 && isMovingTowardMuc(p) && best < 70) {
    best = 70; *label = "MUC FINAL INBOUND";
  } else if (!p.ground && mucD < 14.0f && p.altFt < 9000 && p.vrFpm > 350 && isMovingAwayFromMuc(p) && best < 66) {
    best = 66; *label = "MUC CLIMBOUT";
  }
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
  // Keep the bezel slightly inside the physical 240 px edge. The GC9A01's
  // round mask and some modules' visible area can clip radius-119 pixels, which
  // makes the edge ring look like it randomly disappears at a few angles.
  tft.drawCircle(120, 120, 116, C_GRID);
  tft.drawCircle(120, 120, 115, C_GRIDDIM);
  for (int a = 0; a < 360; a += 10) {
    float r = deg2rad((float)a);
    float s = sinf(r), c = cosf(r);
    int   l = (a % 90 == 0) ? 9 : (a % 30 == 0 ? 6 : 3);
    tft.drawLine(120 + (int)(s*115), 120 - (int)(c*115),
                 120 + (int)(s*(115-l)), 120 - (int)(c*(115-l)),
                 (a % 30 == 0) ? C_GRID : C_GRIDDIM);
  }
  tft.setTextSize(1);
  // Compass letters sit inside small background pads. Without the pads, the
  // crosshair/rim ticks can visually touch the glyphs and make N/S/E/W look
  // overlapped, especially while the radar beam is moving underneath.
  tft.fillRect(113, 20, 14, 9, C_BG);
  tft.fillRect(113, 210, 14, 9, C_BG);
  tft.fillRect(22, 116, 14, 9, C_BG);
  tft.fillRect(204, 116, 14, 9, C_BG);
  tft.setTextColor(C_BRIGHT);
  tft.setCursor(117, 21);  tft.print("N");
  tft.setTextColor(C_GRID);
  tft.setCursor(117, 211); tft.print("S");
  tft.setCursor(26, 117);  tft.print("W");
  tft.setCursor(208, 117); tft.print("E");
}

void pageDots() {
  // Page dots were useful while the page system was being debugged, but on the
  // round GC9A01 they sit exactly where moving radar/rim graphics want to live.
  // Leaving this as a no-op removes bottom-edge jitter without touching every
  // old call site; the page title now identifies the active page.
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
  if (slot < 0 || slot >= ROUTE_CACHE_COUNT) return;
  RouteCache &r = rc[slot];
  if (strcmp(r.forCs, callsign) == 0) return;
  copyStr(r.forCs, sizeof(r.forCs), callsign);
  r.codes[0] = r.cities[0] = r.airline[0] = 0;
  if (callsign[0] == '-' || callsign[0] == 0) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(4500);
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
        r.airline[sizeof(r.airline)-1] = 0;
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

RouteCache *cachedRouteFor(const char *callsign) {
  for (int i = 0; i < ROUTE_CACHE_COUNT; i++)
    if (strcmp(rc[i].forCs, callsign) == 0) return &rc[i];
  return nullptr;
}

bool routeDestinationCode(const Aircraft &p, char *dst, size_t dstN) {
  // Extract the destination from a cached "AAA > BBB" route string. The main
  // radar page only wants the destination tag, not the whole flight card, so
  // this helper keeps that parsing in one place.
  if (dstN == 0) return false;
  dst[0] = 0;
  RouteCache *r = cachedRouteFor(p.flight);
  if (!r || !r->codes[0]) return false;
  const char *arrow = strchr(r->codes, '>');
  if (!arrow) return false;
  arrow++;
  while (*arrow == ' ') arrow++;
  fitCopy(dst, dstN, arrow, 4);
  return dst[0] != 0;
}

bool routeDestIsMuc(const Aircraft &p) {
  RouteCache *r = cachedRouteFor(p.flight);
  return r && r->codes[0] && strstr(r->codes, "> MUC") != nullptr;
}

bool routeOriginIsMuc(const Aircraft &p) {
  RouteCache *r = cachedRouteFor(p.flight);
  return r && strncmp(r->codes, "MUC >", 5) == 0;
}

bool trafficLooksMucInbound(const Aircraft &p) {
  float d = distanceToMucKm(p);
  return routeDestIsMuc(p) ||
         (!p.ground && d < 38.0f && p.altFt < 16000 && isMovingTowardMuc(p));
}

bool trafficLooksMucOutbound(const Aircraft &p) {
  float d = distanceToMucKm(p);
  return routeOriginIsMuc(p) ||
         (!p.ground && d < 30.0f && p.altFt < 16000 && isMovingAwayFromMuc(p));
}

uint16_t trafficColor(const Aircraft &p, bool outOfRange) {
  // One consistent visual grammar across the radar and airport pages:
  // green = inbound/destination MUC, red = outbound/from MUC, blue = ground or
  // out-of-range, yellow = normal non-MUC traffic. Route cache wins when known;
  // otherwise heading/altitude/distance provide a live heuristic.
  if (outOfRange || p.ground) return C_BLUE;
  if (trafficLooksMucInbound(p)) return C_GREEN;
  if (trafficLooksMucOutbound(p)) return C_RED;
  return C_AMBER;
}

// ---------- Extra airport-data API helpers ----------
bool asciiDigit(char c) {
  return c >= '0' && c <= '9';
}

bool metarWindToken(const char *tok) {
  // METAR wind groups look like 25012KT, VRB03KT, or 26014G24KT. This parser is
  // intentionally permissive because the display only needs a short readable
  // summary; the raw METAR remains available underneath if a token is unusual.
  size_t n = strlen(tok);
  return n >= 5 && strstr(tok, "KT") != nullptr &&
         ((asciiDigit(tok[0]) && asciiDigit(tok[1]) && asciiDigit(tok[2])) ||
          strncmp(tok, "VRB", 3) == 0);
}

bool metarVisibilityToken(const char *tok) {
  return strlen(tok) == 4 &&
         asciiDigit(tok[0]) && asciiDigit(tok[1]) &&
         asciiDigit(tok[2]) && asciiDigit(tok[3]);
}

bool metarTempToken(const char *tok) {
  // Temperature/dewpoint is normally TT/DD or MTT/MDD. Require the slash and a
  // numeric/M-prefixed temperature so runway names like 08L/26R do not match.
  const char *slash = strchr(tok, '/');
  if (!slash || slash == tok) return false;
  return asciiDigit(tok[0]) || (tok[0] == 'M' && asciiDigit(tok[1]));
}

void parseMetarSummary() {
  copyStr(mucWx.wind, sizeof(mucWx.wind), "--");
  copyStr(mucWx.vis,  sizeof(mucWx.vis),  "--");
  copyStr(mucWx.temp, sizeof(mucWx.temp), "--");
  copyStr(mucWx.qnh,  sizeof(mucWx.qnh),  "--");

  char work[128];
  copyStr(work, sizeof(work), mucWx.raw);
  char *tok = strtok(work, " \r\n");
  while (tok) {
    if (metarWindToken(tok)) {
      copyStr(mucWx.wind, sizeof(mucWx.wind), tok);
    } else if (strcmp(tok, "CAVOK") == 0) {
      copyStr(mucWx.vis, sizeof(mucWx.vis), "CAVOK");
    } else if (metarVisibilityToken(tok)) {
      if (strcmp(tok, "9999") == 0) copyStr(mucWx.vis, sizeof(mucWx.vis), "10km+");
      else                          copyStr(mucWx.vis, sizeof(mucWx.vis), tok);
    } else if (metarTempToken(tok)) {
      char tmp[8];
      int n = 0;
      while (tok[n] && tok[n] != '/' && n < (int)sizeof(tmp) - 2) {
        tmp[n] = tok[n];
        n++;
      }
      tmp[n++] = 'C';
      tmp[n] = 0;
      copyStr(mucWx.temp, sizeof(mucWx.temp), tmp);
    } else if (tok[0] == 'Q' &&
               asciiDigit(tok[1]) && asciiDigit(tok[2]) &&
               asciiDigit(tok[3]) && asciiDigit(tok[4])) {
      snprintf(mucWx.qnh, sizeof(mucWx.qnh), "%shPa", tok + 1);
    }
    tok = strtok(nullptr, " \r\n");
  }
}

bool fetchMucWeather() {
  uint32_t now = millis();
  if (mucWx.ok && now - mucWx.lastFetch < MUC_WX_REFRESH_MS) return true;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(client, "https://aviationweather.gov/api/data/metar?ids=EDDM&format=raw")) return mucWx.ok;
  http.addHeader("User-Agent", "FreisingDeskRadar/1.0 MUC weather page");
  int code = http.GET();
  if (code != 200) {
    http.end();
    return mucWx.ok;
  }

  String payload = http.getString();
  http.end();
  payload.trim();
  if (payload.length() == 0) return mucWx.ok;

  copyStr(mucWx.raw, sizeof(mucWx.raw), payload.c_str());
  payload = String();
  parseMetarSummary();
  mucWx.ok = true;
  mucWx.lastFetch = now;
  return true;
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
#define RADAR_GRID_R    104
#define RADAR_TRAFFIC_R  96
#define RADAR_RIM_R     101

void radarGrid() {
  tft.drawCircle(120, 120, 36,  C_GRIDDIM);
  tft.drawCircle(120, 120, 72,  C_GRIDDIM);
  tft.drawCircle(120, 120, RADAR_GRID_R, C_GRID);
  tft.drawLine(120, 14, 120, 226, C_GRIDDIM);
  tft.drawLine(14, 120, 226, 120, C_GRIDDIM);
  tft.fillCircle(120, 120, 2, C_BRIGHT);
  tft.setTextSize(1);
  tft.setTextColor(C_GRID);
  // Range label: with the default 30 km radar range, the rings are 10/20/30 km.
  // The user-facing label stays deliberately terse ("20km") to avoid clutter.
  char rng[14];
  snprintf(rng, sizeof(rng), "%.0fkm", (double)(RADAR_RANGE_KM * 2 / 3));
  tft.setCursor(166, 112); tft.print(rng);
}

void radarStatic() {
  tft.fillScreen(C_BG);
  drawBezel();
  radarGrid();
  pageDots();
}

bool radarTextShield(int x, int y);

void eraseBlips(bool clearLabels) {
  for (int i = 0; i < prevBlipCount; i++)
    tft.fillCircle(prevBlips[i].x, prevBlips[i].y, prevBlips[i].r, C_BG);
  prevBlipCount = 0;
  if (clearLabels) {
    tft.fillRect(80, 34, 80, 20, C_BG);
  }
}

void radarLabels() {
  // Main radar readout is intentionally just the destination tag. The earlier
  // callsign + altitude/speed block was too much information for the live radar
  // layer, and redrawing its filled rectangle on every sweep made it visibly
  // jitter. Keep this tiny and refresh it only on page/data updates.
  tft.fillRect(80, 34, 80, 20, C_BG);
  int na = nearestAirborne();
  if (na >= 0) {
    Aircraft &n = planes[na];
    char dest[8], tag[14];
    if (routeDestinationCode(n, dest, sizeof(dest))) {
      snprintf(tag, sizeof(tag), "TO %s", dest);
    } else if (trafficLooksMucInbound(n)) {
      snprintf(tag, sizeof(tag), "TO MUC");
    } else {
      // Destination unknown (route API has no match yet): the callsign is
      // still more useful on the radar layer than a "TO ----" placeholder.
      snprintf(tag, sizeof(tag), "%.10s", n.flight);
    }
    centerText(tag, 39, 2, trafficColor(n, false));
  } else {
    centerText("NO AIRCRAFT", 42, 1, C_DIM);
  }
}

void radarPaint(bool record, bool drawLabels) {
  radarGrid();
  if (record) prevBlipCount = 0;

  for (int i = planeCount - 1; i >= 0; i--) {
    Aircraft &p = planes[i];
    if (p.ground) continue;
    float liveE, liveN;
    liveAircraftOffsetKm(p, &liveE, &liveN);
    float liveD = sqrtf(liveE * liveE + liveN * liveN);
    float liveB = fmodf(atan2f(liveE, liveN) * 57.29578f + 360.0f, 360.0f);
    float b = deg2rad(liveB);
    int x, y; uint8_t r;
    if (liveD <= RADAR_RANGE_KM) {
      uint16_t c = trafficColor(p, false);
      float rr = (liveD / RADAR_RANGE_KM) * RADAR_TRAFFIC_R;
      x = 120 + (int)(sinf(b) * rr);
      y = 120 - (int)(cosf(b) * rr);
      if (radarTextShield(x, y)) continue;
      // Live route cue: a tiny projected track line in front of each aircraft.
      // The triangle is the aircraft's current heading; the short line makes it
      // feel less like a static icon and more like traffic moving through space.
      int hx = x + (int)(sinf(deg2rad(p.trackDeg)) * 13);
      int hy = y - (int)(cosf(deg2rad(p.trackDeg)) * 13);
      tft.drawLine(x, y, hx, hy, c);
      planeTriangle(x, y, p.trackDeg, i == nearestAirborne() ? 7 : 6, c);
      r = 18;
    } else {
      x = 120 + (int)(sinf(b) * RADAR_RIM_R);
      y = 120 - (int)(cosf(b) * RADAR_RIM_R);
      tft.fillCircle(x, y, 3, C_BLUE);
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

  // NOTE: the bezel is deliberately NOT repainted here. Traffic lives at
  // radius <= 96 px (rim dots at 101), and blip-erase circles reach at most
  // ~114 px — still inside the 115 px bezel ring. Skipping ~40 bezel draw
  // calls on every 35 ms sweep frame keeps the beam animation smooth.

  if (!drawLabels) return;
  radarLabels();
}

void radarDynamic() { eraseBlips(true); radarPaint(true, true); }

bool radarTextShield(int x, int y) {
  // The radar beam is drawn pixel-by-pixel so it can skip the UI text zones.
  // That is cheaper and cleaner than drawing through text and then repainting
  // all labels, which caused the little jagged/jittery artifacts on the main
  // page.
  bool topFlight = x >= 76 && x <= 164 && y >= 30 && y <= 60;
  bool rangeLabel = x >= 158 && x <= 198 && y >= 104 && y <= 124;
  return topFlight || rangeLabel;
}

void drawRadarBeam(int deg, uint16_t color) {
  float a = deg2rad((float)deg);
  float s = sinf(a), c = cosf(a);
  for (int r = 8; r <= RADAR_GRID_R; r += 2) {
    int x = 120 + (int)(s * r);
    int y = 120 - (int)(c * r);
    if (radarTextShield(x, y)) continue;
    tft.drawPixel(x, y, color);
    if (r > 22 && r < 102 && !radarTextShield(x + 1, y)) tft.drawPixel(x + 1, y, color);
  }
}

void sweepStep() {
  drawRadarBeam(sweepDeg, C_BG);
  eraseBlips(false);
  radarPaint(true, false);
  sweepDeg = (sweepDeg + 5) % 360;
  drawRadarBeam(sweepDeg, C_SWEEP);
}

// ---------- Flight card (shared by nearest-flight + spotter pages) ----------
void statRow(int y, const char *l1, const char *v1,
             const char *l2, const char *v2, uint16_t vc1, uint16_t vc2) {
  // Fixed boxes are the simplest way to make the tiny display robust. Each
  // value is allowed only the pixels assigned to it; if a route/type/speed gets
  // longer than expected, it clips instead of overwriting the next column.
  printFit(44,  y, l1, 1, C_GRID, 28);
  printFit(74,  y, v1, 1, vc1,    54);
  printFit(132, y, l2, 1, C_GRID, 28);
  printFit(160, y, v2, 1, vc2,    48);
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
      // CPA = closest point of approach to YOU, not to the airport. The time
      // is displayed explicitly as minutes and seconds so there is no ambiguity
      // about "in 3:42"; the distance is the predicted minimum separation in km.
      snprintf(cpa, sizeof(cpa), "CPA %.1fkm in %dm%02ds",
               (double)cd, (int)tSec / 60, (int)tSec % 60);
    } else {
      snprintf(cpa, sizeof(cpa), "away now %.1fkm", (double)n.distKm);
    }
  } else {
    snprintf(cpa, sizeof(cpa), "now %.1fkm from you", (double)n.distKm);
  }
  centerText(cpa, 200, 1, C_CYAN);
}

// ---------- Page 4: MUC AIRPORT ----------
#define MUC_SCALE 26.0f         // px per km; oversized so the two 4 km MUC runways dominate the page.
#define MUC_MAP_R 88            // traffic/map radius inside the round bezel.

void mucToScreen(float e, float n, int *x, int *y) {
  *x = 120 + (int)((e - mucE) * MUC_SCALE);
  *y = 126 - (int)((n - mucN) * MUC_SCALE);
}

void drawRunway(float ce, float cn) {   // centre offset from MUC ref (km)
  float u_e = sinf(deg2rad(RWY_HDG)), u_n = cosf(deg2rad(RWY_HDG));
  int x0, y0, x1, y1;
  mucToScreen(mucE + ce - u_e * RWY_LEN/2, mucN + cn - u_n * RWY_LEN/2, &x0, &y0);
  mucToScreen(mucE + ce + u_e * RWY_LEN/2, mucN + cn + u_n * RWY_LEN/2, &x1, &y1);

  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  float px = len > 0 ? -dy / len * 4.0f : 0;
  float py = len > 0 ?  dx / len * 4.0f : 4.0f;

  // Fill the runway as a real thick slab instead of several horizontal strokes.
  // That makes the 08/26 pair read as runways even when the airport map is small
  // and slightly rotated on the round TFT.
  tft.fillTriangle(x0 + px, y0 + py, x1 + px, y1 + py, x1 - px, y1 - py, C_GREY);
  tft.fillTriangle(x0 + px, y0 + py, x1 - px, y1 - py, x0 - px, y0 - py, C_GREY);

  // Threshold marks and centerline hints. They are oversized on purpose: the
  // real airport is compact on a 240 px round display, so the map is a readable
  // operational schematic rather than a survey drawing.
  tft.fillCircle(x0, y0, 3, C_WHITE);
  tft.fillCircle(x1, y1, 3, C_WHITE);
  tft.drawLine((x0*3+x1)/4, (y0*3+y1)/4, (x0+x1*3)/4, (y0+y1*3)/4, C_GRIDDIM);
}

void runwayLabel(float ce, float cn, const char *label, int yNudge) {
  int x, y;
  mucToScreen(mucE + ce, mucN + cn, &x, &y);
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE);
  tft.setCursor(x - (int)strlen(label) * 3, y + yNudge);
  tft.print(label);
}

bool likelyArrival(const Aircraft &p) {
  if (p.ground) return false;
  float d = distanceToMucKm(p);
  return d < 28.0f && p.altFt < 12000 && p.vrFpm < -180 && isMovingTowardMuc(p);
}

bool likelyDeparture(const Aircraft &p) {
  if (p.ground) return false;
  float d = distanceToMucKm(p);
  return d < 22.0f && p.altFt < 12000 && p.vrFpm > 180 && isMovingAwayFromMuc(p);
}

float mucTrafficScore(const Aircraft &p, bool arrival) {
  float d = distanceToMucKm(p);
  if (arrival && (p.ground || d > 45.0f)) return 99999.0f;
  if (!arrival && d > 30.0f) return 99999.0f;

  float toMuc = fmodf(atan2f(mucE - p.eastKm, mucN - p.northKm) * 57.29578f + 360.0f, 360.0f);
  float fromMuc = fmodf(atan2f(p.eastKm - mucE, p.northKm - mucN) * 57.29578f + 360.0f, 360.0f);
  float eta = etaMinForDistance(d, p);
  if (eta < 0) eta = 60.0f;

  if (arrival) {
    float score = eta * 3.0f + angleDiffDeg(p.trackDeg, toMuc) * 0.18f;
    score += (p.altFt > 14000 ? p.altFt - 14000 : 0) / 1200.0f;
    if (p.vrFpm > -100) score += 8.0f;       // not descending yet
    if (likelyArrival(p)) score -= 16.0f;    // obvious final/inbound
    return score;
  }

  float score = d * 2.2f + angleDiffDeg(p.trackDeg, fromMuc) * 0.18f;
  score += (p.altFt > 12000 ? p.altFt - 12000 : 0) / 1200.0f;
  if (p.ground) score -= 4.0f;               // taxiing aircraft may be the next departure
  if (!p.ground && p.vrFpm < 100) score += 8.0f;
  if (likelyDeparture(p)) score -= 14.0f;    // obvious climbout
  return score;
}

void insertTopCandidate(int idx, float score, int outIdx[2], float outScore[2]) {
  if (idx < 0 || score >= 99999.0f || idx == outIdx[0] || idx == outIdx[1]) return;
  if (score < outScore[0]) {
    outScore[1] = outScore[0]; outIdx[1] = outIdx[0];
    outScore[0] = score;       outIdx[0] = idx;
  } else if (score < outScore[1]) {
    outScore[1] = score;       outIdx[1] = idx;
  }
}

void topMucTraffic(bool arrival, int outIdx[2]) {
  float score[2] = {99999.0f, 99999.0f};
  outIdx[0] = outIdx[1] = -1;
  for (int i = 0; i < planeCount; i++)
    insertTopCandidate(i, mucTrafficScore(planes[i], arrival), outIdx, score);
}

int nextArrivalIdx() {
  int top[2];
  topMucTraffic(true, top);
  return top[0];
}

int nextDepartureIdx() {
  int top[2];
  topMucTraffic(false, top);
  return top[0];
}

void airportStatic() {
  tft.fillScreen(C_BG);
  drawBezel();
  centerText("MUC  MUNICH", 26, 1, C_AMBER);
  pageDots();
}

void airportDynamic() {
  tft.fillRect(24, 36, 192, 180, C_BG);

  // Approach/departure range guide. The runway page is a close-up airport view;
  // aircraft outside the close-up are clamped to this ring, so the page remains
  // live even before an arrival is directly over the runway environment.
  tft.drawCircle(120, 126, MUC_MAP_R, C_GRIDDIM);

  // Runways 08L/26R (north) and 08R/26L (south), drawn as a scaled schematic.
  // MUC has two long parallel runways; the labels show the real reciprocal
  // runway names, so a plane on the 08 end is using the eastbound direction and
  // a plane on the 26 end is using the westbound direction.
  drawRunway(-RWY_STAGGER, +RWY_SEP);
  runwayLabel(-RWY_STAGGER, +RWY_SEP, "08L/26R", -14);
  drawRunway(+RWY_STAGGER, -RWY_SEP);
  runwayLabel(+RWY_STAGGER, -RWY_SEP, "08R/26L", 8);

  // Terminal/apron shape between the runways. It is only a landmark, not a gate
  // map; the important part for spotting is traffic relative to the runway pair.
  int tx, ty;
  mucToScreen(mucE, mucN, &tx, &ty);
  tft.fillRoundRect(tx - 14, ty - 5, 28, 10, 2, C_GRIDDIM);

  int nextArr = nextArrivalIdx();
  int nextDep = nextDepartureIdx();
  for (int i = planeCount - 1; i >= 0; i--) {
    Aircraft &p = planes[i];
    float liveE, liveN;
    liveAircraftOffsetKm(p, &liveE, &liveN);
    float dMucE = liveE - mucE, dMucN = liveN - mucN;
    float dMuc  = sqrtf(dMucE * dMucE + dMucN * dMucN);
    if (dMuc > 18) continue;

    bool low = !p.ground && p.altFt < 9000;
    bool interesting = p.ground || low || i == nextArr || i == nextDep;
    if (!interesting) continue;

    int x, y;
    mucToScreen(liveE, liveN, &x, &y);
    bool clampedToEdge = false;
    int dx = x - 120, dy = y - 126;
    if (dx * dx + dy * dy > MUC_MAP_R * MUC_MAP_R) {
      if (dMuc < 0.01f) continue;
      x = 120 + (int)(dMucE / dMuc * MUC_MAP_R);
      y = 126 - (int)(dMucN / dMuc * MUC_MAP_R);
      clampedToEdge = true;
    }

    if (p.ground) {
      tft.fillCircle(x, y, 3, C_BLUE);
      if (clampedToEdge) tft.drawCircle(x, y, 7, C_BLUE);
    } else if (low) {
      uint16_t c = trafficColor(p, false);
      if (i == nextArr || likelyArrival(p)) c = C_GREEN;
      if (i == nextDep || likelyDeparture(p)) c = C_RED;
      int hx = x + (int)(sinf(deg2rad(p.trackDeg)) * 12);
      int hy = y - (int)(cosf(deg2rad(p.trackDeg)) * 12);
      // Airport traffic intentionally uses dots instead of aircraft triangles:
      // green dots are arrivals, red dots are departures, blue dots are ground
      // or stationary aircraft. The short tail preserves direction without
      // cluttering the enlarged runway drawing.
      tft.drawLine(x, y, hx, hy, c == C_WHITE ? C_GRIDDIM : c);
      tft.fillCircle(x, y, (i == nextArr || i == nextDep) ? 5 : 4, c);
      if (clampedToEdge) tft.drawCircle(x, y, 7, c);
    } else {
      tft.fillCircle(x, y, 2, C_AMBER);   // high/non-airport overflight
    }
  }

  // Bottom legend only: counts change often and caused distracting number
  // flicker. The map itself now carries the information through color.
  tft.setTextSize(1);
  tft.fillCircle(62, 200, 3, C_GREEN); tft.setTextColor(C_GREEN); tft.setCursor(68, 197); tft.print("ARR");
  tft.fillCircle(112, 200, 3, C_RED);  tft.setTextColor(C_RED);   tft.setCursor(118, 197); tft.print("DEP");
  tft.fillCircle(162, 200, 3, C_BLUE); tft.setTextColor(C_BLUE);  tft.setCursor(168, 197); tft.print("GND");
}

void mucOpsStatic() {
  tft.fillScreen(C_BG);
  drawBezel();
  centerText("MUC NEXT", 26, 1, C_AMBER);
  pageDots();
}

void drawMucBoardCell(int x, int y, int w, const char *label, int idx, uint16_t color, bool arrival) {
  // Two-column board cell inspired by FlightScnr's compact detail rows. Each
  // cell owns exactly three short text bands: label, callsign/distance, route.
  // This is much more stable on the round display than one long airport list.
  printFit(x, y, label, 1, color, w);
  if (idx < 0) {
    printFit(x, y + 15, "watching", 1, C_DIM, w);
    return;
  }

  Aircraft &p = planes[idx];
  float d = distanceToMucKm(p);
  float eta = etaMinForDistance(d, p);
  char top[24];
  if (arrival && eta >= 0) snprintf(top, sizeof(top), "%s %.0fmin", p.flight, (double)eta);
  else                     snprintf(top, sizeof(top), "%s %.1fkm", p.flight, (double)d);
  printFit(x, y + 12, top, 1, C_WHITE, w);

  char routeLine[30];
  RouteCache *route = cachedRouteFor(p.flight);
  if (route && route->codes[0]) {
    snprintf(routeLine, sizeof(routeLine), "%.18s", route->codes);
  } else if (p.ground) {
    snprintf(routeLine, sizeof(routeLine), "%s GND", p.typ[0] ? p.typ : "----");
  } else {
    snprintf(routeLine, sizeof(routeLine), "%s %dm", p.typ[0] ? p.typ : "----", (int)(p.altFt * 0.3048f));
  }
  printFit(x, y + 24, routeLine, 1, C_DIM, w);
}

void mucOpsDynamic() {
  tft.fillRect(24, 40, 192, 176, C_BG);
  int arrIdx[2], depIdx[2];
  topMucTraffic(true, arrIdx);
  topMucTraffic(false, depIdx);
  int coolIdx = -1, coolBest = -1;
  const char *coolLabel = "";

  for (int i = 0; i < planeCount; i++) {
    if (distanceToMucKm(planes[i]) > 28.0f) continue;
    const char *label;
    int s = coolScore(planes[i], &label);
    if (s > coolBest) { coolBest = s; coolIdx = i; coolLabel = label; }
  }

  tft.setTextSize(2);
  tft.setTextColor(C_GREEN); tft.setCursor(54, 42);  tft.print("ARR");
  tft.setTextColor(C_RED);   tft.setCursor(146, 42); tft.print("DEP");
  tft.drawFastVLine(120, 62, 94, C_GRIDDIM);
  tft.drawFastHLine(36, 96, 80, C_GRIDDIM);
  tft.drawFastHLine(128, 96, 80, C_GRIDDIM);
  drawMucBoardCell(34, 66, 82, "A1", arrIdx[0], C_GREEN, true);
  drawMucBoardCell(34, 112, 82, "A2", arrIdx[1], C_GREEN, true);
  drawMucBoardCell(128, 66, 82, "D1", depIdx[0], C_RED, false);
  drawMucBoardCell(128, 112, 82, "D2", depIdx[1], C_RED, false);

  tft.setTextSize(1);
  tft.setTextColor(C_GRID);
  tft.drawFastHLine(48, 164, 144, C_GRIDDIM);
  tft.setCursor(34, 178); tft.print("COOL");
  if (coolIdx >= 0) {
    Aircraft &c = planes[coolIdx];
    char coolTop[28];
    snprintf(coolTop, sizeof(coolTop), "%s %s", c.flight, c.typ[0] ? c.typ : "----");
    printFit(74, 178, coolTop, 1, coolBest >= 85 ? C_RED : C_CYAN, 132);
    printFit(48, 192, coolLabel, 1, C_DIM, 156);
  } else {
    printFit(74, 178, "watching traffic", 1, C_DIM, 120);
  }
}

// ---------- Page 6: MUC WEATHER ----------
void dataRow(int y, const char *label, const char *value, uint16_t valueColor) {
  // Shared airport-data row. Labels and values have hard pixel boxes because
  // external APIs can surprise us with long strings; clipping is better than
  // letting one value push through the next row on the round display.
  printFit(46, y, label, 1, C_GRID, 58);
  printFit(106, y, value, 1, valueColor, 86);
}

void mucWeatherStatic() {
  tft.fillScreen(C_BG);
  drawBezel();
  centerText("MUC METAR", 26, 1, C_AMBER);
  pageDots();
}

void mucWeatherDynamic() {
  tft.fillRect(24, 40, 192, 176, C_BG);
  bool ok = fetchMucWeather();
  if (!ok) {
    centerText("weather data", 96, 2, C_DIM);
    centerText("not loaded yet", 118, 1, C_GRID);
    return;
  }

  centerText("EDDM WEATHER", 48, 2, C_CYAN);
  dataRow(78,  "WIND", mucWx.wind, C_WHITE);
  dataRow(96,  "VIS",  mucWx.vis,  C_WHITE);
  dataRow(114, "TEMP", mucWx.temp, C_WHITE);
  dataRow(132, "QNH",  mucWx.qnh,  C_WHITE);

  tft.drawFastHLine(46, 154, 148, C_GRIDDIM);
  char raw1[28], raw2[28];
  fitCopy(raw1, sizeof(raw1), mucWx.raw, 24);
  fitCopy(raw2, sizeof(raw2), mucWx.raw + strlen(raw1), 24);
  printFit(40, 170, raw1, 1, C_DIM, 160);
  printFit(40, 184, raw2, 1, C_DIM, 160);
}

// ---------- Page 7: SPOTTER ----------
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
  if (score >= 40) snprintf(badge, sizeof(badge), "WHY: %.20s", label);
  else             snprintf(badge, sizeof(badge), "WHY: REGULAR TRAFFIC");
  flightCard(planes[ci], rc[1], badge, badgeC);
}

// ---------- Page management ----------
bool skipPage(uint8_t p) {
  if ((p == 1 || p == 2) && nearestAirborne() < 0) return true;
  if ((p == 4 || p == 6) && planeCount == 0) return true;
  return false;
}

void drawPageFull() {
  prevBlipCount = 0;
  sweepDeg = 0;
  switch (page) {
    case 0: radarStatic();   radarDynamic();   break;
    case 1: detailStatic();  detailDynamic();  break;
    case 2: trackStatic();   trackDynamic();   break;
    case 3: airportStatic(); airportDynamic(); break;
    case 4: mucOpsStatic();  mucOpsDynamic();  break;
    case 5: mucWeatherStatic(); mucWeatherDynamic(); break;
    case 6: spotterStatic();    spotterDynamic();    break;
  }
}

void drawPageUpdate() {
  switch (page) {
    case 0: radarDynamic();   break;
    case 1: detailDynamic();  break;
    case 2: trackDynamic();   break;
    case 3: airportDynamic(); break;
    case 4: mucOpsDynamic();  break;
    case 5: mucWeatherDynamic(); break;
    case 6: spotterDynamic();    break;
  }
}

void prefetchRoutes() {
  int na = nearestAirborne();
  if ((page == 0 || page == 1 || page == 2) && na >= 0) fetchRoute(planes[na].flight, ROUTE_SLOT_NEAREST);
  if (page == 4) {
    int arrIdx[2], depIdx[2];
    topMucTraffic(true, arrIdx);
    topMucTraffic(false, depIdx);
    if (arrIdx[0] >= 0) fetchRoute(planes[arrIdx[0]].flight, ROUTE_SLOT_MUC_BASE + 0);
    if (arrIdx[1] >= 0) fetchRoute(planes[arrIdx[1]].flight, ROUTE_SLOT_MUC_BASE + 1);
    if (depIdx[0] >= 0) fetchRoute(planes[depIdx[0]].flight, ROUTE_SLOT_MUC_BASE + 2);
    if (depIdx[1] >= 0) fetchRoute(planes[depIdx[1]].flight, ROUTE_SLOT_MUC_BASE + 3);
  }
  if (page == 6) {
    int ci = coolestIdx();
    if (ci >= 0) fetchRoute(planes[ci].flight, ROUTE_SLOT_SPOTTER);
  }
}

// ---------- Main ----------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Plane Radar v6 (7 pages, refactored) booting...");
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

      // Dynamic spotter interrupt: an emergency squawk or very rare airframe
      // (score >= ALERT_SCORE) yanks the carousel to the SPOTTER page so the
      // event is visible immediately — an A380 on final or a 7700 squawk should
      // not wait behind the weather page. The callsign latch makes this
      // one-shot per aircraft, so the same jet cannot hold the screen hostage.
      int alertIdx = coolestIdx();
      const char *alertLabel;
      if (alertIdx >= 0 && page != 6 &&
          coolScore(planes[alertIdx], &alertLabel) >= ALERT_SCORE &&
          strcmp(planes[alertIdx].flight, lastAlertCs) != 0) {
        copyStr(lastAlertCs, sizeof(lastAlertCs), planes[alertIdx].flight);
        Serial.printf("ALERT: %s -> spotter page (%s)\n",
                      planes[alertIdx].flight, alertLabel);
        page = 6;
        lastPageSwitch = now;
        fetchRoute(planes[alertIdx].flight, ROUTE_SLOT_SPOTTER);
        drawPageFull();
      } else if (first) {
        drawPageFull();
      } else if (page == 0 || page == 3) {
        // Only the genuinely live visual pages repaint on every ADS-B fetch.
        // Text-heavy cards and boards are redrawn when opened/page-switched, so
        // they do not visibly blink every few seconds while the data refreshes.
        drawPageUpdate();
      }
    } else if (++failCount >= 3) {
      splash("API error", "retrying...", C_ORANGE);
    }
  }

  if (page == 0 && now - lastSweepStep >= 35) {
    lastSweepStep = now;
    sweepStep();
  }

  if (page == 3 && now - lastLiveStep >= 1400) {
    lastLiveStep = now;
    airportDynamic();
  }

  if (now - lastPageSwitch >= pageDur[page]) {
    lastPageSwitch = now;
    do { page = (page + 1) % PAGE_COUNT; } while (skipPage(page));
    prefetchRoutes();
    drawPageFull();
  }

  delay(40);
}
