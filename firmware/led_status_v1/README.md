# ESP32-C3 Plane Radar — LED Ring Status Module V1

This is the V1 LED-ring firmware module for the USB-C-powered ESP32-C3 Plane Radar desk device.

## Hardware assumptions

| Item | Setting |
|---|---|
| Controller | ESP32-C3 Super Mini |
| LED ring | WS2812B, 24 LEDs, 5 V |
| LED data GPIO | GPIO5 |
| Level shifter | SN74AHCT125N / 74HCT125 recommended |
| LED ring brightness | 25 / 255 default |
| LED 0 orientation | Top / North |
| LED 6 | East |
| LED 12 | South |
| LED 18 | West |

## Required Arduino library

Install:

```text
FastLED
```

## Files

| File | Purpose |
|---|---|
| `led_status.h` | LED-ring API and configuration |
| `led_status.cpp` | All animation/state logic |
| `PlaneRadar_LEDStatus_Demo.ino` | Standalone demo sketch to test the LED ring before ADS-B integration |

## V1 LED behavior

| Condition | Pattern |
|---|---|
| Booting | White sweep |
| Wi-Fi connecting | Blue rotating dot |
| Wi-Fi connected | Green double flash |
| Captive portal | Blue/white alternating half-ring |
| API fetching | Cyan sweep |
| API OK | Small green pulse |
| API error | Amber/red double blink |
| Data stale | Green fading toward amber |
| No aircraft | Very dim green glow |
| Aircraft 10–25 km | Green bearing sector |
| Aircraft 5–10 km | Cyan/blue bearing sector pulse |
| Aircraft 2–5 km | Orange/red bearing sector pulse |
| Aircraft <2 km | Red/white overhead ripple |
| Quiet mode | Small purple dot at bottom |

## Integration idea

In your final radar firmware, call:

```cpp
ledStatus.update(context, millis());
```

Where `context` contains Wi-Fi/API state, nearest aircraft distance, nearest aircraft bearing, and quiet mode.

## Important

Limit brightness. Do not test the full ring at high white brightness from the ESP32 board power rail.
