/*
  ============================================================================
  plane_radar_v1.ino - rev1.2.9 (rate-limit backoff + quieter rim + WX tweak)
  MUC Desk Radar — a Munich aviation desk instrument
  by Saurav, built iteratively with Claude
  ============================================================================

  WHAT THIS IS
  ------------
  A standalone desk device that watches the sky around your home and Munich
  Airport (MUC/EDDM) on a 1.28" round display: live aircraft positions,
  airport arrivals/departures, a traffic brief, per-aircraft tracking cards,
  and decoded METAR weather. No app, no server — the ESP32 talks directly to
  free public APIs over Wi-Fi.

  HARDWARE
  --------
  * ESP32-C3 Super Mini
  * GC9A01 1.28" round TFT, 240x240, hardware SPI
      VCC->3V3 (never 5V!), GND->GND, SCL->GPIO4, SDA->GPIO3,
      DC->GPIO10, CS->GPIO1, RST->GPIO0
  * Page button: onboard BOOT button (GPIO9, default) or external to GND
  * Full wiring diagrams: docs/display_wiring_guide.html, docs/device_manual.html

  DATA SOURCES (all free, no API key)
  -----------------------------------
  * api.adsb.lol/v2/point/...   live ADS-B aircraft positions (primary feed,
                                fetched every FETCH_INTERVAL_MS, radius
                                ADSB_FETCH_RADIUS_KM around HOME_LAT/LON)
  * api.adsbdb.com/v0/callsign  route (origin > destination) + airline name
                                for selected callsigns, small 4-slot cache
  * aviationweather.gov         EDDM METAR, decoded into the WX page and the
                                MUC map header (temp + wind)

  SETUP (gift-ready)
  ------------------
  Copy config.example.h to config.h, enter Wi-Fi name+password and your
  lat/lon. Everything else has sensible defaults in the CONFIG DEFAULTS
  block below; config.h is gitignored so credentials never reach GitHub.

  THE SIX PAGES (short press = next page + hold, long press = resume auto)
  ------------------------------------------------------------------------
  1 HOME RADAR   North-up scope centred on YOU. Range rings tour 20/40/60 km
                 (weighted by traffic density). Compass counters: white total
                 under the red north arrow, red DEP count west, green ARR
                 count east. South: dim-green scope range + south pointer.
  2 MUC MAP      Airport operations map. Runway schematic at 22 px/km with
                 real 08L/26R + 08R/26L geometry; airborne traffic mapped at
                 a fixed 20 km view scale (blue rim markers out to 40 km);
                 aircraft ON the field drawn at schematic scale so ground
                 traffic spreads realistically. Header: MUC / temp+wind /
                 DEP GND ARR counters. Labels on next arrival + departure:
                 callsign + "fr XXX"/"to XXX", then ARR eta / DEP distance.
  3 TRFC         Traffic brief: NEAR (closest airborne), COOL (best-scored
                 special), HELI (with operator role), EMG (emergency squawk),
                 plus a 100 km "around you" activity count. Emergencies jump
                 the carousel to this page automatically.
  4 NEAREST      Data card for the closest airborne aircraft: callsign,
                 type + airline (brand colour), route codes + full city
                 names, Munich arrival/departure note, then live ALT/SPD/DST,
                 climb trend (mpm) and CPA ("passes 2.1 km from you in
                 1m36s"). A small red rim blip points along the bearing from
                 your position — face it and look up. Ticks every second.
  5 COOLEST      Same card for the highest-scored aircraft (rare airframes,
                 military, emergencies...) with a why-line. Score >= 95
                 (ALERT_SCORE) interrupts the carousel to show it.
  6 MUC WX       Decoded EDDM METAR: wind/vis/cloud/temp/QNH/weather rows,
                 active runway estimate, flight-category status pill
                 (VFR green .. LIFR red), raw METAR tail.

  COLOUR + SYMBOL GRAMMAR (consistent across all pages)
  -----------------------------------------------------
  * GREEN triangle .... arriving at MUC          (points along its heading)
  * RED triangle ...... departing MUC
  * AMBER triangle .... other traffic
  * YELLOW on slab .... aircraft physically on a runway (bigger = heavy)
  * PURPLE dot ........ on the ground / taxiing
  * DARK-BLUE rim dot ... aircraft beyond the drawn scope, direction only
                        (deliberately faint: context, not news)
  * STAR .............. special/rare aircraft (see typeRules/csRules)
  * Rotor cross ....... helicopter
  * RED "!" pill ...... emergency squawk 7700/7600/7500
  * RED rim blip (tracking pages) ... bearing from YOU to the tracked plane
  * Counters: red = departures, green = arrivals, purple = ground, white =
    total; instrument numbers ALWAYS paint last, on background pads.

  ROUND-PANEL RENDERING RULES (the hard-won ones)
  -----------------------------------------------
  * The visible circle ends at r~116; fillRect corners beyond that EAT the
    bezel ring. Use circular clears (clearInnerChrome, r=105) and chord-aware
    text boxes (circleTextBoxW / centerText / printFit) everywhere.
  * Text near the top/bottom chord gets physically clipped by the lens —
    keep bottom labels above y~208 and size pads to the chord width.
  * fillTriangle below ~6 px rasterises as a blob on the GC9A01; tiny
    markers are fillCircle + a 1 px tick instead.
  * Low-flicker updates: aircraft register erase regions via pushBlip();
    the next frame erases exactly those and repaints. radarStep() skips
    frames whose quantised positions are unchanged (radarMotionHash).
  * Between 8 s fetches, positions advance by dead reckoning
    (liveAircraftOffsetKm, 0.4 s buckets) so motion looks continuous.

  ARCHITECTURE / CODE MAP (top to bottom)
  ---------------------------------------
  CONFIG DEFAULTS ....... every tunable, overridable from config.h
  Colours ............... C_* palette (one place, shared grammar)
  Aircraft/RouteCache ... fixed-size caches, no heap churn on ESP32
  Page system ........... PAGE_* enum, pageDur[], ISR-latched button
                          (pageButtonIsr latches presses even during blocking
                          HTTP; loop() consumes the event afterwards)
  Fetch layer ........... fetchPlanes (adsb.lol), fetchRoute (adsbdb),
                          fetchMucWeather (METAR); all failures throttle and
                          surface a short status ("HTTP -11") in the radar
                          status pill; failed fetches retry after 3 s
  Drawing primitives .... planeTriangle, trafficSymbol, bezel, clears,
                          chord-aware text helpers, blip erase system
  Coolness scoring ...... typeRules (airframes; `exact` flag so C17 does not
                          match C172) + csRules (callsigns: GAF, RCH...),
                          situational bonuses; >= 70 = star, >= 95 = alert
  Page renderers ........ radarX, airportX, summaryX, trackX, coolestX,
                          mucWeather* — each has Static (frame) + Dynamic
                          (data) halves; tracking pages add trackLiveTick()
                          for 1 s partial refreshes
  Page management ....... drawPageFull/Update, skipPage, advancePage,
                          prefetchRoutes (yields to button), alert interrupt
  setup()/loop() ........ boot animation -> Wi-Fi -> METAR warm-up -> main
                          cadence: fetch / live steps / auto page carousel

  ============================== CHANGELOG ==================================
  rev1.2.9 (rate-limit backoff + quieter rim + WX tweak)
    - HTTP 429 (rate limited) no longer spams a "retry HTTP 429" pill or feeds
      the failCount reboot spiral: the fetch backs off for 45 s, keeps the last
      good traffic on screen, and resumes quietly. Applies to both the primary
      and the buffered-retry GET.
    - Out-of-range contacts are now a small faint dark-blue DOT (C_EDGE_DOT)
      instead of a line arrow, on both HOME RADAR and the MUC map — less
      distraction. The old per-page rim blues were retired.
    - MUC WX "airport" sub-line brightened from dim green to light grey.
  rev1.2.8 (ADS-B stream + city text hotfix)
    - ADS-B fetch now asks for identity/non-chunked JSON and retries once with
      the older buffered parser if the stream parser fails. This keeps the
      rev1.2.7 heap improvement but fixes "JSON fail" when a server/proxy sends
      a body format the raw stream parser cannot consume.
    - Route city/airline names are transliterated from common UTF-8 European
      accents into display-safe ASCII before they reach the tiny GFX font.
      Example: Duesseldorf now renders as "Duesseldorf" instead of broken bytes.
  rev1.2.7 (final hardening + manual pass)
    - Final pre-run logic pass: boot Wi-Fi failures now restart/retry instead
      of sitting forever on WIFI FAILED, and loop() restarts if Wi-Fi remains
      disconnected through repeated reconnect attempts.
    - ADS-B JSON now streams directly from HTTPClient into ArduinoJson instead
      of first allocating a large temporary String, reducing heap pressure for
      24/7 use.
    - Out-of-range blue contacts are line-drawn rim arrows (shaft + two wings)
      instead of blocky filled mini-shapes; same grammar on HOME RADAR and
      MUC MAP.
  rev1.2.4 (ship it)
    - WX page: MUC header lowered with a small grey "airport" sub-line, and
      the whole data cluster nudged right so it sits centred on the panel.
    - 24/7 self-maintenance in loop(): low-heap restart guard, Wi-Fi
      re-association after repeated fetch failures (restart if still dead),
      and a daily preventive restart at a quiet moment. All millis() timers
      use wrap-safe unsigned subtraction, so the 49.7-day rollover is a
      non-event.
    - Privacy for gifting: the boot-splash name is now OWNER_NAME (config.h);
      config.example.h ships with Munich Airport as a neutral placeholder
      location, and the owner's real coordinates were removed from every
      committed file (they live only in the gitignored config.h).
  rev1.2.3 (final touches)
    - Home radar labels: ground speed added as a 4th line; minimum label
      spacing widened (34 -> 46 px centres) with up/down fallback slots, so
      boxes spread out instead of stacking — each keeps its tether line.
    - Airport labels: full box + the same thin faint green frame as the
      home radar labels.
    - Airport rim blue now matches the home radar rim blue exactly.
    - Ground traffic clamped into the runway corridor (runway-axis
      projection) so ADS-B scatter never paints a parked plane in the grass.
  rev1.2.2 (final follow-through)
    - MUC map: next-ARR/DEP are LATCHED — one arrival is followed from far
      out all the way to touchdown, one departure from the runway until it
      clears the 20 km view, before the next candidate is picked. No more
      label hopping between planes.
    - MUC map labels: three padded lines (callsign+route / ARR-DEP status /
      altitude+speed) that always read on top of runways and traffic.
    - Boot always lands on the HOME RADAR page; alert page-jumps are muted
      for the first 45 s after power-on.
  rev1.2.1 (final polish)
    - Home radar: motion-hash frame skip — no more erase/repaint flicker
      when nothing has moved a pixel.
    - MUC map: on-field aircraft drawn at runway-schematic scale, so ground
      traffic spreads along the real runways/apron instead of collapsing
      into one blob (GND count finally looks like the picture).
    - TRFC header lowered into its slack space.
    - Tracking cards fully metric (climb rate in mpm), and the NEAREST card
      now states "Munich arrival"/"departing Munich" like COOLEST does.
    - Header documentation rewritten as the final build reference; pre-1.2
      changelog history removed (it lives in git).
  rev1.2.0
    - Darker rim blues (radar + fainter still on MUC map); two-line MUC
      labels (callsign+route / status); TRFC page header + emergency jump;
      "-- > --" placeholders instead of empty card rows; red bearing blip
      on tracking pages; 1 s live CPA/DST tick; boot plane animation and
      S A U R A V splash. Earlier history: see git log.
  ============================================================================
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

#ifndef OWNER_NAME
#define OWNER_NAME "S A U R A V"   // boot-splash name — gift recipients set
#endif                             // their own in config.h

#ifndef HOME_AIRPORT_IATA
#define HOME_AIRPORT_IATA "MUC"      // used for route matching + labels
#endif

#ifndef HOME_AIRPORT_ICAO
#define HOME_AIRPORT_ICAO "EDDM"     // used for the METAR request
#endif

#ifndef TRACK_RANGE_MIN_KM
#define TRACK_RANGE_MIN_KM 10.0f     // tracking pages never zoom in past this
#endif

#ifndef RADAR_RANGE_KM
#define RADAR_RANGE_KM 60.0f         // main radar rings read as 20/40/60 km
#endif

#ifndef RADAR_ZOOM_CYCLE_ENABLED
#define RADAR_ZOOM_CYCLE_ENABLED 1   // 1 = page 1 runs weighted zoom window
#endif

#ifndef RADAR_ZOOM_WINDOW_MS
#define RADAR_ZOOM_WINDOW_MS 300000UL // 5 min: best 50%, next 30%, last 20%
#endif

#ifndef RADAR_AUTO_ZOOM_ENABLED
#define RADAR_AUTO_ZOOM_ENABLED 1    // used only when zoom cycling is disabled
#endif

#ifndef RADAR_ZOOM_40_COUNT
#define RADAR_ZOOM_40_COUNT 8        // 8+ airborne contacts -> 40 km scope
#endif

#ifndef RADAR_ZOOM_20_COUNT
#define RADAR_ZOOM_20_COUNT 18       // 18+ airborne contacts -> 20 km scope
#endif

#ifndef ACTIVITY_RADIUS_KM
#define ACTIVITY_RADIUS_KM 100.0f    // TRAFFIC BRIEF busyness count around HOME
                                     // (rev1.1.23: 60 -> 100 km wide-area mood
                                     // gauge; display pages keep their own
                                     // tighter ranges)
#endif

#ifndef ADSB_FETCH_RADIUS_KM
#define ADSB_FETCH_RADIUS_KM ACTIVITY_RADIUS_KM // fetch covers the count radius;
                                     // storage still keeps only the closest
                                     // MAX_AC aircraft, and the radar/MUC map
                                     // clip to 60/40 km themselves
#endif

#ifndef MUC_MAP_VIEW_KM
#define MUC_MAP_VIEW_KM 20.0f        // airport map on-screen scope: traffic
#endif                               // inside this radius is drawn to scale

#ifndef MUC_MAP_EDGE_KM
#define MUC_MAP_EDGE_KM 40.0f        // 20-40 km contacts become blue edge
#endif                               // markers; beyond 40 km: not drawn

#ifndef MUC_MAP_RANGE_KM
#define MUC_MAP_RANGE_KM 60.0f       // ARR/DEP *detection* radius — logic
#endif                               // only, NOT the on-screen map scope

#ifndef BRIEF_HELI_RANGE_KM
#define BRIEF_HELI_RANGE_KM 60.0f    // rotorcraft search can be wider than close-in labels
#endif

#ifndef BRIEF_EMERGENCY_RANGE_KM
#define BRIEF_EMERGENCY_RANGE_KM 200.0f // matches the current regional fetch
#endif

#ifndef MUC_MAP_SCALE
#define MUC_MAP_SCALE 22.0f          // runway-schematic px-per-km (rev1.1.18
#endif                               // enlarged from 16 so the slabs dominate)

// (rev1.1.23: OPENSKY_* config removed together with the OPEN SKY page.)

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
// Single dark glass-cockpit palette.
// The extra named fills keep page-specific drawing readable without scattering
// raw color565() calls through the layout code.
#define C_BG           tft.color565(2, 4, 14)
#define C_GRID         tft.color565(38, 124, 64)
#define C_GRIDDIM      tft.color565(10, 38, 24)
#define C_BRIGHT       tft.color565(0, 220, 255)
#define C_SWEEP        tft.color565(0, 118, 70)
#define C_AMBER        tft.color565(255, 176, 0)
#define C_CYAN         tft.color565(120, 230, 255)
#define C_WHITE        GC9A01A_WHITE
#define C_DIM          tft.color565(120, 130, 145)
#define C_RED          tft.color565(255, 64, 48)
#define C_ORANGE       GC9A01A_ORANGE
#define C_GREEN        tft.color565(60, 240, 120)
#define C_BLUE         tft.color565(72, 150, 255)
#define C_BLUE_DIM     tft.color565(22, 62, 112)    // faint but readable edge contacts
// rev1.2.9: out-of-range contacts are a small faint dot (was a line arrow;
// user: "less distraction"). One quiet dark-blue shared by the home radar and
// the MUC map so a ring of edge traffic no longer pulls the eye. The older
// per-page rim blues (C_EDGE_BLUE / C_EDGE_BLUE_MAP) retired with the arrows.
#define C_EDGE_DOT     tft.color565(12, 40, 92)     // faint dark-blue rim dot
#define C_CENTER_DOT   tft.color565(92, 124, 145)   // dim receiver marker
#define C_PURPLE       tft.color565(186, 96, 255)   // stationary/ground traffic on the MUC map
#define C_GREY         tft.color565(130, 140, 150)
#define C_RANGE_OUTER  tft.color565(20, 74, 40)
#define C_SCOPE_TEXT   tft.color565(42, 118, 56)
#define C_SCOPE_ARROW  tft.color565(6, 30, 18)
#define C_RUNWAY_FILL  tft.color565(34, 38, 46)
#define C_STATUS_FILL  tft.color565(18, 24, 38)

// ---------- Aircraft ----------
struct Aircraft {
  char  flight[10], reg[10], typ[6], sqk[6], op[28];
  float distKm, bearingDeg, trackDeg;
  float eastKm, northKm;                 // relative to home
  int   altFt, gsKt, vrFpm;
  bool  ground;
};
#define MAX_AC 64
Aircraft planes[MAX_AC];
int      planeCount = 0;
int      activityRadiusCount = 0;    // valid aircraft seen within ACTIVITY_RADIUS_KM
float    radarScopeKm = RADAR_RANGE_KM;

// Forward declarations for helpers that live with the MUC-map logic further
// down but are used by the brief/tracking pages above them. Explicit
// prototypes here keep the build independent of the IDE's auto-prototype
// quirks with user-defined reference parameters.
int  nextArrivalIdx();
int  nextDepartureIdx();
bool likelyArrival(const Aircraft &p);
bool likelyDeparture(const Aircraft &p);
int  coolestIdx();
void updateRadarAutoZoom();

// ADS-B fetch state.
//
// `lastFetchAttempt` throttles network retries, while `lastFetch` is only
// updated after a successful aircraft packet. Keeping those timestamps
// separate matters on a standalone desk device: if the API times out, stale
// aircraft should age normally instead of being treated as freshly received
// just because a failed HTTP request happened.
uint32_t lastFetch = 0, lastFetchAttempt = 0, lastPageSwitch = 0;
int      failCount = 0;
bool     haveAircraftData = false;
char     adsbFetchStatus[20] = "waiting";
// rev1.2.9: rate-limit backoff. When the ADS-B API answers 429 (Too Many
// Requests), hammering it again after 3 s only deepens the block and leaves a
// "retry HTTP 429" pill on the desk. Instead we pause fetching until
// `adsbBackoffUntil`, keep showing the last good traffic, and — because
// `adsbRateLimited` skips the failCount bump — never spiral into the network
// re-association / restart guards over what is just polite throttling.
uint32_t adsbBackoffUntil = 0;
bool     adsbRateLimited  = false;
#define  ADSB_RATELIMIT_BACKOFF_MS  45000UL
// Page order (rev1.1.23: back to six live pages — the TAF, MUC INFO and
// OPEN SKY reference pages were removed on user request).
#define PAGE_RADAR    0
#define PAGE_MUC_MAP  1
#define PAGE_SUMMARY  2
#define PAGE_NEAREST  3
#define PAGE_COOLEST  4
#define PAGE_MUC_WX   5
#define PAGE_COUNT    6
uint8_t  page = PAGE_RADAR;
const uint32_t pageDur[PAGE_COUNT] = {
  26000, 17000, 16000, 22000, 22000, 13000
};

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
// up (cool score >= ALERT_SCORE), the carousel jumps straight to the COOLEST
// tracking page once per callsign, so the user cannot miss it while another
// page idles.
#define ALERT_SCORE 95
char lastAlertCs[10] = "";

// MUC offset from home (computed in setup)
float mucE = 0, mucN = 0;

// Route cache slots.
//
// Slot 0 is the nearest-flight card, slot 1 is the coolest-flight card, and
// slots 2-3 are the MUC map's next ARR/DEP rows. Keeping a small fixed cache
// avoids heap churn on ESP32 while still letting the airport page show origin
// > destination information for the two live airport rows.
struct RouteCache {
  char forCs[10], codes[24], cities[30], airline[24];
};

// Airline brand entry (colour + tidy short name). The DEFINITION must live up
// here with the other structs, not next to the airlineBrands[] table lower in
// the file: the Arduino builder hoists auto-generated prototypes (e.g. for
// airlineBrandFor(), which returns AirlineBrand*) to the top, and they must
// find the type already declared. The table + lookups stay lower down.
struct AirlineBrand { const char *pfx; const char *disp; uint8_t r, g, b; };

#define ROUTE_SLOT_NEAREST 0
#define ROUTE_SLOT_COOLEST 1
#define ROUTE_SLOT_MUC_BASE 2
#define ROUTE_CACHE_COUNT 4
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

// rev1.1.23: the MUC TAF, MUC INFO and OPEN SKY pages were removed on user
// request (the carousel is back to six live pages), together with their
// caches (MucTaf, OpenSkyStats) and fetch code.

// rev1.1.23: the Trail history buffers (flown-path polylines for the
// NEAREST/COOLEST mini-maps) were removed together with the mini-maps
// themselves — the tracking pages are now pure data cards.

// Live-motion animation state.
//
// The aircraft API updates in bursts (every FETCH_INTERVAL_MS); between
// fetches the radar and airport pages advance traffic gently by dead
// reckoning. NOTE: the rotating sweep beam was removed on purpose — it looked
// alive but fought with aircraft labels and caused most of the perceived
// jitter. A calm ~1 s traffic step reads far better on this small panel.
#define RADAR_STEP_MS   1200
#define AIRPORT_STEP_MS 1400
#define TRACK_STEP_MS   1000   // rev1.2.0: live CPA/DST tick on tracking pages
uint32_t lastRadarStep = 0, lastLiveStep = 0, radarZoomWindowStart = 0;
uint8_t  radarZoomCycleSlot = 0;

// Dynamic pixels on the radar page (low-flicker erase). Sized with headroom
// beyond MAX_AC because stacked aircraft labels register their own erase
// blips in addition to the aircraft markers themselves.
#define MAX_BLIPS (MAX_AC + 16)
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
// rev1.1.24: `exact` flag added. Device photo showed a Cessna 172 labelled
// "C-17 GLOBEMASTER" — the C17 rule prefix-matched C172. Short military type
// codes now require an exact type match; civilian family rules (B78, A33,
// B76) intentionally keep prefix matching for their -8/-9 / -2/-3 variants.
struct CoolRule { const char *pfx; uint8_t score; const char *label; bool exact; };

const CoolRule typeRules[] = {
  {"A388", 100, "SUPERJUMBO A380"},
  {"A124",  99, "ANTONOV AN-124"},
  {"B52",   99, "B-52 BOMBER", true},
  {"C5M",   97, "C-5 GALAXY", true},
  {"C17",   96, "C-17 GLOBEMASTER", true},
  {"EUFI",  96, "EUROFIGHTER"},
  {"F16",   95, "FIGHTER JET", true},
  {"F35",   95, "FIGHTER JET", true},
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
  for (unsigned i = 0; i < sizeof(typeRules)/sizeof(typeRules[0]); i++) {
    bool m = typeRules[i].exact
                 ? (strcmp(p.typ, typeRules[i].pfx) == 0)
                 : (strncmp(p.typ, typeRules[i].pfx, strlen(typeRules[i].pfx)) == 0);
    if (m && typeRules[i].score > best) {
      best = typeRules[i].score;
      *label = typeRules[i].label;
    }
  }
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
  // The old N/S/E/W glyph pads were the cause of the "grid line is broken"
  // look: those background rectangles literally erased pieces of the centre
  // crosshair and bezel ticks. The rev1.0 radar uses only a compact red north
  // pointer; DEP/ARR/total counters are drawn separately by radarLabels().
  // Other pages call drawBezelChrome(false), so this only affects radar-style
  // chrome and boot/splash screens.
  tft.fillTriangle(120, 9, 116, 18, 124, 18, C_RED);
  tft.drawFastHLine(117, 20, 7, C_RED);
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
  centerText(OWNER_NAME, 108, 1, C_GRID);   // owner's mark (config: OWNER_NAME)
  centerText(l1, 130, 1, C_DIM);
  centerText(l2, 144, 1, color);
}

void bootAnimation() {
  tft.fillScreen(C_BG);
  // Expanding radar ping, as before...
  for (int r = 8; r <= 116; r += 4) {
    tft.drawCircle(120, 120, r, C_SWEEP);
    if (r > 12) tft.drawCircle(120, 120, r - 4, C_BG);
    delay(14);
  }
  tft.drawCircle(120, 120, 112, C_BG);
  // ...then (rev1.2.0) a little aircraft flies west-to-east across the scope,
  // leaving a short dotted contrail. splash() clears the screen afterwards,
  // so the animation needs no cleanup pass of its own.
  int prevX = -1000;
  for (int x = -12; x <= 252; x += 5) {
    if (prevX > -900) tft.fillCircle(prevX, 120, 9, C_BG);
    planeTriangle(x, 120, 90, 6, C_AMBER);
    if ((x / 5) % 4 == 0 && x - 16 > 0 && x - 16 < 240)
      tft.fillCircle(x - 16, 120, 1, C_GRIDDIM);   // contrail puff
    prevX = x;
    delay(16);
  }
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
    // 24/7 rule: never park forever on a boot-time network failure. If the
    // router is late, the password changed, or the Wi-Fi stack wedged during
    // power-up, restart and try again instead of becoming a static error sign
    // on the desk. A bad config still remains obvious because the message
    // stays up for a few seconds on every retry cycle.
    splash("WIFI FAILED", "retrying after reboot", C_RED);
    Serial.println("\nWiFi initial connect failed; rebooting to retry.");
    delay(5000);
    ESP.restart();
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

void appendAsciiChunk(char *dst, size_t n, size_t &o, const char *chunk) {
  // Small helper for copyScreenText(). It appends a replacement such as "ue"
  // only while there is still room for the trailing NUL. Keeping this separate
  // makes the UTF-8 table below readable and avoids repeated bounds logic.
  if (!chunk) return;
  for (size_t i = 0; chunk[i] && o < n - 1; i++) dst[o++] = chunk[i];
}

void copyScreenText(char *dst, size_t n, const char *src) {
  // Human-readable API strings (city names and airline legal names) can contain
  // UTF-8 accents such as Duesseldorf's German umlaut. The GC9A01 pages use the
  // tiny built-in GFX font, which is byte-oriented ASCII; drawing raw UTF-8
  // bytes produces broken-looking characters. This function transliterates the
  // common European characters we see in airport names into plain ASCII, then
  // drops anything still unknown. Callsigns still use copyStr() because those
  // should be strict printable ASCII, not language text.
  if (!src) src = "";
  size_t o = 0;
  for (size_t i = 0; src[i] && o < n - 1; ) {
    uint8_t c = (uint8_t)src[i];
    if (c >= 32 && c <= 126) {
      dst[o++] = (char)c;
      i++;
      continue;
    }

    const char *rep = "";
    uint8_t d = (uint8_t)src[i + 1];

    if (c == 0xC2 && src[i + 1]) {
      // Non-breaking spaces show up from some APIs; use a normal space.
      if (d == 0xA0) rep = " ";
      i += 2;
    } else if (c == 0xC3 && src[i + 1]) {
      // Latin-1 supplement in UTF-8: German, French, Spanish, Nordic names.
      switch (d) {
        case 0x80: case 0x81: case 0x82: case 0x83: case 0x85: rep = "A"; break;
        case 0x84: rep = "Ae"; break;
        case 0x86: rep = "AE"; break;
        case 0x87: rep = "C"; break;
        case 0x88: case 0x89: case 0x8A: case 0x8B: rep = "E"; break;
        case 0x8C: case 0x8D: case 0x8E: case 0x8F: rep = "I"; break;
        case 0x91: rep = "N"; break;
        case 0x92: case 0x93: case 0x94: case 0x95: rep = "O"; break;
        case 0x96: rep = "Oe"; break;
        case 0x98: rep = "O"; break;
        case 0x99: case 0x9A: case 0x9B: rep = "U"; break;
        case 0x9C: rep = "Ue"; break;
        case 0x9D: rep = "Y"; break;
        case 0x9F: rep = "ss"; break;
        case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA5: rep = "a"; break;
        case 0xA4: rep = "ae"; break;
        case 0xA6: rep = "ae"; break;
        case 0xA7: rep = "c"; break;
        case 0xA8: case 0xA9: case 0xAA: case 0xAB: rep = "e"; break;
        case 0xAC: case 0xAD: case 0xAE: case 0xAF: rep = "i"; break;
        case 0xB1: rep = "n"; break;
        case 0xB2: case 0xB3: case 0xB4: case 0xB5: rep = "o"; break;
        case 0xB6: rep = "oe"; break;
        case 0xB8: rep = "o"; break;
        case 0xB9: case 0xBA: case 0xBB: rep = "u"; break;
        case 0xBC: rep = "ue"; break;
        case 0xBD: case 0xBF: rep = "y"; break;
      }
      i += 2;
    } else if (c == 0xC4 && src[i + 1]) {
      // Central/Eastern European airport names: Zagreb, Lodz, Gdansk, etc.
      switch (d) {
        case 0x84: case 0x85: rep = "a"; break;
        case 0x86: case 0x87: case 0x8C: case 0x8D: rep = "c"; break;
        case 0x98: case 0x99: rep = "e"; break;
        case 0xB9: case 0xBA: rep = "l"; break;
      }
      i += 2;
    } else if (c == 0xC5 && src[i + 1]) {
      switch (d) {
        case 0x81: case 0x82: rep = "l"; break;
        case 0x83: case 0x84: rep = "n"; break;
        case 0x98: case 0x99: rep = "r"; break;
        case 0x9A: case 0x9B: rep = "s"; break;
        case 0xB9: case 0xBA: case 0xBD: case 0xBE: rep = "z"; break;
      }
      i += 2;
    } else if ((c & 0xE0) == 0xC0 && src[i + 1]) {
      i += 2;      // unknown 2-byte UTF-8 sequence
    } else if ((c & 0xF0) == 0xE0 && src[i + 1] && src[i + 2]) {
      i += 3;      // unknown 3-byte UTF-8 sequence
    } else {
      i++;         // malformed byte; skip it
    }
    appendAsciiChunk(dst, n, o, rep);
  }
  dst[o] = 0;
  for (int j = (int)strlen(dst) - 1; j >= 0 && dst[j] == ' '; j--) dst[j] = 0;
}

void setAdsbFetchStatus(const char *status) {
  // This is intentionally short and sanitized because it is both printed to
  // Serial and drawn inside a tiny radar-page pill. The aim is a useful desk
  // diagnostic ("HTTP -11", "JSON fail", ...) without turning the main page
  // into a log viewer.
  copyStr(adsbFetchStatus, sizeof(adsbFetchStatus), status);
  if (adsbFetchStatus[0] == 0) strcpy(adsbFetchStatus, "unknown");
}

bool fetchPlanes() {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(ADSB_HTTP_TIMEOUT_MS);
  // HTTP/1.0 plus identity encoding strongly discourages chunked/compressed
  // transfer bodies. ArduinoJson can stream-parse plain JSON beautifully, but
  // raw chunk framing looks like invalid JSON and caused the "JSON fail" desk
  // symptom after the rev1.2.7 heap-saving change.
  http.useHTTP10(true);

  int radiusNm = (int)(ADSB_FETCH_RADIUS_KM / 1.852f) + 4;
  char url[120];
  snprintf(url, sizeof(url), "https://api.adsb.lol/v2/point/%.4f/%.4f/%d",
           (double)HOME_LAT, (double)HOME_LON, radiusNm);
  if (!http.begin(client, url)) {
    setAdsbFetchStatus("begin fail");
    Serial.println("ADS-B begin failed.");
    return false;
  }
  http.addHeader("Accept", "application/json");
  http.addHeader("Accept-Encoding", "identity");
  adsbRateLimited = false;
  int code = http.GET();
  if (code == 429) {
    // Rate limited: pause fetching for a while and quietly keep showing the
    // last good traffic. Marked so loop() does NOT count this toward the
    // failCount reboot/re-associate spiral — it is throttling, not a fault.
    adsbRateLimited = true;
    adsbBackoffUntil = millis() + ADSB_RATELIMIT_BACKOFF_MS;
    setAdsbFetchStatus("API busy");
    Serial.println("ADS-B 429 (rate limited) -> backing off");
    http.end();
    return false;
  }
  if (code != 200) {
    char status[20];
    snprintf(status, sizeof(status), "HTTP %d", code);
    setAdsbFetchStatus(status);
    Serial.printf("ADS-B fetch failed: %d (%s)\n",
                  code, http.errorToString(code).c_str());
    http.end();
    return false;
  }

  JsonDocument filter;
  JsonObject f = filter["ac"].add<JsonObject>();
  f["flight"] = f["lat"] = f["lon"] = f["alt_baro"] = f["gs"] = f["track"] = true;
  f["r"] = f["t"] = f["squawk"] = f["baro_rate"] = f["ownOp"] = f["desc"] = true;

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    // Compatibility escape hatch: if a CDN/proxy still sends something the raw
    // stream parser cannot consume, retry once using HTTPClient's buffered path.
    // That path was the old known-good behavior, so this turns a blank desk
    // radar into a one-off extra request instead of a permanent "JSON fail".
    Serial.printf("ADS-B stream JSON error: %s; retrying buffered parser.\n", err.c_str());
    doc.clear();

    WiFiClientSecure retryClient;
    retryClient.setInsecure();
    HTTPClient retry;
    retry.setTimeout(ADSB_HTTP_TIMEOUT_MS);
    retry.useHTTP10(true);
    if (!retry.begin(retryClient, url)) {
      setAdsbFetchStatus("begin fail");
      Serial.println("ADS-B buffered retry begin failed.");
      return false;
    }
    retry.addHeader("Accept", "application/json");
    retry.addHeader("Accept-Encoding", "identity");
    int retryCode = retry.GET();
    if (retryCode == 200) {
      String payload = retry.getString();
      err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
      payload = String();
    } else {
      if (retryCode == 429) {          // same rate-limit backoff as the primary GET
        adsbRateLimited = true;
        adsbBackoffUntil = millis() + ADSB_RATELIMIT_BACKOFF_MS;
        setAdsbFetchStatus("API busy");
      } else {
        char status[20];
        snprintf(status, sizeof(status), "HTTP %d", retryCode);
        setAdsbFetchStatus(status);
      }
      Serial.printf("ADS-B buffered retry failed: %d (%s)\n",
                    retryCode, retry.errorToString(retryCode).c_str());
      retry.end();
      return false;
    }
    retry.end();
    if (err) {
      setAdsbFetchStatus("JSON fail");
      Serial.printf("ADS-B buffered JSON error: %s\n", err.c_str());
      return false;
    }
  }

  planeCount = 0;
  activityRadiusCount = 0;
  for (JsonObject ac : doc["ac"].as<JsonArray>()) {
    if (!ac["lat"].is<float>() || !ac["lon"].is<float>()) continue;

    bool onGround = ac["alt_baro"].is<const char *>();
    const char *cs = ac["flight"] | "";
    if (onGround && cs[0] == 0) continue;          // skip ground vehicles

    float lat = ac["lat"], lon = ac["lon"];
    float dHome = haversineKm(HOME_LAT, HOME_LON, lat, lon);
    if (dHome <= ACTIVITY_RADIUS_KM) activityRadiusCount++;

    // Count every valid aircraft in the regional fetch, but only keep a
    // bounded working set for the display pages. Once the array is full, keep
    // the closest contacts by replacing the farthest stored aircraft. That is
    // much better than trusting feed order, especially near Munich where a 60
    // km request can return more aircraft than the tiny display should manage.
    int slot = planeCount;
    if (planeCount >= MAX_AC) {
      slot = 0;
      for (int k = 1; k < MAX_AC; k++)
        if (planes[k].distKm > planes[slot].distKm) slot = k;
      if (dHome >= planes[slot].distKm) continue;
    }

    Aircraft &p = planes[slot];
    p.ground = onGround;
    copyStr(p.flight, sizeof(p.flight), cs);
    if (p.flight[0] == 0) strcpy(p.flight, "------");
    copyStr(p.reg, sizeof(p.reg), ac["r"] | "");
    copyStr(p.typ, sizeof(p.typ), ac["t"] | "");
    copyStr(p.sqk, sizeof(p.sqk), ac["squawk"] | "");
    const char *opName = ac["ownOp"] | (const char *)nullptr;
    if (!opName) opName = ac["desc"] | "";
    copyStr(p.op, sizeof(p.op), opName);

    p.distKm     = dHome;
    p.bearingDeg = bearingDegF(HOME_LAT, HOME_LON, lat, lon);
    p.eastKm     = (lon - HOME_LON) * 111.32f * cosf(deg2rad(lat));
    p.northKm    = (lat - HOME_LAT) * 110.57f;
    p.trackDeg   = ac["track"] | 0.0f;
    p.altFt      = onGround ? 0 : (int)(ac["alt_baro"] | 0);
    p.gsKt       = (int)(ac["gs"] | 0.0f);
    p.vrFpm      = ac["baro_rate"] | 0;
    if (planeCount < MAX_AC) planeCount++;
  }

  for (int i = 0; i < planeCount - 1; i++)
    for (int j = i + 1; j < planeCount; j++)
      if (planes[j].distKm < planes[i].distKm) {
        Aircraft t = planes[i]; planes[i] = planes[j]; planes[j] = t;
      }

  updateRadarAutoZoom();

  Serial.printf("Aircraft: %d stored, %d within %.0fkm, heap: %u\n",
                planeCount, activityRadiusCount, (double)ACTIVITY_RADIUS_KM,
                ESP.getFreeHeap());
  setAdsbFetchStatus("OK");
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
        copyScreenText(r.airline, sizeof(r.airline), fr["airline"]["name"] | "");
        if (o[0] && d[0]) {
          char oCode[8], dCode[8], oCity[18], dCity[18];
          copyStr(oCode, sizeof(oCode), o);
          copyStr(dCode, sizeof(dCode), d);
          copyScreenText(oCity, sizeof(oCity), oc);
          copyScreenText(dCity, sizeof(dCity), dc);
          snprintf(r.codes, sizeof(r.codes), "%s > %s", oCode, dCode);
          snprintf(r.cities, sizeof(r.cities), "%.12s > %.12s", oCity, dCity);
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

bool routeOriginCode(const Aircraft &p, char *dst, size_t dstN) {
  // Counterpart of routeDestinationCode(): extract the origin ("AAA" from a
  // cached "AAA > BBB" route). The MUC map's next-arrival label uses it to
  // show where the flight is coming FROM (rev1.1.20).
  if (dstN == 0) return false;
  dst[0] = 0;
  RouteCache *r = cachedRouteFor(p.flight);
  if (!r || !r->codes[0]) return false;
  const char *arrow = strchr(r->codes, '>');
  if (!arrow) return false;
  int n = 0;
  while (r->codes + n < arrow && r->codes[n] != ' ' &&
         n < (int)dstN - 1 && n < 4) {
    dst[n] = r->codes[n];
    n++;
  }
  dst[n] = 0;
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
         (!p.ground && d < MUC_MAP_RANGE_KM && p.altFt < 24000 && isMovingTowardMuc(p));
}

bool trafficLooksMucOutbound(const Aircraft &p) {
  float d = distanceToMucKm(p);
  return routeOriginIsMuc(p) ||
         (!p.ground && d < MUC_MAP_RANGE_KM && p.altFt < 24000 && isMovingAwayFromMuc(p));
}

uint16_t trafficColor(const Aircraft &p, bool outOfRange) {
  // One consistent visual grammar across the radar and airport pages:
  // green = inbound/destination MUC, red = outbound/from MUC, blue = out-of-
  // range, purple = ground/stationary, yellow = normal non-MUC traffic.
  // Route cache wins when known;
  // otherwise heading/altitude/distance provide a live heuristic.
  if (outOfRange) return C_BLUE_DIM;
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

// rev1.1.23: TAF parsing/fetch and the OpenSky stats fetch were removed
// together with their pages. The shared METAR token helpers stay — the
// weather page still uses them.

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

void rimBearingDotAt(int cx, int cy, float bearingDeg, int r, uint16_t color) {
  // rev1.2.9: tiny out-of-range aircraft marker. Was a line arrow (shaft +
  // wings); replaced with a small quiet dot on user request ("less
  // distraction") since a rim full of arrows stole attention from real
  // traffic. It sits at radius r along bearing-from-centre and only says
  // "there is more traffic this way", not the aircraft's own heading.
  float b = deg2rad(bearingDeg);
  float sx = sinf(b), sy = -cosf(b);
  int px = cx + (int)(sx * r);
  int py = cy + (int)(sy * r);
  tft.fillCircle(px, py, 2, color);
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
#define RADAR_RIM_R     107   // rim circles as far out as the bezel allows

float radarDisplayRangeKm() {
  if (RADAR_ZOOM_CYCLE_ENABLED) return radarScopeKm;
  return RADAR_AUTO_ZOOM_ENABLED ? radarScopeKm : RADAR_RANGE_KM;
}

float radarZoomCycleSlotKm(uint8_t slot) {
  // Home radar "tour" scopes. Keep the named close/medium/wide values even
  // though the fetch radius remains wide; only the display projection changes.
  // The clamps make the helper safe if someone later sets RADAR_RANGE_KM below
  // 60 km in config.h.
  float closeKm = 20.0f;
  float mediumKm = 40.0f;
  if (closeKm > RADAR_RANGE_KM) closeKm = RADAR_RANGE_KM;
  if (mediumKm > RADAR_RANGE_KM) mediumKm = RADAR_RANGE_KM;
  if (slot == 0) return closeKm;
  if (slot == 1) return mediumKm;
  return RADAR_RANGE_KM;
}

int radarAirborneInWideScope() {
  int airborne = 0;
  for (int i = 0; i < planeCount; i++)
    if (!planes[i].ground && planes[i].distKm <= RADAR_RANGE_KM) airborne++;
  return airborne;
}

void radarZoomWeightedOrder(uint8_t order[3]) {
  // Pick the most useful zoom order from current traffic density. Slots are:
  //   0 = close 20 km, 1 = medium 40 km, 2 = wide RADAR_RANGE_KM.
  //
  // Busy: close range gets the longest dwell because readable labels matter.
  // Medium: the 40 km view is the best compromise, with 20 km as the next
  // label-friendly view and 60 km as brief context.
  // Quiet: wide range deserves the longest dwell, then medium, then close.
  int airborne = radarAirborneInWideScope();
  if (airborne >= RADAR_ZOOM_20_COUNT) {
    order[0] = 0; order[1] = 1; order[2] = 2;
  } else if (airborne >= RADAR_ZOOM_40_COUNT) {
    order[0] = 1; order[1] = 0; order[2] = 2;
  } else {
    order[0] = 2; order[1] = 1; order[2] = 0;
  }
}

uint8_t radarZoomWeightedSlotForElapsed(uint32_t elapsedMs) {
  uint8_t order[3];
  radarZoomWeightedOrder(order);

  // The user asked for a 50/30/20 split inside a 5-minute window. Computing it
  // from RADAR_ZOOM_WINDOW_MS keeps the proportions intact if the window is
  // tuned later in config.h.
  const uint32_t windowMs = (uint32_t)RADAR_ZOOM_WINDOW_MS;
  const uint32_t primaryMs = windowMs / 2;           // 50%
  const uint32_t secondaryMs = (windowMs * 3UL) / 10UL; // 30%
  if (elapsedMs < primaryMs) return order[0];
  if (elapsedMs < primaryMs + secondaryMs) return order[1];
  return order[2];                                  // remaining 20%
}

void resetRadarZoomCycle() {
  // Start a fresh weighted 5-minute window whenever the radar page is shown.
  // The initial slot is whatever the current traffic density says deserves the
  // 50% dwell, not always the closest view.
  if (!RADAR_ZOOM_CYCLE_ENABLED) return;
  radarZoomWindowStart = millis();
  radarZoomCycleSlot = radarZoomWeightedSlotForElapsed(0);
  radarScopeKm = radarZoomCycleSlotKm(radarZoomCycleSlot);
}

bool updateRadarZoomCycle(uint32_t now) {
  if (!RADAR_ZOOM_CYCLE_ENABLED || page != PAGE_RADAR) return false;
  if (radarZoomWindowStart == 0) {
    resetRadarZoomCycle();
    return true;
  }

  uint32_t elapsed = now - radarZoomWindowStart;
  if (elapsed >= (uint32_t)RADAR_ZOOM_WINDOW_MS) {
    radarZoomWindowStart = now;
    elapsed = 0;
  }

  uint8_t nextSlot = radarZoomWeightedSlotForElapsed(elapsed);
  if (nextSlot == radarZoomCycleSlot) return false;

  radarZoomCycleSlot = nextSlot;
  radarScopeKm = radarZoomCycleSlotKm(radarZoomCycleSlot);
  return true;
}

void updateRadarAutoZoom() {
  // Keep the fetch/data radius wide, but choose the HOME RADAR display radius
  // from the current traffic load. This is the key desk-display compromise:
  // at busy office hours the radar zooms to 20 km so labels breathe; late at
  // night it opens back out to 60 km so the screen does not look empty.
  //
  // Hysteresis avoids a tiny count change flipping the scope every 8-second
  // ADS-B fetch. The "relax" thresholds are intentionally lower than the
  // "zoom in" thresholds, so the page feels steady on a desk.
  if (RADAR_ZOOM_CYCLE_ENABLED) return;

  if (!RADAR_AUTO_ZOOM_ENABLED) {
    radarScopeKm = RADAR_RANGE_KM;
    return;
  }

  int airborne = radarAirborneInWideScope();

  float next = radarScopeKm;
  if (radarScopeKm <= 20.5f) {
    if (airborne <= RADAR_ZOOM_40_COUNT + 4) next = 40.0f;
  } else if (radarScopeKm <= 40.5f) {
    if (airborne >= RADAR_ZOOM_20_COUNT) next = 20.0f;
    else if (airborne <= RADAR_ZOOM_40_COUNT - 2) next = RADAR_RANGE_KM;
  } else {
    if (airborne >= RADAR_ZOOM_20_COUNT) next = 20.0f;
    else if (airborne >= RADAR_ZOOM_40_COUNT) next = 40.0f;
    else next = RADAR_RANGE_KM;
  }

  if (next > RADAR_RANGE_KM) next = RADAR_RANGE_KM;
  radarScopeKm = next;
}

float radarRingKm(int ring) {
  // The display range can be 20/40/60 km. Use human-friendly rings rather
  // than awkward thirds like 6/13/20: close range = 5/10/20, mid range =
  // 10/20/40, wide range = 20/40/60.
  float scope = radarDisplayRangeKm();
  if (scope <= 20.5f) {
    const float r[3] = {5.0f, 10.0f, 20.0f};
    return r[ring - 1];
  }
  if (scope <= 40.5f) {
    const float r[3] = {10.0f, 20.0f, 40.0f};
    return r[ring - 1];
  }
  const float r[3] = {20.0f, 40.0f, 60.0f};
  return r[ring - 1];
}

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

void radarDrawRangeLabels() {
  // Truthful range marks: the two inner numbers stay on the real east-side
  // ring intersections, while the active outer scope (20/40/60 km) gets its
  // own south compass marker. This keeps the inner ruler physically meaningful
  // and lets the headline range sit where the eye expects a south instrument.
  float scope = radarDisplayRangeKm();
  const int y = 132;
  tft.setTextSize(1);
  for (int ring = 1; ring <= 2; ring++) {
    float km = radarRingKm(ring);
    char label[8];
    snprintf(label, sizeof(label), "%d", (int)km);

    int r = (int)(RADAR_TRAFFIC_R * (km / scope));
    int w = (int)strlen(label) * 6;
    int x = 120 + r - w / 2;
    if (x < 132) x = 132;
    if (x + w > 232) x = 232 - w;

    tft.fillRect(x - 2, y - 1, w + 4, 9, C_BG);
    tft.setTextColor(ring == 3 ? C_RANGE_OUTER : C_GRIDDIM);
    tft.setCursor(x, y);
    tft.print(label);
  }

  // South full-scope indicator. The number uses dim green and the smallest
  // built-in GFX font so it reads as a range limit, not another traffic count.
  // The small dark open arrow below it echoes a classic compass-card south
  // pointer without adding another big letter to the already busy round scope.
  char outer[8];
  snprintf(outer, sizeof(outer), "%dkm", (int)radarRingKm(3));
  int w = (int)strlen(outer) * 6;
  int x = 120 - w / 2;
  tft.fillRect(x - 2, 205, w + 4, 22, C_BG);
  tft.setTextColor(C_SCOPE_TEXT);
  tft.setCursor(x, 208);
  tft.print(outer);
  tft.drawTriangle(120, 225, 114, 217, 126, 217, C_SCOPE_ARROW);
  tft.drawLine(120, 225, 120, 218, C_SCOPE_ARROW);
}

void radarDrawReferenceDots() {
  // Receiver/"me" marker. It is drawn BEFORE aircraft, not as a top overlay:
  // the dot remains present at the centre, but any aircraft passing over your
  // exact bearing gets drawn above it. The MUC dot was removed because it added
  // clutter without helping aircraft-label readability on the first page.
  tft.fillCircle(120, 120, 2, C_CENTER_DOT);
  tft.drawCircle(120, 120, 3, C_BG);
}

void radarGrid(bool withKmLabels) {
  // Inner rings: subtle SOLID lines in one consistent tone (user feedback:
  // the second ring looked randomly darker). Only the outer range ring is
  // visually different — dashed and brighter — because it means "edge of
  // the configured scope" rather than just a distance marker.
  float scope = radarDisplayRangeKm();
  int r1 = (int)(RADAR_TRAFFIC_R * (radarRingKm(1) / scope));
  int r2 = (int)(RADAR_TRAFFIC_R * (radarRingKm(2) / scope));
  int r3 = (int)(RADAR_TRAFFIC_R * (radarRingKm(3) / scope));
  tft.drawCircle(120, 120, r1, C_GRIDDIM);
  tft.drawCircle(120, 120, r2, C_GRIDDIM);
  dashedCircle(120, 120, r3, C_RANGE_OUTER);
  tft.drawLine(120, 26, 120, 214, C_GRIDDIM);
  tft.drawLine(26, 120, 214, 120, C_GRIDDIM);
  // The white "you are here" dot is drawn separately; keep the grid
  // itself neutral so the centre does not flicker between colours.

  // East-side range labels sit on small background pads so range/grid lines do
  // not strike through the digits. The positions are tied to the actual ring
  // radii, so the range marks behave like a real distance ruler.
  if (!withKmLabels) return;
  radarDrawRangeLabels();
}

int radarLabelPriority(const Aircraft &p, float tieDistanceKm) {
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
  int prox = 999 - (int)(tieDistanceKm * 10.0f); // closer = higher inside a tier
  if (prox < 0) prox = 0;
  return tier + prox;
}

#define RADAR_LABEL_TARGET     3
#define RADAR_LABEL_CANDIDATES 12

bool radarCandidateAlreadyListed(int idx, const int outIdx[], int count) {
  for (int i = 0; i < count; i++)
    if (outIdx[i] == idx) return true;
  return false;
}

void radarAddLabelCandidate(int idx, int outIdx[], int maxN, int *count) {
  if (idx < 0 || *count >= maxN) return;
  if (radarCandidateAlreadyListed(idx, outIdx, *count)) return;
  outIdx[*count] = idx;
  (*count)++;
}

int selectRadarLabelCandidates(int outIdx[], int maxN) {
  for (int i = 0; i < maxN; i++) outIdx[i] = -1;
  int best[3] = {-1, -1, -1};
  int bandIdx[3] = {-1, -1, -1};
  float scope = radarDisplayRangeKm();
  float band1 = radarRingKm(1);
  float band2 = radarRingKm(2);

  // First pass: one preferred candidate per visible range band. The band edges
  // follow the actual zoom rings, so a 20 km view starts with 0-5 / 5-10 /
  // 10-20 km, a 40 km view with 0-10 / 10-20 / 20-40 km, and a 60 km view
  // with 0-20 / 20-40 / 40-60 km. This preserves the user's "one from each
  // distance band" feel before fallback candidates are considered.
  for (int i = 0; i < planeCount; i++) {
    Aircraft &p = planes[i];
    if (p.ground) continue;
    float liveE, liveN;
    liveAircraftOffsetKm(p, &liveE, &liveN);
    float liveD = sqrtf(liveE * liveE + liveN * liveN);
    if (liveD > scope) continue;
    int band = 2;
    if (liveD <= band1) band = 0;
    else if (liveD <= band2) band = 1;
    int s = radarLabelPriority(p, liveD);
    if (s > best[band]) {
      best[band] = s;
      bandIdx[band] = i;
    }
  }

  // Add the band winners first, highest priority first. If a rare/emergency
  // aircraft is in the far band, it should get first chance at screen space
  // even though we are still respecting the band-first candidate policy.
  int count = 0;
  for (int pass = 0; pass < 3; pass++) {
    int bestBand = -1;
    int bestScore = -1;
    for (int band = 0; band < 3; band++) {
      if (bandIdx[band] < 0) continue;
      if (radarCandidateAlreadyListed(bandIdx[band], outIdx, count)) continue;
      if (best[band] > bestScore) {
        bestScore = best[band];
        bestBand = band;
      }
    }
    if (bestBand >= 0)
      radarAddLabelCandidate(bandIdx[bestBand], outIdx, maxN, &count);
  }

  // Fallback queue: keep adding the best remaining visible aircraft. The
  // drawing step may reject a label because the box would overlap, sit outside
  // the round safe area, or collide with protected instruments. Having more
  // candidates means the renderer can keep trying until it actually places up
  // to three labels whenever there are enough visible aircraft.
  while (count < maxN) {
    int fillIdx = -1, fillScore = -1;
    for (int i = 0; i < planeCount; i++) {
      Aircraft &p = planes[i];
      if (p.ground) continue;
      float liveE, liveN;
      liveAircraftOffsetKm(p, &liveE, &liveN);
      float liveD = sqrtf(liveE * liveE + liveN * liveN);
      if (liveD > scope) continue;
      if (radarCandidateAlreadyListed(i, outIdx, count)) continue;
      int s = radarLabelPriority(p, liveD);
      if (s > fillScore) {
        fillScore = s;
        fillIdx = i;
      }
    }
    if (fillIdx < 0) break;
    radarAddLabelCandidate(fillIdx, outIdx, maxN, &count);
  }
  return count;
}

void trafficSummaryCounts(int *dep, int *arr, int *total) {
  *dep = *arr = *total = 0;
  float scope = radarDisplayRangeKm();
  for (int i = 0; i < planeCount; i++) {
    Aircraft &p = planes[i];
    if (p.ground || p.distKm > scope) continue;
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
    tft.fillRect(101, 23, 38, 17, C_BG);   // total (white) under the arrow
    tft.fillRect(17, 111, 36, 14, C_BG);   // DEP counter at the west point
    tft.fillRect(187, 111, 36, 14, C_BG);  // ARR counter at the east point
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

void radarCounter(int cx, int y, int value, uint16_t color) {
  // One helper for total/DEP/ARR prevents the counters from drifting to
  // different sizes during UI tweaks. The text is padded and drawn last so
  // grid lines and aircraft symbols cannot make the top/side numbers jagged
  // or unreadable.
  char num[8];
  snprintf(num, sizeof(num), "%d", value);
  const uint8_t size = 1;
  int w = (int)strlen(num) * 6 * size + 4;
  int h = 8 * size + 2;
  tft.fillRect(cx - w / 2, y - 1, w, h, C_BG);
  tft.setTextSize(size);
  tft.setTextColor(color);
  tft.setCursor(cx - (int)strlen(num) * 3 * size, y);
  tft.print(num);
}

void radarLabels() {
  // Home-page compass instrumentation (rev1.0 layout):
  //   - red north ARROW at the top (real compass style, replaces the "N"),
  //   - total contacts in white directly under the arrow,
  //   - DEP count in red at the west point, ARR count in green at the east
  //     point (where the W/E letters used to sit — the letters are gone).
  // All three numbers deliberately use size 1. The first rev1.0 photos showed
  // size-2 counters overpowering the traffic labels and making the scope feel
  // like a scoreboard; these are meant to be peripheral instruments.
  int dep, arr, total;
  trafficSummaryCounts(&dep, &arr, &total);

  // North arrow: small filled red triangle pointing outward/up.
  tft.fillTriangle(120, 9, 116, 18, 124, 18, C_RED);
  tft.drawFastHLine(117, 20, 7, C_RED);

  radarCounter(120, 28, total, C_WHITE);
  radarCounter(35,  115, dep,  C_RED);
  radarCounter(205, 115, arr,  C_GREEN);
}

void radarFetchStatusPill() {
  // Startup/network diagnostic for standalone use. Earlier revisions left the
  // boot splash on screen until the first successful ADS-B response, which
  // looked like a freeze when the API was slow, blocked, or returning an HTTP
  // error. This pill lets the radar shell stay visible while clearly saying
  // that the firmware is still alive and retrying in the background.
  if (haveAircraftData && failCount == 0) return;

  char msg[24];
  if (!haveAircraftData && failCount == 0)
    snprintf(msg, sizeof(msg), "waiting ADS-B");
  else
    snprintf(msg, sizeof(msg), "retry %s", adsbFetchStatus);

  tft.fillRoundRect(48, 198, 144, 17, 5, C_STATUS_FILL);
  centerText(msg, 202, 1, failCount ? C_ORANGE : C_DIM);
}

bool radarTryDrawAircraftLabel(int idx, bool record,
                               int labelCx[RADAR_LABEL_TARGET],
                               int labelCy[RADAR_LABEL_TARGET],
                               int *labelUsed) {
  // Try to place one stacked aircraft label. This is deliberately separate
  // from the aircraft-marker drawing pass so the renderer can walk a fallback
  // queue in priority order: if the best label cannot fit, the next candidate
  // gets a chance instead of leaving the screen with only one or two labels.
  if (idx < 0 || idx >= planeCount || *labelUsed >= RADAR_LABEL_TARGET)
    return false;

  Aircraft &p = planes[idx];
  if (p.ground) return false;

  float liveE, liveN;
  liveAircraftOffsetKm(p, &liveE, &liveN);
  float liveD = sqrtf(liveE * liveE + liveN * liveN);
  float scope = radarDisplayRangeKm();
  if (liveD > scope) return false;

  float liveB = fmodf(atan2f(liveE, liveN) * 57.29578f + 360.0f, 360.0f);
  float b = deg2rad(liveB);
  float rr = (liveD / scope) * RADAR_TRAFFIC_R;
  int x = 120 + (int)(sinf(b) * rr);
  int y = 120 - (int)(cosf(b) * rr);
  if (radarTextShield(x, y)) return false;

  // rev1.2.3: fourth line — ground speed (user request), and the label boxes
  // spread harder: minimum centre spacing grew 34 -> 46 px (two 41 px-tall
  // boxes at 34 px could still overlap), plus two extra vertical fallback
  // slots per side so a rejected spot dodges up/down instead of giving up.
  // Every placed box keeps its tether line to the aircraft marker.
  char l3[10], l4[12];
  if (p.altFt > 0) snprintf(l3, sizeof(l3), "%dm", (int)(p.altFt * 0.3048f));
  else             snprintf(l3, sizeof(l3), "--m");
  if (p.gsKt > 0)  snprintf(l4, sizeof(l4), "%dkm/h", (int)(p.gsKt * 1.852f));
  else             snprintf(l4, sizeof(l4), "--km/h");

  const int yOff[3] = {0, -24, 24};   // preferred, dodge up, dodge down
  for (int side = 0; side < 2; side++) {
    for (int v = 0; v < 3; v++) {
      int lx = ((x < 120) == (side == 0)) ? x + 10 : x - 52;
      int ly = y - 18 + yOff[v];
      if (ly < 48) ly = 48;
      if (ly > 156) ly = 156;
      int cxl = lx + 21, cyl = ly + 18;   // label-box centre
      bool fits = (cxl - 120) * (cxl - 120) + (cyl - 120) * (cyl - 120) <= 88 * 88 &&
                  !radarTextShield(lx, ly) && !radarTextShield(lx + 42, ly + 36);
      for (int k = 0; k < *labelUsed && fits; k++) {
        int dx = cxl - labelCx[k], dy = cyl - labelCy[k];
        if (dx * dx + dy * dy < 46 * 46) fits = false;
      }
      if (!fits) continue;

      int labelEdgeX = lx > x ? lx : lx + 42;
      int lineStartX = x + (labelEdgeX > x ? 7 : -7);
      int lineStartY = y;
      int lineEndY = ly + 14;
      tft.fillRect(lx - 2, ly - 2, 46, 41, C_BG);
      tft.drawRect(lx - 2, ly - 2, 46, 41, C_GRIDDIM);   // thin faint green frame
      tft.drawLine(lineStartX, lineStartY, labelEdgeX, lineEndY, C_DIM);

      printFit(lx, ly,      p.flight, 1, C_WHITE, 42);
      printFit(lx, ly + 10, p.typ,    1, C_CYAN,  42);
      printFit(lx, ly + 20, l3,       1, C_AMBER, 42);
      printFit(lx, ly + 30, l4,       1, C_DIM,   42);
      if (record) {
        pushBlip(cxl, cyl, 34);
        pushBlip((lineStartX + labelEdgeX) / 2,
                 (lineStartY + lineEndY) / 2,
                 18);
      }
      labelCx[*labelUsed] = cxl;
      labelCy[*labelUsed] = cyl;
      (*labelUsed)++;
      return true;
    }
  }
  return false;
}

void radarPaint(bool record, bool drawLabels, bool drawGridLabels) {
  radarGrid(drawGridLabels);
  radarDrawReferenceDots();
  if (record) prevBlipCount = 0;
  float scope = radarDisplayRangeKm();
  int labelIdx[RADAR_LABEL_CANDIDATES];
  int labelCount = selectRadarLabelCandidates(labelIdx, RADAR_LABEL_CANDIDATES);
  int labelCx[RADAR_LABEL_TARGET] = {0, 0, 0};
  int labelCy[RADAR_LABEL_TARGET] = {0, 0, 0};
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
    if (liveD <= scope) {
      uint16_t c = trafficColor(p, false);
      float rr = (liveD / scope) * RADAR_TRAFFIC_R;
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

    } else {
      // Out-of-range contact: a small faint dark-blue dot at the rim along
      // bearing-from-you (rev1.2.9, was a line arrow). It marks direction of
      // extra traffic without pulling the eye off in-scope aircraft.
      rimBearingDotAt(120, 120, liveB, RADAR_RIM_R - 3, C_EDGE_DOT);
      x = 120 + (int)(sinf(b) * (RADAR_RIM_R - 4));
      y = 120 - (int)(cosf(b) * (RADAR_RIM_R - 4));
      if (record) pushBlip(x, y, 12);
    }
  }

  // Label pass: walk the priority/band candidate queue until three labels are
  // actually placed. This is the key "try hard for 3 labels" behavior: a
  // rejected candidate no longer leaves a permanent empty label slot.
  for (int c = 0; c < labelCount && labelUsed < RADAR_LABEL_TARGET; c++)
    radarTryDrawAircraftLabel(labelIdx[c], record, labelCx, labelCy, &labelUsed);

  // With the scope widened to 98 px, rim markers and blip erases can brush the
  // bezel — but the compact bezel is now just 1 circle + 12 ticks, so simply
  // re-painting it every frame is cheap and keeps the edge ring pixel-perfect.
  drawBezelRing();

  // Stable overlays are intentionally drawn after live traffic: range labels
  // and counters should stay readable even when aircraft pass underneath them.
  radarDrawRangeLabels();
  if (drawLabels) radarLabels();
  radarFetchStatusPill();
}

void radarDynamic() { eraseBlips(true); radarPaint(true, true, true); }

bool radarTextShield(int x, int y) {
  // Reserved zones for the compass instrumentation: aircraft markers and
  // their stacked labels are simply not drawn here, so the arrow and the
  // three counters never fight with moving traffic for the same pixels.
  bool northBlock = x >= 104 && x <= 136 && y >= 4   && y <= 38;  // arrow+total
  bool rangeBlock = x >= 132 && x <= 196 && y >= 128 && y <= 145; // inner km ruler
  bool southBlock = x >= 88  && x <= 152 && y >= 200 && y <= 230; // south scope marker
  bool depBlock   = x >= 18  && x <= 54  && y >= 108 && y <= 130; // west number
  bool arrBlock   = x >= 186 && x <= 222 && y >= 108 && y <= 130; // east number
  return northBlock || rangeBlock || southBlock || depBlock || arrBlock;
}

uint32_t lastRadarMotionHash = 0;

uint32_t radarMotionHash() {
  // rev1.2.1 flicker fix: hash the QUANTIZED dead-reckoned positions of all
  // aircraft. Dead reckoning advances in 0.4 s buckets, so between buckets
  // every position is pixel-identical — and erasing + repainting an
  // unchanged frame is pure flicker. radarStep() now skips those frames.
  uint32_t h = 2166136261u;
  for (int i = 0; i < planeCount; i++) {
    float e, n;
    liveAircraftOffsetKm(planes[i], &e, &n);
    int qx = (int)(e * 8.0f), qy = (int)(n * 8.0f);   // 125 m grid ~ 1 px
    h = (h ^ (uint32_t)(qx * 31 + qy)) * 16777619u;
  }
  return h ^ ((uint32_t)planeCount << 24) ^ (uint32_t)radarScopeKm;
}

void radarStep() {
  // Calm live motion: advance dead-reckoned traffic roughly once per second.
  // This replaced the rotating sweep beam — see the animation-state comment
  // near the top of the file for why the beam was removed.
  uint32_t hash = radarMotionHash();
  if (hash == lastRadarMotionHash) return;   // nothing moved a pixel: no repaint
  lastRadarMotionHash = hash;
  eraseBlips(false);
  radarPaint(true, true, false);
}

// ---------- Airline brand colours + short names (shared by tracking pages) ----------

// One flat table, keyed by ICAO callsign prefix, does double duty:
//   * brand colour — the carrier's dominant logo colour, mapped to the closest
//     tone the fixed UI palette can show (Ryanair navy, DHL yellow, Wizz pink).
//   * short name   — a tidy, screen-safe carrier label. adsbdb hands back long
//     legal names ("Deutsche Lufthansa AG", "Lufthansa CityLine GmbH") that
//     overflow the round card, so when the prefix is known we print this clean
//     name (kept <=10 chars) instead. Adding a carrier is still one line.
// Coverage = the carriers that realistically appear around Munich (MUC/EDDM):
// the Lufthansa group and its regional feeders, European low-cost, the main
// long-haul flag carriers, and the cargo integrators. An unknown carrier keeps
// its adsbdb name (truncated) and a neutral amber accent.
// (struct AirlineBrand is declared up near RouteCache — see the note there.)
const AirlineBrand airlineBrands[] = {
  // -- Lufthansa group & Star Alliance feeders (the bulk of MUC traffic) --
  {"DLH", "Lufthansa",  255, 190, 40},  // Lufthansa - crane yellow
  {"CLH", "Lufthansa",  255, 190, 40},  // Lufthansa CityLine (regional feed)
  {"LHX", "Lufthansa",  255, 190, 40},  // Lufthansa City Airlines
  {"DLA", "Dolomiti",   0,  90, 150},   // Air Dolomiti - LH Italian feeder, big at MUC
  {"AUA", "Austrian",   200, 16, 46},   // Austrian Airlines - red
  {"SWR", "Swiss",      200, 16, 46},   // Swiss Int'l - red
  {"EWG", "Eurowings",  128, 0,  90},   // Eurowings - burgundy
  {"BEL", "Brussels",   0,  40, 110},   // Brussels Airlines - navy
  {"SAS", "SAS",        0,  50, 120},   // Scandinavian - blue
  {"LOT", "LOT",        0,  60, 130},   // LOT Polish - blue
  {"TAP", "TAP",        0, 120, 90},    // TAP Air Portugal - green
  {"AEE", "Aegean",     0,  90, 160},   // Aegean - blue
  {"FIN", "Finnair",    0,  60, 130},   // Finnair - blue
  {"SIA", "Singapore",  40, 40, 120},   // Singapore Airlines - navy
  {"UAL", "United",     0,  60, 130},   // United - blue
  {"ACA", "Air Canada", 210, 0,  40},   // Air Canada - red
  // -- SkyTeam / other flag carriers --
  {"AFR", "Air France", 0,  40, 140},   // Air France - blue
  {"KLM", "KLM",        0, 161, 228},   // KLM - light blue
  {"DAL", "Delta",      200, 16, 46},   // Delta - red
  {"ITY", "ITA",        0,  40, 90},    // ITA Airways - blue
  {"AAL", "American",   90, 110, 130},  // American - silver/blue
  {"IBE", "Iberia",     210, 0, 50},    // Iberia - red
  {"EIN", "Aer Lingus", 0, 120, 80},    // Aer Lingus - green
  // -- oneworld / Gulf / Turkish long-haul --
  {"BAW", "British",    23, 66, 133},   // British Airways - blue
  {"UAE", "Emirates",   218, 30, 40},   // Emirates - red
  {"QTR", "Qatar",      93, 26, 60},    // Qatar Airways - burgundy
  {"ETD", "Etihad",     200, 160, 90},  // Etihad - sand/gold
  {"THY", "Turkish",    200, 16, 46},   // Turkish Airlines - red
  // -- low-cost --
  {"RYR", "Ryanair",    7,  60, 165},   // Ryanair - navy blue
  {"WZZ", "Wizz Air",   230, 40, 130},  // Wizz Air - pink
  {"EZY", "easyJet",    255, 102, 0},   // easyJet - orange
  {"EJU", "easyJet",    255, 102, 0},   // easyJet Europe
  {"VLG", "Vueling",    255, 204, 0},   // Vueling - yellow
  {"PGT", "Pegasus",    240, 78, 60},   // Pegasus - red/orange
  {"CFG", "Condor",     255, 173, 0},   // Condor - yellow
  {"SXS", "SunExpres",  0,  90, 160},   // SunExpress - blue
  // -- cargo / integrators --
  {"DHL", "DHL",        255, 204, 0},   // DHL - yellow
  {"BCS", "DHL",        255, 204, 0},   // DHL European Air Transport
  {"FDX", "FedEx",      255, 102, 0},   // FedEx - orange
  {"UPS", "UPS",        100, 60, 20},   // UPS - brown
};

// Look up the brand-table entry for an aircraft by its callsign prefix.
const AirlineBrand *airlineBrandFor(const Aircraft &n) {
  for (unsigned i = 0; i < sizeof(airlineBrands)/sizeof(airlineBrands[0]); i++)
    if (strncmp(n.flight, airlineBrands[i].pfx, 3) == 0)
      return &airlineBrands[i];
  return NULL;
}

uint16_t airlineAccentColor(const Aircraft &n, const char *who) {
  (void)who;   // callsign prefix is the reliable key; the name is decorative
  const AirlineBrand *b = airlineBrandFor(n);
  if (b) return tft.color565(b->r, b->g, b->b);
  return C_AMBER;   // unknown carrier: neutral accent
}

// Screen-safe carrier name: the tidy table label when the prefix is known,
// otherwise the adsbdb name. Always clipped to maxChars so a long legal name
// can never run off the round card.
void airlineShortName(const Aircraft &n, const char *adsbdbName,
                      char *dst, size_t dstN, int maxChars) {
  const AirlineBrand *b = airlineBrandFor(n);
  fitCopy(dst, dstN, b ? b->disp : (adsbdbName ? adsbdbName : ""), maxChars);
}

// ---------- Page 3: TRAFFIC BRIEF (candidate pickers) ----------
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
    if (planes[i].distKm > BRIEF_EMERGENCY_RANGE_KM) continue;
    if (planes[i].distKm < bestD) { bestD = planes[i].distKm; best = i; }
  }
  return best;
}

int bestHelicopterNearIdx(const int used[], int usedN) {
  int best = -1;
  float bestD = 9999.0f;
  for (int i = 0; i < planeCount; i++) {
    if (idxAlreadyUsed(i, used, usedN)) continue;
    if (!isHelicopter(planes[i]) || planes[i].distKm > BRIEF_HELI_RANGE_KM) continue;
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

const char *heliRoleName(const Aircraft &p) {
  // Best-effort helicopter role from the callsign (rev1.1.23). German
  // operator conventions around Munich:
  //   CHX / CHRIST / HUMMEL = "Christoph" HEMS air-rescue fleet (ADAC/DRF)
  //   SAR                   = military search-and-rescue
  //   PIROL / BPO           = federal police (Bundespolizei)
  //   POL / EDEL / SPREE / LIBEL = state police squadrons
  // Unknown prefixes return nullptr — typically corporate/private machines.
  if (!strncmp(p.flight, "CHX", 3) || !strncmp(p.flight, "CHRIST", 6) ||
      !strncmp(p.flight, "HUMMEL", 6) || !strncmp(p.flight, "AMB", 3))
    return "RESCUE";
  if (!strncmp(p.flight, "SAR", 3)) return "SAR";
  if (!strncmp(p.flight, "POL", 3) || !strncmp(p.flight, "PIROL", 5) ||
      !strncmp(p.flight, "BPO", 3) || !strncmp(p.flight, "EDEL", 4) ||
      !strncmp(p.flight, "SPREE", 5) || !strncmp(p.flight, "LIBEL", 5))
    return "POLICE";
  if (!strncmp(p.flight, "GAM", 3)) return "MILITARY";
  return nullptr;
}

void specialReasonLine(const Aircraft &p, char *dst, size_t dstN) {
  const char *label;
  int score = coolScore(p, &label);
  float mucD = distanceToMucKm(p);
  float eta = etaMinForDistance(mucD, p);

  if (isEmergencySquawk(p)) {
    snprintf(dst, dstN, "SQK %s %.1fkm %s", p.sqk, (double)p.distKm, compass8(p.bearingDeg));
  } else if (isHelicopter(p)) {
    // rev1.1.23: lead with the operator role (police/rescue/...) when the
    // callsign gives it away; otherwise "CIVIL" + type.
    const char *role = heliRoleName(p);
    if (role)
      snprintf(dst, dstN, "%s %s %.1fkm %s", role, p.typ[0] ? p.typ : "",
               (double)p.distKm, compass8(p.bearingDeg));
    else
      snprintf(dst, dstN, "CIVIL %s %.1fkm %s", p.typ[0] ? p.typ : "HELI",
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
  // the tag carries the category instead (documented in the header/changelog).
  printFit(36, y, tag, 1, tagColor, 34);
  if (idx < 0) {
    printFit(78, y, "none nearby", 1, C_DIM, 96);
    return;
  }
  Aircraft &p = planes[idx];
  printFit(78, y, p.flight, 1, C_WHITE, 50);
  printFit(130, y, p.typ[0] ? p.typ : "----", 1, C_CYAN, 30);

  char pos[16];
  snprintf(pos, sizeof(pos), "%.0fkm %s", (double)p.distKm, compass8(p.bearingDeg));
  printFit(162, y, pos, 1, C_DIM, 46);

  // Second band: reason for the special/emergency rows, otherwise the best
  // context we have — route first, airline second, full type name last.
  char detail[34];
  if (withReason) {
    specialReasonLine(p, detail, sizeof(detail));
  } else {
    RouteCache *route = cachedRouteFor(p.flight);
    if (route && route->codes[0])        fitCopy(detail, sizeof(detail), route->codes, 22);
    else if (route && route->airline[0]) airlineShortName(p, route->airline, detail, sizeof(detail), 20);
    else                                 fitCopy(detail, sizeof(detail), typeName(p.typ), 22);
  }
  printFit(78, y + 11, detail, 1, C_DIM, 126);
}

uint16_t activityColor(int count) {
  // Regional busyness colour for the bottom of TRAFFIC BRIEF. The thresholds
  // are deliberately broad because this is a quick mood gauge, not a precise
  // ATC capacity metric.
  if (count < 18) return C_GREEN;
  if (count < 36) return C_AMBER;
  return C_RED;
}

void drawActivityCounter() {
  // Counts valid aircraft in the same 60 km regional fetch used by the radar
  // and MUC map. This gives the user a quick "how busy is the sky" number
  // without drawing every contact as a full label on the main scope.
  // rev1.1.24: label moved ABOVE the number — at y=224 the round panel chord
  // is so narrow that "100km around you" was clipped to "100km a". The count
  // is centred on HOME_LAT/LON (your location), not on the airport.
  uint16_t c = activityColor(activityRadiusCount);
  tft.drawFastHLine(78, 186, 84, C_GRIDDIM);
  char label[22];
  snprintf(label, sizeof(label), "%.0fkm around you", (double)ACTIVITY_RADIUS_KM);
  centerText(label, 192, 1, C_DIM);
  char n[8];
  snprintf(n, sizeof(n), "%d", activityRadiusCount);
  centerText(n, 204, 2, c);
}

void summaryStatic() {
  // Bezel-less by design: this is a compact traffic brief, not a map.
  tft.fillScreen(C_BG);
  pageDots();
}

void summaryDynamic() {
  // TRAFFIC BRIEF: the four quick things a Munich plane-watcher asks first:
  // what's closest, what's special, any helicopters, and any emergencies.
  // MUC ARR/DEP moved to the airport map because the brief footer was too
  // tight on the real round display.
  tft.fillScreen(C_BG);
  // Hardware-photo follow-up: the MUC row was removed from this page because
  // it crowded the bottom chord. MUC ARR/DEP now lives only on the airport
  // map where the extra route/status context has room to breathe.
  // rev1.2.0: header matches the MUC map page grammar — big size-3 amber
  // word with a small sub-line under it. rev1.2.1: lowered a few px so the
  // header doesn't hug the top edge (there was slack above the NEAR row).
  centerText("TRFC", 24, 3, C_AMBER);
  centerText("nearby", 54, 1, C_DIM);

  int used[4] = {-1, -1, -1, -1};
  int usedN = 0;

  int nearIdx = nearestAirborne();
  drawBriefRow(78, "NEAR", C_WHITE, nearIdx, false);
  if (nearIdx >= 0) used[usedN++] = nearIdx;

  int coolIdx = bestCoolNearIdx(used, usedN);
  if (coolIdx < 0) coolIdx = bestCoolMucIdx(used, usedN);
  drawBriefRow(108, "COOL", C_AMBER, coolIdx, true);
  if (coolIdx >= 0) used[usedN++] = coolIdx;

  // rev1.1.24: withReason=true so the detail line runs through
  // specialReasonLine() and reports the operator role (POLICE/RESCUE/...)
  // instead of repeating the bare type code.
  int heliIdx = bestHelicopterNearIdx(used, usedN);
  drawBriefRow(138, "HELI", C_CYAN, heliIdx, true);
  if (heliIdx >= 0) used[usedN++] = heliIdx;

  int emgIdx = bestEmergencyNearIdx();
  drawBriefRow(162, "EMG", C_RED, emgIdx, true);

  drawActivityCounter();
}

// ---------- Pages 4+5: NEAREST / COOLEST tracking (shared renderer) ----------
void trackStatic() {
  tft.fillScreen(C_BG);
  // rev1.1.23: data-card pages get the plain quiet ring (no tick "grid"),
  // same treatment as the MUC map page.
  tft.drawCircle(120, 120, 116, C_GRIDDIM);
  pageDots();
}

// Previous bearing-blip position so the 1 s live tick can erase it cleanly.
int trackBlipPrevX = -1000, trackBlipPrevY = -1000;
int trackBlipPrevTX = -1000, trackBlipPrevTY = -1000;

void trackDrawBearingBlip(const Aircraft &n) {
  // rev1.2.0: small RED rim blip pointing along the bearing from YOUR
  // position to the tracked aircraft - step outside, face this way, look up.
  // This one remains a compact dot/tick because it has a custom 1 s erase
  // path and stays inside r=104 so clearInnerChrome() covers it fully.
  if (trackBlipPrevX > -900) {
    tft.drawLine(trackBlipPrevX, trackBlipPrevY,
                 trackBlipPrevTX, trackBlipPrevTY, C_BG);
    tft.fillCircle(trackBlipPrevX, trackBlipPrevY, 3, C_BG);
  }
  float b = deg2rad(n.bearingDeg);
  int bx = 120 + (int)(sinf(b) * 98);
  int by = 120 - (int)(cosf(b) * 98);
  int tx = 120 + (int)(sinf(b) * 104);
  int ty = 120 - (int)(cosf(b) * 104);
  tft.drawLine(bx, by, tx, ty, C_RED);
  tft.fillCircle(bx, by, 2, C_RED);
  trackBlipPrevX = bx;  trackBlipPrevY = by;
  trackBlipPrevTX = tx; trackBlipPrevTY = ty;
}

void trackDrawLiveRows(const Aircraft &n) {
  // The card's dynamic half: big numbers + trend + CPA. Painted on its own
  // background pads so the 1 s live tick can refresh it WITHOUT clearing the
  // whole card (a full clear every second flickered badly on the GC9A01).
  tft.fillRect(28, 120, 184, 64, C_BG);
  tft.fillRect(48, 186, 144, 22, C_BG);

  char row[26];
  snprintf(row, sizeof(row), "ALT %dm", (int)(n.altFt * 0.3048f));
  centerText(row, 122, 2, C_WHITE);
  snprintf(row, sizeof(row), "SPD %dkm/h", (int)(n.gsKt * 1.852f));
  centerText(row, 144, 2, C_WHITE);
  // Live distance from the dead-reckoned position, so DST and CPA both move
  // between fetches instead of jumping every 8 s.
  float liveE, liveN;
  liveAircraftOffsetKm(n, &liveE, &liveN);
  float liveDist = sqrtf(liveE * liveE + liveN * liveN);
  snprintf(row, sizeof(row), "DST %.1fkm %s", (double)liveDist, compass8(n.bearingDeg));
  centerText(row, 166, 2, C_GREEN);

  // rev1.2.1: fully metric — climb rate in metres per minute (feed delivers
  // ft/min; * 0.3048). Thresholds stay in fpm internally (300 fpm ~ 90 mpm).
  const char *trend = n.vrFpm > 300 ? "climbing" : (n.vrFpm < -300 ? "descending" : "level");
  snprintf(row, sizeof(row), "%s %+dmpm", trend, (int)(n.vrFpm * 0.3048f));
  centerText(row, 188, 1, C_DIM);

  // Closest point of approach to YOU — answers the desk-radar question:
  // "will it pass over me, and when?"
  float spdKms = n.gsKt * 1.852f / 3600.0f;
  float vx = sinf(deg2rad(n.trackDeg)) * spdKms;
  float vy = cosf(deg2rad(n.trackDeg)) * spdKms;
  char cpa[36];
  float v2 = vx * vx + vy * vy;
  if (v2 > 1e-8f) {
    float tSec = -(liveE * vx + liveN * vy) / v2;
    if (tSec > 0 && tSec < 3600) {
      float ce = liveE + vx * tSec, cn = liveN + vy * tSec;
      float cd = sqrtf(ce * ce + cn * cn);
      snprintf(cpa, sizeof(cpa), "CPA %.1fkm in %dm%02ds",
               (double)cd, (int)tSec / 60, (int)tSec % 60);
    } else {
      snprintf(cpa, sizeof(cpa), "moving away");
    }
  } else {
    snprintf(cpa, sizeof(cpa), "stationary");
  }
  centerText(cpa, 200, 1, C_CYAN);
}

void trackLiveTick() {
  // rev1.2.0: 1 s partial refresh for the tracking pages — the CPA countdown
  // and DST now tick live between fetches (user: "doesn't feel live").
  int idx = (page == PAGE_NEAREST) ? nearestAirborne() : coolestIdx();
  if (idx < 0) return;
  trackDrawLiveRows(planes[idx]);
  trackDrawBearingBlip(planes[idx]);
}

void trackPageDraw(int idx, const char *headerTag, uint16_t tagColor,
                   const char *whyLine, uint16_t whyColor) {
  // rev1.2.5: tag colour and why-line colour are separate — the page tag is
  // quiet grey, while the MUC-relation line carries the traffic grammar:
  // GREEN = arriving at Munich, RED = departing Munich.
  // rev1.1.23: pure DATA CARD, per user request ("remove the tracking and
  // grid altogether, just give me data in nice format, centered and as big
  // as possible"). No mini-map, no rings, no trail — one aircraft, an
  // identity block on top and big centred numbers below.
  clearInnerChrome(false);
  if (idx < 0) {
    centerText(headerTag, 96, 1, C_DIM);
    centerText("no contacts", 116, 2, C_DIM);
    return;
  }
  Aircraft &n = planes[idx];
  RouteCache *route = cachedRouteFor(n.flight);

  // --- identity block ---
  // rev1.1.24: whole card pushed down and spread out ("bring down the top
  // label so it fills the screen better"), and the route now also shows the
  // FULL city names from adsbdb under the code pair.
  centerText(headerTag, 22, 1, tagColor);
  // Callsign as the hero: size 3 when it fits the top chord, size 2 as a
  // fallback for long callsigns.
  centerText(n.flight, 36, (int)strlen(n.flight) <= 7 ? 3 : 2, C_AMBER);
  char who[34];
  if (route && route->airline[0]) {
    // rev1.2.6: use the tidy short carrier name (<=10 chars) rather than the
    // raw adsbdb legal name, so "type + airline" always fits the round card's
    // ~28-char chord at y=64 (14 type + 2 gap + 10 airline) instead of being
    // clipped mid-word.
    char al[16];
    airlineShortName(n, route->airline, al, sizeof(al), 10);
    snprintf(who, sizeof(who), "%.14s  %s", typeName(n.typ), al);
  } else {
    fitCopy(who, sizeof(who), typeName(n.typ), 26);
  }
  // Airline brand colour when the carrier is known (Lufthansa yellow,
  // Ryanair navy, ...).
  centerText(who, 64, 1,
             (route && route->airline[0]) ? airlineAccentColor(n, route->airline) : C_DIM);
  // rev1.2.0: nothing renders as an EMPTY row any more — unknown route shows
  // explicit placeholders instead of a hole in the card.
  if (route && route->codes[0])  centerText(route->codes, 76, 2, C_CYAN);
  else                           centerText("-- > --", 76, 2, C_GRIDDIM);
  if (route && route->cities[0]) centerText(route->cities, 94, 1, C_DIM);
  else                           centerText("route n/a", 94, 1, C_GRIDDIM);
  if (whyLine && whyLine[0]) centerText(whyLine, 106, 1, whyColor);

  tft.drawFastHLine(72, 116, 96, C_GRIDDIM);

  trackDrawLiveRows(n);
  trackDrawBearingBlip(n);
}

void trackDynamic() {
  // rev1.2.1: the NEAREST card gets the same MUC-relation line the COOLEST
  // card has — if the closest aircraft is a Munich arrival/departure, say so
  // right under the route lines.
  int idx = nearestAirborne();
  const char *why = NULL;
  uint16_t whyColor = C_DIM;
  if (idx >= 0) {
    if (likelyArrival(planes[idx]))        { why = "Munich arrival";   whyColor = C_GREEN; }
    else if (likelyDeparture(planes[idx])) { why = "departing Munich"; whyColor = C_RED; }
  }
  // rev1.2.5: quiet grey page tag (was cyan).
  trackPageDraw(idx, "NEAREST", C_DIM, why, whyColor);
}

void coolestStatic() {
  tft.fillScreen(C_BG);
  tft.drawCircle(120, 120, 116, C_GRIDDIM);   // plain ring, no tick grid
  pageDots();
}

void buildCoolWhyLine(const Aircraft &p, const char *label, char *dst, size_t dstN) {
  // Human-readable reason for the COOLEST page. This is intentionally less
  // coded than the brief-page status strings: the page title already says it
  // is special, so this line answers "why should I care right now?" without
  // wasting pixels on a repeated "WHY:" prefix.
  if (isEmergencySquawk(p)) {
    snprintf(dst, dstN, "emergency squawk");
  } else if (isHelicopter(p)) {
    snprintf(dst, dstN, "helicopter nearby");
  } else if (p.ground && distanceToMucKm(p) < 6.0f) {
    snprintf(dst, dstN, "on field at MUC");
  } else if (likelyArrival(p)) {
    snprintf(dst, dstN, "Munich arrival");
  } else if (likelyDeparture(p)) {
    snprintf(dst, dstN, "departing Munich");
  } else {
    char shortLabel[24];
    fitCopy(shortLabel, sizeof(shortLabel), label, 20);
    snprintf(dst, dstN, "%s", shortLabel);
  }
}

void coolestDynamic() {
  // COOLEST tracking page: same live map as NEAREST, but following the
  // top-scored aircraft, with a compact "why it is cool + MUC status" tag
  // in the header slot (colour signals urgency: red = emergency/very rare).
  int ci = coolestIdx();
  if (ci < 0) {
    trackPageDraw(-1, "COOLEST", C_DIM, NULL, C_DIM);
    return;
  }
  Aircraft &p = planes[ci];
  const char *label;
  int score = coolScore(p, &label);

  // rev1.2.5: quiet grey page tag; the why-line carries the colour —
  // GREEN Munich arrival, RED departure, red also for emergencies/very rare,
  // amber for other notable reasons.
  uint16_t whyColor = score >= 85 ? C_RED : (score >= 50 ? C_AMBER : C_DIM);
  if (likelyArrival(p))            whyColor = C_GREEN;
  else if (likelyDeparture(p))     whyColor = C_RED;
  if (isEmergencySquawk(p))        whyColor = C_RED;

  char why[34];
  buildCoolWhyLine(p, label, why, sizeof(why));
  trackPageDraw(ci, "COOLEST", C_DIM, why, whyColor);
}

// ---------- Page 2: MUC AIRPORT ----------
#define MUC_SCALE MUC_MAP_SCALE // px per km (configurable; rev1.0 default 16
                                // zooms out so approach/departure flows fit)
#define MUC_MAP_CY 132          // centred after removing the old ARR/DEP rows
#define MUC_MAP_R  84           // larger live airport map inside the round bezel

void mucToScreen(float e, float n, int *x, int *y) {
  // Runway/apron schematic projection. This deliberately uses MUC_SCALE, not
  // the 60 km traffic scale, so the runway pair remains large and readable at
  // the centre of the page.
  *x = 120 + (int)((e - mucE) * MUC_SCALE);
  *y = MUC_MAP_CY - (int)((n - mucN) * MUC_SCALE);
}

void mucTrafficToScreen(float e, float n, int *x, int *y) {
  // Aircraft projection for the airport page. rev1.1.18: the on-screen scope
  // is a fixed MUC_MAP_VIEW_KM (default 20 km) — tight enough that the final
  // approach and initial climb are actually visible at useful size. Traffic
  // between the view radius and MUC_MAP_EDGE_KM clamps to blue edge markers;
  // beyond that it is not drawn at all (detection logic still sees 60 km).
  float scale = (float)MUC_MAP_R / MUC_MAP_VIEW_KM;
  *x = 120 + (int)((e - mucE) * scale);
  *y = MUC_MAP_CY - (int)((n - mucN) * scale);
}

void drawRunway(float ce, float cn) {   // centre offset from MUC ref (km)
  float u_e = sinf(deg2rad(RWY_HDG)), u_n = cosf(deg2rad(RWY_HDG));
  int x0, y0, x1, y1;
  mucToScreen(mucE + ce - u_e * RWY_LEN/2, mucN + cn - u_n * RWY_LEN/2, &x0, &y0);
  mucToScreen(mucE + ce + u_e * RWY_LEN/2, mucN + cn + u_n * RWY_LEN/2, &x1, &y1);

  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  // rev1.1.19: slab half-width trimmed 5.0 → 3.5 px — at the 22 px/km scale
  // the 5 px slabs looked more like aprons than runways on the real panel.
  float px = len > 0 ? -dy / len * 3.5f : 0;
  float py = len > 0 ?  dx / len * 3.5f : 3.5f;
  // Fill the runway as a precise dark rectangular slab with contrasting edges.
  // Solid rectangular slab with light edges and NO centreline (user note:
  // "fill the black lines and make it more rectangular" — the dim centreline
  // read as an unfilled gap in the slab on the real panel).
  tft.fillTriangle(x0 + px, y0 + py, x1 + px, y1 + py, x1 - px, y1 - py, C_RUNWAY_FILL);
  tft.fillTriangle(x0 + px, y0 + py, x1 - px, y1 - py, x0 - px, y0 - py, C_RUNWAY_FILL);
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
  return d < MUC_MAP_RANGE_KM && p.altFt < 24000 &&
         (p.vrFpm < -120 || routeDestIsMuc(p)) && isMovingTowardMuc(p);
}

bool likelyDeparture(const Aircraft &p) {
  if (p.ground) return false;
  float d = distanceToMucKm(p);
  return d < MUC_MAP_RANGE_KM && p.altFt < 24000 &&
         (p.vrFpm > 120 || routeOriginIsMuc(p)) && isMovingAwayFromMuc(p);
}

float mucTrafficScore(const Aircraft &p, bool arrival) {
  float d = distanceToMucKm(p);
  if (arrival && (p.ground || d > MUC_MAP_RANGE_KM)) return 99999.0f;
  if (!arrival && d > MUC_MAP_RANGE_KM) return 99999.0f;

  float toMuc = fmodf(atan2f(mucE - p.eastKm, mucN - p.northKm) * 57.29578f + 360.0f, 360.0f);
  float fromMuc = fmodf(atan2f(p.eastKm - mucE, p.northKm - mucN) * 57.29578f + 360.0f, 360.0f);
  float eta = etaMinForDistance(d, p);
  if (eta < 0) eta = 60.0f;

  if (arrival) {
    float score = eta * 3.0f + angleDiffDeg(p.trackDeg, toMuc) * 0.18f;
    score += (p.altFt > 14000 ? p.altFt - 14000 : 0) / 1200.0f;
    if (!routeDestIsMuc(p) && !isMovingTowardMuc(p)) score += 80.0f;
    if (p.vrFpm > -100) score += 8.0f;       // not descending yet
    if (likelyArrival(p)) score -= 16.0f;    // obvious final/inbound
    return score;
  }

  // rev1.1.24: PARKED aircraft can no longer win the "next departure" slot.
  // Device photos kept showing motionless GA machines (ELEKT15 etc.) labelled
  // "DEP GND" for minutes. Ground candidates now need actual taxi movement.
  if (p.ground && p.gsKt < 5) return 99999.0f;

  float score = d * 2.2f + angleDiffDeg(p.trackDeg, fromMuc) * 0.18f;
  score += (p.altFt > 12000 ? p.altFt - 12000 : 0) / 1200.0f;
  if (!p.ground && !routeOriginIsMuc(p) && !isMovingAwayFromMuc(p)) score += 80.0f;
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

// rev1.2.2: LATCHED next-ARR/DEP. The raw pickers re-score every redraw, so
// the highlighted aircraft could hop between candidates every couple of
// seconds — you never got to WATCH one. Once picked, an arrival is now
// followed all the way from the edge until it is on the ground at MUC (or
// leaves/turns away), and a departure is followed from the runway until it
// clears the 20 km view. Only then is the next candidate chosen.
char latchedArrCs[10] = "";
char latchedDepCs[10] = "";

int aircraftIdxByCallsign(const char *cs) {
  if (!cs[0]) return -1;
  for (int i = 0; i < planeCount; i++)
    if (strcmp(planes[i].flight, cs) == 0) return i;
  return -1;
}

int latchedMucIdx(bool arrival) {
  char *latch = arrival ? latchedArrCs : latchedDepCs;
  int idx = aircraftIdxByCallsign(latch);
  if (idx >= 0) {
    Aircraft &p = planes[idx];
    float d = distanceToMucKm(p);
    bool keep;
    if (arrival)
      // Follow until touchdown (the payoff!), or it leaves/turns away.
      keep = !p.ground && d <= MUC_MAP_EDGE_KM &&
             (isMovingTowardMuc(p) || d < 8.0f);
    else
      // Follow from taxi/liftoff until it slips past the 20 km view edge.
      keep = d <= MUC_MAP_VIEW_KM * 1.1f;
    if (keep) return idx;
  }
  idx = arrival ? nextArrivalIdx() : nextDepartureIdx();
  copyStr(latch, 10, idx >= 0 ? planes[idx].flight : "");
  return idx;
}

void airportStatic() {
  tft.fillScreen(C_BG);
  // rev1.1.20: plain bezel circle only on this page — the twelve small tick
  // lines ("NSEW grid lines") added nothing over an airport schematic and
  // crowded the new header stack. The home radar keeps its full ticked bezel.
  // rev1.1.22: full-brightness amber was overpowering on the panel; the ring
  // is now a super-faint dark amber, just enough to close the composition.
  tft.drawCircle(120, 120, 116, tft.color565(72, 52, 8));
  pageDots();
}

void drawAirportCounterNumber(int cx, int y, int value, uint16_t color) {
  // Top strip deliberately uses colour-only numbers: green arrivals, red
  // departures, purple ground traffic. The previous "ARR 3 DEP 1 GND 9" text
  // read like an in-page legend and wasted the narrow top chord.
  char s[8];
  snprintf(s, sizeof(s), "%d", value);
  tft.setTextSize(1);
  tft.setTextColor(color);
  tft.setCursor(cx - (int)strlen(s) * 3, y);
  tft.print(s);
}

bool mucMapPointForAircraft(int idx, int *x, int *y) {
  // Recompute the same screen position used by airportDynamic() so labels can
  // be drawn last, on top of all aircraft symbols. Keeping labels last avoids
  // later traffic strokes overwriting the callsign/status text.
  if (idx < 0 || idx >= planeCount) return false;
  Aircraft &p = planes[idx];
  float liveE, liveN;
  liveAircraftOffsetKm(p, &liveE, &liveN);
  float dMucE = liveE - mucE, dMucN = liveN - mucN;
  float dMuc  = sqrtf(dMucE * dMucE + dMucN * dMucN);

  if (dMuc > MUC_MAP_EDGE_KM) return false;   // outside the drawn scope
  mucTrafficToScreen(liveE, liveN, x, y);
  int dx = *x - 120, dy = *y - MUC_MAP_CY;
  if (dx * dx + dy * dy > MUC_MAP_R * MUC_MAP_R) {
    if (dMuc < 0.01f) return false;
    *x = 120 + (int)(dMucE / dMuc * MUC_MAP_R);
    *y = MUC_MAP_CY - (int)(dMucN / dMuc * MUC_MAP_R);
  }
  return true;
}

// rev1.1.19: mucMapDashedTrack() (dashed course extrapolation for next
// ARR/DEP) was removed. On the device it implied a runway assignment we
// cannot actually predict from a straight-line track — arrivals fly curved
// approach patterns, so the dashes often pointed somewhere the aircraft was
// never going. The short 8 px heading stub on every airborne symbol stays.

void drawMucMapNextLabel(int idx, bool arrival, int avoidX, int avoidY,
                         int *outX, int *outY) {
  // The map no longer has separate ARR/DEP rows. Instead, the next arrival
  // and departure get two-line labels attached directly to their aircraft
  // markers, mirroring the main radar's "label belongs to this arrow" grammar.
  int x, y;
  if (!mucMapPointForAircraft(idx, &x, &y)) return;
  Aircraft &p = planes[idx];
  uint16_t color = arrival ? C_GREEN : C_RED;

  float dM = distanceToMucKm(p);
  char status[18];
  if (arrival) {
    float eta = etaMinForDistance(dM, p);
    if (eta >= 0) snprintf(status, sizeof(status), "ARR %.0fm", (double)eta);
    else          snprintf(status, sizeof(status), "ARR %.0fkm", (double)dM);
  } else if (p.ground) {
    snprintf(status, sizeof(status), "DEP GND");
  } else {
    snprintf(status, sizeof(status), "DEP %.0fkm", (double)dM);
  }

  // Where the arrival is coming FROM / where the departure is headed TO,
  // from the cached adsbdb route. Empty when the route is unknown.
  char via[10] = "";
  char code[6];
  if (arrival) {
    if (routeOriginCode(p, code, sizeof(code)) && strcmp(code, "MUC") != 0)
      snprintf(via, sizeof(via), "fr %s", code);
  } else {
    if (routeDestinationCode(p, code, sizeof(code)) && strcmp(code, "MUC") != 0)
      snprintf(via, sizeof(via), "to %s", code);
  }

  // rev1.2.2 layout (final): THREE tight lines, each on its own background
  // pad so the label reads over runways, traffic and anything else —
  //   line 1: CALLSIGN + fr/to route hint      (white + dim)
  //   line 2: ARR eta / DEP distance           (green / red)
  //   line 3: altitude + ground speed          (dim, metric)
  char extra[16];
  if (p.ground)
    snprintf(extra, sizeof(extra), "taxi %dkm/h", (int)(p.gsKt * 1.852f));
  else
    snprintf(extra, sizeof(extra), "%dm %dkm/h",
             (int)(p.altFt * 0.3048f), (int)(p.gsKt * 1.852f));

  int w1 = (int)strlen(p.flight) * 6;
  int w2 = (int)strlen(status) * 6;
  int w3 = via[0] ? (int)strlen(via) * 6 : 0;
  int w4 = (int)strlen(extra) * 6;
  int line1W = w1 + (w3 ? 6 + w3 : 0);
  int total = line1W;
  if (w2 > total) total = w2;
  if (w4 > total) total = w4;

  int lx = x - total / 2;
  if (lx < 10) lx = 10;
  if (lx + total > 230) lx = 230 - total;
  int ly = y - 36;
  if (ly < 64) ly = y + 10;
  if (ly > 178) ly = 178;

  // The two labels (next ARR + next DEP) must never overlap each other:
  // vertical dodge when the three-line boxes would intersect.
  if (avoidX > -900 &&
      lx < avoidX + 110 && lx + total > avoidX &&
      ly < avoidY + 32 && ly + 32 > avoidY) {
    if (avoidY + 34 <= 178) ly = avoidY + 34;
    else                    ly = avoidY - 34;
  }

  int anchorY = (ly < y) ? ly + 29 : ly - 1;
  tft.drawLine(x, y, lx + total / 2, anchorY, C_DIM);
  // rev1.2.3: full background box + thin faint green frame, matching the
  // home radar's label style (user: "will look cleaner").
  tft.fillRect(lx - 2, ly - 2, total + 4, 33, C_BG);
  tft.drawRect(lx - 2, ly - 2, total + 4, 33, C_GRIDDIM);
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE);
  tft.setCursor(lx, ly);
  tft.print(p.flight);
  if (w3) {
    tft.setTextColor(C_DIM);
    tft.setCursor(lx + w1 + 6, ly);
    tft.print(via);
  }
  tft.setTextColor(color);
  tft.setCursor(lx, ly + 10);
  tft.print(status);
  tft.setTextColor(C_DIM);
  tft.setCursor(lx, ly + 20);
  tft.print(extra);
  if (outX) *outX = lx;
  if (outY) *outY = ly;
}

void airportDynamic() {
  clearInnerChrome(false);

  // Count traffic first; the header stack (MUC / AIRPORT / temp+wind /
  // counters) is painted at the END of this function so it always sits on
  // top of the map graphics.
  int arrCnt = 0, depCnt = 0, gndCnt = 0;
  for (int i = 0; i < planeCount; i++) {
    float dM = distanceToMucKm(planes[i]);
    if (planes[i].ground && dM < 6.0f) gndCnt++;
    else if (likelyArrival(planes[i])) arrCnt++;
    else if (likelyDeparture(planes[i])) depCnt++;
  }
  int nextArr = latchedMucIdx(true);
  int nextDep = latchedMucIdx(false);

  // rev1.1.19: no inner view ring. On the device it read as a second cage
  // inside the bezel and meant nothing (the bezel already IS the edge of the
  // view). The scope distance is announced by a single south km label
  // instead, matching the home radar's south-label convention.

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

  // Ground-dot de-clump list (rev1.1.20): at 22 px/km a dozen parked aircraft
  // on the ~2 km apron all land within a few pixels and rendered as one
  // purple blob. Dots closer than 4 px to an already drawn one are skipped;
  // the GND counter still counts every aircraft.
  int ggx[24], ggy[24], ggn = 0;

  for (int i = planeCount - 1; i >= 0; i--) {
    Aircraft &p = planes[i];
    float liveE, liveN;
    liveAircraftOffsetKm(p, &liveE, &liveN);
    float dMucE = liveE - mucE, dMucN = liveN - mucN;
    float dMuc  = sqrtf(dMucE * dMucE + dMucN * dMucN);

    if (dMuc > MUC_MAP_EDGE_KM) continue;   // beyond 40 km: not drawn at all

    // rev1.2.1: aircraft ON THE FIELD are projected at the runway-schematic
    // scale (22 px/km) instead of the 20 km traffic scale (4.2 px/km). At
    // traffic scale the whole 4 km airport collapsed into a ~17 px blob, so
    // "GND 16" never looked like 16 aircraft. At schematic scale the parked
    // and taxiing traffic spreads realistically along the runways and apron.
    bool onField = p.ground && dMuc < 3.5f;   // 3.5 km * 22 px/km stays inside the map
    int x, y;
    bool clampedToEdge = false;
    if (onField) {
      // rev1.2.3: clamp ground traffic into the runway corridor. ADS-B
      // position scatter (and taxiways slightly outside our simplified
      // schematic) could paint a parked aircraft "in the grass"; projecting
      // into runway-axis coordinates and clamping keeps every ground dot on
      // or between the two runway slabs.
      float fe = liveE - mucE, fn = liveN - mucN;
      float ue = sinf(deg2rad(RWY_HDG)), un = cosf(deg2rad(RWY_HDG));
      float along = fe * ue + fn * un;
      float cross = -fe * un + fn * ue;
      float alongMax = RWY_LEN / 2 + RWY_STAGGER;
      if (along >  alongMax) along =  alongMax;
      if (along < -alongMax) along = -alongMax;
      if (cross >  RWY_SEP)  cross =  RWY_SEP;
      if (cross < -RWY_SEP)  cross = -RWY_SEP;
      mucToScreen(mucE + along * ue - cross * un,
                  mucN + along * un + cross * ue, &x, &y);
    } else {
      mucTrafficToScreen(liveE, liveN, &x, &y);
      int dx = x - 120, dy = y - MUC_MAP_CY;
      if (dx * dx + dy * dy > MUC_MAP_R * MUC_MAP_R) {
        if (dMuc < 0.01f) continue;
        x = 120 + (int)(dMucE / dMuc * MUC_MAP_R);
        y = MUC_MAP_CY - (int)(dMucN / dMuc * MUC_MAP_R);
        clampedToEdge = true;
      }
    }

    // Visual grammar of the operations map (rev1.1.18 scope: 20 km on-map,
    // 20-40 km at the edge, >40 km hidden):
    //   blue rim arrow = 20-40 km out, pinned to the edge (home-radar style)
    //   purple circle = stationary/ground aircraft on the field
    //   green symbol  = arriving traffic
    //   red symbol    = departing traffic
    //   star/rotor    = notable aircraft / helicopter, regardless of flow
    //   amber symbol   = other aircraft around MUC
    // Only next ARR/DEP get text labels (no track extrapolation — rev1.1.19).
    bool onRwy = onMucRunway(liveE, liveN) && (p.ground || p.altFt < 900);
    if (clampedToEdge) {
      // Edge contact (20-40 km out): same faint rim DOT as the home radar rim,
      // pushed to the SAME screen rim radius as page 1 (r~102 around the screen
      // centre) instead of hugging the smaller map circle. One guard: markers
      // whose spot falls on the counter / weather strip at the top are skipped
      // — instrument numbers always win.
      float ux = dMucE / dMuc, uy = dMucN / dMuc;
      int cx2 = 120 + (int)(ux * 99);
      int cy2 = 120 - (int)(uy * 99);
      bool onCounters   = (cy2 < 66  && cx2 > 56 && cx2 < 184);
      bool onSouthLabel = (cy2 > 200 && cx2 > 88 && cx2 < 152);
      if (!onCounters && !onSouthLabel) {
        // rev1.2.9: small faint dark-blue dot (was a line arrow), shared with
        // the home radar rim for one consistent, low-distraction edge marker.
        float edgeBearing = fmodf(atan2f(ux, uy) * 57.29578f + 360.0f, 360.0f);
        rimBearingDotAt(120, 120, edgeBearing, 102, C_EDGE_DOT);
      }
    } else if (onRwy) {
      // Aircraft actually on the runway: yellow marker, sized by wake class
      // (a departing A380 visibly dwarfs a CRJ) — per the user's sketch note.
      trafficSymbol(p, x, y, isHeavyType(p) ? 11 : 8, C_AMBER, true);
    } else if (p.ground) {
      // Smaller dot with a 1 px background ring so touching dots still read
      // as separate aircraft, plus the de-clump skip above.
      bool crowded = false;
      for (int g = 0; g < ggn; g++) {
        int gdx = x - ggx[g], gdy = y - ggy[g];
        // rev1.2.1: threshold tightened 4 px -> 3 px — with the schematic
        // projection the dots have real spacing, so fewer need skipping.
        if (gdx * gdx + gdy * gdy < 9) { crowded = true; break; }
      }
      if (!crowded) {
        if (ggn < 24) { ggx[ggn] = x; ggy[ggn] = y; ggn++; }
        tft.drawCircle(x, y, 3, C_BG);
        tft.fillCircle(x, y, 2, C_PURPLE);
      }
    } else {
      uint16_t c = trafficColor(p, false);
      if (i == nextArr || likelyArrival(p)) c = C_GREEN;
      if (i == nextDep || likelyDeparture(p)) c = C_RED;
      bool nextUp = (i == nextArr || i == nextDep);
      // Short projected path line (dead-reckoned direction of travel), then
      // the symbol on top — subtle, but it makes flows readable at a glance.
      int hx = x + (int)(sinf(deg2rad(p.trackDeg)) * 8);
      int hy = y - (int)(cosf(deg2rad(p.trackDeg)) * 8);
      tft.drawLine(x, y, hx, hy, c);
      trafficSymbol(p, x, y, nextUp ? 8 : 5, c, false);
    }
  }

  // Draw the two operation labels last so they sit cleanly above map traffic:
  // one latest likely arrival, one latest likely departure, no separate text
  // board stealing runway space.
  int firstLx = -999, firstLy = -999;
  drawMucMapNextLabel(nextArr, true, -999, -999, &firstLx, &firstLy);
  drawMucMapNextLabel(nextDep, false, firstLx, firstLy, nullptr, nullptr);

  // Header stack + counters are painted LAST, on background pads — instrument
  // text must always win over map graphics (same priority rule as the home
  // radar). rev1.1.20 header, top to bottom exactly as sketched by the user:
  //   MUC          (white)
  //   AIRPORT      (grey, super small)
  //   temp + wind  (super small)
  //   DEP GND ARR  (colour counters)
  // rev1.1.22: header simplified — big size-3 amber "MUC", no AIRPORT word
  // (redundant: the runways below say it), then temp+wind, then counters.
  // rev1.1.23: whole stack lowered a few px — it sat glued to the top edge.
  tft.fillRect(90, 13, 60, 27, C_BG);
  centerText("MUC", 14, 3, C_AMBER);
  if (mucWx.ok) {
    char wxLine[24];
    snprintf(wxLine, sizeof(wxLine), "%s %s", mucWx.temp, mucWx.wind);
    tft.fillRect(72, 41, 96, 10, C_BG);
    centerText(wxLine, 42, 1, C_DIM);
  }
  tft.fillRect(64, 49, 24, 11, C_BG);
  tft.fillRect(108, 49, 24, 11, C_BG);
  tft.fillRect(152, 49, 24, 11, C_BG);
  // Same left/right grammar as the home radar: DEP (red) on the west side,
  // ARR (green) on the east side, ground count between them.
  drawAirportCounterNumber(76, 52, depCnt, C_RED);
  drawAirportCounterNumber(120, 52, gndCnt, C_PURPLE);
  drawAirportCounterNumber(164, 52, arrCnt, C_GREEN);

  // South scope indicator (rev1.1.19), copied from the home radar's grammar:
  // dim-green km figure + small south pointer at the bottom of the panel, so
  // the page announces its own visible range (how far the traffic mapping
  // reaches before contacts become blue edge markers).
  char scopeLbl[8];
  snprintf(scopeLbl, sizeof(scopeLbl), "%dkm", (int)MUC_MAP_VIEW_KM);
  int slw = (int)strlen(scopeLbl) * 6;
  int slx = 120 - slw / 2;
  tft.fillRect(slx - 2, 205, slw + 4, 22, C_BG);
  tft.setTextSize(1);
  tft.setTextColor(C_SCOPE_TEXT);
  tft.setCursor(slx, 208);
  tft.print(scopeLbl);
  tft.drawTriangle(120, 225, 114, 217, 126, 217, C_SCOPE_ARROW);
  tft.drawLine(120, 225, 120, 218, C_SCOPE_ARROW);
}

// The MUC OPS board page was removed in rev1.0: next-ARR/next-DEP now lives
// directly on the MUC MAP page, where the runway/route context makes it useful
// without spending a whole extra carousel page on repeated information.

// ---------- Page 6: MUC WEATHER ----------
void dataRow(int y, const char *label, const char *value, uint16_t valueColor) {
  // Shared airport-data row. Labels and values have hard pixel boxes because
  // external APIs can surprise us with long strings; clipping is better than
  // letting one value push through the next row on the round display. The
  // column cluster is deliberately centered around x=120; the first rev1.0
  // photos showed the weather list leaning too far left.
  const char *icon = ".";
  uint16_t iconColor = C_GRID;
  if (strcmp(label, "WIND") == 0)       { icon = "~"; iconColor = C_CYAN; }
  else if (strcmp(label, "VIS") == 0)   { icon = "o"; iconColor = C_GREEN; }
  else if (strcmp(label, "CLOUD") == 0) { icon = "="; iconColor = C_DIM; }
  else if (strcmp(label, "TEMP") == 0)  { icon = "T"; iconColor = C_AMBER; }
  else if (strcmp(label, "QNH") == 0)   { icon = "Q"; iconColor = C_BLUE; }
  else if (strcmp(label, "WX") == 0)    { icon = "*"; iconColor = C_PURPLE; }
  else if (strcmp(label, "RWY") == 0)   { icon = ">"; iconColor = C_WHITE; }

  // rev1.2.4: whole cluster nudged right (54/68/124 -> 62/76/130) so the
  // icon+label+value block sits visually centred on the round panel.
  tft.fillCircle(62, y + 4, 5, iconColor);
  tft.setTextSize(1);
  tft.setTextColor(C_BG);
  tft.setCursor(59, y + 1);
  tft.print(icon);
  printFit(76, y, label, 1, C_GRID, 46);
  printFit(130, y, value, 1, valueColor, 76);
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
  // rev1.2.4: header lowered off the top edge + small grey "airport"
  // sub-line, matching the header grammar of the other pages.
  // rev1.2.9: sub-line brightened from dim green to a light grey so it reads
  // more clearly under the MUC header.
  centerText("MUC", 26, 2, C_AMBER);
  centerText("airport", 46, 1, C_GREY);
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
  dataRow(62,  "WIND",  mucWx.wind,  C_WHITE);
  dataRow(79,  "VIS",   mucWx.vis,   C_WHITE);
  dataRow(96,  "CLOUD", mucWx.cloud, C_WHITE);
  dataRow(113, "TEMP",  mucWx.temp,  C_WHITE);
  dataRow(130, "QNH",   mucWx.qnh,   C_WHITE);
  dataRow(147, "WX",    mucWx.wx,
          strcmp(mucWx.wx, "DRY") == 0 ? C_WHITE : C_AMBER);

  char rwy[10];
  activeRunway(rwy, sizeof(rwy));
  dataRow(164, "RWY", rwy[0] ? rwy : "--", rwy[0] ? C_CYAN : C_DIM);

  // Field status as a proper pill (see flightCategory for why this is
  // METAR-derived rather than a fake delay percentage).
  uint16_t catColor;
  const char *cat = flightCategory(&catColor);
  tft.fillRoundRect(60, 178, 120, 17, 8, C_STATUS_FILL);
  tft.drawRoundRect(60, 178, 120, 17, 8, catColor);
  tft.fillCircle(72, 186, 4, catColor);
  centerText(cat, 183, 1, catColor);

  // Raw METAR tail: the unfiltered truth for anyone who reads METAR, clipped
  // into two fixed rows so odd station remarks can never break the layout.
  char raw1[26], raw2[26];
  fitCopy(raw1, sizeof(raw1), mucWx.raw, 22);
  fitCopy(raw2, sizeof(raw2), mucWx.raw + strlen(raw1), 22);
  printFit(58, 198, raw1, 1, C_DIM, 124);
  printFit(58, 210, raw2, 1, C_DIM, 124);
}

// rev1.1.23: the MUC TAF, MUC FIELD and OPEN SKY pages (and their shared
// sourceRow renderer) were removed on user request — the carousel is back
// to six live pages.

// ---------- Page management ----------
bool skipPage(uint8_t p) {
  if ((p == PAGE_NEAREST || p == PAGE_COOLEST) && nearestAirborne() < 0) return true;
  if (p == PAGE_SUMMARY && planeCount == 0) return true;
  return false;   // WEATHER is always meaningful because it can fetch METAR.
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
  }
}

bool pageButtonDownNow() {
  // Fast raw read used before starting network work. The normal debounced state
  // machine still owns page changes; this helper only keeps the firmware from
  // entering a blocking HTTP request while the user is actively pressing MODE.
  return digitalRead(PAGE_BUTTON_PIN) == LOW;
}

bool buttonWantsAttention() {
  // rev1.1.24: yield not only to a physically held button but also to an
  // already-latched (completed) press waiting in btnEvent. Previously a press
  // that landed DURING one route fetch didn't stop the NEXT route fetch from
  // starting, so the page turn could lag several extra seconds.
  return pageButtonDownNow() || btnEvent != 0;
}

void prefetchRoutes() {
  // Route lookups are the only "optional" network calls; they always yield
  // to a pending button press so the UI never feels stuck behind HTTP.
  if (buttonWantsAttention()) return;
  int na = nearestAirborne();
  if ((page == PAGE_RADAR || page == PAGE_NEAREST || page == PAGE_SUMMARY) && na >= 0)
    fetchRoute(planes[na].flight, ROUTE_SLOT_NEAREST);
  if (page == PAGE_MUC_MAP) {
    // The airport map owns next arrival/departure rows; fetching those routes
    // only on that page avoids blocking the button for data the brief no
    // longer displays.
    if (buttonWantsAttention()) return;
    int a = latchedMucIdx(true);    // rev1.2.2: fetch routes for the LATCHED
    if (a >= 0) fetchRoute(planes[a].flight, ROUTE_SLOT_MUC_BASE + 0);
    if (buttonWantsAttention()) return;
    int d = latchedMucIdx(false);   // pair, so labels + routes stay in sync
    if (d >= 0) fetchRoute(planes[d].flight, ROUTE_SLOT_MUC_BASE + 1);
    // rev1.1.21: the map header shows temp+wind, so this page keeps the
    // METAR cache warm too (no-op while the cache is fresh). Previously it
    // only filled after visiting the weather page.
    if (buttonWantsAttention()) return;
    fetchMucWeather();
  }
  if (page == PAGE_COOLEST || page == PAGE_SUMMARY) {
    if (buttonWantsAttention()) return;
    int ci = coolestIdx();
    if (ci >= 0) fetchRoute(planes[ci].flight, ROUTE_SLOT_COOLEST);
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
  if (page == PAGE_RADAR) resetRadarZoomCycle();
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
  Serial.println("Plane Radar rev1.2.8 booting...");
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
  // rev1.1.22: warm the METAR cache once at boot so the MUC map header shows
  // temp+wind from the first visit instead of "coming later".
  fetchMucWeather();
  // rev1.2.2: the device ALWAYS wakes up on the home radar.
  page = PAGE_RADAR;
  lastPageSwitch = millis();
  setAdsbFetchStatus("waiting");
  resetRadarZoomCycle();
  drawPageFull();
}

void loop() {
  static uint8_t wifiLostStreak = 0;
  if (WiFi.status() != WL_CONNECTED) {
    splash("wifi lost", "reconnecting...", C_ORANGE);
    WiFi.reconnect();
    delay(4000);
    if (++wifiLostStreak >= 8) {
      Serial.println("MAINT: WiFi stayed down -> restart");
      delay(100);
      ESP.restart();
    }
    drawPageFull();
    return;
  }
  wifiLostStreak = 0;

  uint32_t now = millis();
  handlePageButton(now);

  // ---- 24/7 self-maintenance (rev1.2.4) -----------------------------------
  // This device is meant to sit on a desk and run forever. Three layers keep
  // it healthy without any human attention:
  //  1. HEAP GUARD: repeated HTTPS payloads slowly fragment the heap; if free
  //     heap ever drops below a safe floor, restart (boot takes ~10 s and
  //     always lands on the home radar page).
  //  2. NETWORK GUARD: many consecutive fetch failures mean the Wi-Fi stack
  //     is wedged even though it claims to be connected -> re-associate;
  //     if that still doesn't help -> restart.
  //  3. DAILY PREVENTIVE RESTART: after 24 h uptime, restart at a quiet
  //     moment (not while the user is holding a page or pressing the button).
  //     This resets any slow leak long before it could ever matter.
  if (ESP.getFreeHeap() < 24000) {
    Serial.println("MAINT: heap low -> restart");
    delay(100);
    ESP.restart();
  }
  if (failCount == 15) {
    Serial.println("MAINT: repeated fetch failures -> WiFi re-association");
    WiFi.disconnect();
    delay(200);
    WiFi.reconnect();
    failCount++;             // bump so this branch fires only once per streak
  } else if (failCount >= 40) {
    Serial.println("MAINT: network dead -> restart");
    delay(100);
    ESP.restart();
  }
  if (millis() > 86400000UL && !manualPageHold && !pageButtonDownNow()) {
    Serial.println("MAINT: daily preventive restart");
    delay(100);
    ESP.restart();
  }

  // rev1.1.24: after a failed fetch (HTTP -11 read timeout etc.) retry after
  // 3 s instead of waiting out the full interval — the "retry ..." pill was
  // sitting on screen for whole cycles even when the API had already
  // recovered. Successful fetches keep the polite 8 s cadence.
  uint32_t fetchInterval = failCount ? 3000UL : FETCH_INTERVAL_MS;
  bool fetchDue = (now - lastFetchAttempt >= fetchInterval || lastFetchAttempt == 0);
  // rev1.2.9: while a 429 rate-limit backoff is active, do not fetch at all —
  // the API asked us to ease off, so we wait out adsbBackoffUntil and keep the
  // last good traffic on screen. (int32 subtraction is millis()-rollover safe.)
  if ((int32_t)(now - adsbBackoffUntil) < 0) fetchDue = false;
  bool buttonBusy = pageButtonDownNow() || btnEvent != 0;
  if (fetchDue && !buttonBusy) {
    bool first = !haveAircraftData;
    lastFetchAttempt = now;
    if (fetchPlanes()) {
      lastFetch = millis();
      haveAircraftData = true;
      failCount = 0;
      handlePageButton(millis());
      // rev1.1.22: prefetch also runs on manually held pages. The old
      // !manualPageHold guard meant a page you short-pressed to stay on
      // NEVER fetched its routes or METAR — exactly where you're looking at
      // it the longest. prefetchRoutes() still yields to live button presses.
      if (!pageButtonDownNow()) prefetchRoutes();

      // Dynamic special-traffic interrupt: an emergency squawk or very rare
      // airframe (score >= ALERT_SCORE) yanks the carousel to COOLEST TRACK so the
      // event is visible immediately — an A380 on final or a 7700 squawk should
      // not wait behind the weather page. The callsign latch makes this
      // one-shot per aircraft, so the same jet cannot hold the screen hostage.
      int alertIdx = coolestIdx();
      const char *alertLabel;
      // rev1.2.0: emergencies jump to the TRAFFIC page (its EMG row carries
      // squawk + position context); rare airframes still go to COOLEST.
      uint8_t alertPage = (alertIdx >= 0 && isEmergencySquawk(planes[alertIdx]))
                              ? PAGE_SUMMARY : PAGE_COOLEST;
      // rev1.2.2: no alert jumps in the first 45 s after power-on.
      if (!manualPageHold && alertIdx >= 0 && page != alertPage &&
          millis() > 45000UL &&
          coolScore(planes[alertIdx], &alertLabel) >= ALERT_SCORE &&
          strcmp(planes[alertIdx].flight, lastAlertCs) != 0) {
        copyStr(lastAlertCs, sizeof(lastAlertCs), planes[alertIdx].flight);
        Serial.printf("ALERT: %s -> page %d (%s)\n",
                      planes[alertIdx].flight, alertPage, alertLabel);
        page = alertPage;
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
    } else {
      // rev1.2.9: a 429 rate-limit is throttling, not a fault — don't bump
      // failCount (which drives the WiFi re-associate / restart guards) and
      // don't repaint an alarming pill; just keep the last good traffic until
      // the backoff expires.
      if (!adsbRateLimited) {
        failCount++;
        Serial.printf("ADS-B retry %d: %s\n", failCount, adsbFetchStatus);
        handlePageButton(millis());
        // Redraw even on failed fetches so the user sees a live radar shell plus
        // the latest retry reason. This is especially important before the first
        // successful packet, when otherwise the display can look stuck at boot.
        if (first || page == PAGE_RADAR) drawPageFull();
        else drawPageUpdate();
      } else if (first) {
        // Very first packet was rate-limited: still show the live radar shell
        // so the device never looks frozen at boot.
        drawPageFull();
      }
    }
  }

  bool radarZoomChanged = updateRadarZoomCycle(now);
  if (radarZoomChanged) {
    drawPageFull();
    lastRadarStep = now;
  }

  if (page == PAGE_RADAR && !radarZoomChanged && now - lastRadarStep >= RADAR_STEP_MS) {
    lastRadarStep = now;
    radarStep();
  }

  if (page == PAGE_MUC_MAP && now - lastLiveStep >= AIRPORT_STEP_MS) {
    lastLiveStep = now;
    airportDynamic();
  }

  if ((page == PAGE_NEAREST || page == PAGE_COOLEST) &&
      now - lastLiveStep >= TRACK_STEP_MS) {
    lastLiveStep = now;
    trackLiveTick();   // partial redraw: numbers + rim blip only, no flicker
  }

  if (AUTO_SCROLL_ENABLED && haveAircraftData && !manualPageHold &&
      now - lastPageSwitch >= pageDur[page]) {
    advancePage(false);
  }

  delay(40);
}
