# 24-LED Ring Behavior Specification V1

## Orientation

| Direction | LED index |
|---|---:|
| North / top | 0 |
| East / right | 6 |
| South / bottom | 12 |
| West / left | 18 |

## System states

| State | Pattern |
|---|---|
| Booting | White sweep |
| Wi-Fi connecting | Blue rotating dot |
| Wi-Fi connected | Full green double flash |
| Wi-Fi disconnected | Broken rotating red arc |
| Captive portal | Alternating blue/white half-ring |
| API fetching | Cyan sweep |
| API success | Small green pulse at bottom |
| API error | Amber/red blink |
| Data stale | Slow green-to-amber fade |
| Quiet mode | Purple dot at bottom/LED 12 |

## Aircraft states

| Distance | `TrafficAlert` | Pattern |
|---:|---|---|
| No aircraft | `None` | Very dim green idle glow |
| >25 km | `Distant` | Dim green |
| 10–25 km | `Nearby` | Green sector at aircraft bearing |
| 5–10 km | `Close` | Cyan/blue pulsing sector |
| 2–5 km | `VeryClose` | Orange/red pulsing sector |
| <2 km | `Overhead` | Full red-white ripple |

## Special states

| State | Pattern |
|---|---|
| A380/B747/rare aircraft | Purple/cyan sweep |
| Emergency squawk | Full red strobe |
| Helicopter | Planned V2 yellow rotor pattern |
| Last-overhead replay | Planned V2/V3 fading bearing dot |

## Priority order

| Priority | Condition |
|---:|---|
| 1 | Emergency / critical |
| 2 | Wi-Fi disconnected/connecting/setup |
| 3 | API error/fetching/stale |
| 4 | Overhead / very close low aircraft |
| 5 | Special aircraft |
| 6 | Normal nearby aircraft |
| 7 | Idle |
| 8 | Page-specific decorative state |
