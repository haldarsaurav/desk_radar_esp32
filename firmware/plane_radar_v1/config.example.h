// config.example.h — copy to config.h and fill in your values.
// config.h is gitignored so your Wi-Fi credentials never reach GitHub.
#pragma once

#define WIFI_SSID       "YOUR_WIFI_NAME"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

// Radar center (default: Freising town center — fine-tune if you like)
#define HOME_LAT        48.4028
#define HOME_LON        11.7411

// Radar display range in km (planes further away show as rim dots)
#define RADAR_RANGE_KM  30.0

// How often to fetch new aircraft data (milliseconds; be nice to the free API)
#define FETCH_INTERVAL_MS 8000
