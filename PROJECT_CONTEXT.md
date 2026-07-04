# PROJECT_CONTEXT.md — MUC Desk Radar

> Fast-reload context primer. Read this first to reconstruct the full picture of the project
> before continuing work in a new session (any assistant or a human). It is a dense factual
> snapshot, not a tutorial. Source of truth is always `firmware/plane_radar_v1/plane_radar_v1.ino`.

---

## 1. One-line description
Standalone ESP32-C3 desk instrument that renders live ADS-B aircraft traffic, Munich Airport (MUC/EDDM)
operations, per-aircraft tracking, and decoded METAR weather on a 1.28" 240×240 round GC9A01 display. No
app, no server — the MCU calls free public APIs directly over Wi-Fi.

## 2. Hardware (final build = 2 parts only)
- **ESP32-C3 Super Mini** (board marking HW-466AB), USB-C, onboard PCB antenna. 3.3 V logic. RISC-V single
  core @160 MHz, 400 KB SRAM, 384 KB ROM, ~4 MB flash. Onboard BOOT button = GPIO9, user LED = GPIO8.
- **GC9A01 1.28" round IPS LCD**, 240×240, 4-wire hardware SPI, 3.3 V.
- Powered over USB-C. No LED ring, no battery, no level shifter, no buzzer in the final build (those appear
  only in older concept notes — ignore them).

### Pin map (display → ESP32-C3)
| Display | ESP32-C3 | define |
|---|---|---|
| SCL/SCLK | GPIO4 | `TFT_SCLK 4` |
| SDA/MOSI | GPIO3 | `TFT_MOSI 3` |
| DC | GPIO10 | `TFT_DC 10` |
| CS | GPIO1 | `TFT_CS 1` |
| RST | GPIO0 | `TFT_RST 0` |
| VCC | 3V3 | — (never 5 V) |
| GND | GND | — |
| BLK | 3V3 | backlight always-on |
Page button = onboard BOOT (GPIO9) by default; external button option = GPIO2→GND (`PAGE_BUTTON_PIN`).

## 3. Data feeds (all free, no key)
- `api.adsb.lol/v2/point/<lat>/<lon>/<nm>` — live ADS-B positions. Fetched every `FETCH_INTERVAL_MS`
  (default 8000 ms). Radius = `ADSB_FETCH_RADIUS_KM` (= `ACTIVITY_RADIUS_KM`, default 100 km). Uses
  `useHTTP10(true)` + `Accept-Encoding: identity` to avoid chunked/compressed bodies that broke JSON parse.
  429 → 45 s backoff (`ADSB_RATELIMIT_BACKOFF_MS`), does NOT count toward fail/reboot spiral.
- `api.adsbdb.com/v0/callsign/<cs>` — route (origin→dest) + airline. Small 4-slot cache (`rc[]`).
- `aviationweather.gov` — EDDM METAR, decoded into `mucWx`. Refresh 5 min (`MUC_WX_REFRESH_MS`).

## 4. Page system (6 live pages)
Enum order: `PAGE_RADAR(0) MUC_MAP(1) SUMMARY(2) NEAREST(3) COOLEST(4) MUC_WX(5)`, `PAGE_COUNT 6`.
Auto-carousel dwell `pageDur[] = {26000,17000,16000,22000,22000,13000}` ms.
- Button: short press (≥40 ms, <1200 ms) = next page + freeze (`manualPageHold`); long press (≥1200 ms) =
  resume carousel. Interrupt-latched via ISR (`pageButtonIsr`) so presses survive blocking HTTP calls.
- Interrupts: emergency squawk or coolScore ≥ `ALERT_SCORE (95)` jumps carousel to the coolest/traffic
  page, once per callsign (`lastAlertCs`).

### Page content
1. **HOME RADAR** — north-up scope centred on HOME_LAT/LON. Rings tour 20/40/60 km (weighted zoom cycle,
   `RADAR_ZOOM_WINDOW_MS` 5 min, 50/30/20 split). Counters: white total (N), red DEP (W), green ARR (E).
2. **MUC MAP** — runway schematic at `MUC_MAP_SCALE` 22 px/km, real 08L/26R + 08R/26L geometry
   (`RWY_HDG 82`, `RWY_LEN 4`, `RWY_SEP 1.15`, `RWY_STAGGER 0.75`; MUC_LAT 48.3538 / MUC_LON 11.7861).
   Airborne mapped at 20 km view (`MUC_MAP_VIEW_KM`), 20–40 km → blue rim dots, on-field at schematic scale.
   Header MUC + temp/wind; footer DEP/GND/ARR. Labels on next ARR + DEP.
3. **TRFC** (summary) — NEAR / COOL / HELI / EMG rows + activity count within `ACTIVITY_RADIUS_KM` (100 km).
4. **NEAREST** — tracking card, closest airborne. 1 s live tick (`trackLiveTick`). ALT/SPD/DST, climb mpm,
   CPA. Red rim bearing blip.
5. **COOLEST** — same card for `coolestIdx()` with why-line (`buildCoolWhyLine`).
6. **MUC WX** — decoded METAR rows + active-runway estimate + flight-category pill (VFR..LIFR) + raw tail.

