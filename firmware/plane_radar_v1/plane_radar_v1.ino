/*
  plane_radar_v1.ino — rev1.0 — Munich aviation desk radar (ESP32-C3 + GC9A01).

  Hardware: ESP32-C3 Super Mini + GC9A01 1.28" round display (240x240)
  Wiring:   see docs/display_wiring_guide.html and docs/device_manual.html

  Data:     https://api.adsb.lol          (aircraft positions, free, no key)
            https://api.adsbdb.com        (airline + route lookup by callsign)
            https://aviationweather.gov   (MUC METAR weather, free, no key)
  Setup:    copy config.example.h to config.h, enter Wi-Fi name+password and
            your location. Everything else has sensible defaults (see the
            CONFIG DEFAULTS block below) so the device is gift-ready: a new
            owner only edits Wi-Fi + lat/lon + home airport.

  ================================ rev1.0 =================================
  This revision restructured the firmware from the experimental v13 build
  into the final product layout:
    - Page order changed to: HOME RADAR > TRAFFIC BRIEF > NEAREST TRACK >
      COOLEST TRACK > MUC MAP > WEATHER > LEGEND.
    - Removed the separate MUC OPS board page (next ARR/DEP now live on the
      MUC MAP page) and the SPOTTER card page (replaced by the COOLEST
      tracking page, which shows the same data plus a live path view).
    - Home radar: red compass-north arrow instead of "N", S/W/E letters
      removed, DEP count (red) sits at the west point, ARR count (green) at
      the east point, total (white) under the north arrow. Inner rings are
      subtle solid lines; only the outer range ring is dashed.
    - Radar labels are now priority-picked (emergency > helicopter > special
      > nearest) instead of one-per-distance-band, so the interesting traffic
      gets named and the rest stay compact symbols.
    - New TRAFFIC BRIEF page: five compact entries (nearest / coolest /
      helicopter / emergency / next MUC ARR+DEP) with graceful fallbacks.
    - Nearest + Coolest tracking pages share one renderer with a properly
      visible mini-map (the old full-screen rings were mostly hidden behind
      the text bands, which looked like a broken grid).
    - MUC MAP zoomed out (configurable), header replaced by live counters +
      temperature/wind, next ARR/DEP rows at the bottom, short projected
      path lines on moving traffic.
    - Weather page centred with a plain "MUC" header (the old long header
      clipped on the round glass).
    - New LEGEND page documents every colour/symbol so the other pages can
      stay clutter-free.
    - Page button rewritten around a CHANGE interrupt: presses are latched
      even while the firmware is inside a blocking HTTP fetch, which was the
      root cause of "button sometimes stops working". Long press still
      resumes the auto carousel. Pin + auto-scroll are configurable.
    - Every page now redraws from cached data after every successful fetch,
      so no page goes stale while the user stays on it.
  =========================================================================

  Colour/symbol grammar (see LEGEND page):
    red=DEP MUC, green=ARR MUC, amber=other traffic, blue square=out of
    range (edge-clamped), star=special aircraft, rotor symbol=helicopter,
    purple circle=on ground, red "!" dot=emergency squawk.

  Button hardware note (GPIO choice):
    PAGE_BUTTON_PIN defaults to 2 (external button to GND, INPUT_PULLUP).
    The Super Mini's onboard BOOT button is GPIO9 and IS usable as the page
    button after boot (it is only sampled as a strapping pin at reset; the
    MatixYo radar uses it the same way) — set PAGE_BUTTON_PIN 9 in config.h
    to use it and skip soldering. Do NOT use the RESET button: it hard-resets
    the chip. Caveat for GPIO9: holding it during power-on enters the ROM
    bootloader, so "press while plugging in" will look like a hang (that is
    normal ESP32-C3 behaviour, release and it boots).

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

// ======================= CONFIG DEFAULTS =======================
// Everything here can be overridden from config.h — a gift recipient only
// HAS to set Wi-Fi + HOME_LAT/HOME_LON there; these defaults cover the rest.
#ifndef PAGE_INTERVAL_MS
#define PAGE_INTERVAL_MS 12000       // auto-scroll dwell per page (ms)
#endif

#ifndef AUTO_SCROLL_ENABLED
#define AUTO_SCROLL_ENABLED 1        // 0 = only the button changes pages
#endif

#ifndef PAGE_BUTTON_ENABLED
#define PAGE_BUTTON_ENABLED 1        // 0 = no button wired (pure auto-scroll)
#endif

#ifndef PAGE_BUTTON_PIN
#define PAGE_BUTTON_PIN 9            // 9 = onboard BOOT button (chosen as the
#endif                               // enclosure default; 2 = external button)

#ifndef HOME_AIRPORT_IATA
#define HOME_AIRPORT_IATA "MUC"      // used for route matching + labels
#endif

#ifndef HOME_AIRPORT_ICAO
#define HOME_AIRPORT_ICAO "EDDM"     // used for the METAR request
#endif

#ifndef TRACK_RANGE_MIN_KM
#define TRACK_RANGE_MIN_KM 10.0f     // tracking pages never zoom in past this
#endif

#ifndef MUC_MAP_SCALE
#define MUC_MAP_SCALE 16.0f          // airport map px-per-km (26 = close-up,
#endif                               // 16 shows the approach/departure flows)

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
#define C_GRID    tft.color565(38, 124, 64)
#define C_GRIDDIM tft.color565(10, 38, 24)
#define C_BRIGHT  tft.color565(0, 220, 255)
#define C_SWEEP   tft.color565(0, 118, 70)
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

// Forward declarations for helpers that live with the MUC-map logic further
// down but are used by the brief/tracking pages above them. Explicit
// prototypes here keep the build independent of the IDE's auto-prototype
// quirks with user-defined reference parameters.
int  nextArrivalIdx();
int  nextDepartureIdx();
bool likelyArrival(const Aircraft &p);
bool likelyDeparture(const Aircraft &p);
int  coolestIdx();

uint32_t lastFetch = 0, lastPageSwitch = 0;
int      failCount = 0;
// rev1.0 page order: live radar first, the daily-driver pages next, then
// reference pages (weather/legend) at the end of the carousel.
#define PAGE_RADAR    0
#define PAGE_SUMMARY  1
#define PAGE_NEAREST  2
#define PAGE_COOLEST  3
#define PAGE_MUC_MAP  4
#define PAGE_MUC_WX   5
#define PAGE_LEGEND   6
#define PAGE_COUNT    7
uint8_t  page = PAGE_RADAR;
// Radar and the tracking pages carry the most live value, so they hold
// longer; reference pages rotate faster to keep the desk display moving.
const uint32_t pageDur[PAGE_COUNT] = {26000, 16000, 20000, 16000, 16000, 12000, 10000};

// Manual page button.
//
// Wiring is active-low: one side of a momentary pushbutton goes to
// PAGE_BUTTON_PIN, the other side goes to GND. INPUT_PULLUP keeps the pin HIGH
// when the button is idle, so no external resistor is required. A short press
// advances to the next available page and freezes the carousel there; a long
// press resumes automatic cycling. That gives the physical control the exact
// desk-device behaviour the user asked for: browse pages, then stay put.
#define PAGE_BUTTON_MIN_MS  40     // presses shorter than this are bounce
#define PAGE_BUTTON_LONG_MS 1200   // held longer than this = resume carousel
#define ADSB_HTTP_TIMEOUT_MS    6500
#define ROUTE_HTTP_TIMEOUT_MS   2200
#define METAR_HTTP_TIMEOUT_MS   4500

// Interrupt-latched button (rev1.0 reliability fix).
//
// Root cause of "the button sometimes stops working": the old polled state
// machine only sampled the pin inside loop(), but fetchPlanes()/fetchRoute()
// block for up to several seconds inside HTTPClient. A press that started
// AND ended inside such a window was completely invisible to the poller.
// A CHANGE interrupt cannot miss the edges: the ISR timestamps the press on
// the falling edge and classifies it (short/long) on the rising edge, and
// loop() consumes the latched event whenever it next runs — even if that is
// two seconds later, the page still turns exactly once per press.
bool              manualPageHold = false;
volatile uint32_t btnDownAtMs    = 0;   // falling-edge timestamp (0 = none)
volatile uint8_t  btnEvent       = 0;   // 0=none, 1=short press, 2=long press
// NOTE: the ISR body (pageButtonIsr) is defined further down, next to
// handlePageButton(). It must NOT be the first function definition in this
// file: the Arduino builder inserts its auto-generated prototypes right
// before the first function, and any prototype mentioning Trail/RouteCache
// would then land above those struct definitions and break the build.

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

// Flown-path history buffers. One trail follows the nearest airborne
// aircraft (NEAREST page), a second follows the current coolest aircraft
// (COOLEST page). Both reset automatically when "their" aircraft changes.
#define TRAIL_MAX 20
struct Trail {
  float e[TRAIL_MAX], n[TRAIL_MAX];
  int   len = 0;
  char  forCs[10] = "";
};
Trail trailNear, trailCool;

void trailPush(Trail &t, const Aircraft &p) {
  if (strcmp(t.forCs, p.flight) != 0) {
    strncpy(t.forCs, p.flight, sizeof(t.forCs) - 1);
    t.forCs[sizeof(t.forCs) - 1] = 0;
    t.len = 0;
  }
  if (t.len == TRAIL_MAX) {
    memmove(t.e, t.e + 1, sizeof(float) * (TRAIL_MAX - 1));
    memmove(t.n, t.n + 1, sizeof(float) * (TRAIL_MAX - 1));
    t.len--;
  }
  t.e[t.len] = p.eastKm;
  t.n[t.len] = p.northKm;
  t.len++;
}

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

int circleTextBoxW(int y, uint8_t size) {
  // A 240x240 round TFT does not have a rectangular safe area. The top and
  // bottom rows are much narrower than the middle, so a normal centred string
  // can look fine in code but get physically clipped by the circular glass.
  // This helper estimates the chord width available at the text row and gives
  // centerText() a real pixel box to clip against before drawing.
  int rowMid = y + 4 * size;
  int dy = abs(rowMid - 120);
  const int safeR = 108;     // leaves room for lens mask, bezel, and tick marks
  if (dy >= safeR) return 42;
  int half = (int)sqrtf((float)(safeR * safeR - dy * dy)) - 8;
  int w = half * 2;
  if (w > 208) w = 208;
  if (w < 42)  w = 42;
  return w;
}

void centerText(const char *s, int y, uint8_t size, uint16_t color) {
  char b[48];
  int boxW = circleTextBoxW(y, size);
  fitCopy(b, sizeof(b), s, boxW / (6 * size));
  tft.setTextSize(size);
  tft.setTextColor(color);
  int x = 120 - (int)strlen(b) * 3 * size;
  int minX = 120 - boxW / 2;
  if (x < minX) x = minX;
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

bool isEmergencySquawk(const Aircraft &p) {
  // 7700/7600/7500 are the transponder states worth treating as immediate
  // special traffic: general emergency, radio failure, and unlawful
  // interference. They outrank every other "cool aircraft" reason.
  return strcmp(p.sqk, "7700") == 0 || strcmp(p.sqk, "7600") == 0 ||
         strcmp(p.sqk, "7500") == 0;
}

int coolScore(const Aircraft &p, const char **label) {
  int best = 0; *label = "REGULAR TRAFFIC";
  if (isEmergencySquawk(p)) { *label = "EMERGENCY SQUAWK"; return 100; }

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

bool isHelicopter(const Aircraft &p) {
  // Keep helicopter recognition deliberately broad. ADS-B type codes vary
  // across feeds, but most rotorcraft around Munich show up as EC35/EC45/H135,
  // H145, R44, AS50, A109, B06, etc. A distinct symbol makes them readable
  // everywhere without needing another API.
  return strncmp(p.typ, "EC", 2) == 0 || strncmp(p.typ, "H", 1) == 0 ||
         strncmp(p.typ, "R44", 3) == 0 || strncmp(p.typ, "R66", 3) == 0 ||
         strncmp(p.typ, "AS", 2) == 0 || strncmp(p.typ, "A109", 4) == 0 ||
         strncmp(p.typ, "B06", 3) == 0 || strncmp(p.typ, "B407", 4) == 0;
}

bool isNotableAircraft(const Aircraft &p) {
  const char *label;
  return coolScore(p, &label) >= 70;
}

bool isHeavyType(const Aircraft &p) {
  // Wake-category style size hint ("size based on real values"): widebodies
  // and large military transports get a visibly bigger marker on the runway.
  return strncmp(p.typ, "A33", 3) == 0 || strncmp(p.typ, "A34", 3) == 0 ||
         strncmp(p.typ, "A35", 3) == 0 || strncmp(p.typ, "A38", 3) == 0 ||
         strncmp(p.typ, "B74", 3) == 0 || strncmp(p.typ, "B76", 3) == 0 ||
         strncmp(p.typ, "B77", 3) == 0 || strncmp(p.typ, "B78", 3) == 0 ||
         strncmp(p.typ, "MD1", 3) == 0 || strncmp(p.typ, "C17", 3) == 0 ||
         strncmp(p.typ, "A40", 3) == 0 || strncmp(p.typ, "A12", 3) == 0 ||
         strncmp(p.typ, "C5M", 3) == 0 || strncmp(p.typ, "IL7", 3) == 0;
}

// ---------- Shared chrome ----------
void drawCompassLetters() {
  // Compass letters sit on small background pads so ticks, crosshairs, and
  // live traffic pixels never visually collide with the glyphs. They are
  // re-drawn after every circular clear because that clear wipes this band.
  tft.setTextSize(1);
  tft.fillRect(113, 13, 14, 9, C_BG);
  tft.fillRect(113, 218, 14, 9, C_BG);
  tft.fillRect(13, 116, 14, 9, C_BG);
  tft.fillRect(214, 116, 14, 9, C_BG);
  tft.setTextColor(C_BRIGHT);
  tft.setCursor(117, 14);  tft.print("N");
  tft.setTextColor(C_GRID);
  tft.setCursor(117, 219); tft.print("S");
  tft.setCursor(17, 117);  tft.print("W");
  tft.setCursor(218, 117); tft.print("E");
}

void clearInnerChrome(bool withCompass) {
  // Clear a page's content area WITHOUT touching the bezel ring or tick band.
  //
  // Why a circle: any fillRect wide enough for real content has corners whose
  // distance from screen centre exceeds the bezel radius (e.g. a rect corner
  // at (28,36) sits at r=124 while the bezel lives at r=115/116). Those rect
  // clears were exactly what kept "eating" chunks of the outer ring. A circle
  // clear of radius 105 stays inside the tick band (ticks end at r=106) by
  // construction, so the ring can never be overwritten again.
  tft.fillCircle(120, 120, 105, C_BG);
  if (withCompass) drawCompassLetters();
}

void clearInner() {
  clearInnerChrome(true);
}

void drawBezelRing() {
  // Compact bezel (user feedback: "make the edge ring nicer & compact").
  // One thin ring + only twelve 30-degree ticks instead of the previous busy
  // 36-tick double ring. Kept slightly inside the physical 240 px edge because
  // some GC9A01 modules clip radius-119 pixels at a few angles.
  tft.drawCircle(120, 120, 116, C_GRID);
  for (int a = 0; a < 360; a += 30) {
    float r = deg2rad((float)a);
    float s = sinf(r), c = cosf(r);
    int   l = (a % 90 == 0) ? 8 : 5;
    tft.drawLine(120 + (int)(s*116), 120 - (int)(c*116),
                 120 + (int)(s*(116-l)), 120 - (int)(c*(116-l)),
                 (a % 90 == 0) ? C_GRID : C_GRIDDIM);
  }
}

void drawBezelChrome(bool withCompass) {
  drawBezelRing();
  if (withCompass) drawCompassLetters();
}

void drawBezel() {
  drawBezelChrome(true);
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
  // Copy + sanitize. The ADS-B feed occasionally delivers callsigns with
  // non-ASCII or control bytes; the GFX font renders those as solid blocks
  // (the "@@@@@@@@" seen on the traffic board photo). Filtering to printable
  // ASCII here fixes every page at once instead of patching each printf.
  if (!src) src = "";
  size_t o = 0;
  for (size_t i = 0; src[i] && o < n - 1; i++) {
    char c = src[i];
    if (c < 32 || c > 126) continue;   // drop control/8-bit bytes entirely
    dst[o++] = c;
  }
  dst[o] = 0;
  for (int i = (int)strlen(dst) - 1; i >= 0 && dst[i] == ' '; i--) dst[i] = 0;
}

bool fetchPlanes() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(ADSB_HTTP_TIMEOUT_MS);

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

  // Feed both path histories from the fresh fetch: the NEAREST page follows
  // the closest airborne aircraft, the COOLEST page follows the top-scored
  // one. trailPush() resets automatically when the tracked callsign changes,
  // which is exactly the "auto-switch to the new nearest aircraft" behaviour
  // the tracking pages need.
  int na = nearestAirborne();
  if (na >= 0) trailPush(trailNear, planes[na]);
  int ci = coolestIdx();
  if (ci >= 0) trailPush(trailCool, planes[ci]);

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
  http.setTimeout(ROUTE_HTTP_TIMEOUT_MS);
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
  // green = inbound/destination MUC, red = outbound/from MUC, blue = out-of-
  // range, purple = ground/stationary, yellow = normal non-MUC traffic.
  // Route cache wins when known;
  // otherwise heading/altitude/distance provide a live heuristic.
  if (outOfRange) return C_BLUE;
  if (p.ground) return C_PURPLE;
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
  http.setTimeout(METAR_HTTP_TIMEOUT_MS);
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

void starSymbol(int cx, int cy, int size, uint16_t color) {
  // Star = "notable/cool" across all moving map pages. A line-star is more
  // legible than a filled 5-point polygon at 6-10 px on this display.
  tft.drawLine(cx, cy - size, cx, cy + size, color);
  tft.drawLine(cx - size, cy, cx + size, cy, color);
  tft.drawLine(cx - size + 1, cy - size + 1, cx + size - 1, cy + size - 1, color);
  tft.drawLine(cx - size + 1, cy + size - 1, cx + size - 1, cy - size + 1, color);
  tft.fillCircle(cx, cy, 2, color);
}

void emergencySymbol(int cx, int cy, uint16_t color) {
  // Emergency is not a decorative "cool" marker. It gets a distinct alert dot
  // with an exclamation mark so 7700/7600/7500 traffic can be spotted fast.
  tft.fillCircle(cx, cy, 7, color);
  tft.setTextSize(1);
  tft.setTextColor(C_BG);
  tft.setCursor(cx - 2, cy - 4);
  tft.print("!");
}

void helicopterSymbol(int cx, int cy, float trackDeg, int size, uint16_t color) {
  // Helicopter = compact fuselage, tail boom, and rotor bar. It is intentionally
  // not heading-perfect; the goal is instant rotorcraft recognition at tiny size.
  float t = deg2rad(trackDeg), s = sinf(t), c = cosf(t);
  int noseX = cx + (int)(s * size);
  int noseY = cy - (int)(c * size);
  int tailX = cx - (int)(s * (size + 5));
  int tailY = cy + (int)(c * (size + 5));
  int rx0 = cx - (int)(c * (size + 3));
  int ry0 = cy - (int)(s * (size + 3));
  int rx1 = cx + (int)(c * (size + 3));
  int ry1 = cy + (int)(s * (size + 3));
  tft.drawLine(tailX, tailY, noseX, noseY, color);
  tft.drawLine(rx0, ry0, rx1, ry1, color);
  tft.fillCircle(cx, cy, 3, color);
  tft.fillCircle(tailX, tailY, 1, color);
}

void trafficSymbol(const Aircraft &p, int x, int y, int size, uint16_t color, bool runwayHighlight) {
  // One marker rule for radar, track, airport map, and spotter context:
  // emergencies get an alert dot, helicopters are rotor symbols, cool/notable
  // aircraft are stars, and normal fixed-wing traffic is a heading arrow.
  // Runway traffic stays a yellow aircraft marker because runway occupancy is
  // more important than category.
  if (runwayHighlight) {
    planeTriangle(x, y, p.trackDeg, size, C_AMBER);
  } else if (isEmergencySquawk(p)) {
    emergencySymbol(x, y, C_RED);
  } else if (isHelicopter(p)) {
    helicopterSymbol(x, y, p.trackDeg, size, color);
  } else if (isNotableAircraft(p)) {
    starSymbol(x, y, size, color);
  } else {
    planeTriangle(x, y, p.trackDeg, size, color);
  }
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
// rings stay honest when RADAR_RANGE_KM changes. TRAFFIC_R was widened from
// 92 to 98 px (user note: "the distance to the edge can be smaller, keep it
// super subtle") — the scope now runs almost to the compact bezel.
#define RADAR_TRAFFIC_R  98
#define RADAR_RIM_R     108   // rim squares as far out as the bezel allows

void dashedCircle(int cx, int cy, int r, uint16_t color) {
  // All range rings are dashed (user sketch): they are range references, not
  // cages around the data. Short line segments are cheaper than per-pixel
  // arcs and look crisp enough on the GC9A01.
  for (int a = 0; a < 360; a += 14) {
    float a0 = deg2rad((float)a);
    float a1 = deg2rad((float)(a + 8));
    tft.drawLine(cx + (int)(sinf(a0) * r), cy - (int)(cosf(a0) * r),
                 cx + (int)(sinf(a1) * r), cy - (int)(cosf(a1) * r),
                 color);
  }
}

void radarGrid(bool withKmLabels) {
  // Inner rings: subtle SOLID lines in one consistent tone (user feedback:
  // the second ring looked randomly darker). Only the outer range ring is
  // visually different — dashed and brighter — because it means "edge of
  // the configured scope" rather than just a distance marker.
  tft.drawCircle(120, 120, RADAR_TRAFFIC_R / 3,     C_GRIDDIM);
  tft.drawCircle(120, 120, RADAR_TRAFFIC_R * 2 / 3, C_GRIDDIM);
  dashedCircle(120, 120, RADAR_TRAFFIC_R,           C_GRID);
  tft.drawLine(120, 26, 120, 214, C_GRIDDIM);
  tft.drawLine(26, 120, 214, 120, C_GRIDDIM);
  tft.fillCircle(120, 120, 2, C_BRIGHT);

  // Small 10/20/30 markers on the NE diagonal, one per ring, sitting on tiny
  // background pads so the dashed rings do not strike through the digits
  // (matches the user's hand sketch). Live-motion frames skip the text.
  if (!withKmLabels) return;
  tft.setTextSize(1);
  tft.setTextColor(C_GRID);
  for (int ring = 1; ring <= 3; ring++) {
    int r  = RADAR_TRAFFIC_R * ring / 3 - 6;
    int lx = 120 + (int)(r * 0.707f);
    int ly = 120 - (int)(r * 0.707f) - 4;
    char km[8];
    snprintf(km, sizeof(km), "%d", (int)(RADAR_RANGE_KM * ring / 3));
    int w = (int)strlen(km) * 6 + 2;
    tft.fillRect(lx - 1, ly - 1, w, 9, C_BG);
    tft.setCursor(lx, ly);
    tft.print(km);
  }
}

int radarLabelPriority(const Aircraft &p) {
  // Which aircraft deserve one of the three text labels on the home radar.
  // Priority ladder (user spec): emergency > helicopter > special/notable >
  // MUC-related > plain nearest. Distance breaks ties inside each tier so
  // the label always goes to the closest example of the most interesting
  // category; everything else stays a compact symbol.
  const char *label;
  int tier = 0;
  if (isEmergencySquawk(p))                 tier = 5000;
  else if (isHelicopter(p))                 tier = 4000;
  else if (coolScore(p, &label) >= 70)      tier = 3000;
  else if (trafficLooksMucInbound(p) ||
           trafficLooksMucOutbound(p))      tier = 2000;
  else                                      tier = 1000;
  int prox = 999 - (int)(p.distKm * 10.0f); // closer = higher inside a tier
  if (prox < 0) prox = 0;
  return tier + prox;
}

void selectRadarLabelIndexes(int outIdx[3]) {
  outIdx[0] = outIdx[1] = outIdx[2] = -1;
  int best[3] = {-1, -1, -1};
  for (int i = 0; i < planeCount; i++) {
    Aircraft &p = planes[i];
    if (p.ground || p.distKm > RADAR_RANGE_KM) continue;
    int s = radarLabelPriority(p);
    if (s > best[0]) {
      best[2] = best[1]; outIdx[2] = outIdx[1];
      best[1] = best[0]; outIdx[1] = outIdx[0];
      best[0] = s;       outIdx[0] = i;
    } else if (s > best[1]) {
      best[2] = best[1]; outIdx[2] = outIdx[1];
      best[1] = s;       outIdx[1] = i;
    } else if (s > best[2]) {
      best[2] = s;       outIdx[2] = i;
    }
  }
}

void trafficSummaryCounts(int *dep, int *arr, int *total) {
  *dep = *arr = *total = 0;
  for (int i = 0; i < planeCount; i++) {
    Aircraft &p = planes[i];
    if (p.ground || p.distKm > RADAR_RANGE_KM) continue;
    (*total)++;
    if (trafficLooksMucOutbound(p))      (*dep)++;
    else if (trafficLooksMucInbound(p))  (*arr)++;
  }
}

void radarStatic() {
  tft.fillScreen(C_BG);
  drawBezel();
  radarGrid(true);
  pageDots();
}

bool radarTextShield(int x, int y);

void eraseBlips(bool clearLabels) {
  for (int i = 0; i < prevBlipCount; i++)
    tft.fillCircle(prevBlips[i].x, prevBlips[i].y, prevBlips[i].r, C_BG);
  prevBlipCount = 0;
  if (clearLabels) {
    tft.fillRect(100, 24, 40, 20, C_BG);   // total (white) under the N arrow
    tft.fillRect(18, 108, 40, 20, C_BG);   // DEP counter at the west point
    tft.fillRect(182, 108, 40, 20, C_BG);  // ARR counter at the east point
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
  // Home-page compass instrumentation (rev1.0 layout):
  //   - red north ARROW at the top (real compass style, replaces the "N"),
  //   - total contacts in white directly under the arrow,
  //   - DEP count in red at the west point, ARR count in green at the east
  //     point (where the W/E letters used to sit — the letters are gone).
  // All three numbers are size 2 for visual balance. No OTH counter: the
  // LEGEND page documents the grammar instead of crowding the scope.
  int dep, arr, total;
  trafficSummaryCounts(&dep, &arr, &total);

  // North arrow: small filled red triangle pointing outward/up.
  tft.fillTriangle(120, 6, 113, 20, 127, 20, C_RED);

  char num[8];
  tft.setTextSize(2);
  snprintf(num, sizeof(num), "%d", total);
  tft.setTextColor(C_WHITE);
  tft.setCursor(120 - (int)strlen(num) * 6, 26);
  tft.print(num);

  snprintf(num, sizeof(num), "%d", dep);
  tft.setTextColor(C_RED);
  tft.setCursor(38 - (int)strlen(num) * 6, 112);   // centred on west point
  tft.print(num);

  snprintf(num, sizeof(num), "%d", arr);
  tft.setTextColor(C_GREEN);
  tft.setCursor(202 - (int)strlen(num) * 6, 112);  // centred on east point
  tft.print(num);
}

void radarPaint(bool record, bool drawLabels, bool drawGridLabels) {
  radarGrid(drawGridLabels);
  if (record) prevBlipCount = 0;
  int labelIdx[3];
  selectRadarLabelIndexes(labelIdx);
  int labelCx[3] = {0, 0, 0};
  int labelCy[3] = {0, 0, 0};
  int labelUsed = 0;

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
      trafficSymbol(p, x, y, i == nearestAirborne() ? 7 : 6, c, false);
      // Blip radius 15 covers the marker plus the 13 px heading line while
      // staying inside the wider 98 px scope (98 + 15 = 113 < bezel at 116;
      // the compact bezel is also re-painted after traffic, see below).
      if (record) pushBlip(x, y, 15);

      // Stacked mini-label (callsign / type / altitude) for the nearest few
      // aircraft, matching a classic radar-scope readout. The label sits on
      // whichever side of the marker has room, is clamped to the inner disc,
      // and registers its own erase blip so it can never ghost or smear.
      bool labelThis = (i == labelIdx[0] || i == labelIdx[1] || i == labelIdx[2]);
      if (labelThis && p.altFt > 0) {
        char l3[10];
        snprintf(l3, sizeof(l3), "%dm", (int)(p.altFt * 0.3048f));
        bool placed = false;
        for (int side = 0; side < 2 && !placed; side++) {
          int lx = ((x < 120) == (side == 0)) ? x + 10 : x - 52;
          int ly = y - 13;
          if (ly < 48) ly = 48;
          if (ly > 166) ly = 166;
          int cxl = lx + 21, cyl = ly + 13;   // label-box centre
          bool fits = (cxl - 120) * (cxl - 120) + (cyl - 120) * (cyl - 120) <= 82 * 82 &&
                      !radarTextShield(lx, ly) && !radarTextShield(lx + 42, ly);
          for (int k = 0; k < labelUsed && fits; k++) {
            int dx = cxl - labelCx[k], dy = cyl - labelCy[k];
            if (dx * dx + dy * dy < 42 * 42) fits = false;
          }
          if (!fits) continue;
          printFit(lx, ly,      p.flight, 1, C_WHITE, 42);
          printFit(lx, ly + 10, p.typ,    1, C_CYAN,  42);
          printFit(lx, ly + 20, l3,       1, C_AMBER, 42);
          if (record) pushBlip(cxl, cyl, 30);
          if (labelUsed < 3) {
            labelCx[labelUsed] = cxl;
            labelCy[labelUsed] = cyl;
            labelUsed++;
          }
          placed = true;
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

  // With the scope widened to 98 px, rim squares and blip erases can brush the
  // bezel — but the compact bezel is now just 1 circle + 12 ticks, so simply
  // re-painting it every frame is cheap and keeps the edge ring pixel-perfect.
  drawBezelRing();

  // No compass letters on the home radar (rev1.0): the red north arrow and
  // the DEP/total/ARR numbers occupy the cardinal points instead.
  if (drawLabels) radarLabels();
}

void radarDynamic() { eraseBlips(true); radarPaint(true, true, true); }

bool radarTextShield(int x, int y) {
  // Reserved zones for the compass instrumentation: aircraft markers and
  // their stacked labels are simply not drawn here, so the arrow and the
  // three counters never fight with moving traffic for the same pixels.
  bool northBlock = x >= 100 && x <= 140 && y >= 4   && y <= 46;  // arrow+total
  bool depBlock   = x >= 14  && x <= 62  && y >= 104 && y <= 132; // west number
  bool arrBlock   = x >= 178 && x <= 226 && y >= 104 && y <= 132; // east number
  return northBlock || depBlock || arrBlock;
}

void radarStep() {
  // Calm live motion: advance dead-reckoned traffic roughly once per second.
  // This replaced the rotating sweep beam — see the animation-state comment
  // near the top of the file for why the beam was removed.
  eraseBlips(false);
  radarPaint(true, true, false);
}

// ---------- Airline brand colours (shared by the tracking pages) ----------

// Airline brand colours (user request: colour-code carriers by their dominant
// logo colour — Ryanair blue, DHL yellow, Wizz pink, ...). One flat table keyed
// by ICAO callsign prefix keeps additions one line each; the palette constant
// closest to the real brand colour is used since the UI palette is fixed.
struct AirlineBrand { const char *pfx; uint8_t r, g, b; };
const AirlineBrand airlineBrands[] = {
  {"RYR", 7,  60, 165},   // Ryanair navy blue
  {"DHL", 255, 204, 0},   // DHL yellow
  {"BCS", 255, 204, 0},   // DHL European Air Transport
  {"WZZ", 230, 40, 130},  // Wizz Air pink
  {"DLH", 255, 190, 40},  // Lufthansa crane yellow
  {"CLH", 255, 190, 40},  // Lufthansa CityLine
  {"LHX", 255, 190, 40},  // Lufthansa City Airlines
  {"AUA", 200, 16, 46},   // Austrian red
  {"SWR", 200, 16, 46},   // Swiss red
  {"EWG", 128, 0,  90},   // Eurowings burgundy
  {"CFG", 255, 173, 0},   // Condor yellow
  {"BAW", 23, 66, 133},   // British Airways blue
  {"AFR", 0,  40, 140},   // Air France blue
  {"KLM", 0, 161, 228},   // KLM light blue
  {"EZY", 255, 102, 0},   // easyJet orange
  {"EJU", 255, 102, 0},   // easyJet Europe
  {"UAE", 218, 30, 40},   // Emirates red
  {"QTR", 93, 26, 60},    // Qatar burgundy
  {"THY", 200, 16, 46},   // Turkish red
  {"PGT", 240, 78, 60},   // Pegasus
  {"LOT", 0, 60, 130},    // LOT blue
  {"SAS", 0, 50, 120},    // SAS blue
  {"IBE", 210, 0, 50},    // Iberia red
  {"VLG", 255, 204, 0},   // Vueling yellow
  {"TAP", 0, 120, 90},    // TAP green
  {"FDX", 255, 102, 0},   // FedEx orange
  {"UPS", 100, 60, 20},   // UPS brown
};

uint16_t airlineAccentColor(const Aircraft &n, const char *who) {
  (void)who;   // callsign prefix is the reliable key; the name is decorative
  for (unsigned i = 0; i < sizeof(airlineBrands)/sizeof(airlineBrands[0]); i++)
    if (strncmp(n.flight, airlineBrands[i].pfx, 3) == 0)
      return tft.color565(airlineBrands[i].r, airlineBrands[i].g, airlineBrands[i].b);
  return C_AMBER;   // unknown carrier: neutral accent
}

// ---------- Page 2: TRAFFIC BRIEF (candidate pickers) ----------
bool idxAlreadyUsed(int idx, const int used[], int usedN) {
  for (int i = 0; i < usedN; i++)
    if (used[i] == idx) return true;
  return false;
}

int bestEmergencyNearIdx() {
  int best = -1;
  float bestD = 9999.0f;
  for (int i = 0; i < planeCount; i++) {
    if (!isEmergencySquawk(planes[i])) continue;
    if (planes[i].distKm > RADAR_RANGE_KM + 15.0f) continue;
    if (planes[i].distKm < bestD) { bestD = planes[i].distKm; best = i; }
  }
  return best;
}

int bestHelicopterNearIdx(const int used[], int usedN) {
  int best = -1;
  float bestD = 9999.0f;
  for (int i = 0; i < planeCount; i++) {
    if (idxAlreadyUsed(i, used, usedN)) continue;
    if (!isHelicopter(planes[i]) || planes[i].distKm > RADAR_RANGE_KM) continue;
    if (planes[i].distKm < bestD) { bestD = planes[i].distKm; best = i; }
  }
  return best;
}

int bestCoolNearIdx(const int used[], int usedN) {
  int best = -1, bestScore = 69;
  const char *label;
  for (int i = 0; i < planeCount; i++) {
    if (idxAlreadyUsed(i, used, usedN)) continue;
    if (planes[i].distKm > RADAR_RANGE_KM) continue;
    int score = coolScore(planes[i], &label);
    if (score > bestScore) { bestScore = score; best = i; }
  }
  return best;
}

int bestCoolMucIdx(const int used[], int usedN) {
  int best = -1, bestScore = 49;
  const char *label;
  for (int i = 0; i < planeCount; i++) {
    if (idxAlreadyUsed(i, used, usedN)) continue;
    float dMuc = distanceToMucKm(planes[i]);
    if (dMuc > 18.0f) continue;
    int score = coolScore(planes[i], &label);
    if (trafficLooksMucInbound(planes[i]) || trafficLooksMucOutbound(planes[i]))
      score += 8;
    if (planes[i].ground && dMuc < 6.0f)
      score += 6;
    if (score > bestScore) { bestScore = score; best = i; }
  }
  return best;
}

void specialReasonLine(const Aircraft &p, char *dst, size_t dstN) {
  const char *label;
  int score = coolScore(p, &label);
  float mucD = distanceToMucKm(p);
  float eta = etaMinForDistance(mucD, p);

  if (isEmergencySquawk(p)) {
    snprintf(dst, dstN, "SQK %s %.1fkm %s", p.sqk, (double)p.distKm, compass8(p.bearingDeg));
  } else if (isHelicopter(p)) {
    snprintf(dst, dstN, "%s %.1fkm %s", p.typ[0] ? p.typ : "HELI",
             (double)p.distKm, compass8(p.bearingDeg));
  } else if (p.ground && mucD < 6.0f) {
    snprintf(dst, dstN, "ON GND AT MUC");
  } else if (trafficLooksMucInbound(p) && eta >= 0 && eta < 90) {
    snprintf(dst, dstN, "ARR MUC %.0fM %.0f%s", (double)eta,
             (double)p.bearingDeg, compass8(p.bearingDeg));
  } else if (trafficLooksMucOutbound(p)) {
    snprintf(dst, dstN, "DEP MUC %.1fkm", (double)mucD);
  } else if (score >= 70) {
    fitCopy(dst, dstN, label, 22);
  } else {
    snprintf(dst, dstN, "%s %.1fkm %s", p.typ[0] ? p.typ : "A/C",
             (double)p.distKm, compass8(p.bearingDeg));
  }
}

void drawBriefRow(int y, const char *tag, uint16_t tagColor, int idx, bool withReason) {
  // One compact TRAFFIC BRIEF entry. Two fixed text bands, no icon column —
  // the user asked for the decorative symbols next to names to go; colour on
  // the tag carries the category instead (documented on the LEGEND page).
  printFit(28, y, tag, 1, tagColor, 34);
  if (idx < 0) {
    printFit(66, y, "none nearby", 1, C_DIM, 90);
    return;
  }
  Aircraft &p = planes[idx];
  printFit(66, y, p.flight, 1, C_WHITE, 54);
  printFit(122, y, p.typ[0] ? p.typ : "----", 1, C_CYAN, 30);

  char pos[16];
  snprintf(pos, sizeof(pos), "%.0fkm %s", (double)p.distKm, compass8(p.bearingDeg));
  printFit(156, y, pos, 1, C_DIM, 56);

  // Second band: reason for the special/emergency rows, otherwise the best
  // context we have — route first, airline second, full type name last.
  char detail[34];
  if (withReason) {
    specialReasonLine(p, detail, sizeof(detail));
  } else {
    RouteCache *route = cachedRouteFor(p.flight);
    if (route && route->codes[0])        fitCopy(detail, sizeof(detail), route->codes, 22);
    else if (route && route->airline[0]) fitCopy(detail, sizeof(detail), route->airline, 22);
    else                                 fitCopy(detail, sizeof(detail), typeName(p.typ), 22);
  }
  printFit(66, y + 11, detail, 1, C_DIM, 140);
}

void summaryStatic() {
  // Bezel-less by design: this is a compact traffic brief, not a map.
  tft.fillScreen(C_BG);
  pageDots();
}

void summaryDynamic() {
  // TRAFFIC BRIEF: the five things a Munich plane-watcher actually asks —
  // what's closest, what's special, any helicopters, any emergencies, and
  // what MUC is doing next. Fixed row slots with graceful fallbacks; the
  // legend moved to its own page so this one stays dense but readable.
  tft.fillScreen(C_BG);
  centerText("TRAFFIC BRIEF", 16, 1, C_AMBER);
  tft.drawFastHLine(82, 27, 76, C_GRIDDIM);

  int used[4] = {-1, -1, -1, -1};
  int usedN = 0;

  int nearIdx = nearestAirborne();
  drawBriefRow(36, "NEAR", C_WHITE, nearIdx, false);
  if (nearIdx >= 0) used[usedN++] = nearIdx;

  int coolIdx = bestCoolNearIdx(used, usedN);
  if (coolIdx < 0) coolIdx = bestCoolMucIdx(used, usedN);
  drawBriefRow(68, "COOL", C_AMBER, coolIdx, true);
  if (coolIdx >= 0) used[usedN++] = coolIdx;

  int heliIdx = bestHelicopterNearIdx(used, usedN);
  drawBriefRow(100, "HELI", C_CYAN, heliIdx, false);
  if (heliIdx >= 0) used[usedN++] = heliIdx;

  int emgIdx = bestEmergencyNearIdx();
  drawBriefRow(132, "EMG", C_RED, emgIdx, true);

  // Fifth slot: MUC operations, one arrival line + one departure line.
  tft.drawFastHLine(28, 160, 184, C_GRIDDIM);
  int arrIdx = nextArrivalIdx();
  int depIdx = nextDepartureIdx();

  printFit(28, 168, "ARR", 1, C_GREEN, 24);
  if (arrIdx >= 0) {
    Aircraft &a = planes[arrIdx];
    float eta = etaMinForDistance(distanceToMucKm(a), a);
    char line[30];
    if (eta >= 0) snprintf(line, sizeof(line), "%s in %.0fm", a.flight, (double)eta);
    else          snprintf(line, sizeof(line), "%s %.0fkm out", a.flight, (double)distanceToMucKm(a));
    printFit(56, 168, line, 1, C_WHITE, 96);
    RouteCache *r = cachedRouteFor(a.flight);
    if (r && r->codes[0]) printFit(156, 168, r->codes, 1, C_DIM, 56);
  } else {
    printFit(56, 168, "quiet", 1, C_DIM, 60);
  }

  printFit(28, 182, "DEP", 1, C_RED, 24);
  if (depIdx >= 0) {
    Aircraft &d = planes[depIdx];
    char line[30];
    if (d.ground) snprintf(line, sizeof(line), "%s on field", d.flight);
    else          snprintf(line, sizeof(line), "%s %.0fkm", d.flight, (double)distanceToMucKm(d));
    printFit(56, 182, line, 1, C_WHITE, 96);
    RouteCache *r = cachedRouteFor(d.flight);
    if (r && r->codes[0]) printFit(156, 182, r->codes, 1, C_DIM, 56);
  } else {
    printFit(56, 182, "quiet", 1, C_DIM, 60);
  }
}

// ---------- Pages 3+4: NEAREST / COOLEST tracking (shared renderer) ----------
void trackStatic() {
  tft.fillScreen(C_BG);
  drawBezelChrome(false);
  pageDots();
}

void trackPageDraw(int idx, const char *headerTag, uint16_t tagColor, Trail &trail) {
  // Shared renderer for the two tracking pages. rev1.0 layout fix: the old
  // full-screen rings (r=43/86) were mostly hidden behind the header/footer
  // text bands, which read as "the grid vanishes at the top and bottom". The
  // mini-map now lives in its own vertical band (y 76..180) with rings that
  // fit entirely inside it, and the text gets clean dedicated rows above and
  // below — nothing overlaps, nothing gets half-erased.
  clearInnerChrome(false);
  if (idx < 0) {
    centerText(headerTag, 96, 1, C_DIM);
    centerText("no contacts", 116, 2, C_DIM);
    return;
  }
  Aircraft &n = planes[idx];
  RouteCache *route = cachedRouteFor(n.flight);

  // --- header band ---
  centerText(headerTag, 16, 1, tagColor);
  centerText(n.flight, 28, 2, C_AMBER);
  char who[34];
  if (route && route->airline[0])
    snprintf(who, sizeof(who), "%.14s  %.14s", typeName(n.typ), route->airline);
  else
    fitCopy(who, sizeof(who), typeName(n.typ), 26);
  // Airline brand colour when the carrier is known (Lufthansa yellow,
  // Ryanair navy, ...) — the small visual identity the user asked for.
  centerText(who, 48, 1,
             (route && route->airline[0]) ? airlineAccentColor(n, route->airline) : C_DIM);
  if (route && route->codes[0]) centerText(route->codes, 60, 1, C_CYAN);

  // --- mini map: home centred, aircraft + path around it ---
  const int cx = 120, cy = 128, mapR = 52;
  float range = n.distKm * 1.5f;
  if (range < TRACK_RANGE_MIN_KM) range = TRACK_RANGE_MIN_KM;
  float scale = (float)mapR / range;

  tft.drawCircle(cx, cy, mapR / 2, C_GRIDDIM);
  tft.drawCircle(cx, cy, mapR,     C_GRIDDIM);
  tft.drawLine(cx - mapR - 6, cy, cx + mapR + 6, cy, C_GRIDDIM);
  tft.drawLine(cx, cy - mapR - 6, cx, cy + mapR + 6, C_GRIDDIM);
  tft.setTextSize(1);
  tft.setTextColor(C_GRID);
  tft.setCursor(cx + mapR + 6, cy - 10);
  tft.printf("%.0f", (double)range);
  tft.setCursor(cx + mapR + 6, cy + 2);
  tft.print("km");

  // Munich Airport marker whenever it is inside the view: two runway strokes
  // at the real 08/26 heading, so arrivals visually connect to the airport.
  int mx = cx + (int)(mucE * scale);
  int my = cy - (int)(mucN * scale);
  if ((mx - cx) * (mx - cx) + (my - cy) * (my - cy) <= (mapR - 4) * (mapR - 4)) {
    int ue = (int)(sinf(deg2rad(RWY_HDG)) * 7.0f);
    int un = (int)(cosf(deg2rad(RWY_HDG)) * 7.0f);
    tft.drawLine(mx - ue, my + un - 2, mx + ue, my - un - 2, C_AMBER);
    tft.drawLine(mx - ue, my + un + 2, mx + ue, my - un + 2, C_AMBER);
    tft.setTextColor(C_AMBER);
    tft.setCursor(mx - 8, my + 6);
    tft.print("MUC");
  }

  tft.fillCircle(cx, cy, 3, C_WHITE);   // you

  // Flown path: fading polyline from the trail history buffer.
  int prevX = 0, prevY = 0;
  bool havePrev = false;
  for (int i = 0; i < trail.len; i++) {
    int x = cx + (int)(trail.e[i] * scale);
    int y = cy - (int)(trail.n[i] * scale);
    bool inView = (x - cx) * (x - cx) + (y - cy) * (y - cy) <= (mapR + 2) * (mapR + 2);
    if (!inView) { havePrev = false; continue; }
    uint16_t c = (i > trail.len - 4) ? C_BRIGHT : (i > trail.len - 10 ? C_SWEEP : C_GRIDDIM);
    if (havePrev) tft.drawLine(prevX, prevY, x, y, c);
    tft.fillCircle(x, y, (i == trail.len - 1) ? 2 : 1, c);
    prevX = x; prevY = y; havePrev = true;
  }

  // Aircraft + projected course (ahead bright, behind dim), clipped to map.
  int px = cx + (int)(n.eastKm * scale);
  int py = cy - (int)(n.northKm * scale);
  if ((px - cx) * (px - cx) + (py - cy) * (py - cy) <= mapR * mapR) {
    float t = deg2rad(n.trackDeg), s = sinf(t), c = cosf(t);
    for (int d = 0; d < mapR; d += 8) {
      int xa = px + (int)(s * d),       ya = py - (int)(c * d);
      int xb = px + (int)(s * (d + 4)), yb = py - (int)(c * (d + 4));
      if ((xa - cx) * (xa - cx) + (ya - cy) * (ya - cy) > mapR * mapR) break;
      tft.drawLine(xa, ya, xb, yb, C_GRID);
    }
    trafficSymbol(n, px, py, 7, trafficColor(n, false), false);
  } else {
    // Aircraft outside the mini-map: edge-clamped blue square (global rule).
    float b = deg2rad(n.bearingDeg);
    int ex = cx + (int)(sinf(b) * (mapR + 4));
    int ey = cy - (int)(cosf(b) * (mapR + 4));
    tft.fillRect(ex - 2, ey - 2, 5, 5, C_BLUE);
  }

  // --- footer band: bearing / altitude / speed, then closest approach ---
  char live[40];
  snprintf(live, sizeof(live), "BRG %.0f%s  %dm  %dkm/h",
           (double)n.bearingDeg, compass8(n.bearingDeg),
           (int)(n.altFt * 0.3048f), (int)(n.gsKt * 1.852f));
  centerText(live, 188, 1, C_WHITE);

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
      // CPA = closest point of approach to YOU (km), time as minutes+seconds.
      snprintf(cpa, sizeof(cpa), "CPA %.1fkm in %dm%02ds",
               (double)cd, (int)tSec / 60, (int)tSec % 60);
    } else {
      snprintf(cpa, sizeof(cpa), "away now %.1fkm", (double)n.distKm);
    }
  } else {
    snprintf(cpa, sizeof(cpa), "now %.1fkm from you", (double)n.distKm);
  }
  centerText(cpa, 202, 1, C_CYAN);
}

void trackDynamic() {
  trackPageDraw(nearestAirborne(), "NEAREST", C_DIM, trailNear);
}

void coolestStatic() {
  tft.fillScreen(C_BG);
  drawBezelChrome(false);
  pageDots();
}

void coolestDynamic() {
  // COOLEST tracking page: same live map as NEAREST, but following the
  // top-scored aircraft, with a compact "why it is cool + MUC status" tag
  // in the header slot (colour signals urgency: red = emergency/very rare).
  int ci = coolestIdx();
  if (ci < 0) {
    trackPageDraw(-1, "COOLEST", C_AMBER, trailCool);
    return;
  }
  Aircraft &p = planes[ci];
  const char *label;
  int score = coolScore(p, &label);
  uint16_t tagColor = score >= 85 ? C_RED : (score >= 50 ? C_AMBER : C_DIM);

  char why[34];
  float mucD = distanceToMucKm(p);
  float eta = etaMinForDistance(mucD, p);
  if (p.ground && mucD < 6.0f)
    snprintf(why, sizeof(why), "%.20s - AT MUC", label);
  else if (likelyArrival(p) && eta >= 0 && eta < 90)
    snprintf(why, sizeof(why), "%.16s - MUC %dM", label, (int)(eta + 0.5f));
  else if (likelyDeparture(p))
    snprintf(why, sizeof(why), "%.16s - LEAVING", label);
  else
    fitCopy(why, sizeof(why), label, 26);
  trackPageDraw(ci, why, tagColor, trailCool);
}

// ---------- Page 4: MUC AIRPORT ----------
#define MUC_SCALE MUC_MAP_SCALE // px per km (configurable; rev1.0 default 16
                                // zooms out so approach/departure flows fit)
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
  float px = len > 0 ? -dy / len * 5.0f : 0;
  float py = len > 0 ?  dx / len * 5.0f : 5.0f;
  uint16_t runwayFill = tft.color565(34, 38, 46);

  // Fill the runway as a dark, precise rectangular slab with light edges. The
  // earlier pale runway fill looked chunky in photos; a solid dark runway reads
  // more like an airport diagram and leaves coloured traffic symbols readable.
  // Solid rectangular slab with light edges and NO centreline (user note:
  // "fill the black lines and make it more rectangular" — the dim centreline
  // read as an unfilled gap in the slab on the real panel).
  tft.fillTriangle(x0 + px, y0 + py, x1 + px, y1 + py, x1 - px, y1 - py, runwayFill);
  tft.fillTriangle(x0 + px, y0 + py, x1 - px, y1 - py, x0 - px, y0 - py, runwayFill);
  tft.drawLine(x0 + px, y0 + py, x1 + px, y1 + py, C_GREY);
  tft.drawLine(x0 - px, y0 - py, x1 - px, y1 - py, C_GREY);
  tft.drawLine(x0 + px, y0 + py, x0 - px, y0 - py, C_GREY);
  tft.drawLine(x1 + px, y1 + py, x1 - px, y1 - py, C_GREY);
}

void runwayEndScreen(float ce, float cn, int dir, int *x, int *y) {
  // Screen position of one runway end (dir -1 = west/08 end, +1 = east/26 end).
  float u_e = sinf(deg2rad(RWY_HDG)), u_n = cosf(deg2rad(RWY_HDG));
  mucToScreen(mucE + ce + dir * u_e * RWY_LEN / 2,
              mucN + cn + dir * u_n * RWY_LEN / 2, x, y);
}

void runwayTag(int x, int y, const char *label) {
  // Runway designator on a small pad, anchored NEXT TO a runway end instead of
  // centred on top of the slab (user note: "runway names in a nicer place").
  int w = (int)strlen(label) * 6 + 4;
  tft.fillRect(x - 2, y - 1, w, 10, C_BG);
  tft.setTextSize(1);
  tft.setTextColor(C_GREY);
  tft.setCursor(x, y);
  tft.print(label);
}

bool onMucRunway(float e, float n) {
  // Very small surface heuristic: project a live aircraft position onto each
  // runway centreline. If it is within the runway length and only a few hundred
  // metres off the centreline, draw it as "on runway" in yellow. This is not a
  // certified surface-movement model; it is a readable spotting cue.
  float ue = sinf(deg2rad(RWY_HDG)), un = cosf(deg2rad(RWY_HDG));
  const float centres[2][2] = {
    {-RWY_STAGGER, +RWY_SEP},
    {+RWY_STAGGER, -RWY_SEP}
  };
  for (int i = 0; i < 2; i++) {
    float de = e - (mucE + centres[i][0]);
    float dn = n - (mucN + centres[i][1]);
    float along = de * ue + dn * un;
    float cross = -de * un + dn * ue;
    if (fabsf(along) <= RWY_LEN / 2 + 0.25f && fabsf(cross) <= 0.28f)
      return true;
  }
  return false;
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
  drawBezelChrome(false);
  pageDots();
}

void airportDynamic() {
  clearInnerChrome(false);

  // rev1.0 header: the airport-name title is gone. Line 1 is a colour-coded
  // "how busy is MUC right now" counter strip (arrivals inbound, departures
  // out, aircraft on the ground) matching the home-page counter style, and
  // line 2 is live weather context (temperature + wind) from the cached
  // METAR — the two facts a spotter wants before reading the map.
  int arrCnt = 0, depCnt = 0, gndCnt = 0;
  for (int i = 0; i < planeCount; i++) {
    float dM = distanceToMucKm(planes[i]);
    if (planes[i].ground && dM < 6.0f) gndCnt++;
    else if (likelyArrival(planes[i])) arrCnt++;
    else if (likelyDeparture(planes[i])) depCnt++;
  }
  char chip[12];
  tft.setTextSize(1);
  snprintf(chip, sizeof(chip), "ARR %d", arrCnt);
  printFit(58, 26, chip, 1, C_GREEN, 40);
  snprintf(chip, sizeof(chip), "DEP %d", depCnt);
  printFit(102, 26, chip, 1, C_RED, 40);
  snprintf(chip, sizeof(chip), "GND %d", gndCnt);
  printFit(146, 26, chip, 1, C_PURPLE, 40);
  if (mucWx.ok) {
    char wxLine[24];
    snprintf(wxLine, sizeof(wxLine), "%s  %s", mucWx.temp, mucWx.wind);
    centerText(wxLine, 38, 1, C_DIM);
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
  drawRunway(+RWY_STAGGER, -RWY_SEP);
  int ex, ey;
  runwayEndScreen(-RWY_STAGGER, +RWY_SEP, -1, &ex, &ey);   // west end, north rwy
  runwayTag(ex - 4, ey - 18, "08L/26R");
  runwayEndScreen(+RWY_STAGGER, -RWY_SEP, +1, &ex, &ey);   // east end, south rwy
  runwayTag(ex - 38, ey + 12, "08R/26L");

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
    if (dMuc > 26) continue;   // wider catch radius for the zoomed-out map

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
    //   green symbol  = arriving traffic
    //   red symbol    = departing traffic
    //   star/rotor    = notable aircraft / helicopter, regardless of flow
    //   high overflights are intentionally not drawn — they belong to the
    //   radar page and only cluttered the runway view.
    bool onRwy = onMucRunway(liveE, liveN) && (p.ground || p.altFt < 900);
    if (clampedToEdge) {
      tft.fillRect(x - 2, y - 2, 5, 5, C_BLUE);
    } else if (onRwy) {
      // Aircraft actually on the runway: yellow marker, sized by wake class
      // (a departing A380 visibly dwarfs a CRJ) — per the user's sketch note.
      trafficSymbol(p, x, y, isHeavyType(p) ? 11 : 8, C_AMBER, true);
    } else if (p.ground) {
      tft.fillCircle(x, y, 3, C_PURPLE);
    } else if (low) {
      uint16_t c = trafficColor(p, false);
      if (i == nextArr || likelyArrival(p)) c = C_GREEN;
      if (i == nextDep || likelyDeparture(p)) c = C_RED;
      bool nextUp = (i == nextArr || i == nextDep);
      // Short projected path line (dead-reckoned direction of travel), then
      // the symbol on top — subtle, but it makes flows readable at a glance.
      int hx = x + (int)(sinf(deg2rad(p.trackDeg)) * 11);
      int hy = y - (int)(cosf(deg2rad(p.trackDeg)) * 11);
      tft.drawLine(x, y, hx, hy, c);
      trafficSymbol(p, x, y, nextUp ? 8 : 6, c, false);
    }
  }

  // Bottom rows: the next arrival and next departure, so the map can be
  // compared against FlightRadar24 directly. Kept chord-safe (short) so the
  // circular clear always covers them and nothing ghosts on live updates.
  if (nextArr >= 0) {
    Aircraft &a = planes[nextArr];
    float eta = etaMinForDistance(distanceToMucKm(a), a);
    char line[26];
    RouteCache *r = cachedRouteFor(a.flight);
    if (eta >= 0) snprintf(line, sizeof(line), "A %s %.0fm %s", a.flight, (double)eta,
                           (r && r->codes[0]) ? r->codes : "");
    else          snprintf(line, sizeof(line), "A %s %.0fkm", a.flight, (double)distanceToMucKm(a));
    centerText(line, 196, 1, C_GREEN);
  }
  if (nextDep >= 0) {
    Aircraft &d = planes[nextDep];
    char line[22];
    snprintf(line, sizeof(line), "D %s %s", d.flight,
             d.ground ? "GND" : "AIR");
    centerText(line, 208, 1, C_RED);
  }
}

// The MUC OPS board page was removed in rev1.0: its next-ARR/next-DEP data
// now lives directly on the MUC MAP page (bottom rows) and on the TRAFFIC
// BRIEF page, so a whole extra page of the same information was redundant.

// ---------- Page 6: MUC WEATHER ----------
void dataRow(int y, const char *label, const char *value, uint16_t valueColor) {
  // Shared airport-data row. Labels and values have hard pixel boxes because
  // external APIs can surprise us with long strings; clipping is better than
  // letting one value push through the next row on the round display.
  const char *icon = ".";
  uint16_t iconColor = C_GRID;
  if (strcmp(label, "WIND") == 0)       { icon = "~"; iconColor = C_CYAN; }
  else if (strcmp(label, "VIS") == 0)   { icon = "o"; iconColor = C_GREEN; }
  else if (strcmp(label, "CLOUD") == 0) { icon = "="; iconColor = C_DIM; }
  else if (strcmp(label, "TEMP") == 0)  { icon = "T"; iconColor = C_AMBER; }
  else if (strcmp(label, "QNH") == 0)   { icon = "Q"; iconColor = C_BLUE; }
  else if (strcmp(label, "WX") == 0)    { icon = "*"; iconColor = C_PURPLE; }
  else if (strcmp(label, "RWY") == 0)   { icon = ">"; iconColor = C_WHITE; }

  tft.fillCircle(45, y + 4, 5, iconColor);
  tft.setTextSize(1);
  tft.setTextColor(C_BG);
  tft.setCursor(42, y + 1);
  tft.print(icon);
  printFit(58, y, label, 1, C_GRID, 46);
  printFit(108, y, value, 1, valueColor, 82);
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
  // Plain "MUC" header: the long "MUC WEATHER" title clipped on the round
  // glass (photos showed "MUC WEAT"); a short centred code reads cleaner
  // and frees a row so the data block sits visually centred on the panel.
  tft.fillScreen(C_BG);
  centerText("MUC", 20, 2, C_AMBER);
  tft.drawFastHLine(96, 40, 48, C_GRIDDIM);
  bool ok = fetchMucWeather();
  if (!ok) {
    centerText("no METAR yet", 104, 2, C_DIM);
    centerText("retrying on next visit", 128, 1, C_GRID);
    return;
  }

  // Full aviation snapshot: wind, visibility, lowest cloud layer, temperature,
  // pressure, significant weather, and the estimated runway in use. Rows are
  // evenly spaced with icon dots; values colour up only when operationally
  // interesting (wet weather amber, runway-in-use cyan).
  dataRow(54,  "WIND",  mucWx.wind,  C_WHITE);
  dataRow(71,  "VIS",   mucWx.vis,   C_WHITE);
  dataRow(88,  "CLOUD", mucWx.cloud, C_WHITE);
  dataRow(105, "TEMP",  mucWx.temp,  C_WHITE);
  dataRow(122, "QNH",   mucWx.qnh,   C_WHITE);
  dataRow(139, "WX",    mucWx.wx,
          strcmp(mucWx.wx, "DRY") == 0 ? C_WHITE : C_AMBER);

  char rwy[10];
  activeRunway(rwy, sizeof(rwy));
  dataRow(156, "RWY", rwy[0] ? rwy : "--", rwy[0] ? C_CYAN : C_DIM);

  // Field status as a proper pill (see flightCategory for why this is
  // METAR-derived rather than a fake delay percentage).
  uint16_t catColor;
  const char *cat = flightCategory(&catColor);
  tft.fillRoundRect(58, 172, 124, 17, 8, tft.color565(18, 24, 38));
  tft.drawRoundRect(58, 172, 124, 17, 8, catColor);
  tft.fillCircle(70, 180, 4, catColor);
  centerText(cat, 177, 1, catColor);

  // Raw METAR tail: the unfiltered truth for anyone who reads METAR, clipped
  // into two fixed rows so odd station remarks can never break the layout.
  char raw1[26], raw2[26];
  fitCopy(raw1, sizeof(raw1), mucWx.raw, 22);
  fitCopy(raw2, sizeof(raw2), mucWx.raw + strlen(raw1), 22);
  printFit(66, 196, raw1, 1, C_DIM, 108);
  printFit(66, 208, raw2, 1, C_DIM, 108);
}

// ---------- Page 7: LEGEND ----------
void legendRow(int y, const char *text) {
  printFit(84, y - 4, text, 1, C_WHITE, 120);
}

void legendStatic() {
  tft.fillScreen(C_BG);
  pageDots();
}

void legendDynamic() {
  // Dedicated legend page (rev1.0): every colour and symbol used on the
  // radar/tracking/airport pages, drawn with the REAL rendering functions so
  // the legend can never drift out of sync with the actual UI. Having this
  // page is what lets all the other pages stay free of on-screen captions.
  tft.fillScreen(C_BG);
  centerText("LEGEND", 16, 1, C_AMBER);
  tft.drawFastHLine(96, 27, 48, C_GRIDDIM);

  planeTriangle(64, 40, 45, 6, C_RED);
  legendRow(44, "departing MUC");
  planeTriangle(64, 62, 45, 6, C_GREEN);
  legendRow(66, "arriving MUC");
  planeTriangle(64, 84, 45, 6, C_AMBER);
  legendRow(88, "other traffic");
  tft.fillRect(61, 103, 6, 6, C_BLUE);
  legendRow(110, "outside range");
  starSymbol(64, 128, 5, C_AMBER);
  legendRow(132, "special aircraft");
  helicopterSymbol(64, 150, 90, 5, C_CYAN);
  legendRow(154, "helicopter");
  tft.fillCircle(64, 172, 4, C_PURPLE);
  legendRow(176, "on ground");
  emergencySymbol(64, 194, C_RED);
  legendRow(198, "emergency squawk");
}

// ---------- Page management ----------
bool skipPage(uint8_t p) {
  if ((p == PAGE_NEAREST || p == PAGE_COOLEST) && nearestAirborne() < 0) return true;
  if (p == PAGE_SUMMARY && planeCount == 0) return true;
  return false;   // LEGEND and WEATHER are always meaningful
}

void drawPageFull() {
  prevBlipCount = 0;
  switch (page) {
    case PAGE_RADAR:    radarStatic();      radarDynamic();      break;
    case PAGE_SUMMARY:  summaryStatic();    summaryDynamic();    break;
    case PAGE_NEAREST:  trackStatic();      trackDynamic();      break;
    case PAGE_COOLEST:  coolestStatic();    coolestDynamic();    break;
    case PAGE_MUC_MAP:  airportStatic();    airportDynamic();    break;
    case PAGE_MUC_WX:   mucWeatherStatic(); mucWeatherDynamic(); break;
    case PAGE_LEGEND:   legendStatic();     legendDynamic();     break;
  }
}

void drawPageUpdate() {
  switch (page) {
    case PAGE_RADAR:    radarDynamic();      break;
    case PAGE_SUMMARY:  summaryDynamic();    break;
    case PAGE_NEAREST:  trackDynamic();      break;
    case PAGE_COOLEST:  coolestDynamic();    break;
    case PAGE_MUC_MAP:  airportDynamic();    break;
    case PAGE_MUC_WX:   mucWeatherDynamic(); break;
    case PAGE_LEGEND:   /* static content */ break;
  }
}

