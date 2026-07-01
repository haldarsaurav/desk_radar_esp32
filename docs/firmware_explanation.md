# Firmware Module Explanation — `led_status_v1`

## Main files

| File | Role |
|---|---|
| `led_status.h` | Constants, enums, `LedContext`, `LedStatus` class declaration |
| `led_status.cpp` | Animation implementation and priority logic |
| `PlaneRadar_LEDStatus_Demo.ino` | Standalone demo cycling through LED states |

## Core architecture

| Component | Purpose |
|---|---|
| `LedContext` | Input state from the future radar firmware |
| `LedStatus::update()` | Decides which LED animation to draw |
| `CRGB leds_[24]` | Software colour buffer for the 24 LEDs |
| `FastLED.show()` | Sends one full frame over GPIO5 to the ring |

## Important constants

| Constant | Value | Meaning |
|---|---:|---|
| `LED_PIN` | 5 | ESP32 GPIO for WS2812B data |
| `LED_COUNT` | 24 | Number of LEDs |
| `LED_BRIGHTNESS` | 25 | Global brightness cap |
| `LED_INDEX_OFFSET` | 0 | Orientation correction offset |

## `LedContext` fields

| Field | Meaning |
|---|---|
| `wifi` | Booting/connecting/connected/disconnected/captive portal |
| `api` | Idle/fetching/ok/error/stale |
| `traffic` | None/distant/nearby/close/very close/overhead/emergency/special |
| `hasAircraft` | Whether valid aircraft data exists |
| `aircraftBearingDeg` | Bearing of closest aircraft, 0=N, 90=E |
| `aircraftDistanceKm` | Distance of closest aircraft |
| `quietMode` | Adds purple quiet marker |
| `specialLargeAircraft` | Future rare aircraft alert flag |
| `emergencySquawk` | Highest-priority emergency alert flag |

## Main logic in `update()`

| Order | Check | Output |
|---:|---|---|
| 1 | Emergency | Full red strobe |
| 2 | Wi-Fi boot/connecting/captive/disconnected | Corresponding Wi-Fi pattern |
| 3 | API error/fetching/stale | Corresponding API pattern |
| 4 | Temporary Wi-Fi/API success flashes | Green pulse/flash |
| 5 | Special aircraft | Purple/cyan sweep |
| 6 | Aircraft overhead | Red/white ripple |
| 7 | Aircraft present | Bearing sector |
| 8 | No aircraft | Idle green glow |
| 9 | Quiet mode | Purple marker overlay |
| 10 | `FastLED.show()` | Sends final frame |

## Functions worth knowing

| Function | Purpose |
|---|---|
| `bearingToLed()` | Converts aircraft bearing to LED index |
| `setSector()` | Lights a directional group of LEDs around a center LED |
| `drawBoot()` | White sweep |
| `drawWifiConnecting()` | Blue chase |
| `drawWifiDisconnected()` | Broken red arc |
| `drawApiFetching()` | Cyan sweep |
| `drawApiError()` | Amber/red blink |
| `drawAircraftSector()` | Main aircraft direction + distance visual |
| `drawOverheadRipple()` | Very close/overhead aircraft alert |
| `drawQuietMarker()` | Purple dot at LED 12 |

## First test

| Step | Action |
|---:|---|
| 1 | Open `PlaneRadar_LEDStatus_Demo.ino` in Arduino IDE |
| 2 | Install `FastLED` |
| 3 | Select ESP32-C3 board target |
| 4 | Flash sketch |
| 5 | Confirm all animations cycle every 4 seconds |

## Future integration

The ADS-B firmware should classify real data and fill this structure:

```cpp
LedContext ctx;
ctx.wifi = WiFi.isConnected() ? WifiState::Connected : WifiState::Disconnected;
ctx.api = apiFetchInProgress ? ApiState::Fetching : lastApiOk ? ApiState::Ok : ApiState::Error;
ctx.hasAircraft = aircraftCount > 0;
ctx.aircraftBearingDeg = closestAircraft.bearingDeg;
ctx.aircraftDistanceKm = closestAircraft.distanceKm;
ctx.traffic = classifyTraffic(closestAircraft.distanceKm);
ctx.quietMode = quietModeEnabled;
ledStatus.update(ctx, millis());
```
