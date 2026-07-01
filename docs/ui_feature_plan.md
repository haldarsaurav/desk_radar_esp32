# UI / Feature Plan

## Screen pages

| Page | Screen content | LED ring behavior |
|---|---|---|
| Radar | Radar rings, aircraft dots, N/E/S/W, MUC label | Closest aircraft bearing + distance colour |
| Closest Aircraft | Callsign, type, distance, altitude, speed, direction | Bearing sector + distance alert |
| MUC Airport | Runway/airport-focused local view | Arrival/departure or airport activity visualization |
| Stats | Aircraft count, closest, highest, update age, Wi-Fi/API | Traffic-density meter |
| System | Wi-Fi, API, USB power, quiet mode, brightness | Quadrant status dashboard |

## V1 features

| Feature | Status |
|---|---|
| LED ring status engine | Implemented |
| Wi-Fi/API LED patterns | Implemented in demo module |
| Aircraft bearing/distance LED pattern | Implemented in demo module |
| Display UI pages | Planned |
| ADS-B API fetch integration | Planned |
| Page button handling | Planned |
| White upright enclosure | Rendered/planned |

## V2 ideas

| Feature | Notes |
|---|---|
| Last-overhead memory | Store recent close aircraft in RAM |
| Traffic density meter | Map aircraft count to LED fill level |
| Special aircraft classifier | A380/B747/helicopter/private jet patterns |
| Theme selection | Green/cyan/amber/night |
| LED orientation calibration | Store offset in flash |
| Web portal settings | Brightness, quiet mode, units, coordinates |

## Deferred ideas

| Idea | Reason |
|---|---|
| Actual aircraft photos/liveries | More complex: API dependency, JPEG decoding, caching |
| Battery/18650 | Removed from V1 |
| Direct ADS-B receiver | Bigger RF project |
| Official airport schedule | Requires separate flight/airport API |
