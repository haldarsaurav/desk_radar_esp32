# ✈ MUC Desk Radar — v1.2

A Munich-aviation desk instrument: an **ESP32-C3 Super Mini** drives a 1.28″
round **GC9A01** display and shows **live ADS-B traffic** around your home,
Munich Airport (MUC/EDDM) operations, decoded aviation weather, and the
coolest aircraft in the sky right now — a tiny air-traffic-control scope for
your desk that runs 24/7 on nothing but Wi-Fi.

Live data comes from free, key-less APIs: [adsb.lol](https://api.adsb.lol)
(positions), [adsbdb](https://www.adsbdb.com) (routes + airlines) and
[aviationweather.gov](https://aviationweather.gov) (EDDM METAR).

> 📖 **New here?** Read the illustrated
> [Plane Spotter's Manual](docs/spotters_manual.html) — every page, colour,
> symbol and aviation term explained, with annotated screen mockups.

## The six pages

Short-press the button for the next page (the carousel pauses there);
long-press (1.2 s) to resume auto-rotation.

| # | Page | What it shows |
|---|------|---------------|
| 1 | **HOME RADAR** | North-up scope centred on you, touring 20/40/60 km zoom by traffic density. Compass counters: total under the red north arrow, DEP (red) west, ARR (green) east. Up to three aircraft get boxed labels — callsign, type, altitude, speed — tethered to their symbols. Out-of-range traffic becomes dark-blue rim ticks. |
| 2 | **MUC MAP** | Airport operations: true-geometry 08L/26R + 08R/26L runway schematic, airborne traffic on a 20 km scope, ground traffic spread along the real field. The next arrival and departure are **latched** — watch one plane fly its entire approach — with labels showing callsign, origin/destination, ETA/distance, altitude + speed. Header: temp, wind, DEP/GND/ARR counts. |
| 3 | **TRFC** | The 10-second briefing: nearest aircraft, coolest aircraft (with why), nearest helicopter **with operator role** (POLICE / RESCUE / SAR / CIVIL, decoded from the callsign), any emergency squawk, and a 100 km activity count around you. |
| 4 | **NEAREST** | Data card for the closest aircraft: airline in brand colour, route with full city names, big ALT/SPD/DST, climb trend (m/min), and **CPA** — how close it will pass to you and when, ticking live every second. A red rim blip points where to look outside. |
| 5 | **COOLEST** | Same card for the highest-scored aircraft (A380s, Antonovs, military, government callsigns…) with a why-line. Score ≥ 95 or any emergency interrupts the carousel automatically. |
| 6 | **WEATHER** | EDDM METAR decoded: wind, visibility, cloud, temp, QNH, significant weather, estimated active runway, VFR/MVFR/IFR/LIFR status pill, and the raw METAR for purists. |

## Colour & symbol grammar

| Symbol | Meaning |
|---|---|
| 🟢 green triangle | arriving at Munich (points along heading) |
| 🔴 red triangle | departing Munich |
| 🟡 amber triangle | other traffic |
| 🟡 yellow on runway slab | aircraft on the runway (bigger = heavy) |
| 🟣 purple dot | on the ground / taxiing |
| 🔵 dark-blue rim dot + tick | beyond drawn range, direction only |
| ✳ star | special / rare aircraft |
| ✚ rotor cross | helicopter |
| ❗ red alert | emergency squawk 7700 / 7600 / 7500 → jumps to TRFC |
| 🔺 red rim blip (cards) | bearing from **you** to the tracked aircraft |

## Hardware

| Part | Notes |
|---|---|
| ESP32-C3 Super Mini | any ESP32-C3 board works with pin tweaks |
| GC9A01 1.28″ round TFT | 240×240, SPI |
| Momentary button (optional) | or use the onboard BOOT button (GPIO9, default) |

**Wiring** (full diagrams in [docs/display_wiring_guide.html](docs/display_wiring_guide.html)):

| GC9A01 | ESP32-C3 |
|---|---|
| VCC | **3V3 — never 5 V!** |
| GND | GND |
| SCL/SCK | GPIO4 |
| SDA/MOSI | GPIO3 |
| DC | GPIO10 |
| CS | GPIO1 |
| RST | GPIO0 |
| button | GPIO2 → GND, or BOOT (GPIO9) |

## Quick start

1. `cp firmware/plane_radar_v1/config.example.h firmware/plane_radar_v1/config.h`
2. Edit `config.h`: Wi-Fi name + password, **your latitude/longitude**, and
   your name for the boot splash (`OWNER_NAME`). `config.h` is gitignored —
   credentials and your home location never reach GitHub.
3. Arduino IDE: board **ESP32C3 Dev Module**, *USB CDC On Boot: Enabled*.
   Install libraries **Adafruit GC9A01A** and **ArduinoJson**.
4. Upload. The device boots with a radar ping + fly-through animation and
   starts scanning the sky.

Everything else — ranges, zoom behaviour, fetch cadence, button pin, airport —
has sensible defaults and is overridable from `config.h`
(see the CONFIG DEFAULTS block at the top of the firmware).

## Built to run forever

The firmware is designed for unattended 24/7 desk duty: an interrupt-latched
button that never misses a press even during network calls, wrap-safe timers,
flicker-free partial rendering with motion-hash frame skipping, fast retry +
Wi-Fi re-association after network failures, a low-heap restart guard, and
one preventive self-restart per day at a quiet moment (~10 s, lands back on
the radar page).

## Adapting it to your airport

MUC is baked in as the default, but the airport reference point, runway
geometry (heading/length/separation), IATA/ICAO codes and all ranges are
config values — a different home airport is a handful of lines in `config.h`.

## Docs

* [Plane Spotter's Manual](docs/spotters_manual.html) — the illustrated guide
* [Device manual](docs/device_manual.html) — wiring, setup, controls
* [Wiring guide](docs/display_wiring_guide.html) — pin-to-pin with schematic
* [Aviation reference](docs/aviation_reference.html) — squawks, METAR codes, type codes
* [Design changelog](docs/plane_radar_ui_change_log.md) — how the UI evolved

## Data & credits

Positions: [adsb.lol](https://api.adsb.lol) · routes/airlines:
[adsbdb](https://www.adsbdb.com) · weather: [aviationweather.gov](https://aviationweather.gov)
· field facts: [OurAirports](https://ourairports.com). All community/free —
be kind to their rate limits. Built by Saurav, iterated with Claude.

*For enjoyment only — never use this display for navigation or operational
decisions.*
