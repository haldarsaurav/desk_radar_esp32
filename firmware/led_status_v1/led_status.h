#pragma once

#include <Arduino.h>
#include <FastLED.h>

// ---------- Hardware settings ----------
static constexpr uint8_t LED_PIN = 5;
static constexpr uint8_t LED_COUNT = 24;
static constexpr uint8_t LED_BRIGHTNESS = 25;   // Safe desk brightness, 0–255

// Orientation mapping:
// LED 0  = North / 12 o'clock
// LED 6  = East  / 3 o'clock
// LED 12 = South / 6 o'clock
// LED 18 = West  / 9 o'clock
static constexpr int LED_INDEX_OFFSET = 0;      // adjust if your physical LED 0 is not at top

// ---------- System state ----------
enum class WifiState : uint8_t {
  Booting,
  Connecting,
  Connected,
  Disconnected,
  CaptivePortal
};

enum class ApiState : uint8_t {
  Idle,
  Fetching,
  Ok,
  Error,
  Stale
};

enum class TrafficAlert : uint8_t {
  None,
  Distant,      // >25 km
  Nearby,       // 10–25 km
  Close,        // 5–10 km
  VeryClose,    // 2–5 km
  Overhead,     // <2 km
  Emergency,
  Special
};

struct LedContext {
  WifiState wifi = WifiState::Booting;
  ApiState api = ApiState::Idle;
  TrafficAlert traffic = TrafficAlert::None;

  bool hasAircraft = false;
  float aircraftBearingDeg = 0.0f;     // 0=N, 90=E, 180=S, 270=W
  float aircraftDistanceKm = 999.0f;
  bool lowAltitude = false;
  bool quietMode = false;

  // Optional future flags
  bool specialLargeAircraft = false;
  bool helicopter = false;
  bool emergencySquawk = false;
};

class LedStatus {
public:
  void begin();
  void update(const LedContext& ctx, uint32_t nowMs);
  void flashWifiConnected(uint32_t nowMs);
  void flashApiOk(uint32_t nowMs);

private:
  CRGB leds_[LED_COUNT];

  uint32_t wifiConnectedFlashStartMs_ = 0;
  uint32_t apiOkFlashStartMs_ = 0;

  void clear();
  void show();

  int bearingToLed(float bearingDeg) const;

  void drawQuietMarker();
  void drawBoot(uint32_t nowMs);
  void drawWifiConnecting(uint32_t nowMs);
  void drawWifiConnectedFlash(uint32_t nowMs);
  void drawWifiDisconnected(uint32_t nowMs);
  void drawCaptivePortal(uint32_t nowMs);

  void drawApiFetching(uint32_t nowMs);
  void drawApiError(uint32_t nowMs);
  void drawApiOkPulse(uint32_t nowMs);
  void drawDataStale(uint32_t nowMs);

  void drawIdleGlow(uint32_t nowMs);
  void drawAircraftSector(float bearingDeg, TrafficAlert level, uint32_t nowMs);
  void drawOverheadRipple(uint32_t nowMs);
  void drawEmergency(uint32_t nowMs);
  void drawSpecialAircraft(uint32_t nowMs);

  void setSector(int center, int radius, const CRGB& color, uint8_t falloff);
  void fadeAll(uint8_t amount);
};