bool pageButtonDownNow() {
  // Fast raw read used before starting network work. The normal debounced state
  // machine still owns page changes; this helper only keeps the firmware from
  // entering a blocking HTTP request while the user is actively pressing MODE.
  return digitalRead(PAGE_BUTTON_PIN) == LOW;
}

void prefetchRoutes() {
  // Route lookups are the only "optional" network calls; they always yield
  // to a pending button press so the UI never feels stuck behind HTTP.
  if (pageButtonDownNow()) return;
  int na = nearestAirborne();
  if ((page == PAGE_RADAR || page == PAGE_NEAREST || page == PAGE_SUMMARY) && na >= 0)
    fetchRoute(planes[na].flight, ROUTE_SLOT_NEAREST);
  if (page == PAGE_MUC_MAP || page == PAGE_SUMMARY) {
    // The map + brief pages both show the next arrival/departure rows.
    if (pageButtonDownNow()) return;
    int a = nextArrivalIdx();
    if (a >= 0) fetchRoute(planes[a].flight, ROUTE_SLOT_MUC_BASE + 0);
    if (pageButtonDownNow()) return;
    int d = nextDepartureIdx();
    if (d >= 0) fetchRoute(planes[d].flight, ROUTE_SLOT_MUC_BASE + 1);
  }
  if (page == PAGE_COOLEST || page == PAGE_SUMMARY) {
    if (pageButtonDownNow()) return;
    int ci = coolestIdx();
    if (ci >= 0) fetchRoute(planes[ci].flight, ROUTE_SLOT_SPOTTER);
  }
}

