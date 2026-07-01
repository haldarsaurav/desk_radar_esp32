# Project Summary / Chat Reconstruction

This is a concise reconstruction of the planning chat, decisions, and generated files. It is not a verbatim transcript, but it captures the technical content needed to continue in Claude Code, ChatGPT, or GitHub.

## Original idea

| Item | Decision |
|---|---|
| Base inspiration | MakerWorld ESP32 Plane Radar project |
| Goal | Build a desk-friendly aircraft radar display with display + LED halo |
| Data source | Online ADS-B/aircraft API, not direct RF radar |
| Location strategy | Configure static latitude/longitude; no GPS module |
| Environment | Desk/work device, quiet visual status preferred |

## Hardware evolution

| Stage | Decision |
|---|---|
| Initial concept | ESP32-C3 + round display + light ring + button |
| Battery idea | Considered 18650 portable version |
| Current V1 scope | Battery removed; USB-C power only |
| Display | 1.28 inch GC9A01, 240×240 SPI |
| LED ring | 72 mm WS2812B, 24 LEDs |
| Enclosure | White 3D-printed upright desk body |
| Controls | Power button/switch + MODE button |
| Side USB-C | Power-only side port for clean desk/dock routing |
| GPS | Not needed |
| Buzzer | Optional/deferred; visual LED alerts prioritized |

## Key dimensional decisions

| Part | Dimension / note |
|---|---|
| GC9A01 display active area | ~32.4 mm diameter |
| GC9A01 module PCB | ~38 × 45.5 mm |
| 72 mm WS2812B ring | ~72 mm outer diameter |
| Ring width | ~9 mm |
| Approx ring inner hole | ~54 mm |
| Fit conclusion | 72 mm ring is correct; display visible area fits inside ring |
| Mechanical warning | Do not force full rectangular display PCB through ring; mount display behind front opening |

## LED ring concept

| LED index | Direction |
|---:|---|
| 0 | North / top |
| 6 | East / right |
| 12 | South / bottom |
| 18 | West / left |

## Main LED design language

| Condition | Pattern |
|---|---|
| Booting | White sweep |
| Wi-Fi connecting | Blue rotating dot |
| Wi-Fi connected | Green double flash |
| Captive portal | Blue/white alternating half-ring |
| Wi-Fi disconnected | Broken red rotating arc |
| API fetching | Cyan sweep |
| API OK | Small green pulse at bottom |
| API error | Amber/red blink |
| Data stale | Green fading toward amber |
| No aircraft | Very dim green idle glow |
| Aircraft 10–25 km | Green sector at aircraft bearing |
| Aircraft 5–10 km | Cyan/blue pulsing sector |
| Aircraft 2–5 km | Orange/red pulsing sector |
| Aircraft <2 km / overhead | Full red-white ripple |
| Quiet mode | Purple dot at bottom |
| Special aircraft | Purple/cyan sweep |
| Emergency | Full red strobe |

## LED priority

| Priority | State |
|---:|---|
| 1 | Emergency / critical |
| 2 | Wi-Fi disconnected or setup states |
| 3 | API error/fetching/stale |
| 4 | Very close / overhead aircraft |
| 5 | Special aircraft |
| 6 | Normal nearby aircraft |
| 7 | Idle |
| 8 | Decorative/page-specific effects |

## Pages discussed

| Page | Screen content | LED ring behavior |
|---|---|---|
| Radar | Concentric radar rings, aircraft dots, MUC label | Direction + distance sector |
| Closest Aircraft | Callsign, type, distance, altitude, speed, direction | Distance gauge + bearing |
| MUC Airport | Runway/airport-oriented view | Arrival/departure split possible |
| Stats | Aircraft count, closest, highest, update age, Wi-Fi/API | Traffic density/status |
| System | Wi-Fi/API/USB/quiet/brightness | Quadrant status dashboard |

## Aircraft data ideas

| Idea | Feasibility |
|---|---|
| Nearby aircraft from configured coordinates | Realistic |
| Closest aircraft page | Realistic |
| Last-overhead memory | Realistic |
| Traffic density meter | Realistic |
| Directional LED pointer | Realistic |
| Aircraft type silhouettes | Realistic later |
| Exact aircraft photo/livery | Possible but not V1; API/photo reliability and ESP32 memory make it harder |
| True RF radar | Not this project |
| Official arrivals/departures count | Not without another API; can only infer from tracks |

## Purchased/planned cart

| Item | Status |
|---|---|
| ESP32-C3 Super Mini boards | Planned/buying |
| GC9A01 1.28 inch displays | Planned/buying |
| 72 mm WS2812B 24-LED ring | Planned/buying |
| SN74AHCT125N chips | Planned/buying |
| USB-C breakout boards | Optional; useful for side power port if CC resistors handled |
| Heat shrink | Planned/buying |
| ATtiny/Digispark boards | For separate LED earrings prototype |
| Buttons/switches | Still needed if not already ordered |

## Design export files

| File | Purpose |
|---|---|
| `hardware/diagrams/plane_radar_fritzing_style_wiring.png` | Wiring diagram |
| `hardware/diagrams/plane_radar_electrical_schematic.png` | Schematic/net diagram |
| `design/renders/white_upright_usb_c_desk_concept.png` | White upright product concept |
| `firmware/led_status_v1/` | LED status module source |
