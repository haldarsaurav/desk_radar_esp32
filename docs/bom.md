# BOM — USB-C Powered Basic Version

## Required parts

| Priority | Part | Qty | Search phrase | Notes |
|---:|---|---:|---|---|
| 1 | ESP32-C3 Super Mini USB-C | 2 | `ESP32 C3 Super Mini USB C development board` | Buy 2 for spare/debug |
| 1 | 1.28 inch GC9A01 round TFT | 1–2 | `1.28 inch GC9A01 round TFT display 240x240 SPI` | Display module for radar screen |
| 1 | WS2812B LED ring, 72 mm, 24 LEDs | 1 | `WS2812B LED ring 72mm 24 bit 5V` | Main LED halo |
| 1 | SN74AHCT125N or 74HCT125 | 1+ | `SN74AHCT125N DIP 74AHCT125` | Level shifter for LED data |
| 1 | Momentary MODE button | 1+ | `momentary push button tactile switch` | GPIO input |
| 1 | Latching power switch/button | 1 | `latching push button switch` or `mini slide switch` | Cuts +5V rail |
| 1 | 330–470 Ω resistor | 1 | `330 ohm resistor` / `470 ohm resistor` | In series with WS2812B DIN |
| 1 | 1000 µF electrolytic capacitor | 1 | `1000uF 10V electrolytic capacitor` | Across LED 5V/GND |
| 1 | 100 nF ceramic capacitor | 1 | `100nF ceramic capacitor` | Across SN74AHCT125N VCC/GND |
| 1 | Thin silicone wire | 1 set | `30AWG silicone wire kit` | Final internal wiring |
| 1 | Heat-shrink tubing | 1 set | `heat shrink tube kit` | Insulation/strain relief |

## Optional parts

| Part | Use |
|---|---|
| USB-C female breakout board | Side-mounted power-only input |
| 5.1 kΩ resistors | Required from CC1/CC2 to GND if USB-C breakout lacks them |
| JST 2-pin connectors | Removable LED ring/front panel |
| M2 screws/brass inserts | Later 3D-printed enclosure assembly |
| Passive buzzer | Future audible alerts; optional |
| Slide switch | Future mute/quiet switch |

## Parts deliberately skipped for V1

| Part | Reason |
|---|---|
| 18650 battery | Removed from V1 to simplify power |
| TP4056 charger | Not needed without battery |
| Boost converter | Not needed without battery |
| GPS module | Static coordinates are enough |
| Direct ADS-B receiver | Too large/complex for this version |
