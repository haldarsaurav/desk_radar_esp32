// ============================================================================
//  config.example.h  —  MUC Desk Radar personal settings
// ============================================================================
//
//  HOW TO USE THIS FILE
//  --------------------
//  1.  Make a copy of this file in the same folder and rename the copy to:
//          config.h
//  2.  Open config.h and fill in the three [REQUIRED] blocks below
//      (your Wi-Fi, your location, your home airport).
//  3.  Leave everything else as-is unless you want to tweak it — every
//      optional line already has a sensible built-in default.
//  4.  Flash the firmware (see the Build Guide).
//
//  WHY TWO FILES?
//  --------------
//  config.h holds your Wi-Fi password, so it is intentionally kept OUT of
//  version control (it is listed in .gitignore). This file — config.example.h
//  — is the safe, shareable template with no secrets in it. If you give the
//  project to a friend, they only ever edit their own private config.h.
//
//  TIP: lines that start with "//#define" are OPTIONAL overrides. The value
//  shown is already the default. To change one, delete the leading "//" and
//  edit the number/text.
// ============================================================================
#pragma once


// ============================================================================
//  [REQUIRED 1 of 3]  Wi-Fi network
// ============================================================================
//  The device needs 2.4 GHz Wi-Fi (the ESP32-C3 does not do 5 GHz).
//  Type the network name and password EXACTLY, including capital letters.
#define WIFI_SSID       "YOUR_WIFI_NAME"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"


// ============================================================================
//  [REQUIRED 2 of 3]  Your location  (the centre of the radar)
// ============================================================================
//  This is the dot in the middle of the scope — usually your home or desk.
//  Enter it in decimal degrees. Easiest way to find yours:
//    Google Maps -> right-click your rooftop -> click the lat,lon numbers.
//
//  The placeholder below is Munich Airport itself, so the radar will look
//  centred on the airfield until you change it. CHANGE IT to your address.
#define HOME_LAT        48.3538
#define HOME_LON        11.7861


// ============================================================================
//  [REQUIRED 3 of 3]  Your home airport
// ============================================================================
//  Used for route labels ("to MUC" / "from MUC"), the arrivals/departures
//  logic, and which airport's weather (METAR) is decoded on the WX page.
//  IATA = 3-letter code (MUC), ICAO = 4-letter code (EDDM).
//  Defaults are Munich; uncomment and edit for a different airport.
//#define HOME_AIRPORT_IATA "MUC"
//#define HOME_AIRPORT_ICAO "EDDM"

//  Name shown on the boot splash under the big "PLANE RADAR" title.
//  Make it yours — spaced-out capitals look great on the round screen.
//#define OWNER_NAME "S A U R A V"


// ============================================================================
//  OPTIONAL TUNING  —  safe to ignore. Defaults are already good.
// ============================================================================
//  Uncomment a line (remove the //) to override the built-in default.

// ---- Home radar range ------------------------------------------------------
//  Maximum scope range in km. Page 1 gently tours 20 / 40 / 60 km so labels
//  stay readable; the data feed always stays wide. Contacts outside the drawn
//  ring appear as faint blue bearing dots on the rim.
#define RADAR_RANGE_KM  60.0

//  Weighted zoom tour (default ON): each 5-minute window spends 50% of the
//  time at whichever zoom suits current traffic, 30% at the next, 20% on the
//  wide context view. Turn this off to use the density thresholds instead.
//#define RADAR_ZOOM_CYCLE_ENABLED 1
//#define RADAR_ZOOM_WINDOW_MS 300000UL

//  Traffic-density auto-zoom (used only if the tour above is OFF): zoom in to
//  40 km once this many aircraft are airborne, and to 20 km when it is busier.
//#define RADAR_AUTO_ZOOM_ENABLED 1
//#define RADAR_ZOOM_40_COUNT 8
//#define RADAR_ZOOM_20_COUNT 18

// ---- Data feed -------------------------------------------------------------
//  How often to pull fresh aircraft positions, in milliseconds. Please stay
//  polite to the free public API — 8000 (8 s) is a good, gentle default.
#define FETCH_INTERVAL_MS 8000

//  Wide-area "how busy is the sky" count (shown on the Traffic Brief page)
//  and the radius the feed covers. The map pages keep their own tighter
//  ranges no matter what you set here.
//#define ACTIVITY_RADIUS_KM 100.0f
//#define ADSB_FETCH_RADIUS_KM ACTIVITY_RADIUS_KM

// ---- Airport (MUC) map page ------------------------------------------------
//  Detection radius for the arrivals/departures logic (logic only — not the
//  on-screen zoom, which stays a readable runway schematic).
//#define MUC_MAP_RANGE_KM 60.0f

//  Runway-schematic zoom, in pixels per km (bigger = chunkier runways).
//#define MUC_MAP_SCALE 22.0f

// ---- Traffic Brief wider searches -----------------------------------------
//  Helicopters and emergency squawks get their own wider search so they can
//  still be flagged even when the close-in scene is crowded.
//#define BRIEF_HELI_RANGE_KM 60.0f
//#define BRIEF_EMERGENCY_RANGE_KM 200.0f

// ---- Behaviour -------------------------------------------------------------
//  Auto page carousel: 1 (default) = pages rotate on their own, 0 = only the
//  button changes pages.
//#define AUTO_SCROLL_ENABLED 1

//  Physical page button: 1 (default) = a button is wired, 0 = none.
//#define PAGE_BUTTON_ENABLED 1

//  Which GPIO the page button uses.
//    9  (default) = the Super Mini's onboard BOOT button — zero soldering.
//                   Just don't HOLD it while plugging in USB (that enters the
//                   chip's flashing mode; simply unplug and re-plug to exit).
//    2            = an external push-button wired between GPIO2 and GND.
#define PAGE_BUTTON_PIN 9