## 5. Coolness scoring (`coolScore`)
Emergency squawk (7700/7600/7500) = 100 outright. Otherwise max of `typeRules[]` (type prefix; `exact`
flag for short military codes so C17 ≠ C172) and `csRules[]` (callsign prefix), then situational bonuses:
LOW OVERHEAD PASS 82, FAST LOW 78, MUC FINAL INBOUND 70, MUC CLIMBOUT 66. Thresholds: ≥70 = star,
≥95 = alert interrupt. Notable type highlights: A380=100, An-124/B-52=99, C-5=97, C-17/Eurofighter=96,
F16/F35=95, A400M=93, C-130/Il-76=90, B747=92. Callsigns: AF1=100, SAM=98, GAF/NATO=95, RRR=92, RCH=86.

## 6. Config (`config.h`, gitignored; template `config.example.h`)
Required: `WIFI_SSID`, `WIFI_PASSWORD`, `HOME_LAT`, `HOME_LON`, (home airport IATA/ICAO default MUC/EDDM).
Optional named defines with defaults in the CONFIG DEFAULTS block at top of the .ino: `RADAR_RANGE_KM 60`,
`FETCH_INTERVAL_MS 8000`, zoom cycle knobs, `MUC_MAP_*`, `BRIEF_*`, `PAGE_INTERVAL_MS`, `AUTO_SCROLL_ENABLED`,
`PAGE_BUTTON_ENABLED`, `PAGE_BUTTON_PIN 9`, `OWNER_NAME`. Any config value overrides the default via `#ifndef`.

## 7. Architecture / code map (~3,436 lines, 136 functions, 7 structs)
Top→bottom in the .ino: CONFIG DEFAULTS → C_* colour palette → structs (Aircraft, RouteCache,
AirlineBrand, MucWeather, Blip, CoolRule, TypeName) → math/geo helpers → text/layout helpers
(`fitCopy/printFit/circleTextBoxW/centerText`, `copyStr` ASCII sanitize, `copyScreenText` UTF-8→ASCII) →
type-name table + coolness scoring → fetch layer (`fetchPlanes/fetchRoute/fetchMucWeather`) → METAR parser
(`parseMetarSummary`) → drawing primitives (`planeTriangle/trafficSymbol/star/heli/emergency`, bezel,
`clearInnerChrome`, blip erase) → radar renderers → airport renderers (`mucMapPointForAircraft`,
`drawRunway`, arrival/departure selection) → tracking/coolest cards → weather page → page management
(`drawPageFull/Update`, `skipPage`, `prefetchRoutes`) → `setup()`/`loop()`.

Fixed-size caches only (no heap churn): `planes[MAX_AC=64]`, `rc[4]`, `prevBlips[MAX_AC+16]`.

## 8. Round-display gotchas (hard-won, keep in mind when editing UI)
- Visible glass ends ~r116; `fillRect` corners beyond that eat the bezel → clear in circles
  (`clearInnerChrome` r=105), use chord-aware text boxes (`circleTextBoxW/centerText/printFit`).
- Text near top/bottom chord gets clipped by the lens; keep bottom labels above ~y208.
- `fillTriangle` below ~6 px rasterises as a blob → tiny markers use `fillCircle`+tick.
- Low-flicker: aircraft register erase regions via `pushBlip`; next frame erases exactly those.
  `radarStep()` skips frames whose quantised positions are unchanged (`radarMotionHash`).
- Between 8 s fetches, positions advance by dead reckoning (`liveAircraftOffsetKm`, 0.4 s buckets).
- Sweep beam was removed on purpose (fought labels / caused jitter). Calm ~1 s traffic step reads better.

## 9. Reliability behaviours
- Wi-Fi fail at boot → show message, `ESP.restart()` after 5 s (never park forever on a desk).
- `lastFetch` (success) vs `lastFetchAttempt` (throttle) kept separate so stale aircraft age normally.
- Feed errors surface a short pill on the radar page ("HTTP -11", "JSON fail", "API busy") and retry.

## 10. History notes (so you don't re-add removed things)
- rev1.1.23: removed the MUC TAF, MUC INFO and OPEN SKY reference pages + OpenSky code → back to 6 pages.
  Also removed flown-path trail buffers / mini-maps (tracking pages are pure data cards now).
- rev1.1.24: `exact` flag added to type rules (C-17 was matching Cessna C172).
- rev1.2.9: out-of-range contacts became a single faint dark-blue rim dot (was line arrows).
- Older concept docs mention an LED ring / level shifter / battery / buzzer — NOT in the final build.

## 11. Documentation index (in `docs/`)
- `spotters_field_manual.html` (+PDF) — user-facing: pages, symbols, terms, scoring, glossary, cheat card.
- `build_guide.html` (+PDF) — 2-part build: BOM, wiring schematic, flashing steps, troubleshooting.
- `datasheet_esp32c3_supermini.html` (+PDF) — ESP32-C3 Super Mini (HW-466AB) reference card.
- `datasheet_gc9a01_display.html` (+PDF) — GC9A01 1.28" round LCD reference card.
- `../README.md` — GitHub showcase (no source published).
- Original datasheets: `../../docs/datasheet/esp32-c3_datasheet.pdf`, `1.28_inch_round_LCD_datasheet.pdf`.

## 12. Build/flash quick facts
Arduino IDE + ESP32 core (Espressif). Libraries: Adafruit GFX, Adafruit GC9A01A, ArduinoJson (v6+).
Board: ESP32C3 Dev Module, USB CDC On Boot: Enabled. Serial 115200. Force bootloader = hold BOOT, tap RST,
release BOOT. Copy `config.example.h`→`config.h`, set Wi-Fi + lat/lon, flash.
