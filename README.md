# ESP32-C3 Plane Radar Desk Display — Project Export

This repository/package captures the working plan, hardware decisions, wiring diagrams, UI ideas, and V1 LED-ring firmware module for the USB-C-powered Plane Radar desk device.

## Current project scope

| Topic | Decision |
|---|---|
| Core project | ESP32-C3 internet-based aircraft/ADS-B desk radar |
| Power | USB-C only for V1, no battery |
| Display | 1.28 inch round GC9A01 SPI TFT, 240×240 |
| Light ring | 72 mm WS2812B 24-LED addressable ring |
| Enclosure style | Upright desk device, matte white 3D-printed body |
| Controls | Latching power switch/button, separate MODE button |
| Side port | Side-mounted USB-C power-only input |
| GPS | Not needed; use configured static coordinates |
| Buzzer | Deferred/optional; quiet/work-friendly visual alerts preferred |
| Firmware focus | Start with LED-ring status module and basic hardware test |

## File structure

| Path | Contents |
|---|---|
| `firmware/led_status_v1/` | Arduino/FastLED V1 LED status module and demo sketch |
| `hardware/diagrams/` | Fritzing-style wiring and schematic diagrams in PNG/SVG |
| `hardware/reference_screenshots/` | Shopping/reference screenshots used during planning |
| `design/renders/` | Product/UI concept renders generated during planning |
| `docs/` | BOM, wiring notes, UI plan, firmware explanation, project summary |
| `ai_coworking/` | Prompts and handoff notes for Claude Code / ChatGPT coworking |
| `github/` | Suggested GitHub setup commands and repo notes |

## First hardware bring-up order

| Step | Action | Expected result |
|---:|---|---|
| 1 | Wire only ESP32-C3 + LED ring through SN74AHCT125N | LED demo runs without flicker |
| 2 | Run `PlaneRadar_LEDStatus_Demo.ino` | All LED states cycle automatically |
| 3 | Verify LED 0 physical orientation | LED 0 should be at top/North; adjust `LED_INDEX_OFFSET` if needed |
| 4 | Add GC9A01 display wiring | Display test sketch works |
| 5 | Flash/merge base ADS-B radar firmware | Radar page fetches aircraft data |
| 6 | Integrate `LedContext` updates | LED ring reflects Wi-Fi/API/aircraft states |

## Important warnings

| Warning | Reason |
|---|---|
| Do not power GC9A01 from 5 V in the final wiring | Use ESP32 3.3 V for display VCC/logic safety |
| Keep LED brightness low initially | 24 WS2812B LEDs can draw high current at full white |
| Use SN74AHCT125N or 74HCT125 for LED data | Reliable 3.3 V ESP32 → 5 V WS2812B signaling |
| Side USB-C breakout may need CC resistors | USB-C-to-C sources require 5.1 kΩ pull-downs on CC1 and CC2 |
| Do not power via side USB-C and ESP32 USB-C simultaneously during debugging | Avoid backfeeding unless power isolation is designed |

## Current firmware module

The current module is not a full ADS-B firmware yet. It is the LED-ring status engine and demo test.

Main file to test first:

```text
firmware/led_status_v1/PlaneRadar_LEDStatus_Demo.ino
```

Required Arduino library:

```text
FastLED
```
