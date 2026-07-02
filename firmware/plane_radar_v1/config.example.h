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

// Radar scope range in km (aircraft further out become blue rim squares).
// 20 keeps the home page calm; 30 shows the whole MUC approach flow.
#define RADAR_RANGE_KM  20.0

// How often to fetch new aircraft data (ms; be nice to the free API).
#define FETCH_INTERVAL_MS 8000

// Tracking pages never zoom in closer than this view radius (km). Default 10.
//#define TRACK_RANGE_MIN_KM 10.0f

// Airport map zoom, pixels per km. 26 = runway close-up, 16 (default) also
// shows the approach and departure flows around the field.
//#define MUC_MAP_SCALE 16.0f

// Auto page carousel: 1 (default) = pages rotate by themselves, 0 = button only.
//#define AUTO_SCROLL_ENABLED 1

// Physical page button: 1 (default) = enabled, 0 = not wired.
//#define PAGE_BUTTON_ENABLED 1

// Page button GPIO. 9 (default) = the Super Mini's onboard BOOT button —
// zero soldering for the enclosure; just don't hold it while plugging in USB
// (that enters the ESP32-C3 ROM bootloader — release and re-plug).
// Use 2 for an external button wired to GND.
#define PAGE_BUTTON_PIN 9
