# Claude Code / ChatGPT Coworking Handoff Prompt

You are helping continue an ESP32-C3 Plane Radar desk-display project.

## Current target

Build the USB-C-powered V1:

| Item | Setting |
|---|---|
| Board | ESP32-C3 Super Mini |
| Display | 1.28 inch GC9A01 240×240 SPI |
| LEDs | 72 mm WS2812B 24-LED ring |
| Level shifter | SN74AHCT125N / 74HCT125 |
| LED data pin | GPIO5 |
| MODE button | GPIO7 to GND, `INPUT_PULLUP` |
| Power | Side USB-C power-only input through latching switch |
| Enclosure | White upright desk device |
| GPS | None; use configured static coordinates |

## Existing code

Start with:

```text
firmware/led_status_v1/
```

This contains:
- `led_status.h`
- `led_status.cpp`
- `PlaneRadar_LEDStatus_Demo.ino`

## Task sequence

| Step | Task |
|---:|---|
| 1 | Verify LED module compiles for ESP32-C3 in Arduino IDE or PlatformIO |
| 2 | Convert the demo to PlatformIO if desired |
| 3 | Add display test for GC9A01 with the known pin map |
| 4 | Merge LED module into the base ESP32 Plane Radar firmware |
| 5 | Add page manager: Radar / Closest Aircraft / MUC / Stats / System |
| 6 | Add MODE button short/double/long press handling |
| 7 | Use ADS-B data to fill `LedContext` |
| 8 | Add configurable LED brightness and orientation offset |
| 9 | Prepare clean GitHub repo structure |

## Required behavior

Use tables in documentation and planning. Keep V1 simple and robust. Do not add battery, GPS, aircraft photos, or direct ADS-B receiver yet.

## LED behavior summary

| State | Pattern |
|---|---|
| Boot | White sweep |
| Wi-Fi connecting | Blue rotating dot |
| Wi-Fi connected | Green double flash |
| Captive portal | Blue/white half ring |
| Wi-Fi disconnected | Broken red arc |
| API fetching | Cyan sweep |
| API error | Amber/red blink |
| Data stale | Green/amber fade |
| No aircraft | Dim green glow |
| Nearby aircraft | Bearing sector |
| Close aircraft | Cyan/blue sector |
| Very close | Orange/red sector |
| Overhead | Red/white ripple |
| Quiet | Purple bottom dot |

## Important electrical notes

| Note | Detail |
|---|---|
| Display VCC | ESP32 3V3 |
| LED ring VCC | +5V switched rail |
| Level shifter VCC | +5V switched rail |
| LED data resistor | 330–470 Ω between AHCT output and LED DIN |
| LED bulk capacitor | 1000 µF across LED 5V/GND |
| AHCT decoupling | 100 nF near IC |
| USB-C CC | CC1/CC2 need 5.1 kΩ to GND if breakout lacks them |
