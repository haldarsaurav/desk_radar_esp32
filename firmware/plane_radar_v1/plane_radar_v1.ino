/*
  plane_radar_v1.ino — LIVE ADS-B plane radar for the Freising desk device.

  Hardware: ESP32-C3 Super Mini + GC9A01 1.28" round display
  Wiring:   see docs/display_wiring_guide.html

  Data:     https://api.adsb.lol          (aircraft positions, free, no key)
            https://api.adsbdb.com        (airline + route lookup by callsign)
            https://aviationweather.gov   (MUC METAR weather, free, no key)
  Setup:    copy config.example.h to config.h, enter Wi-Fi name+password.

  Pages (auto-cycle; rare/emergency traffic force-jumps to SPOTTER):
    1. RADAR   — calm live radar, stacked labels, traffic counter (no sweep)
    2. FLIGHT  — full-panel FlightRadar-style card for the nearest aircraft
    3. TRACK   — nearest aircraft's flown path + closest approach (CPA)
    4. MUC MAP — Munich Airport runway close-up with live traffic + wind
    5. MUC OPS — full-panel split board: next two arrivals / departures
    6. SPOTTER — coolest aircraft right now, with live MUC status/ETA
    7. MUC WX  — aviation weather report + runway in use + field status

  Chrome rules: the round bezel + compass letters live ONLY on map-like pages
  (RADAR, TRACK, MUC MAP). Text pages (FLIGHT, MUC OPS, SPOTTER, MUC WX) are
  deliberately bezel-less and use the full panel for content.

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
#define C_PURPLE  tft.color565(186, 96, 255)   // stationary/ground traffic on the MUC map
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
uint8_t  page      = 0;   // 0 radar, 1 flight, 2 track, 3 MUC map, 4 MUC ops, 5 spotter, 6 weather
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
  char wind[16]  = "--";
  char vis[12]   = "--";
  char temp[12]  = "--";
  char qnh[12]   = "--";
  char cloud[14] = "--";     // lowest reported layer, e.g. "BKN 1800ft"
  char wx[12]    = "DRY";    // significant weather (rain/snow/fog/...)
  int  visM      = -1;       // parsed visibility in metres (-1 = unknown)
  int  ceilFt    = -1;       // parsed BKN/OVC ceiling in feet (-1 = none/unknown)
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

// Live-motion animation state.
//
// The aircraft API updates in bursts (every FETCH_INTERVAL_MS); between
// fetches the radar and airport pages advance traffic gently by dead
// reckoning. NOTE: the rotating sweep beam was removed on purpose — it looked
// alive but fought with aircraft labels and caused most of the perceived
// jitter. A calm ~1 s traffic step reads far better on this small panel.
#define RADAR_STEP_MS   1200
#define AIRPORT_STEP_MS 1400
uint32_t lastRadarStep = 0, lastLiveStep = 0;

// Dynamic pixels on the radar page (low-flicker erase). Sized with headroom
// beyond MAX_AC because stacked aircraft labels register their own erase
// blips in addition to the aircraft markers themselves.
#define MAX_BLIPS (MAX_AC + 8)
struct Blip { int16_t x, y; uint8_t r; };
Blip prevBlips[MAX_BLIPS];
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

  // Quantize the projection to 0.4 s buckets. Without this, every 35 ms sweep
  // repaint moves each triangle by a fraction of a pixel, which shows up as a
  // constant +-1 px shimmer. With it, aircraft hold a stable position and then
  // take a clean visible step — smoother AND calmer to look at.
  ageSec = floorf(ageSec / 0.4f) * 0.4f;

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

// ---------- Aircraft type code -> human-readable name ----------
// ICAO type designators (A320, B77W, ...) are compact but cryptic on a
// consumer display. This table covers the airframes that realistically appear
// around Munich; anything unknown falls back to the raw code, so the UI never
// shows an empty field. Kept as one flat table so adding a type is one line.
struct TypeName { const char *code; const char *name; };
const TypeName typeNames[] = {
  {"A19N", "Airbus A319neo"},    {"A20N", "Airbus A320neo"},
  {"A21N", "Airbus A321neo"},    {"A319", "Airbus A319"},
  {"A320", "Airbus A320"},       {"A321", "Airbus A321"},
  {"A332", "Airbus A330-200"},   {"A333", "Airbus A330-300"},
  {"A339", "Airbus A330-900"},   {"A343", "Airbus A340-300"},
  {"A345", "Airbus A340-500"},   {"A346", "Airbus A340-600"},
  {"A359", "Airbus A350-900"},   {"A35K", "Airbus A350-1000"},
  {"A388", "Airbus A380"},       {"BCS1", "Airbus A220-100"},
  {"BCS3", "Airbus A220-300"},   {"B37M", "Boeing 737 MAX 7"},
  {"B38M", "Boeing 737 MAX 8"},  {"B39M", "Boeing 737 MAX 9"},
  {"B737", "Boeing 737-700"},    {"B738", "Boeing 737-800"},
  {"B739", "Boeing 737-900"},    {"B744", "Boeing 747-400"},
  {"B748", "Boeing 747-8"},      {"B752", "Boeing 757-200"},
  {"B763", "Boeing 767-300"},    {"B772", "Boeing 777-200"},
  {"B77L", "Boeing 777-200LR"},  {"B773", "Boeing 777-300"},
  {"B77W", "Boeing 777-300ER"},  {"B788", "Boeing 787-8"},
  {"B789", "Boeing 787-9"},      {"B78X", "Boeing 787-10"},
  {"E190", "Embraer E190"},      {"E195", "Embraer E195"},
  {"E290", "Embraer E190-E2"},   {"E295", "Embraer E195-E2"},
  {"CRJ9", "Bombardier CRJ900"}, {"AT76", "ATR 72-600"},
  {"DH8D", "Dash 8 Q400"},       {"A400", "Airbus A400M"},
  {"C17",  "C-17 Globemaster"},  {"C130", "C-130 Hercules"},
  {"A124", "Antonov An-124"},    {"MD11", "McDonnell MD-11"},
  {"GLF6", "Gulfstream G650"},   {"GL7T", "Global 7500"},
  {"CL60", "Challenger 604"},    {"F2TH", "Falcon 2000"},
  {"PC24", "Pilatus PC-24"},     {"PC12", "Pilatus PC-12"},
  {"EC35", "Airbus H135"},       {"EC45", "Airbus H145"},
};

const char *typeName(const char *code) {
  if (!code || !code[0]) return "unknown type";
  for (unsigned i = 0; i < sizeof(typeNames)/sizeof(typeNames[0]); i++)
    if (strcmp(code, typeNames[i].code) == 0) return typeNames[i].name;
  return code;   // graceful fallback: the raw ICAO code beats an empty label
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
void drawCompassLetters() {
  // Compass letters sit on small background pads so ticks/crosshair/sweep
  // pixels never visually collide with the glyphs. They are re-drawn after
  // every clearInner() because the circular clear wipes this band.
  tft.setTextSize(1);
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

void clearInner() {
  // Clear a page's content area WITHOUT touching the bezel ring or tick band.
  //
  // Why a circle: any fillRect wide enough for real content has corners whose
  // distance from screen centre exceeds the bezel radius (e.g. a rect corner
  // at (28,36) sits at r=124 while the bezel lives at r=115/116). Those rect
  // clears were exactly what kept "eating" chunks of the outer ring. A circle
  // clear of radius 105 stays inside the tick band (ticks end at r=106) by
  // construction, so the ring can never be overwritten again.
  tft.fillCircle(120, 120, 105, C_BG);
  drawCompassLetters();
}

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
  drawCompassLetters();
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

bool metarCloudToken(const char *tok) {
  // Cloud groups look like FEW040, SCT025, BKN018, OVC004 (+ optional CB/TCU).
  return strlen(tok) >= 6 &&
         (strncmp(tok, "FEW", 3) == 0 || strncmp(tok, "SCT", 3) == 0 ||
          strncmp(tok, "BKN", 3) == 0 || strncmp(tok, "OVC", 3) == 0) &&
         asciiDigit(tok[3]) && asciiDigit(tok[4]) && asciiDigit(tok[5]);
}

const char *metarWxName(const char *tok) {
  // Map the common present-weather groups to something a person reads at a
  // glance. Unmatched tokens return nullptr and are simply skipped — the raw
  // METAR at the bottom of the page still carries the full truth.
  struct WxName { const char *code; const char *name; };
  static const WxName names[] = {
    {"TSRA", "T-STORM"}, {"+TSRA", "T-STORM"}, {"TS", "T-STORM"},
    {"+RA", "HVY RAIN"}, {"-RA", "LT RAIN"},   {"RA", "RAIN"},
    {"SHRA", "SHOWERS"}, {"-SHRA", "SHOWERS"}, {"+SHRA", "SHOWERS"},
    {"+SN", "HVY SNOW"}, {"-SN", "LT SNOW"},   {"SN", "SNOW"},
    {"DZ", "DRIZZLE"},   {"-DZ", "DRIZZLE"},   {"GR", "HAIL"},
    {"FG", "FOG"},       {"BR", "MIST"},       {"HZ", "HAZE"},
  };
  for (unsigned i = 0; i < sizeof(names)/sizeof(names[0]); i++)
    if (strcmp(tok, names[i].code) == 0) return names[i].name;
  return nullptr;
}

void parseMetarSummary() {
  copyStr(mucWx.wind,  sizeof(mucWx.wind),  "--");
  copyStr(mucWx.vis,   sizeof(mucWx.vis),   "--");
  copyStr(mucWx.temp,  sizeof(mucWx.temp),  "--");
  copyStr(mucWx.qnh,   sizeof(mucWx.qnh),   "--");
  copyStr(mucWx.cloud, sizeof(mucWx.cloud), "--");
  copyStr(mucWx.wx,    sizeof(mucWx.wx),    "DRY");
  mucWx.visM = -1;
  mucWx.ceilFt = -1;

  char work[128];
  copyStr(work, sizeof(work), mucWx.raw);
  char *tok = strtok(work, " \r\n");
  while (tok) {
    if (metarWindToken(tok)) {
      copyStr(mucWx.wind, sizeof(mucWx.wind), tok);
    } else if (strcmp(tok, "CAVOK") == 0) {
      copyStr(mucWx.vis, sizeof(mucWx.vis), "CAVOK");
      copyStr(mucWx.cloud, sizeof(mucWx.cloud), "CLEAR");
      mucWx.visM = 10000;
    } else if (strcmp(tok, "NSC") == 0 || strcmp(tok, "NCD") == 0) {
      copyStr(mucWx.cloud, sizeof(mucWx.cloud), "CLEAR");
    } else if (metarCloudToken(tok)) {
      // Report the FIRST (lowest) layer for the display and remember the
      // lowest broken/overcast layer as the operational ceiling.
      int hFt = (tok[3]-'0') * 10000 + (tok[4]-'0') * 1000 + (tok[5]-'0') * 100;
      if (mucWx.cloud[0] == '-')
        snprintf(mucWx.cloud, sizeof(mucWx.cloud), "%.3s %dft", tok, hFt);
      if ((strncmp(tok, "BKN", 3) == 0 || strncmp(tok, "OVC", 3) == 0) &&
          (mucWx.ceilFt < 0 || hFt < mucWx.ceilFt))
        mucWx.ceilFt = hFt;
    } else if (metarWxName(tok)) {
      copyStr(mucWx.wx, sizeof(mucWx.wx), metarWxName(tok));
    } else if (metarVisibilityToken(tok)) {
      mucWx.visM = atoi(tok);
      if (strcmp(tok, "9999") == 0) {
        copyStr(mucWx.vis, sizeof(mucWx.vis), "10km+");
        mucWx.visM = 10000;
      } else {
        snprintf(mucWx.vis, sizeof(mucWx.vis), "%.1fkm", mucWx.visM / 1000.0);
      }
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
// Ring radii are exactly 1/3, 2/3 and 3/3 of RADAR_TRAFFIC_R so the painted
// rings really do mean 10/20/30 km with the default 30 km range (the old
// 36/72/104 rings were subtly misaligned with the traffic projection).
#define RADAR_TRAFFIC_R  96
#define RADAR_RIM_R     101

void radarGrid() {
  tft.drawCircle(120, 120, RADAR_TRAFFIC_R / 3,     C_GRIDDIM);
  tft.drawCircle(120, 120, RADAR_TRAFFIC_R * 2 / 3, C_GRIDDIM);
  tft.drawCircle(120, 120, RADAR_TRAFFIC_R,         C_GRID);
  tft.drawLine(120, 24, 120, 216, C_GRIDDIM);
  tft.drawLine(24, 120, 216, 120, C_GRIDDIM);
  tft.fillCircle(120, 120, 2, C_BRIGHT);

  // Per-ring km labels on the NE diagonal, tucked just inside each ring where
  // they dodge the crosshair, the traffic counter, and the bezel tick band.
  tft.setTextSize(1);
  tft.setTextColor(C_GRID);
  for (int ring = 1; ring <= 3; ring++) {
    int r = RADAR_TRAFFIC_R * ring / 3;
    int lx = 120 + (int)((r - 14) * 0.707f);
    int ly = 120 - (int)((r - 14) * 0.707f) - 4;
    char km[8];
    snprintf(km, sizeof(km), "%d", (int)(RADAR_RANGE_KM * ring / 3));
    tft.setCursor(lx, ly);
    tft.print(km);
  }
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
    tft.fillRect(80, 28, 80, 14, C_BG);    // traffic-counter chip
  }
}

void pushBlip(int x, int y, uint8_t r) {
  // Register a screen region for erase on the next repaint. Aircraft markers
  // AND their stacked labels go through here, so nothing ever ghosts.
  if (prevBlipCount < MAX_BLIPS) {
    prevBlips[prevBlipCount].x = x;
    prevBlips[prevBlipCount].y = y;
    prevBlips[prevBlipCount].r = r;
    prevBlipCount++;
  }
}

void radarLabels() {
  // Traffic-counter chip: how many airborne aircraft are within the close
  // range around home, colour-coded by how busy the sky actually is. Green
  // means quiet, amber means normal MUC business, red means look up now.
  tft.fillRect(80, 28, 80, 14, C_BG);
  int nearby = 0;
  for (int i = 0; i < planeCount; i++)
    if (!planes[i].ground && planes[i].distKm <= RADAR_RANGE_KM / 2) nearby++;
  uint16_t c = nearby <= 2 ? C_GREEN : (nearby <= 6 ? C_AMBER : C_RED);
  char chip[16];
  snprintf(chip, sizeof(chip), "%d NEARBY", nearby);
  centerText(chip, 31, 1, c);
}

void radarPaint(bool record, bool drawLabels) {
  radarGrid();
  if (record) prevBlipCount = 0;
  int labeled = 0;

  for (int i = planeCount - 1; i >= 0; i--) {
    Aircraft &p = planes[i];
    if (p.ground) continue;
    float liveE, liveN;
    liveAircraftOffsetKm(p, &liveE, &liveN);
    float liveD = sqrtf(liveE * liveE + liveN * liveN);
    float liveB = fmodf(atan2f(liveE, liveN) * 57.29578f + 360.0f, 360.0f);
    float b = deg2rad(liveB);
    int x, y;
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
      if (record) pushBlip(x, y, 18);

      // Stacked mini-label (callsign / type / altitude) for the nearest few
      // aircraft, matching a classic radar-scope readout. The label sits on
      // whichever side of the marker has room, is clamped to the inner disc,
      // and registers its own erase blip so it can never ghost or smear.
      if (labeled < 3 && p.altFt > 0 && rr < 70.0f) {
        char l3[10];
        snprintf(l3, sizeof(l3), "%dm", (int)(p.altFt * 0.3048f));
        int lx = (x < 120) ? x + 10 : x - 52;
        int ly = y - 13;
        if (ly < 46) ly = 46;
        if (ly > 168) ly = 168;
        int cxl = lx + 21, cyl = ly + 13;   // label-box centre
        bool fits = (cxl - 120) * (cxl - 120) + (cyl - 120) * (cyl - 120) <= 84 * 84 &&
                    !radarTextShield(lx, ly) && !radarTextShield(lx + 42, ly);
        if (fits) {
          printFit(lx, ly,      p.flight, 1, C_WHITE, 42);
          printFit(lx, ly + 10, p.typ,    1, C_CYAN,  42);
          printFit(lx, ly + 20, l3,       1, C_AMBER, 42);
          if (record) pushBlip(cxl, cyl, 30);
          labeled++;
        }
      }
    } else {
      // Out-of-range contact: small blue square pinned to the rim — direction
      // cue only, consistent with the blue "outside range" grammar elsewhere.
      x = 120 + (int)(sinf(b) * RADAR_RIM_R);
      y = 120 - (int)(cosf(b) * RADAR_RIM_R);
      tft.fillRect(x - 2, y - 2, 5, 5, C_BLUE);
      if (record) pushBlip(x, y, 5);
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
  // Reserved zone for the traffic-counter chip: aircraft markers and their
  // stacked labels are simply not drawn here, so the counter never fights
  // with moving traffic for the same pixels.
  return x >= 76 && x <= 164 && y >= 26 && y <= 46;
}

void radarStep() {
  // Calm live motion: advance dead-reckoned traffic roughly once per second.
  // This replaced the rotating sweep beam — see the animation-state comment
  // near the top of the file for why the beam was removed.
  eraseBlips(false);
  radarPaint(true, false);
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
  // Full-page FlightRadar-style card. The FLIGHT and SPOTTER pages carry no
  // bezel ring or compass letters (per design: that chrome wasted space on
  // text-heavy pages), so a plain full-screen clear is safe here and gives
  // the card the whole round panel to breathe in.
  tft.fillScreen(C_BG);

  if (topBadge[0]) centerText(topBadge, 22, 1, badgeColor);
  centerText(n.flight, 38, 3, C_AMBER);

  // Airline row with a compact "tail logo" stand-in: a coloured roundel
  // carrying the callsign's two-letter prefix. Real airline logos would need
  // bitmap assets; the roundel gives each carrier a stable visual identity
  // (colour is derived from the prefix, so Lufthansa is always the same hue).
  const char *who = r.airline[0] ? r.airline : n.op;
  if (who[0]) {
    char name[22];
    fitCopy(name, sizeof(name), who, 20);
    const uint16_t roundelPalette[5] = {
      C_CYAN, C_AMBER, C_GREEN, C_BLUE, C_PURPLE
    };
    uint16_t rcol = roundelPalette[((uint8_t)n.flight[0] + (uint8_t)n.flight[1]) % 5];
    int nameW  = (int)strlen(name) * 6;
    int x0     = 120 - (nameW + 24) / 2;
    tft.fillCircle(x0 + 9, 72, 9, rcol);
    tft.setTextSize(1);
    tft.setTextColor(C_BG);
    tft.setCursor(x0 + 4, 69);
    tft.print(n.flight[0]); tft.print(n.flight[1]);
    tft.setTextColor(C_WHITE);
    tft.setCursor(x0 + 24, 69);
    tft.print(name);
  }

  if (r.codes[0]) {
    centerText(r.codes, 88, 2, C_CYAN);
    if (r.cities[0]) centerText(r.cities, 106, 1, C_DIM);
  } else {
    centerText("route unknown", 94, 1, C_DIM);
  }

  tft.drawFastHLine(40, 118, 160, C_GRIDDIM);

  char v1[16], v2[16];
  uint16_t distC = n.distKm < 5 ? C_RED : (n.distKm < 12 ? C_AMBER : C_WHITE);
  snprintf(v1, sizeof(v1), "%.1f km", (double)n.distKm);
  snprintf(v2, sizeof(v2), "%.0f %s", (double)n.bearingDeg, compass8(n.bearingDeg));
  statRow(126, "DIST", v1, "BRG", v2, distC, C_WHITE);

  uint16_t vsC = n.vrFpm > 300 ? C_BRIGHT : (n.vrFpm < -300 ? C_AMBER : C_WHITE);
  if (n.ground) snprintf(v1, sizeof(v1), "GROUND");
  else          snprintf(v1, sizeof(v1), "%d m", (int)(n.altFt * 0.3048f));
  snprintf(v2, sizeof(v2), "%+d m/s", (int)(n.vrFpm * 0.00508f));
  statRow(142, "ALT", v1, "V/S", v2, C_WHITE, vsC);

  snprintf(v1, sizeof(v1), "%d km/h", (int)(n.gsKt * 1.852f));
  snprintf(v2, sizeof(v2), "%.0f %s", (double)n.trackDeg, compass8(n.trackDeg));
  statRow(158, "SPD", v1, "TRK", v2, C_WHITE, C_WHITE);

  char tr[16];
  snprintf(tr, sizeof(tr), "%s %s", n.typ[0] ? n.typ : "----", n.reg);
  snprintf(v2, sizeof(v2), "%s", n.sqk[0] ? n.sqk : "----");
  statRow(174, "A/C", tr, "SQK", v2, C_WHITE, C_WHITE);

  // "Where do I look" strip: kept because it is the one piece of chrome that
  // earns its pixels — it converts screen data into a real-world direction.
  char look[20];
  snprintf(look, sizeof(look), "LOOK %s", compass8(n.bearingDeg));
  centerText(look, 196, 1, C_AMBER);
  float b = deg2rad(n.bearingDeg);
  int ax = 120 - (int)(strlen(look) * 3) - 12, ay = 199;
  tft.fillTriangle(ax + (int)(sinf(b)*6),      ay - (int)(cosf(b)*6),
                   ax + (int)(sinf(b+2.6f)*5), ay - (int)(cosf(b+2.6f)*5),
                   ax + (int)(sinf(b-2.6f)*5), ay - (int)(cosf(b-2.6f)*5), C_AMBER);
}

// ---------- Page 2: FLIGHT ----------
void detailStatic() {
  // Bezel-less by design: the flight card owns the whole panel.
  tft.fillScreen(C_BG);
  pageDots();
}

void detailDynamic() {
  int na = nearestAirborne();
  if (na < 0) {
    tft.fillScreen(C_BG);
    centerText("no contacts", 110, 2, C_DIM);
    return;
  }
  // The old "NEAREST FLIGHT" heading was dropped: its row now carries the
  // human-readable aircraft name ("Airbus A350-900" instead of "A359"),
  // which is far more useful to a spotter than a static page title. The raw
  // ICAO code + registration stay available in the A/C stat row below.
  flightCard(planes[na], rc[0], typeName(planes[na].typ), C_CYAN);
}

// ---------- Page 3: TRACK ----------
void trackStatic() {
  tft.fillScreen(C_BG);
  drawBezel();
  pageDots();
}

void trackDynamic() {
  clearInner();
  int na = nearestAirborne();
  if (na < 0) { centerText("no contacts", 110, 2, C_DIM); return; }
  Aircraft &n = planes[na];

  // Zoomed-out ground view: home stays centred and the wider range keeps much
  // more of an arrival/departure path visible at once (the old 1.5x zoom cut
  // most MUC approaches off at the screen edge).
  float range = n.distKm * 1.8f;
  if (range < 12) range = 12;
  float scale = 86.0f / range;

  tft.drawCircle(120, 126, 43, C_GRIDDIM);
  tft.drawCircle(120, 126, 86, C_GRIDDIM);
  tft.setTextSize(1); tft.setTextColor(C_GRID);
  tft.setCursor(164, 130); tft.printf("%.0fkm", (double)range);

  // Munich Airport marker, drawn whenever it falls inside the view. Arrivals
  // and departures now visually connect to the airport (two mini runway
  // strokes at the real 08/26 heading) instead of tracking to empty space.
  int mx = 120 + (int)(mucE * scale);
  int my = 126 - (int)(mucN * scale);
  if ((mx - 120) * (mx - 120) + (my - 126) * (my - 126) <= 80 * 80) {
    int ue = (int)(sinf(deg2rad(RWY_HDG)) * 8.0f);
    int un = (int)(cosf(deg2rad(RWY_HDG)) * 8.0f);
    tft.drawLine(mx - ue, my + un - 2, mx + ue, my - un - 2, C_GREY);
    tft.drawLine(mx - ue, my + un + 2, mx + ue, my - un + 2, C_GREY);
    tft.setTextColor(C_GREY);
    tft.setCursor(mx - 8, my + 8);
    tft.print("MUC");
  }

  tft.fillCircle(120, 126, 3, C_CYAN);
  tft.drawCircle(120, 126, 6, C_CYAN);
  tft.setTextColor(C_CYAN);
  tft.setCursor(111, 136); tft.print("YOU");

  // Flown path as a connected polyline that fades with age (bright = recent,
  // dim = old) — reads as a real flight track rather than disconnected dots.
  int prevX = 0, prevY = 0;
  bool havePrev = false;
  for (int i = 0; i < trailLen; i++) {
    int x = 120 + (int)(trailE[i] * scale);
    int y = 126 - (int)(trailN[i] * scale);
    bool inView = (x-120)*(x-120) + (y-126)*(y-126) <= 88*88;
    if (!inView) { havePrev = false; continue; }
    uint16_t c = (i > trailLen - 4) ? C_BRIGHT : (i > trailLen - 10 ? C_SWEEP : C_GRIDDIM);
    if (havePrev) tft.drawLine(prevX, prevY, x, y, c);
    tft.fillCircle(x, y, (i == trailLen - 1) ? 2 : 1, c);
    prevX = x; prevY = y; havePrev = true;
  }

  int px = 120 + (int)(n.eastKm * scale);
  int py = 126 - (int)(n.northKm * scale);
  if ((px-120)*(px-120) + (py-126)*(py-126) <= 88*88) {
    dashedLine(px, py, n.trackDeg, 150, C_GRID);
    dashedLine(px, py, n.trackDeg + 180.0f, 150, C_GRIDDIM);
    planeTriangle(px, py, n.trackDeg, 7, C_AMBER);
  }

  // Header band: callsign + route context; footer band: labeled live numbers.
  // Everything is centred and kept within the circle-safe rows, so long
  // callsigns or big numbers clip gracefully instead of touching the bezel.
  centerText(n.flight, 28, 2, C_AMBER);
  RouteCache *route = cachedRouteFor(n.flight);
  if (route && route->codes[0]) centerText(route->codes, 48, 1, C_CYAN);

  char live[40];
  snprintf(live, sizeof(live), "ALT %dm  SPD %dkm/h",
           (int)(n.altFt * 0.3048f), (int)(n.gsKt * 1.852f));
  centerText(live, 186, 1, C_WHITE);

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
  pageDots();
}

void airportDynamic() {
  clearInner();

  // Header: airport name with its identifier/wind line beneath. Placed at
  // y=31/41 — BELOW the "N" compass pad (y 20..29) — which fixes the bug
  // where the header text overwrote the N label after every live update.
  centerText("MUNICH AIRPORT", 31, 1, C_AMBER);
  // Identifier line doubles as a live field-wind chip when a METAR is cached —
  // a very "real airport display" touch that also hints at the runway
  // direction in use (winds from ~260 favour 26L/R, from ~080 favour 08L/R).
  if (mucWx.ok && mucWx.wind[0] && strcmp(mucWx.wind, "--") != 0) {
    char sub[26];
    snprintf(sub, sizeof(sub), "EDDM  %s", mucWx.wind);
    centerText(sub, 41, 1, C_DIM);
  } else {
    centerText("MUC / EDDM", 41, 1, C_DIM);
  }

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

    // Visual grammar of the operations map (bottom legend removed — the
    // shapes and colours ARE the legend, consistent across the device):
    //   blue square   = traffic outside the close-up, pinned to the map edge
    //   purple circle = stationary/ground aircraft on the field
    //   green arrow   = arriving traffic (ring marks the NEXT arrival)
    //   red arrow     = departing traffic (ring marks the NEXT departure)
    //   high overflights are intentionally not drawn — they belong to the
    //   radar page and only cluttered the runway view.
    if (clampedToEdge) {
      tft.fillRect(x - 2, y - 2, 5, 5, C_BLUE);
    } else if (p.ground) {
      tft.fillCircle(x, y, 3, C_PURPLE);
    } else if (low) {
      uint16_t c = trafficColor(p, false);
      if (i == nextArr || likelyArrival(p)) c = C_GREEN;
      if (i == nextDep || likelyDeparture(p)) c = C_RED;
      bool nextUp = (i == nextArr || i == nextDep);
      planeTriangle(x, y, p.trackDeg, nextUp ? 8 : 6, c);
      if (nextUp) tft.drawCircle(x, y, 11, c);   // "next up" highlight ring
    }
  }
}

void mucOpsStatic() {
  // Bezel-less by design: a text board gains nothing from the compass ring,
  // and the freed edge pixels let the two columns breathe.
  tft.fillScreen(C_BG);
  pageDots();
}

void drawMucBoardCell(int x, int y, int w, const char *label, int idx, uint16_t color, bool arrival) {
  // Two-column board cell inspired by FlightScnr's compact detail rows. Each
  // cell owns exactly three short text bands: label, callsign/distance, route.
  // This is much more stable on the round display than one long airport list.
  printFit(x, y, label, 1, color, w);
  if (idx < 0) {
    printFit(x, y + 14, "watching...", 1, C_DIM, w);
    return;
  }

  Aircraft &p = planes[idx];
  float d = distanceToMucKm(p);
  float eta = etaMinForDistance(d, p);

  // Line 1: callsign (the "who").
  printFit(x, y + 14, p.flight, 1, C_WHITE, w);

  // Line 2: the operational answer — minutes out for arrivals (when speed
  // data allows an ETA), distance from the field for departures.
  char when[20];
  if (arrival && eta >= 0) snprintf(when, sizeof(when), "in %.0f min", (double)eta);
  else if (arrival)        snprintf(when, sizeof(when), "%.1f km out", (double)d);
  else if (p.ground)       snprintf(when, sizeof(when), "on field");
  else                     snprintf(when, sizeof(when), "%.1f km away", (double)d);
  printFit(x, y + 26, when, 1, color, w);

  // Line 3: route context when the route API knows the flight, otherwise the
  // aircraft type + altitude as a graceful fallback.
  char routeLine[30];
  RouteCache *route = cachedRouteFor(p.flight);
  if (route && route->codes[0]) {
    snprintf(routeLine, sizeof(routeLine), "%.13s", route->codes);
  } else if (p.ground) {
    snprintf(routeLine, sizeof(routeLine), "%s GND", p.typ[0] ? p.typ : "----");
  } else {
    snprintf(routeLine, sizeof(routeLine), "%s %dm", p.typ[0] ? p.typ : "----", (int)(p.altFt * 0.3048f));
  }
  printFit(x, y + 38, routeLine, 1, C_DIM, w);
}

void mucOpsDynamic() {
  // FIDS-style split board: arrivals on the left in green, departures on the
  // right in red (same colour grammar as the radar/map pages). Exactly the
  // next two flights per side — the spotter page owns "interesting aircraft",
  // so no COOL footer competes with the board. Bezel-less, so the columns use
  // the full panel width.
  tft.fillScreen(C_BG);
  centerText("MUC TRAFFIC BOARD", 16, 1, C_AMBER);

  int arrIdx[2], depIdx[2];
  topMucTraffic(true, arrIdx);
  topMucTraffic(false, depIdx);

  tft.setTextSize(2);
  tft.setTextColor(C_GREEN); tft.setCursor(50, 34);  tft.print("ARR");
  tft.setTextColor(C_RED);   tft.setCursor(152, 34); tft.print("DEP");
  tft.drawFastHLine(28, 52, 88, C_GREEN);
  tft.drawFastHLine(124, 52, 88, C_RED);
  tft.drawFastVLine(120, 58, 128, C_GRIDDIM);

  drawMucBoardCell(28,  60,  88, "NEXT", arrIdx[0], C_GREEN, true);
  drawMucBoardCell(28,  124, 88, "THEN", arrIdx[1], C_GREEN, true);
  drawMucBoardCell(124, 60,  88, "NEXT", depIdx[0], C_RED, false);
  drawMucBoardCell(124, 124, 88, "THEN", depIdx[1], C_RED, false);

  // Footer: when the board saw its data (age of the last successful fetch),
  // so a stale board is honest about being stale.
  char age[24];
  snprintf(age, sizeof(age), "data %lus ago", (unsigned long)((millis() - lastFetch) / 1000UL));
  centerText(age, 206, 1, C_GRIDDIM);
}

// ---------- Page 6: MUC WEATHER ----------
void dataRow(int y, const char *label, const char *value, uint16_t valueColor) {
  // Shared airport-data row. Labels and values have hard pixel boxes because
  // external APIs can surprise us with long strings; clipping is better than
  // letting one value push through the next row on the round display.
  printFit(46, y, label, 1, C_GRID, 58);
  printFit(106, y, value, 1, valueColor, 86);
}

const char *flightCategory(uint16_t *color) {
  // Operations hint derived from the METAR itself (visibility + ceiling),
  // modelled on the standard VFR/MVFR/IFR flight-category bands. This is
  // honest, locally computed data — we deliberately do NOT fake a delay
  // percentage here, because no free API provides trustworthy 12 h delay
  // statistics (see changelog on why aviationstack was removed).
  bool visGood     = mucWx.visM < 0 || mucWx.visM >= 8000;
  bool visMarginal = mucWx.visM < 0 || mucWx.visM >= 3000;
  bool ceilGood     = mucWx.ceilFt < 0 || mucWx.ceilFt >= 3000;
  bool ceilMarginal = mucWx.ceilFt < 0 || mucWx.ceilFt >= 1000;
  if (visGood && ceilGood)         { *color = C_GREEN; return "NORMAL OPS"; }
  if (visMarginal && ceilMarginal) { *color = C_AMBER; return "MARGINAL OPS"; }
  *color = C_RED;
  return "LOW VIS OPS";
}

void activeRunway(char *dst, size_t dstN) {
  // Estimate the runway direction in use from the METAR wind: aircraft take
  // off and land into the wind, so winds from the west (~260) mean 26L/26R
  // operations and winds from the east (~080) mean 08L/08R. Variable/calm
  // winds give no answer, and we say so instead of guessing.
  dst[0] = 0;
  if (!mucWx.ok) return;
  if (!asciiDigit(mucWx.wind[0]) || !asciiDigit(mucWx.wind[1]) ||
      !asciiDigit(mucWx.wind[2])) return;          // "VRB..." or unparsed
  int windFrom = (mucWx.wind[0]-'0') * 100 + (mucWx.wind[1]-'0') * 10 + (mucWx.wind[2]-'0');
  if (angleDiffDeg((float)windFrom, RWY_HDG) < 90.0f)
    copyStr(dst, dstN, "08 L/R");
  else
    copyStr(dst, dstN, "26 L/R");
}

void mucWeatherStatic() {
  // Bezel-less by design: a weather report gains nothing from a compass ring.
  tft.fillScreen(C_BG);
  pageDots();
}

void mucWeatherDynamic() {
  tft.fillScreen(C_BG);
  centerText("MUC WEATHER", 16, 1, C_AMBER);
  bool ok = fetchMucWeather();
  if (!ok) {
    centerText("no METAR yet", 104, 2, C_DIM);
    centerText("retrying on next visit", 128, 1, C_GRID);
    return;
  }

  // Full aviation snapshot: wind, visibility, lowest cloud layer, temperature,
  // pressure, significant weather, and the estimated runway in use. Values
  // are colour-coded only when they say something operationally interesting.
  dataRow(36,  "WIND",  mucWx.wind,  C_WHITE);
  dataRow(54,  "VIS",   mucWx.vis,   C_WHITE);
  dataRow(72,  "CLOUD", mucWx.cloud, C_WHITE);
  dataRow(90,  "TEMP",  mucWx.temp,  C_WHITE);
  dataRow(108, "QNH",   mucWx.qnh,   C_WHITE);
  dataRow(126, "WX",    mucWx.wx,
          strcmp(mucWx.wx, "DRY") == 0 ? C_WHITE : C_AMBER);

  char rwy[10];
  activeRunway(rwy, sizeof(rwy));
  dataRow(144, "RWY", rwy[0] ? rwy : "--", rwy[0] ? C_CYAN : C_DIM);

  // Field status strip (see flightCategory for why this is METAR-derived).
  uint16_t catColor;
  const char *cat = flightCategory(&catColor);
  tft.drawFastHLine(40, 162, 160, C_GRIDDIM);
  tft.fillCircle(70, 175, 3, catColor);
  centerText(cat, 171, 1, catColor);

  // Raw METAR tail: the unfiltered truth for anyone who reads METAR, clipped
  // into two fixed rows so odd station remarks can never break the layout.
  char raw1[26], raw2[26];
  fitCopy(raw1, sizeof(raw1), mucWx.raw, 22);
  fitCopy(raw2, sizeof(raw2), mucWx.raw + strlen(raw1), 22);
  printFit(54, 188, raw1, 1, C_DIM, 132);
  printFit(54, 200, raw2, 1, C_DIM, 132);
}

// ---------- Page 7: SPOTTER ----------
void spotterStatic() {
  // Bezel-less by design: the flight card owns the whole panel.
  tft.fillScreen(C_BG);
  pageDots();
}

void spotterDynamic() {
  int ci = coolestIdx();
  if (ci < 0) {
    tft.fillScreen(C_BG);
    centerText("no contacts", 110, 2, C_DIM);
    return;
  }
  Aircraft &p = planes[ci];
  const char *label;
  int score = coolScore(p, &label);
  uint16_t badgeC = score >= 85 ? C_RED : (score >= 50 ? C_AMBER : C_DIM);

  // One concise spotting line instead of the old "WHY:" prefix — what the
  // aircraft is, plus its live operational status around Munich:
  //   "SUPERJUMBO A380 - MUC 12MIN"  (inbound, with ETA)
  //   "C-17 GLOBEMASTER - AT MUC"    (on the field right now)
  //   "QUEEN B747 - LEAVING MUC"     (on climbout)
  //   "RARE QUAD A340 - 24KM AWAY"   (notable but just passing by)
  // Departure times for parked aircraft are not in any free feed, so ground
  // traffic honestly says "AT MUC" instead of inventing a departure ETA.
  char badge[34];
  float mucD = distanceToMucKm(p);
  float eta = etaMinForDistance(mucD, p);
  if (p.ground && mucD < 6.0f) {
    snprintf(badge, sizeof(badge), "%.18s - AT MUC", label);
  } else if (likelyArrival(p) && eta >= 0 && eta < 90) {
    snprintf(badge, sizeof(badge), "%.15s - MUC %dMIN", label, (int)(eta + 0.5f));
  } else if (likelyDeparture(p)) {
    snprintf(badge, sizeof(badge), "%.15s - LEAVING MUC", label);
  } else {
    snprintf(badge, sizeof(badge), "%.15s - %.0fKM AWAY", label, (double)p.distKm);
  }
  flightCard(p, rc[1], badge, badgeC);
}

// ---------- Page management ----------
bool skipPage(uint8_t p) {
  if ((p == 1 || p == 2) && nearestAirborne() < 0) return true;
  if ((p == 4 || p == 5) && planeCount == 0) return true;
  return false;
}

void drawPageFull() {
  prevBlipCount = 0;
  switch (page) {
    case 0: radarStatic();   radarDynamic();   break;
    case 1: detailStatic();  detailDynamic();  break;
    case 2: trackStatic();   trackDynamic();   break;
    case 3: airportStatic(); airportDynamic(); break;
    case 4: mucOpsStatic();  mucOpsDynamic();  break;
    case 5: spotterStatic();    spotterDynamic();    break;
    case 6: mucWeatherStatic(); mucWeatherDynamic(); break;
  }
}

void drawPageUpdate() {
  switch (page) {
    case 0: radarDynamic();   break;
    case 1: detailDynamic();  break;
    case 2: trackDynamic();   break;
    case 3: airportDynamic(); break;
    case 4: mucOpsDynamic();  break;
    case 5: spotterDynamic();    break;
    case 6: mucWeatherDynamic(); break;
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
  if (page == 5) {
    int ci = coolestIdx();
    if (ci >= 0) fetchRoute(planes[ci].flight, ROUTE_SLOT_SPOTTER);
  }
}

// ---------- Main ----------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Plane Radar v8 (calm radar, card pages) booting...");
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
      if (alertIdx >= 0 && page != 5 &&
          coolScore(planes[alertIdx], &alertLabel) >= ALERT_SCORE &&
          strcmp(planes[alertIdx].flight, lastAlertCs) != 0) {
        copyStr(lastAlertCs, sizeof(lastAlertCs), planes[alertIdx].flight);
        Serial.printf("ALERT: %s -> spotter page (%s)\n",
                      planes[alertIdx].flight, alertLabel);
        page = 5;
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

  if (page == 0 && now - lastRadarStep >= RADAR_STEP_MS) {
    lastRadarStep = now;
    radarStep();
  }

  if (page == 3 && now - lastLiveStep >= AIRPORT_STEP_MS) {
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
