// ============================================================================
//  config.h  —  MY personal settings  (this file stays private)
// ============================================================================
//
//  This is your working copy of config.example.h. It holds your Wi-Fi
//  password, so it is deliberately kept out of version control (.gitignore).
//  Never commit or share this file — share config.example.h instead.
//
//  Only the three [REQUIRED] blocks below usually need touching. Everything
//  else has a good built-in default; see config.example.h for the full menu
//  of optional tuning knobs.
// ============================================================================
#pragma once


// ---- [REQUIRED 1] Wi-Fi  (2.4 GHz only) ------------------------------------
#define WIFI_SSID       "House LANister"
#define WIFI_PASSWORD   "38694509275404434045"


// ---- [REQUIRED 2] My location  (centre of the radar, decimal degrees) ------
//  Freising town centre. Right-click a spot in Google Maps to get your own.
#define HOME_LAT        48.39576799207869
#define HOME_LON        11.773933419079786


// ---- [REQUIRED 3] Home airport  (labels, ARR/DEP logic, METAR) -------------
//  Munich (MUC / EDDM) is the built-in default, so these can stay commented.
//#define HOME_AIRPORT_IATA "MUC"
//#define HOME_AIRPORT_ICAO "EDDM"

//  Name on the boot splash.
//#define OWNER_NAME "S A U R A V"


// ---- Optional overrides I actually use -------------------------------------
//  Radar scope range (rings read as 20 / 40 / 60 km).
#define RADAR_RANGE_KM  60.0

//  Aircraft refresh interval, ms. 8 s is gentle on the free API.
#define FETCH_INTERVAL_MS 8000
