# ✈️ MUC Desk Radar

**A standalone desk instrument that watches the real sky around you and around Munich Airport — on a 1.28″ round display.**

No phone app. No companion server. An ESP32‑C3 joins your Wi‑Fi, pulls live aircraft and weather data straight from free public feeds, and paints it onto a glass‑cockpit round screen: live traffic, airport operations, per‑aircraft tracking cards, rare‑aircraft alerts, and decoded METAR weather.

> **Note on the code:** this repository is a **showcase**. The firmware source is not published here. This README is meant to give you a clear picture of *what the device does*, *how it's built*, and *what to expect* — without the source itself.

---

## What it looks like

A black puck with a round radar face. Traffic drifts across a north‑up scope; the airport page shows Munich's real runways; tracking cards count down the closest approach of the plane overhead. It runs 24/7 on USB‑C and needs no interaction — though one button lets you browse.

---

## Features at a glance

- **Six live pages**, auto‑rotating on a tuned carousel, or browse manually with one button.
- **Live ADS‑B traffic** around your home location and Munich Airport, refreshed every few seconds.
- **Dead‑reckoned motion** between updates so aircraft glide instead of jumping.
- **Airport operations map** with Munich's real parallel‑runway geometry and on‑field ground traffic.
- **Per‑aircraft tracking cards** — callsign, type, airline, route with full city names, live altitude/speed/distance, climb trend, and closest‑point‑of‑approach ("passes 2.1 km from you in 1m36s").
- **Coolness engine** — automatically spots rare airframes (A380, An‑124, C‑17…), military and government callsigns, and interesting situations, and can interrupt the carousel for a genuine rarity.
- **Emergency awareness** — transponder squawks 7700/7600/7500 are flagged and jump to the front.
- **Decoded METAR weather** for Munich (wind, visibility, cloud, temp, QNH, flight category).
- **Gift‑ready setup** — one small config file; a new owner only sets Wi‑Fi and their location.
- **Robust for 24/7 desk life** — auto‑reconnect, rate‑limit backoff, and graceful "last good data" fallback.

---

## The six pages

| # | Page | What it shows |
|---|------|---------------|
| 1 | **Home Radar** | North‑up scope centred on you. Range rings tour 20/40/60 km. Colour‑coded traffic with arrival/departure/total counters. |
| 2 | **MUC Map** | Munich Airport ops view — real runway schematic, on‑field ground traffic, live temp/wind, DEP/GND/ARR counts, next arrival & departure. |
| 3 | **Traffic Brief** | The four aircraft worth knowing right now: nearest, coolest, nearest helicopter, any emergency — plus a wide‑area "how busy is the sky" count. |
| 4 | **Nearest** | Full tracking card for the closest airborne aircraft, updating every second, with a rim blip pointing where to look. |
| 5 | **Coolest** | Same card locked onto the highest‑scored aircraft, with a one‑line reason it's special. |
| 6 | **MUC WX** | Decoded Munich METAR: wind, visibility, cloud, temperature, QNH, active‑runway estimate, and a VFR→LIFR flight‑category pill. |

*(A companion **Spotter's Field Manual** documents every symbol, colour and term.)*

---

## Hardware — just two parts

| Part | Role |
|------|------|
| **ESP32‑C3 Super Mini** (HW‑466AB, USB‑C) | The brain — Wi‑Fi microcontroller, powered & flashed over USB‑C. |
| **GC9A01 1.28″ round LCD** (240×240, SPI) | The face — every page is drawn here. |

Seven signal wires between them, powered over USB‑C. The onboard BOOT button doubles as the page control, so no extra parts are strictly required. Full wiring, flashing and troubleshooting live in the **Build & Flash Guide**.

**Data feeds (all free, no API key):** live ADS‑B positions, on‑demand route/airline lookups, and Munich METAR weather.

---

## Under the hood

The firmware is a single, heavily‑commented Arduino sketch — roughly **3,400 lines**, **136 functions**, and **7 data structures** — with no dynamic memory churn (fixed‑size caches keep it stable on the microcontroller). A quick map of what's inside:

| Area | Responsibility | Approx. functions |
|------|----------------|:---:|
| **Math & geo** | Haversine distance, bearings, ETA, closest‑point‑of‑approach, dead‑reckoning | ~11 |
| **Text & layout** | Chord‑aware fitting for a round screen, UTF‑8 → ASCII transliteration | ~8 |
| **Networking / fetch** | ADS‑B positions, route/airline lookups, METAR — with timeouts, retries & rate‑limit backoff | ~6 |
| **METAR decoder** | Tokenises and decodes raw airport weather into readable fields | ~8 |
| **Coolness scoring** | Type & callsign rules, situational bonuses, emergency detection | ~7 |
| **Drawing primitives** | Aircraft triangles, star/rotor/emergency symbols, bezel, low‑flicker blip erasing | ~15 |
| **Radar page** | Zoom cycling, range rings, label selection, counters, motion stepping | ~25 |
| **Airport (MUC) page** | Runway schematic, ground/air mapping, arrival/departure selection | ~20 |
| **Tracking cards** | Nearest & Coolest cards with 1‑second live ticks | ~12 |
| **Traffic brief** | Nearest/cool/heli/emergency selection & activity counter | ~10 |
| **Page system** | Carousel timing, interrupt‑latched button handling, alert interrupts | ~8 |
| **Boot / setup / loop** | Splash animation, Wi‑Fi, main cadence | ~6 |

Every tunable — ranges, zoom behaviour, refresh intervals, button pin — is a named define with a safe default, overridable from a single config file.

---

## What to expect

Plug it in, and after a short radar‑ping boot animation it connects to Wi‑Fi and shows live traffic within seconds. From then on it just runs: the carousel cycles the pages, tracking cards tick, and the sky above your desk becomes something you can actually read. Glance over when the rim blip lights — that's a plane, and it's telling you where to look up.

---

*MUC Desk Radar · Munich / EDDM · built for the desk of a plane‑spotter.*