void advancePage(bool manual) {
  // Central page-change path. Keeping auto-cycle and button-cycle in one helper
  // prevents subtle differences like one path skipping empty pages while the
  // other lands on them. Manual presses redraw cached data immediately; the
  // automatic carousel can spend time prefetching route labels before drawing.
  uint8_t start = page;
  do {
    page = (page + 1) % PAGE_COUNT;
  } while (skipPage(page) && page != start);

  manualPageHold = manual ? true : manualPageHold;
  lastPageSwitch = millis();
  if (!manual) prefetchRoutes();
  drawPageFull();
}

void resumeAutoPages() {
  // Long-press escape hatch. A short press means "stay on the page I chose";
  // a long press means "go back to the normal rotating dashboard".
  manualPageHold = false;
  lastPageSwitch = millis();
  Serial.println("Manual page hold OFF; auto carousel resumed.");
}

void IRAM_ATTR pageButtonIsr() {
  // Classifies presses entirely inside the interrupt so nothing is lost
  // during blocking HTTP calls: falling edge stamps the press, rising edge
  // measures the hold time and latches exactly one event for loop().
  uint32_t now = millis();
  if (digitalRead(PAGE_BUTTON_PIN) == LOW) {
    btnDownAtMs = now;                       // press started (or bounced)
  } else if (btnDownAtMs != 0) {
    uint32_t held = now - btnDownAtMs;
    btnDownAtMs = 0;                         // consume so bounces can't refire
    if (held >= PAGE_BUTTON_LONG_MS)      btnEvent = 2;
    else if (held >= PAGE_BUTTON_MIN_MS)  btnEvent = 1;
  }
}

