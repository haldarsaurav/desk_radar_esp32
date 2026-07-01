#include <Arduino.h>
#include "led_status.h"

/*
  PlaneRadar_LEDStatus_Demo.ino

  Standalone LED-ring demo for the ESP32-C3 Plane Radar project.

  Hardware:
  - ESP32-C3 Super Mini
  - WS2812B 24 LED ring
  - LED data on GPIO5 through SN74AHCT125N recommended
  - LED ring 5V and GND
  - Common ground

  This demo cycles through the V1 LED states so you can verify:
  - LED direction/orientation
  - brightness
  - connection-state animations
  - aircraft bearing-sector animation
*/

LedStatus ledStatus;
LedContext ctx;

uint32_t lastStageChange = 0;
uint8_t stage = 0;

void setup() {
  delay(300);
  ledStatus.begin();

  ctx.wifi = WifiState::Booting;
  ctx.api = ApiState::Idle;
  ctx.quietMode = false;

  lastStageChange = millis();
}

void loop() {
  uint32_t now = millis();

  if (now - lastStageChange > 4000) {
    lastStageChange = now;
    stage = (stage + 1) % 12;

    // Reset defaults
    ctx = LedContext{};
    ctx.wifi = WifiState::Connected;
    ctx.api = ApiState::Ok;

    switch (stage) {
      case 0:
        ctx.wifi = WifiState::Booting;
        break;

      case 1:
        ctx.wifi = WifiState::Connecting;
        break;

      case 2:
        ctx.wifi = WifiState::Connected;
        ledStatus.flashWifiConnected(now);
        break;

      case 3:
        ctx.wifi = WifiState::CaptivePortal;
        break;

      case 4:
        ctx.wifi = WifiState::Disconnected;
        break;

      case 5:
        ctx.wifi = WifiState::Connected;
        ctx.api = ApiState::Fetching;
        break;

      case 6:
        ctx.wifi = WifiState::Connected;
        ctx.api = ApiState::Error;
        break;

      case 7:
        ctx.wifi = WifiState::Connected;
        ctx.api = ApiState::Stale;
        break;

      case 8:
        ctx.wifi = WifiState::Connected;
        ctx.api = ApiState::Ok;
        ctx.hasAircraft = true;
        ctx.aircraftBearingDeg = 45;       // NE
        ctx.aircraftDistanceKm = 18;
        ctx.traffic = TrafficAlert::Nearby;
        break;

      case 9:
        ctx.wifi = WifiState::Connected;
        ctx.api = ApiState::Ok;
        ctx.hasAircraft = true;
        ctx.aircraftBearingDeg = 270;      // West
        ctx.aircraftDistanceKm = 7;
        ctx.traffic = TrafficAlert::Close;
        break;

      case 10:
        ctx.wifi = WifiState::Connected;
        ctx.api = ApiState::Ok;
        ctx.hasAircraft = true;
        ctx.aircraftBearingDeg = 180;      // South
        ctx.aircraftDistanceKm = 3;
        ctx.traffic = TrafficAlert::VeryClose;
        break;

      case 11:
        ctx.wifi = WifiState::Connected;
        ctx.api = ApiState::Ok;
        ctx.hasAircraft = true;
        ctx.aircraftDistanceKm = 1.2;
        ctx.traffic = TrafficAlert::Overhead;
        ctx.quietMode = true;
        break;
    }
  }

  // In a real firmware, these would come from WiFi/API/ADS-B parser state.
  ledStatus.update(ctx, now);

  delay(20);
}
