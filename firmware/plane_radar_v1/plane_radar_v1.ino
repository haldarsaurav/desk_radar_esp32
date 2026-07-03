/*
  plane_radar_v1.ino — rev1.1.22 — Munich aviation desk radar (ESP32-C3 + GC9A01).

  Hardware: ESP32-C3 Super Mini + GC9A01 1.28" round display (240x240)
  Wiring:   see docs/display_wiring_guide.html and docs/device_manual.html

  Data:     https://api.adsb.lol          (aircraft positions, free, no key)
            https://api.adsbdb.com        (airline + route lookup by callsign)
            https://aviationweather.gov   (MUC METAR/TAF weather, free, no key)
            https://ourairports.com/data  (baked-in MUC field/runway facts)
            https://opensky-network.org   (optional regional category check)
  Setup:    copy config.example.h to config.h, enter Wi-Fi name+password and
            your location. Everything else has sensible defaults (see the
            CONFIG DEFAULTS block below) so the device is gift-ready: a new
            owner only edits Wi-Fi + lat/lon + home airport.

  =============================== rev1.1.22 ===============================
  MUC MAP header final pass + prefetch bug fix:
    - Bezel ring: full amber was overpowering -> super-faint dark amber.
    - "AIRPORT" word removed; "MUC" grown to size-3 amber.
    - BUG FIX: prefetchRoutes() never ran on manually held pages
      (!manualPageHold guard), so a page you short-pressed to watch never
      loaded its ARR/DEP routes ("fr/to" line) or METAR (temp+wind line).
      Prefetch now runs on held pages too, still yielding to button presses.
    - METAR is fetched once at boot, so the map header has temp+wind
      immediately instead of after the first weather-page visit.
  =========================================================================

  =============================== rev1.1.21 ===============================
  MUC MAP page, photo feedback on the rev1.1.20 header:
    - "MUC" header is now amber and size-2 (same style as the weather page
      header); AIRPORT sits a little lower beneath it. Counters moved down
      with the taller stack (y 36 -> 45).
    - Bezel circle recoloured green -> amber to match the MUC header.
    - The two ARR/DEP labels never overlap each other any more: the second
      label dodges vertically (or horizontally as a fallback) when its box
      would intersect the first.
    - METAR is now also kept warm from this page (was: weather page only),
      so the header temp+wind line actually fills in.
  =========================================================================

  =============================== rev1.1.20 ===============================
  MUC MAP page, header + ground traffic pass:
    - Header stack added at the top, per the user's sketch: "MUC" (white),
      "AIRPORT" (grey super small), temp + wind (super small), then the
      DEP/GND/ARR counters. The whole stack paints last on background pads.
    - The separate temp/wind line below the counters is gone — it lives in
      the header now.
    - Bezel tick marks removed on this page only ("NSEW grid small lines add
      nothing") — plain circle remains; the home radar keeps its full bezel.
    - Ground traffic de-clumped: smaller dots (r=2) with a 1 px background
      separation ring, and dots within 4 px of an already drawn one are
      skipped. The GND counter still counts every aircraft.
    - Next ARR/DEP labels gained a third line: "fr XXX" (arrival origin) /
      "to XXX" (departure destination) from the cached adsbdb route.
  =========================================================================

  =============================== rev1.1.19 ===============================
  MUC MAP page, device-photo feedback pass:
    - Removed the dashed 20 km view ring + NE km figure: on the panel it read
      as a meaningless second cage inside the bezel. The bezel ring/ticks
      remain the only edge chrome.
    - Blue edge markers pushed to the true screen rim (r=99..105 around the
      panel centre, same radius as the home radar) instead of hugging the
      inner map circle — but they skip the counter/weather strip and the
      south scope label, so instrument numbers are never overwritten.
    - Removed the dashed track extrapolation for next ARR/DEP: a straight
      line can't predict which runway an aircraft will take (approaches are
      curved), so it suggested false certainty. Heading stubs + labels stay.
    - South scope indicator added (home-radar grammar): dim-green "20km" +
      small south pointer, announcing this page's visible range.
    - Runway slabs thinner: half-width 5.0 -> 3.5 px.
    - ARR/DEP/GND counters moved down (y 18 -> 26) so the digits no longer
      clip the bezel ticks; temp/wind strip follows (y 32 -> 40).
  =========================================================================

  =============================== rev1.1.18 ===============================
  MUC MAP page polish (page 2 locked-in pass):
    - Fixed on-screen scope: 20 km to scale (MUC_MAP_VIEW_KM), 20-40 km as
      blue edge markers (MUC_MAP_EDGE_KM), beyond 40 km hidden. Replaces the
      old 60 km squeeze where approach traffic crawled at 1.4 px/km. The 60 km
      radius is still used for ARR/DEP *detection* logic only.
    - Runway schematic enlarged (MUC_MAP_SCALE 16 -> 22) so the slabs dominate.
    - Dashed 20 km view ring + km figure, matching the home radar's outer ring.
    - Edge contacts use the rev1.1.17 dot-with-outward-tick marker in the same
      solid blue as the home radar (the faint squares looked broken).
    - Next ARR/DEP keep their attached labels AND get a long dashed track
      extrapolation, so their route into/out of the field reads instantly.
    - ARR/DEP/GND counters + temp/wind strip now paint LAST on background
      pads (they were occasionally overwritten by label leader lines), and
      follow the home-radar left/right grammar: DEP west, ARR east.
  =========================================================================

  =============================== rev1.1.17 ===============================
  Out-of-range rim marker, final form:
    - The blue rim marker is now a small filled circle (r=2) with a short
      radial tick pointing outward along the bearing-from-you. Every arrow
      and triangle variant tried at this size (rev1.1.6/.9/.12/.15/.16)
      rasterised as an irregular blob on the GC9A01; a filled circle is the
      only primitive that stays clean at 4-5 px, and the tick preserves the
      "more traffic out this way" direction cue.
  =========================================================================

  =============================== rev1.1.16 ===============================
  Home-radar label persistence pass:
    - The home radar still prefers one label per visible range band, using the
      emergency/helicopter/cool/MUC/nearest priority ladder.
    - If a preferred label cannot be drawn because its box would collide or sit
      in a protected instrument zone, the renderer now tries more candidate
      aircraft until it gets up to three labels whenever the screen allows.
    - Airborne aircraft without a reported altitude can still receive a label;
      their altitude row shows "--m" instead of silently disqualifying them.
  =========================================================================

  =============================== rev1.1.15 ===============================
  Home-radar south marker + edge aircraft tuning:
    - The south 20/40/60 km scope label is now dim green instead of white, with
      a tighter text pad so it feels smaller and more integrated with the grid.
    - The south pointer is darker, keeping the compass cue without competing
      with aircraft labels.
    - Out-of-range contacts are now solid blue mini aircraft arrows at the rim,
      which reads cleaner than faint line-only chevrons through the round lens.
  =========================================================================

  =============================== rev1.1.14 ===============================
  Home-radar south scope marker pass:
    - Inner range labels stay on the east-side ruler at their actual grid
      radii, but the active full-scope value (20/40/60 km) moved to the south
      compass point.
    - Added a subtle classic-style south pointer below that scope value, so
      the range limit reads like an instrument marker instead of a loose label.
  =========================================================================

  =============================== rev1.1.13 ===============================
  Home-radar weighted zoom window pass:
    - The home radar no longer spends equal time at 20/40/60 km. It now uses a
      5-minute zoom window: 50% on the most appropriate scope, 30% on the next
      best, and 20% on the remaining context scope.
    - Busy skies prefer 20 km, then 40 km, then 60 km. Moderate skies prefer
      40 km, then 20 km, then 60 km. Quiet skies prefer 60 km, then 40 km,
      then 20 km.
    - This keeps the page alive and varied, but gives readable labels more
      dwell time when Munich traffic is dense.
  =========================================================================

  =============================== rev1.1.12 ===============================
  Home-radar edge bearing arrow pass:
    - Out-of-range home-radar contacts are now tiny faint blue bearing arrows
      instead of single radial ticks. The short shaft + arrowhead makes the
      direction easier to understand while staying quieter than full dots.
    - Slightly strengthened the dim blue used for edge contacts so the arrows
      survive the GC9A01 glass/lens without turning into a bright halo.
  =========================================================================

  =============================== rev1.1.11 ===============================
  Home-radar truthful range ruler pass:
    - Range numbers now sit on the actual east-side grid radii instead of as a
      floating caption. The labels still form a straight ruler line, but 5,
      10, 20, 40, and 60 km physically correspond to their rings.
    - The centre receiver dot is smaller and drawn in a dim blue-white so it
      marks "you" without shouting over nearby aircraft.
  =========================================================================

  =============================== rev1.1.10 ===============================
  Home-radar zoom tour pass:
    - Added a configurable home-radar zoom cycle: close 20 km, medium 40 km,
      and wide 60 km, changing every 10 seconds by default.
    - The zoom cycle takes priority over density auto-zoom while enabled, so
      the first page deliberately "breathes" through the three scopes instead
      of jumping based only on traffic count.
    - Returning to the radar page restarts the tour at close range, which keeps
      labels readable first and then opens the view out to regional context.
  =========================================================================

  =============================== rev1.1.9 ================================
  Home-radar scale readability pass:
    - Replaced the separate diagonal range numbers with one straight compact
      range strip under the top traffic count. The strip always shows the
      current auto-zoom rings: 5/10/20 km, 10/20/40 km, or 20/40/60 km.
    - Changed out-of-range blue contacts from rim circles to tiny faint radial
      bearing ticks. They still show "traffic exists in this bearing" but no
      longer look like a cloud of blue dots around the bezel.
    - Added a small shield for the range strip so live traffic cannot draw
      through the text.
  =========================================================================

  =============================== rev1.1.8 ================================
  Standalone boot/fetch resilience pass:
    - The device no longer sits on a full-screen "fetching aircraft data"
      splash while waiting for the first ADS-B packet. After Wi-Fi connects it
      draws the normal radar page immediately and keeps fetching in the
      background.
    - ADS-B failures now store a compact on-screen reason such as HTTP status,
      begin failure, or JSON failure. The radar page shows a small retry label
      instead of looking frozen.
    - Split "last fetch attempt" from "last successful aircraft data" so a
      failed request does not make stale aircraft positions look fresh.
    - Auto-scroll waits for the first successful ADS-B response, keeping the
      startup diagnostic visible on the main radar page.
  =========================================================================

  =============================== rev1.1.7 ================================
  Final home-radar readability pass:
    - Home radar keeps fetching the wider 60 km ADS-B region, but the displayed
      scope auto-zooms to 20 / 40 / 60 km based on airborne traffic density.
      Busy office-hour skies zoom in for readable labels; quiet night skies
      zoom back out.
    - Removed the MUC reference dot from the main radar. The receiver position
      is now a small white centre dot drawn before aircraft, so planes can pass
      over it instead of being hidden by it.
    - Out-of-range contacts reverted from rim arrows to faint blue circles.
    - Aircraft label stacks now get tiny background pads so callsigns/type/
      altitude are easier to read over the grid.
  =========================================================================

  =============================== rev1.1.6 ================================
  Final home-radar polish:
    - Home radar counters now share one drawing helper so total, DEP, and ARR
      always use the same size, with DEP/ARR keeping their red/green colours.
    - Range labels are redrawn on top of live traffic; the outer 60 km label
      moved to the top-right arc so the grid/ring no longer cuts through it.
    - The user's receiver position and Munich Airport are both shown as small
      blue reference dots on the main scope.
    - Out-of-range contacts are now faint blue bearing arrows instead of
      squares, so the rim marker points along the aircraft's bearing from you.
  =========================================================================

  =============================== rev1.1.5 ================================
  Three new source pages, with existing pages left alone:
    - Added MUC TAF, a slow-refresh AviationWeather forecast page that shows
      valid period, wind, visibility, cloud, weather, change groups, and raw
      TAF text clipped into safe rows.
    - Added MUC FIELD, a no-network OurAirports-derived reference page for
      EDDM elevation, runway dimensions/surface, and key tower/ATIS channels.
    - Added OPEN SKY, an optional OpenSky regional category-count page that
      cross-checks the Munich 60 km picture by source/category rather than
      replacing the current ADS-B live radar feed.
  =========================================================================

  =============================== rev1.1.4 ================================
  60 km scope + MUC map traffic pass:
    - Main radar now uses a 60 km scope by default, so the three range rings
      mean 20 / 40 / 60 km.
    - Regional activity and ADS-B fetch radius now default to 60 km, reducing
      the amount of distracting edge traffic.
    - Out-of-range edge squares use a much fainter blue than normal traffic.
    - Home radar label selection is banded: one preferred label from 0-20 km,
      one from 20-40 km, and one from 40-60 km, with fallback fill if a band
      is empty but enough aircraft exist.
    - MUC MAP draws all stored aircraft around the airport on a 60 km airport
      scope, keeps the runway schematic centred and readable, removes the old
      pale range circle, and labels only the next arrival and next departure.
  =========================================================================

  =============================== rev1.1.3 ================================
  Dark-theme MUC map + traffic brief cleanup:
    - Returned the firmware to one dark glass-cockpit palette.
    - MUC MAP counters now use the same small instrument text size as the main
      radar page.
    - Removed the separate ARR/DEP text rows from MUC MAP; the next arrival
      and next departure now get their details attached directly to their live
      aircraft symbols on the map.
    - TRAFFIC BRIEF adds a bottom 100 km activity counter whose colour changes
      with local busyness.
    - COOLEST TRACK reason text no longer starts with "WHY:".
  =========================================================================

  =============================== rev1.1.1 ================================
  Fresh layout polish pass after re-reading the current UI:
    - Page order changed to HOME RADAR > MUC MAP > TRAFFIC BRIEF >
      NEAREST TRACK > COOLEST TRACK > WEATHER, so the airport view is the
      second page after the main radar.
    - Home radar north pointer made smaller, and the 20 km outer range ring is
      dimmer so traffic remains the visual priority.
    - TRAFFIC BRIEF was vertically re-centred with a two-line "Traffic" /
      "nearby" heading, and the helicopter/emergency searches now use a wider
      configurable range than the main 20 km radar scope.
    - Tracking pages draw a cleaner projected course line using current
      dead-reckoned position, not just the last raw ADS-B point.
    - COOLEST TRACK keeps the normal title and adds a clearer "WHY ..." line
      instead of replacing the title with a cryptic reason string.
    - MUC MAP top chrome is now just colour-coded numbers, not label text; the
      next ARR/DEP detail moved to the top of the page, the bottom separator
      was removed, and the map circle was lowered so temp/wind is not cut.
  =========================================================================

  ================================ rev1.1 =================================
  Hardware-photo polish pass:
    - Carousel trimmed to six useful live pages:
      HOME RADAR > TRAFFIC BRIEF > NEAREST TRACK > COOLEST TRACK >
      MUC MAP > WEATHER. The standalone LEGEND page was removed because the
      real display photos showed it cost useful dwell time and did not need to
      be a live page.
    - TRAFFIC BRIEF now stops after the nearest / coolest / helicopter /
      emergency rows. The old bottom ARR/DEP mini-board was too cramped on the
      lower chord of the round glass.
    - MUC MAP now owns next ARR/DEP detail with callsign, ETA/distance/GND,
      and route/type fallback, so airport data appears where the runway context
      makes it useful.
    - Home radar counters returned to small instrument text. The old compass
      letter pads were removed because their background clears cut visible gaps
      into the grid/crosshair.
    - Main radar range remains the calmer 20 km scope. Aircraft outside that
      range stay as blue edge-clamped squares.
    - Weather rows were re-centred around the middle of the display.
    - Route cache was trimmed to the active nearest/coolest/MUC ARR/DEP slots.
  =========================================================================

  ================================ rev1.0 =================================
  This revision restructured the firmware from the experimental v13 build
  into the final product layout:
    - Page order changed to: HOME RADAR > TRAFFIC BRIEF > NEAREST TRACK >
      COOLEST TRACK > MUC MAP > WEATHER. The standalone LEGEND page from the
      first rev1.0 pass was removed from the carousel after hardware photos
      showed it was less useful than giving live pages more dwell time.
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
    - New TRAFFIC BRIEF page: compact entries for nearest / coolest /
      helicopter / emergency with graceful fallbacks. MUC next ARR/DEP moved
      fully to the airport map, where there is enough spatial context.
    - Nearest + Coolest tracking pages share one renderer with a properly
      visible mini-map (the old full-screen rings were mostly hidden behind
      the text bands, which looked like a broken grid).
    - MUC MAP zoomed out (configurable), header replaced by live counters +
      temperature/wind, next ARR/DEP rows at the bottom, short projected
      path lines on moving traffic.
    - Weather page centred with a plain "MUC" header (the old long header
      clipped on the round glass).
    - The colour/symbol grammar is documented here and in the markdown notes,
      so the live UI can stay clutter-free.
    - Page button rewritten around a CHANGE interrupt: presses are latched
      even while the firmware is inside a blocking HTTP fetch, which was the
      root cause of "button sometimes stops working". Long press still
      resumes the auto carousel. Pin + auto-scroll are configurable.
    - Every page now redraws from cached data after every successful fetch,
      so no page goes stale while the user stays on it.
  =========================================================================

  Colour/symbol grammar:
    red=DEP MUC, green=ARR MUC, amber=other traffic, faint blue tick=home
    radar out-of-range bearing, faint blue square=map out-of-scope contact,
    star=special aircraft, rotor symbol=helicopter, purple circle=on ground,
    red "!" dot=emergency squawk.

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
#define BRIEF_HELI_RANGE_KM 45.0f    // rotorcraft search can be wider than close-in labels
#endif

#ifndef BRIEF_EMERGENCY_RANGE_KM
#define BRIEF_EMERGENCY_RANGE_KM 60.0f // matches the current regional fetch
#endif

#ifndef MUC_MAP_SCALE
#define MUC_MAP_SCALE 22.0f          // runway-schematic px-per-km (rev1.1.18
#endif                               // enlarged from 16 so the slabs dominate)

#ifndef OPENSKY_ENABLED
#define OPENSKY_ENABLED 1            // optional regional cross-check page
#endif

#ifndef OPENSKY_RANGE_KM
#define OPENSKY_RANGE_KM 60.0f       // OpenSky bbox around MUC for page 9
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
#define C_EDGE_BLUE    tft.color565(38, 128, 255)   // solid home-radar rim aircraft
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
// Page order: the original six live pages stay first and keep their behaviour.
// rev1.1.5 only appends three slow/reference pages after MUC WX, so the user's
// existing radar/airport/tracking rhythm remains intact.
#define PAGE_RADAR    0
#define PAGE_MUC_MAP  1
#define PAGE_SUMMARY  2
#define PAGE_NEAREST  3
#define PAGE_COOLEST  4
#define PAGE_MUC_WX   5
#define PAGE_MUC_TAF  6
#define PAGE_MUC_INFO 7
#define PAGE_OPENSKY  8
#define PAGE_COUNT    9
uint8_t  page = PAGE_RADAR;
// First six dwell times are unchanged. New source/reference pages rotate
// quickly because they are useful context, not the main live spotting view.
const uint32_t pageDur[PAGE_COUNT] = {
  26000, 17000, 16000, 22000, 22000, 13000, 13000, 15000, 13000
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

struct MucTaf {
  bool ok = false;
  char raw[160]   = "";
  char valid[14]  = "--";
  char wind[16]   = "--";
  char vis[12]    = "--";
  char cloud[14]  = "--";
  char wx[12]     = "DRY";
  char change[16] = "NONE";
  uint32_t lastFetch = 0;
};
MucTaf mucTaf;

// TAFs normally update far more slowly than aircraft positions. Keeping this
// refresh at 15 minutes makes the forecast page useful without hammering the
// free AviationWeather service or blocking page-button interaction often.
#define MUC_TAF_REFRESH_MS 900000UL

struct OpenSkyStats {
  bool ok = false;
  int total = 0;
  int adsb = 0;
  int mlat = 0;
  int flarm = 0;
  int ground = 0;
  int heavy = 0;
  int rotor = 0;
  int stale = 0;
  uint32_t apiTime = 0;
  uint32_t lastFetch = 0;
};
OpenSkyStats openSky;

// OpenSky is a secondary cross-check, not the main live data source. It is
// queried slowly and only when the page is visible so its credit/rate limits
// cannot disturb the primary adsb.lol radar loop.
#define OPENSKY_REFRESH_MS 300000UL
#define OPENSKY_HTTP_TIMEOUT_MS 5200

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

  int radiusNm = (int)(ADSB_FETCH_RADIUS_KM / 1.852f) + 4;
  char url[120];
  snprintf(url, sizeof(url), "https://api.adsb.lol/v2/point/%.4f/%.4f/%d",
           (double)HOME_LAT, (double)HOME_LON, radiusNm);
  if (!http.begin(client, url)) {
    setAdsbFetchStatus("begin fail");
    Serial.println("ADS-B begin failed.");
    return false;
  }
  int code = http.GET();
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

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  payload = String();
  if (err) {
    setAdsbFetchStatus("JSON fail");
    Serial.printf("ADS-B JSON error: %s\n", err.c_str());
    return false;
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

  // Feed both path histories from the fresh fetch: the NEAREST page follows
  // the closest airborne aircraft, the COOLEST page follows the top-scored
  // one. trailPush() resets automatically when the tracked callsign changes,
  // which is exactly the "auto-switch to the new nearest aircraft" behaviour
  // the tracking pages need.
  int na = nearestAirborne();
  if (na >= 0) trailPush(trailNear, planes[na]);
  int ci = coolestIdx();
  if (ci >= 0) trailPush(trailCool, planes[ci]);

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

bool tafValidPeriodToken(const char *tok) {
  // TAF valid periods look like 0306/0412: day+hour slash day+hour. We keep
  // the raw compact token because it fits the display and avoids pretending to
  // know the user's preferred date format/time zone on a tiny MCU.
  return strlen(tok) >= 9 &&
         asciiDigit(tok[0]) && asciiDigit(tok[1]) &&
         asciiDigit(tok[2]) && asciiDigit(tok[3]) &&
         tok[4] == '/' &&
         asciiDigit(tok[5]) && asciiDigit(tok[6]) &&
         asciiDigit(tok[7]) && asciiDigit(tok[8]);
}

void parseTafSummary() {
  copyStr(mucTaf.valid,  sizeof(mucTaf.valid),  "--");
  copyStr(mucTaf.wind,   sizeof(mucTaf.wind),   "--");
  copyStr(mucTaf.vis,    sizeof(mucTaf.vis),    "--");
  copyStr(mucTaf.cloud,  sizeof(mucTaf.cloud),  "--");
  copyStr(mucTaf.wx,     sizeof(mucTaf.wx),     "DRY");
  copyStr(mucTaf.change, sizeof(mucTaf.change), "NONE");

  int becmg = 0, tempo = 0, prob = 0;
  char work[192];
  copyStr(work, sizeof(work), mucTaf.raw);
  char *tok = strtok(work, " \r\n");
  while (tok) {
    if (tafValidPeriodToken(tok)) {
      copyStr(mucTaf.valid, sizeof(mucTaf.valid), tok);
    } else if (strcmp(tok, "BECMG") == 0) {
      becmg++;
    } else if (strcmp(tok, "TEMPO") == 0) {
      tempo++;
    } else if (strncmp(tok, "PROB", 4) == 0) {
      prob++;
    } else if (metarWindToken(tok) && mucTaf.wind[0] == '-') {
      copyStr(mucTaf.wind, sizeof(mucTaf.wind), tok);
    } else if (strcmp(tok, "CAVOK") == 0 && mucTaf.vis[0] == '-') {
      copyStr(mucTaf.vis, sizeof(mucTaf.vis), "CAVOK");
      copyStr(mucTaf.cloud, sizeof(mucTaf.cloud), "CLEAR");
    } else if (metarVisibilityToken(tok) && mucTaf.vis[0] == '-') {
      if (strcmp(tok, "9999") == 0) copyStr(mucTaf.vis, sizeof(mucTaf.vis), "10km+");
      else snprintf(mucTaf.vis, sizeof(mucTaf.vis), "%.1fkm", atoi(tok) / 1000.0);
    } else if (metarCloudToken(tok) && mucTaf.cloud[0] == '-') {
      int hFt = (tok[3]-'0') * 10000 + (tok[4]-'0') * 1000 + (tok[5]-'0') * 100;
      snprintf(mucTaf.cloud, sizeof(mucTaf.cloud), "%.3s %dft", tok, hFt);
    } else if (metarWxName(tok) && strcmp(mucTaf.wx, "DRY") == 0) {
      copyStr(mucTaf.wx, sizeof(mucTaf.wx), metarWxName(tok));
    }
    tok = strtok(nullptr, " \r\n");
  }

  if (tempo || becmg || prob)
    snprintf(mucTaf.change, sizeof(mucTaf.change), "B%d T%d P%d", becmg, tempo, prob);
}

bool fetchMucTaf() {
  uint32_t now = millis();
  if (mucTaf.lastFetch != 0 && now - mucTaf.lastFetch < MUC_TAF_REFRESH_MS)
    return mucTaf.ok;
  mucTaf.lastFetch = now;  // throttle failures too; redraws happen often

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(METAR_HTTP_TIMEOUT_MS);
  char url[108];
  snprintf(url, sizeof(url),
           "https://aviationweather.gov/api/data/taf?ids=%s&format=raw",
           HOME_AIRPORT_ICAO);
  if (!http.begin(client, url)) return mucTaf.ok;
  http.addHeader("User-Agent", "FreisingDeskRadar/1.1.5 MUC TAF page");
  int code = http.GET();
  if (code != 200) {
    http.end();
    return mucTaf.ok;
  }

  String payload = http.getString();
  http.end();
  payload.trim();
  if (payload.length() == 0) return mucTaf.ok;

  copyStr(mucTaf.raw, sizeof(mucTaf.raw), payload.c_str());
  payload = String();
  parseTafSummary();
  mucTaf.ok = true;
  return true;
}

bool fetchOpenSkyStats() {
  if (!OPENSKY_ENABLED) return openSky.ok;
  uint32_t now = millis();
  if (openSky.lastFetch != 0 && now - openSky.lastFetch < OPENSKY_REFRESH_MS)
    return openSky.ok;
  openSky.lastFetch = now;  // avoid retrying rate-limit failures every redraw

  // OpenSky uses a WGS84 bounding box instead of a radius. Convert the desired
  // MUC-centred page radius into a conservative box; the display labels it as
  // a regional cross-check rather than exact circular coverage.
  float dLat = OPENSKY_RANGE_KM / 110.57f;
  float dLon = OPENSKY_RANGE_KM / (111.32f * cosf(deg2rad(MUC_LAT)));
  char url[220];
  snprintf(url, sizeof(url),
           "https://opensky-network.org/api/states/all?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f&extended=1",
           (double)(MUC_LAT - dLat), (double)(MUC_LON - dLon),
           (double)(MUC_LAT + dLat), (double)(MUC_LON + dLon));

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(OPENSKY_HTTP_TIMEOUT_MS);
  if (!http.begin(client, url)) return openSky.ok;
  http.addHeader("User-Agent", "FreisingDeskRadar/1.1.5 OpenSky page");
  int code = http.GET();
  if (code != 200) {
    http.end();
    return openSky.ok;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  payload = String();
  if (err) {
    Serial.printf("OpenSky JSON error: %s\n", err.c_str());
    return openSky.ok;
  }

  OpenSkyStats next;
  next.apiTime = doc["time"] | 0;
  for (JsonVariant row : doc["states"].as<JsonArray>()) {
    JsonArray st = row.as<JsonArray>();
    if (st.isNull()) continue;
    next.total++;
    bool ground = st[8] | false;
    int source = st[16] | -1;   // 0 ADS-B, 2 MLAT, 3 FLARM when available
    int cat = st[17] | 0;       // requires extended=1
    int lastContact = st[4] | 0;

    if (source == 0) next.adsb++;
    else if (source == 2) next.mlat++;
    else if (source == 3) next.flarm++;
    if (ground) next.ground++;
    if (cat == 8) next.rotor++;
    if (cat == 5 || cat == 6) next.heavy++;
    if (next.apiTime && lastContact && (int)next.apiTime - lastContact > 30)
      next.stale++;
  }
  next.ok = true;
  next.lastFetch = now;
  openSky = next;
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

  char l3[10];
  if (p.altFt > 0) snprintf(l3, sizeof(l3), "%dm", (int)(p.altFt * 0.3048f));
  else             snprintf(l3, sizeof(l3), "--m");

  for (int side = 0; side < 2; side++) {
    int lx = ((x < 120) == (side == 0)) ? x + 10 : x - 52;
    int ly = y - 13;
    if (ly < 48) ly = 48;
    if (ly > 166) ly = 166;
    int cxl = lx + 21, cyl = ly + 13;   // label-box centre
    bool fits = (cxl - 120) * (cxl - 120) + (cyl - 120) * (cyl - 120) <= 90 * 90 &&
                !radarTextShield(lx, ly) && !radarTextShield(lx + 42, ly);
    for (int k = 0; k < *labelUsed && fits; k++) {
      int dx = cxl - labelCx[k], dy = cyl - labelCy[k];
      if (dx * dx + dy * dy < 34 * 34) fits = false;
    }
    if (!fits) continue;

    int labelEdgeX = lx > x ? lx : lx + 42;
    int lineStartX = x + (labelEdgeX > x ? 7 : -7);
    int lineStartY = y;
    int lineEndY = ly + 10;
    tft.fillRect(lx - 2, ly - 2, 46, 31, C_BG);
    tft.drawRect(lx - 2, ly - 2, 46, 31, C_GRIDDIM);
    tft.drawLine(lineStartX, lineStartY, labelEdgeX, lineEndY, C_DIM);

    printFit(lx, ly,      p.flight, 1, C_WHITE, 42);
    printFit(lx, ly + 10, p.typ,    1, C_CYAN,  42);
    printFit(lx, ly + 20, l3,       1, C_AMBER, 42);
    if (record) {
      pushBlip(cxl, cyl, 30);
      pushBlip((lineStartX + labelEdgeX) / 2,
               (lineStartY + lineEndY) / 2,
               18);
    }
    labelCx[*labelUsed] = cxl;
    labelCy[*labelUsed] = cyl;
    (*labelUsed)++;
    return true;
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
      // Out-of-range contact: a small blue dot with a short tick pointing
      // OUTWARD along the bearing-from-you. Rationale (rev1.1.17): every
      // arrow/triangle attempt at this size (4-6 px) rasterises as an
      // irregular blob on the GC9A01 — a filled circle is the only primitive
      // that stays perfectly round that small, and the radial tick still
      // answers "more traffic out this way" without pretending to show the
      // aircraft's own heading.
      float sx = sinf(b);
      float sy = -cosf(b);
      x = 120 + (int)(sx * (RADAR_RIM_R - 5));
      y = 120 + (int)(sy * (RADAR_RIM_R - 5));
      int tx = 120 + (int)(sx * (RADAR_RIM_R + 1));
      int ty = 120 + (int)(sy * (RADAR_RIM_R + 1));
      tft.drawLine(x, y, tx, ty, C_EDGE_BLUE);
      tft.fillCircle(x, y, 2, C_EDGE_BLUE);
      if (record) pushBlip(x, y, 9);
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
    else if (route && route->airline[0]) fitCopy(detail, sizeof(detail), route->airline, 22);
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
  uint16_t c = activityColor(activityRadiusCount);
  char n[8];
  snprintf(n, sizeof(n), "%d", activityRadiusCount);
  tft.drawFastHLine(78, 195, 84, C_GRIDDIM);
  centerText(n, 202, 2, c);
  // rev1.1.23: "around you" — the count is centred on HOME_LAT/LON (your
  // location), not on the airport; the label now says so.
  char label[22];
  snprintf(label, sizeof(label), "%.0fkm around you", (double)ACTIVITY_RADIUS_KM);
  centerText(label, 224, 1, C_DIM);
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
  centerText("Traffic", 30, 2, C_AMBER);
  centerText("nearby", 52, 1, C_DIM);

  int used[4] = {-1, -1, -1, -1};
  int usedN = 0;

  int nearIdx = nearestAirborne();
  drawBriefRow(78, "NEAR", C_WHITE, nearIdx, false);
  if (nearIdx >= 0) used[usedN++] = nearIdx;

  int coolIdx = bestCoolNearIdx(used, usedN);
  if (coolIdx < 0) coolIdx = bestCoolMucIdx(used, usedN);
  drawBriefRow(108, "COOL", C_AMBER, coolIdx, true);
  if (coolIdx >= 0) used[usedN++] = coolIdx;

  int heliIdx = bestHelicopterNearIdx(used, usedN);
  drawBriefRow(138, "HELI", C_CYAN, heliIdx, false);
  if (heliIdx >= 0) used[usedN++] = heliIdx;

  int emgIdx = bestEmergencyNearIdx();
  drawBriefRow(162, "EMG", C_RED, emgIdx, true);

  drawActivityCounter();
}

// ---------- Pages 4+5: NEAREST / COOLEST tracking (shared renderer) ----------
void trackStatic() {
  tft.fillScreen(C_BG);
  drawBezelChrome(false);
  pageDots();
}

void drawProjectedCourse(int cx, int cy, int px, int py, const Aircraft &p,
                         float scale, int mapR, uint16_t color) {
  // Tracking-page extrapolation cue. The previous renderer drew a same-length
  // dashed ray from the last raw ADS-B point, so fast and slow aircraft looked
  // identical. This line starts from the current dead-reckoned position and
  // scales the forward cue by about two minutes of travel, capped to keep the
  // mini-map uncluttered. Small dots mark rough 1 min / 2 min positions when
  // they fit inside the map.
  float t = deg2rad(p.trackDeg);
  float s = sinf(t), c = cosf(t);
  float kmPerMin = groundSpeedKmh(p) / 60.0f;
  int aheadPx = (int)(kmPerMin * 2.0f * scale);
  if (aheadPx < 16) aheadPx = 16;
  if (aheadPx > 40) aheadPx = 40;

  int bx = px - (int)(s * 10);
  int by = py + (int)(c * 10);
  if ((bx - cx) * (bx - cx) + (by - cy) * (by - cy) <= mapR * mapR)
    tft.drawLine(bx, by, px, py, C_GRIDDIM);

  for (int d = 0; d < aheadPx; d += 8) {
    int xa = px + (int)(s * d),       ya = py - (int)(c * d);
    int xb = px + (int)(s * (d + 5)), yb = py - (int)(c * (d + 5));
    if ((xa - cx) * (xa - cx) + (ya - cy) * (ya - cy) > mapR * mapR) break;
    if ((xb - cx) * (xb - cx) + (yb - cy) * (yb - cy) > mapR * mapR) break;
    tft.drawLine(xa, ya, xb, yb, color);
  }

  for (int minute = 1; minute <= 2; minute++) {
    int mx = px + (int)(s * kmPerMin * minute * scale);
    int my = py - (int)(c * kmPerMin * minute * scale);
    if ((mx - cx) * (mx - cx) + (my - cy) * (my - cy) <= mapR * mapR)
      tft.fillCircle(mx, my, 1, minute == 1 ? C_GRID : C_GRIDDIM);
  }
}

void trackPageDraw(int idx, const char *headerTag, uint16_t tagColor,
                   Trail &trail, const char *whyLine) {
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
  centerText(headerTag, 12, 1, tagColor);
  centerText(n.flight, 24, 2, C_AMBER);
  char who[34];
  if (route && route->airline[0])
    snprintf(who, sizeof(who), "%.14s  %.14s", typeName(n.typ), route->airline);
  else
    fitCopy(who, sizeof(who), typeName(n.typ), 26);
  // Airline brand colour when the carrier is known (Lufthansa yellow,
  // Ryanair navy, ...) — the small visual identity the user asked for.
  centerText(who, 44, 1,
             (route && route->airline[0]) ? airlineAccentColor(n, route->airline) : C_DIM);
  if (route && route->codes[0]) centerText(route->codes, 56, 1, C_CYAN);
  if (whyLine && whyLine[0]) centerText(whyLine, 68, 1, tagColor);

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
  float liveE, liveN;
  liveAircraftOffsetKm(n, &liveE, &liveN);
  int px = cx + (int)(liveE * scale);
  int py = cy - (int)(liveN * scale);
  if ((px - cx) * (px - cx) + (py - cy) * (py - cy) <= mapR * mapR) {
    drawProjectedCourse(cx, cy, px, py, n, scale, mapR, C_GRID);
    trafficSymbol(n, px, py, 7, trafficColor(n, false), false);
  } else {
    // Aircraft outside the mini-map: edge-clamped blue square (global rule).
    float b = deg2rad(n.bearingDeg);
    int ex = cx + (int)(sinf(b) * (mapR + 4));
    int ey = cy - (int)(cosf(b) * (mapR + 4));
    tft.fillRect(ex - 2, ey - 2, 5, 5, C_BLUE_DIM);
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
    float tSec = -(liveE * vx + liveN * vy) / v2;
    if (tSec > 0 && tSec < 3600) {
      float ce = liveE + vx * tSec, cn = liveN + vy * tSec;
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
  trackPageDraw(nearestAirborne(), "NEAREST", C_DIM, trailNear, NULL);
}

void coolestStatic() {
  tft.fillScreen(C_BG);
  drawBezelChrome(false);
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
    trackPageDraw(-1, "COOLEST", C_AMBER, trailCool, NULL);
    return;
  }
  Aircraft &p = planes[ci];
  const char *label;
  int score = coolScore(p, &label);
  uint16_t tagColor = score >= 85 ? C_RED : (score >= 50 ? C_AMBER : C_DIM);

  char why[34];
  buildCoolWhyLine(p, label, why, sizeof(why));
  trackPageDraw(ci, "COOLEST", tagColor, trailCool, why);
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

  // rev1.1.23: ONE-line label — callsign, status and fr/to sit side by side
  // instead of stacked, per user feedback ("saves space"). Widths are
  // measured so the whole line can be centred on the aircraft and clamped
  // to the panel.
  int w1 = (int)strlen(p.flight) * 6;
  int w2 = (int)strlen(status) * 6;
  int w3 = via[0] ? (int)strlen(via) * 6 : 0;
  int total = w1 + 6 + w2 + (w3 ? 6 + w3 : 0);

  int lx = x - total / 2;
  if (lx < 8) lx = 8;
  if (lx + total > 232) lx = 232 - total;
  int ly = y - 16;
  if (ly < 62) ly = y + 10;
  if (ly > 204) ly = 204;

  // The two labels (next ARR + next DEP) must never overlap each other:
  // vertical dodge when the single-line boxes would intersect.
  if (avoidX > -900 &&
      lx < avoidX + 140 && lx + total > avoidX &&
      ly < avoidY + 12 && ly + 12 > avoidY) {
    if (avoidY + 14 <= 204) ly = avoidY + 14;
    else                    ly = avoidY - 14;
  }

  int anchorY = (ly < y) ? ly + 9 : ly - 1;
  tft.drawLine(x, y, lx + total / 2, anchorY, C_DIM);
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE);
  tft.setCursor(lx, ly);
  tft.print(p.flight);
  tft.setTextColor(color);
  tft.setCursor(lx + w1 + 6, ly);
  tft.print(status);
  if (w3) {
    tft.setTextColor(C_DIM);
    tft.setCursor(lx + w1 + 6 + w2 + 6, ly);
    tft.print(via);
  }
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
  int nextArr = nextArrivalIdx();
  int nextDep = nextDepartureIdx();

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

    int x, y;
    mucTrafficToScreen(liveE, liveN, &x, &y);
    bool clampedToEdge = false;
    int dx = x - 120, dy = y - MUC_MAP_CY;
    if (dx * dx + dy * dy > MUC_MAP_R * MUC_MAP_R) {
      if (dMuc < 0.01f) continue;
      x = 120 + (int)(dMucE / dMuc * MUC_MAP_R);
      y = MUC_MAP_CY - (int)(dMucN / dMuc * MUC_MAP_R);
      clampedToEdge = true;
    }

    // Visual grammar of the operations map (rev1.1.18 scope: 20 km on-map,
    // 20-40 km at the edge, >40 km hidden):
    //   blue dot + tick = 20-40 km out, pinned to the edge (home-radar style)
    //   purple circle = stationary/ground aircraft on the field
    //   green symbol  = arriving traffic
    //   red symbol    = departing traffic
    //   star/rotor    = notable aircraft / helicopter, regardless of flow
    //   amber symbol   = other aircraft around MUC
    // Only next ARR/DEP get text labels (no track extrapolation — rev1.1.19).
    bool onRwy = onMucRunway(liveE, liveN) && (p.ground || p.altFt < 900);
    if (clampedToEdge) {
      // Edge contact (20-40 km out): same dot-with-outward-tick marker as the
      // home radar rim, and pushed to the SAME screen rim radius as page 1
      // (r=99..105 around the screen centre) instead of hugging the smaller
      // map circle. One guard: markers whose spot falls on the counter /
      // weather strip at the top are skipped — instrument numbers always win.
      float ux = dMucE / dMuc, uy = dMucN / dMuc;
      int cx2 = 120 + (int)(ux * 99);
      int cy2 = 120 - (int)(uy * 99);
      bool onCounters   = (cy2 < 66  && cx2 > 56 && cx2 < 184);
      bool onSouthLabel = (cy2 > 200 && cx2 > 88 && cx2 < 152);
      if (!onCounters && !onSouthLabel) {
        int tx2 = 120 + (int)(ux * 105);
        int ty2 = 120 - (int)(uy * 105);
        tft.drawLine(cx2, cy2, tx2, ty2, C_EDGE_BLUE);
        tft.fillCircle(cx2, cy2, 2, C_EDGE_BLUE);
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
        if (gdx * gdx + gdy * gdy < 16) { crowded = true; break; }
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

  tft.fillCircle(54, y + 4, 5, iconColor);
  tft.setTextSize(1);
  tft.setTextColor(C_BG);
  tft.setCursor(51, y + 1);
  tft.print(icon);
  printFit(68, y, label, 1, C_GRID, 46);
  printFit(124, y, value, 1, valueColor, 76);
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
  dataRow(56,  "WIND",  mucWx.wind,  C_WHITE);
  dataRow(73,  "VIS",   mucWx.vis,   C_WHITE);
  dataRow(90,  "CLOUD", mucWx.cloud, C_WHITE);
  dataRow(107, "TEMP",  mucWx.temp,  C_WHITE);
  dataRow(124, "QNH",   mucWx.qnh,   C_WHITE);
  dataRow(141, "WX",    mucWx.wx,
          strcmp(mucWx.wx, "DRY") == 0 ? C_WHITE : C_AMBER);

  char rwy[10];
  activeRunway(rwy, sizeof(rwy));
  dataRow(158, "RWY", rwy[0] ? rwy : "--", rwy[0] ? C_CYAN : C_DIM);

  // Field status as a proper pill (see flightCategory for why this is
  // METAR-derived rather than a fake delay percentage).
  uint16_t catColor;
  const char *cat = flightCategory(&catColor);
  tft.fillRoundRect(60, 174, 120, 17, 8, C_STATUS_FILL);
  tft.drawRoundRect(60, 174, 120, 17, 8, catColor);
  tft.fillCircle(72, 182, 4, catColor);
  centerText(cat, 179, 1, catColor);

  // Raw METAR tail: the unfiltered truth for anyone who reads METAR, clipped
  // into two fixed rows so odd station remarks can never break the layout.
  char raw1[26], raw2[26];
  fitCopy(raw1, sizeof(raw1), mucWx.raw, 22);
  fitCopy(raw2, sizeof(raw2), mucWx.raw + strlen(raw1), 22);
  printFit(58, 198, raw1, 1, C_DIM, 124);
  printFit(58, 210, raw2, 1, C_DIM, 124);
}

// ---------- Page 7: MUC TAF ----------
void sourceRow(int y, const char *label, const char *value, uint16_t valueColor) {
  // Generic data row for the three appended source pages. It gives labels and
  // values fixed pixel boxes; that keeps runway dimensions, frequencies, and
  // forecast groups from crashing into each other on the circular display.
  //
  // The boxes are also chord-aware: rows near the top/bottom get narrower so
  // text never runs under the physical round lens mask.
  int boxW = circleTextBoxW(y, 1);
  int x0 = 120 - boxW / 2;
  int labelW = boxW > 132 ? 50 : 42;
  int gap = 6;
  int valueW = boxW - labelW - gap;
  if (valueW < 36) valueW = 36;
  printFit(x0, y, label, 1, C_GRID, labelW);
  printFit(x0 + labelW + gap, y, value, 1, valueColor, valueW);
}

void mucTafStatic() {
  tft.fillScreen(C_BG);
  pageDots();
}

void mucTafDynamic() {
  tft.fillScreen(C_BG);
  centerText("MUC TAF", 18, 2, C_AMBER);
  centerText("forecast", 40, 1, C_DIM);
  tft.drawFastHLine(76, 54, 88, C_GRIDDIM);

  if (!fetchMucTaf()) {
    centerText("no TAF yet", 104, 2, C_DIM);
    centerText("aviationweather retry", 128, 1, C_GRID);
    return;
  }

  sourceRow(66,  "VALID", mucTaf.valid,  C_WHITE);
  sourceRow(82,  "WIND",  mucTaf.wind,   C_WHITE);
  sourceRow(98,  "VIS",   mucTaf.vis,    C_WHITE);
  sourceRow(114, "CLOUD", mucTaf.cloud,  C_WHITE);
  sourceRow(130, "WX",    mucTaf.wx,
            strcmp(mucTaf.wx, "DRY") == 0 ? C_WHITE : C_AMBER);
  sourceRow(146, "CHG",   mucTaf.change, strcmp(mucTaf.change, "NONE") == 0 ? C_DIM : C_CYAN);

  // Raw TAF carries the full forecast text. It is clipped into three safe
  // rows rather than wrapped dynamically, because fixed rows are much more
  // reliable near the lower chord of the round panel.
  char r1[27], r2[27], r3[27];
  fitCopy(r1, sizeof(r1), mucTaf.raw, 24);
  fitCopy(r2, sizeof(r2), mucTaf.raw + strlen(r1), 24);
  fitCopy(r3, sizeof(r3), mucTaf.raw + strlen(r1) + strlen(r2), 24);
  centerText(r1, 174, 1, C_DIM);
  centerText(r2, 186, 1, C_DIM);
  centerText(r3, 198, 1, C_DIM);
}

// ---------- Page 8: MUC FIELD ----------
void mucInfoStatic() {
  tft.fillScreen(C_BG);
  pageDots();
}

void mucInfoDynamic() {
  // No network call here: the page is a baked-in OurAirports reference card.
  // Static facts belong in firmware because downloading multi-megabyte CSVs
  // on an ESP32 just to learn "two 4000 m concrete runways" would be wasteful.
  tft.fillScreen(C_BG);
  centerText("MUC FIELD", 18, 2, C_AMBER);
  centerText("OurAirports facts", 40, 1, C_DIM);
  tft.drawFastHLine(72, 54, 96, C_GRIDDIM);

  sourceRow(62,  "ID",    "EDDM / MUC",       C_WHITE);
  sourceRow(78,  "TYPE",  "large airport",    C_WHITE);
  sourceRow(94,  "ELEV",  "1487ft / 453m",    C_WHITE);

  tft.drawFastHLine(54, 112, 132, C_GRIDDIM);
  sourceRow(124, "08L26R", "4000x60m concrete", C_CYAN);
  sourceRow(140, "08R26L", "4000x60m concrete", C_CYAN);

  tft.drawFastHLine(54, 158, 132, C_GRIDDIM);
  sourceRow(166, "ATIS",  "123.130 MHz",      C_AMBER);
  sourceRow(180, "TWR N", "118.705 MHz",      C_GREEN);
  sourceRow(194, "TWR S", "120.505 MHz",      C_GREEN);
}

// ---------- Page 9: OPEN SKY ----------
void openSkyStatic() {
  tft.fillScreen(C_BG);
  pageDots();
}

void openSkyDynamic() {
  tft.fillScreen(C_BG);
  centerText("OPEN SKY", 18, 2, C_AMBER);
  centerText("MUC regional check", 40, 1, C_DIM);
  tft.drawFastHLine(76, 54, 88, C_GRIDDIM);

  if (!OPENSKY_ENABLED) {
    centerText("disabled", 106, 2, C_DIM);
    centerText("set OPENSKY_ENABLED 1", 130, 1, C_GRID);
    return;
  }
  if (!fetchOpenSkyStats()) {
    centerText("no OpenSky data", 104, 2, C_DIM);
    centerText("rate limit or offline", 128, 1, C_GRID);
    return;
  }

  char n[12];
  snprintf(n, sizeof(n), "%d", openSky.total);
  centerText(n, 62, 3, openSky.total > 45 ? C_RED : (openSky.total > 22 ? C_AMBER : C_GREEN));
  centerText("states in bbox", 90, 1, C_DIM);

  char value[18];
  snprintf(value, sizeof(value), "%d ADSB / %d MLAT", openSky.adsb, openSky.mlat);
  sourceRow(112, "SRC", value, C_WHITE);
  snprintf(value, sizeof(value), "%d FLARM", openSky.flarm);
  sourceRow(128, "FLRM", value, openSky.flarm ? C_CYAN : C_DIM);
  snprintf(value, sizeof(value), "%d heavy", openSky.heavy);
  sourceRow(144, "HVY", value, openSky.heavy ? C_AMBER : C_DIM);
  snprintf(value, sizeof(value), "%d rotor", openSky.rotor);
  sourceRow(160, "HELI", value, openSky.rotor ? C_PURPLE : C_DIM);
  snprintf(value, sizeof(value), "%d ground", openSky.ground);
  sourceRow(176, "GND", value, openSky.ground ? C_GREEN : C_DIM);
  snprintf(value, sizeof(value), "%d stale", openSky.stale);
  sourceRow(192, "AGE", value, openSky.stale ? C_AMBER : C_DIM);

  char footer[26];
  snprintf(footer, sizeof(footer), "%.0fkm bbox backup", (double)OPENSKY_RANGE_KM);
  centerText(footer, 216, 1, C_DIM);
}

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
    case PAGE_MUC_TAF:  mucTafStatic();     mucTafDynamic();     break;
    case PAGE_MUC_INFO: mucInfoStatic();    mucInfoDynamic();    break;
    case PAGE_OPENSKY:  openSkyStatic();    openSkyDynamic();    break;
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
    case PAGE_MUC_TAF:  mucTafDynamic();     break;
    case PAGE_MUC_INFO: mucInfoDynamic();    break;
    case PAGE_OPENSKY:  openSkyDynamic();    break;
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
  if (page == PAGE_MUC_MAP) {
    // The airport map owns next arrival/departure rows; fetching those routes
    // only on that page avoids blocking the button for data the brief no
    // longer displays.
    if (pageButtonDownNow()) return;
    int a = nextArrivalIdx();
    if (a >= 0) fetchRoute(planes[a].flight, ROUTE_SLOT_MUC_BASE + 0);
    if (pageButtonDownNow()) return;
    int d = nextDepartureIdx();
    if (d >= 0) fetchRoute(planes[d].flight, ROUTE_SLOT_MUC_BASE + 1);
    // rev1.1.21: the map header shows temp+wind, so this page keeps the
    // METAR cache warm too (no-op while the cache is fresh). Previously it
    // only filled after visiting the weather page.
    if (pageButtonDownNow()) return;
    fetchMucWeather();
  }
  if (page == PAGE_COOLEST || page == PAGE_SUMMARY) {
    if (pageButtonDownNow()) return;
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
  Serial.println("Plane Radar rev1.1.22 booting...");
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
  lastPageSwitch = millis();
  setAdsbFetchStatus("waiting");
  resetRadarZoomCycle();
  drawPageFull();
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

  bool fetchDue = (now - lastFetchAttempt >= FETCH_INTERVAL_MS || lastFetchAttempt == 0);
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
    } else {
      failCount++;
      Serial.printf("ADS-B retry %d: %s\n", failCount, adsbFetchStatus);
      handlePageButton(millis());
      // Redraw even on failed fetches so the user sees a live radar shell plus
      // the latest retry reason. This is especially important before the first
      // successful packet, when otherwise the display can look stuck at boot.
      if (first || page == PAGE_RADAR) drawPageFull();
      else drawPageUpdate();
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

  if (AUTO_SCROLL_ENABLED && haveAircraftData && !manualPageHold &&
      now - lastPageSwitch >= pageDur[page]) {
    advancePage(false);
  }

  delay(40);
}