void handlePageButton(uint32_t now) {
  (void)now;
  if (!PAGE_BUTTON_ENABLED) return;

  // Consume events latched by the ISR. Exactly one event per physical press:
  // the ISR zeroes its timestamp when it classifies a release, so switch
  // bounce cannot double-fire, and no debounce polling window can miss a
  // press that happened during a blocking HTTP call.
  if (btnEvent == 1) {
    btnEvent = 0;
    Serial.println("Button: next page (manual hold).");
    advancePage(true);
  } else if (btnEvent == 2) {
    btnEvent = 0;
    Serial.println("Button: long press, resume auto carousel.");
    resumeAutoPages();
    drawPageFull();
  }
}

// ---------- Main ----------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Plane Radar rev1.0 booting...");
  pinMode(PAGE_BUTTON_PIN, INPUT_PULLUP);
  if (PAGE_BUTTON_ENABLED)
    attachInterrupt(digitalPinToInterrupt(PAGE_BUTTON_PIN), pageButtonIsr, CHANGE);
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
  handlePageButton(now);

  bool fetchDue = (now - lastFetch >= FETCH_INTERVAL_MS || lastFetch == 0);
  bool buttonBusy = pageButtonDownNow() || btnEvent != 0;
  if (fetchDue && !buttonBusy) {
    bool first = (lastFetch == 0);
    lastFetch = now;
    if (fetchPlanes()) {
      failCount = 0;
      handlePageButton(millis());
      if (!manualPageHold && !pageButtonDownNow()) prefetchRoutes();

      // Dynamic special-traffic interrupt: an emergency squawk or very rare
      // airframe (score >= ALERT_SCORE) yanks the carousel to SPECIAL so the
      // event is visible immediately — an A380 on final or a 7700 squawk should
      // not wait behind the weather page. The callsign latch makes this
      // one-shot per aircraft, so the same jet cannot hold the screen hostage.
      int alertIdx = coolestIdx();
      const char *alertLabel;
      if (!manualPageHold && alertIdx >= 0 && page != PAGE_COOLEST &&
          coolScore(planes[alertIdx], &alertLabel) >= ALERT_SCORE &&
          strcmp(planes[alertIdx].flight, lastAlertCs) != 0) {
        copyStr(lastAlertCs, sizeof(lastAlertCs), planes[alertIdx].flight);
        Serial.printf("ALERT: %s -> coolest page (%s)\n",
                      planes[alertIdx].flight, alertLabel);
        page = PAGE_COOLEST;
        lastPageSwitch = now;
        drawPageFull();
      } else if (first) {
        drawPageFull();
      } else {
        // rev1.0: EVERY page repaints from the fresh cache after a fetch, so
        // no page goes stale while the user stays on it. Fetch cadence
        // (network) and this redraw cadence are deliberately the same event —
        // redrawing more often than data changes would only add flicker.
        drawPageUpdate();
      }
    } else if (++failCount >= 3) {
      splash("API error", "retrying...", C_ORANGE);
    }
  }

  if (page == PAGE_RADAR && now - lastRadarStep >= RADAR_STEP_MS) {
    lastRadarStep = now;
    radarStep();
  }

  if (page == PAGE_MUC_MAP && now - lastLiveStep >= AIRPORT_STEP_MS) {
    lastLiveStep = now;
    airportDynamic();
  }

  if (AUTO_SCROLL_ENABLED && !manualPageHold &&
      now - lastPageSwitch >= pageDur[page]) {
    advancePage(false);
  }

  delay(40);
}
