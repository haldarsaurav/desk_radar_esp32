// config.example.h — copy to config.h and fill in your values.
// config.h is gitignored so your Wi-Fi credentials never reach GitHub.
//
// GIFT-READY SETUP: a new owner only needs the three [REQUIRED] blocks.
// Everything after them is optional tuning with safe built-in defaults —
// commented-out lines show the default; uncomment to override (see the
// CONFIG DEFAULTS block at the top of plane_radar_v1.ino).
#pragma once

// [REQUIRED] Wi-Fi
#define WIFI_SSID       "YOUR_WIFI_NAME"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

// [REQUIRED] Device location (centre of the radar scope)
#define HOME_LAT        48.39576799207869
#define HOME_LON        11.773933419079786

// [REQUIRED] Home airport (route matching, METAR request, page labels)
//#define HOME_AIRPORT_IATA "MUC"
//#define HOME_AIRPORT_ICAO "EDDM"

// ---- Optional tuning -----------------------------------------------------

// Maximum home-radar scope range in km. The first page can tour through
// 20 / 40 / 60 km for label readability while the ADS-B fetch stays wide.
// Contacts beyond the current display scope become faint blue bearing ticks.
#define RADAR_RANGE_KM  60.0

// Home-radar weighted zoom tour. Default is ON: every 5-minute window gives
// 50% of the time to the zoom level best suited to current traffic density,
// 30% to the next-best level, and 20% to the remaining context view.
// Disable this if you prefer traffic-density auto zoom using the thresholds
// below.
//#define RADAR_ZOOM_CYCLE_ENABLED 1
//#define RADAR_ZOOM_WINDOW_MS 300000UL

// Home-radar traffic-density auto zoom. Used only when the zoom tour above is
// disabled. The device still fetches 60 km, but page 1 can zoom to 40 km or
// 20 km when the sky is crowded so aircraft labels are readable.
//#define RADAR_AUTO_ZOOM_ENABLED 1
//#define RADAR_ZOOM_40_COUNT 8
//#define RADAR_ZOOM_20_COUNT 18

// How often to fetch new aircraft data (ms; be nice to the free API).
#define FETCH_INTERVAL_MS 8000

// Wide-area activity count (TRAFFIC BRIEF footer, centred on your location)
// and ADS-B fetch radius. Display pages keep their own tighter ranges
// (radar 60 km, MUC map 20/40 km) regardless of this value.
//#define ACTIVITY_RADIUS_KM 100.0f
//#define ADSB_FETCH_RADIUS_KM ACTIVITY_RADIUS_KM

// MUC Airport page traffic radius. The runways stay as a readable centered
// schematic, while aircraft positions are mapped on this wider airport scope.
//#define MUC_MAP_RANGE_KM 60.0f

// Traffic Brief wider searches. Helicopters and emergency squawks get their
// own searches so they can still be called out if the main scene is crowded.
//#define BRIEF_HELI_RANGE_KM 45.0f
//#define BRIEF_EMERGENCY_RANGE_KM 60.0f

// Airport map runway-schematic zoom, pixels per km (default 22).
//#define MUC_MAP_SCALE 22.0f

// Auto page carousel: 1 (default) = pages rotate by themselves, 0 = button only.
//#define AUTO_SCROLL_ENABLED 1

// Physical page button: 1 (default) = enabled, 0 = not wired.
//#define PAGE_BUTTON_ENABLED 1

// Page button GPIO. 9 (default) = the Super Mini's onboard BOOT button —
// zero soldering for the enclosure; just don't hold it while plugging in USB
// (that enters the ESP32-C3 ROM bootloader — release and re-plug).
// Use 2 for an external button wired to GND.
#define PAGE_BUTTON_PIN 9
