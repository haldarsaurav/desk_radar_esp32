# MUC Desk Radar — rev1.0

A polished Munich-aviation desk instrument: an ESP32-C3 Super Mini drives a
1.28″ round GC9A01 display and shows **live ADS-B traffic** around your home,
Munich Airport operations, aviation weather, and the coolest aircraft in the
sky right now — like a tiny air-traffic-control scope for your desk.

Live data comes from free, key-less APIs: [adsb.lol](https://api.adsb.lol)
(positions), [adsbdb](https://www.adsbdb.com) (routes + airlines) and
[aviationweather.gov](https://aviationweather.gov) (EDDM METAR).

## Pages

| # | Page | What it shows |
|---|------|---------------|
| 1 | **HOME RADAR** | Live scope around your location: red north arrow, total contacts under it, DEP count (red) west, ARR count (green) east. Priority-picked labels (emergency > helicopter > special > nearest). |
| 2 | **TRAFFIC BRIEF** | Five compact entries: nearest aircraft, coolest aircraft (with why), nearest helicopter, any emergency, next MUC arrival + departure. |
| 3 | **NEAREST TRACK** | Follows the closest aircraft: flown path, projected course, MUC marker, bearing/altitude/speed, closest-point-of-approach prediction. |
| 4 | **COOLEST TRACK** | Same live map, following the highest-scored aircraft (A380s, military, government callsigns, emergencies...) with a "why it's cool + MUC status" tag. |
| 5 | **MUC MAP** | Airport diagram with both runways, live traffic flows with projected paths, ARR/DEP/GND counters, temperature + wind, and next arrival/departure rows (compare with FlightRadar24!). |
| 6 | **WEATHER** | EDDM METAR decoded: wind, visibility, cloud/ceiling, temperature, QNH, weather, estimated runway in use, and a NORMAL/MARGINAL/LOW-VIS OPS status derived from visibility + ceiling. |
| 7 | **LEGEND** | Every colour and symbol, drawn with the real rendering code. |

Rare aircraft and emergency squawks **interrupt the carousel** and jump to the
COOLEST page automatically (once per aircraft).

## Colour & symbol legend

| Symbol / colour | Meaning |
|---|---|
| red arrow | departing Munich (DEP) |
| green arrow | arriving Munich (ARR) |
| amber arrow | other traffic |
| blue square (rim) | outside configured range, direction only |
| star | special / notable aircraft |
| rotor symbol | helicopter |
| purple circle | on ground / stationary |
| red "!" dot | emergency squawk (7700 / 7600 / 7500) |

## Hardware

- ESP32-C3 Super Mini
- GC9A01 1.28″ round TFT, 240×240, SPI
- optional momentary pushbutton (page control)
- optional WS2812B ring + SN74AHCT125N (planned, see `firmware/led_status_v1`)

### Wiring (display → ESP32-C3)

| GC9A01 | ESP32-C3 |
|---|---|
| VCC | **3V3** (never 5V!) |
| GND | GND |
| SCL/SCK | GPIO4 |
| SDA/MOSI | GPIO3 |
| DC | GPIO10 |
| CS | GPIO1 |
| RST | GPIO0 |

Button: GPIO2 → button → GND (or set `PAGE_BUTTON_PIN 9` to use the onboard
**BOOT button with zero soldering** — just don't hold it while plugging in USB).

Full visual guide: [`docs/display_wiring_guide.html`](docs/display_wiring_guide.html)
and [`docs/device_manual.html`](docs/device_manual.html).

## Setup (gift-ready)

1. Copy `firmware/plane_radar_v1/config.example.h` → `config.h`.
2. Fill in the three **[REQUIRED]** blocks: Wi-Fi, latitude/longitude, home airport.
3. Optional: tune range, zoom, auto-scroll, button pin (all documented in the file).
4. Arduino IDE: board **ESP32C3 Dev Module**, *USB CDC On Boot: Enabled*,
   libraries **Adafruit GC9A01A** + **ArduinoJson** → Upload.

## Controls

- **Short press** — next page, and stay there (carousel pauses).
- **Long press (1.2 s)** — resume the automatic carousel.
- Presses are interrupt-latched, so they register even during network fetches.

## Docs

- [`docs/plane_radar_ui_change_log.md`](docs/plane_radar_ui_change_log.md) — full design history
- [`docs/device_manual.html`](docs/device_manual.html) — wiring schematic + legend, printable
- [`docs/bom.md`](docs/bom.md), [`docs/wiring_notes.md`](docs/wiring_notes.md) — hardware planning
