# Wiring Notes

## Pin map

| Function | ESP32-C3 pin |
|---|---:|
| LED ring data | GPIO5 |
| MODE button | GPIO7 |
| Display RST | GPIO0 |
| Display CS | GPIO1 |
| Display DC | GPIO10 |
| Display SDA/MOSI | GPIO3 |
| Display SCL/SCLK | GPIO4 |

## Power wiring

| Net | Connects to |
|---|---|
| USB-C VBUS | Latching power switch input |
| Switched +5V rail | ESP32 5V/VBUS, WS2812B 5V, SN74AHCT125N VCC |
| GND rail | ESP32 GND, display GND, LED GND, SN74AHCT125N GND |
| ESP32 3V3 | GC9A01 VCC |

## USB-C side power input

| Pin | Wiring |
|---|---|
| VBUS | To power switch input |
| GND | To common GND |
| CC1 | 5.1 kΩ to GND if breakout has no CC resistor |
| CC2 | 5.1 kΩ to GND if breakout has no CC resistor |
| D+/D- | Leave unconnected for V1 power-only port |

## SN74AHCT125N channel 1 wiring

| IC pin | Connection |
|---:|---|
| 14 / VCC | +5V switched rail |
| 7 / GND | Common GND |
| 1 / /OE1 | GND |
| 2 / 1A | ESP32 GPIO5 |
| 3 / 1Y | 330–470 Ω resistor → WS2812B DIN |

## Unused SN74AHCT125N pins

| Pin group | Recommendation |
|---|---|
| Unused /OE pins | Tie to +5V to disable unused channels |
| Unused A inputs | Tie to GND if convenient |
| Unused Y outputs | Leave unconnected |

## Capacitors

| Capacitor | Placement |
|---|---|
| 100 nF ceramic | Across SN74AHCT125N VCC/GND, physically close to IC |
| 1000 µF electrolytic | Across LED ring 5V/GND |

## Main warnings

| Warning | Why |
|---|---|
| Do not power GC9A01 from 5V | Use 3.3V for display VCC/logic safety |
| Avoid dual USB power | Side USB-C and ESP32 USB-C can backfeed each other |
| Start with low LED brightness | Prevent USB/current issues |
| Keep grounds common | Data signaling requires common reference |
