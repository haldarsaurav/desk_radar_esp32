#include "led_status.h"

void LedStatus::begin() {
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds_, LED_COUNT);
  FastLED.setBrightness(LED_BRIGHTNESS);
  clear();
  show();
}

void LedStatus::update(const LedContext& ctx, uint32_t nowMs) {
  clear();

  // ---------- Highest priority ----------
  if (ctx.emergencySquawk || ctx.traffic == TrafficAlert::Emergency) {
    drawEmergency(nowMs);
    if (ctx.quietMode) drawQuietMarker();
    show();
    return;
  }

  // ---------- Connection states ----------
  if (ctx.wifi == WifiState::Booting) {
    drawBoot(nowMs);
    if (ctx.quietMode) drawQuietMarker();
    show();
    return;
  }

  if (ctx.wifi == WifiState::Connecting) {
    drawWifiConnecting(nowMs);
    if (ctx.quietMode) drawQuietMarker();
    show();
    return;
  }

  if (ctx.wifi == WifiState::CaptivePortal) {
    drawCaptivePortal(nowMs);
    if (ctx.quietMode) drawQuietMarker();
    show();
    return;
  }

  if (ctx.wifi == WifiState::Disconnected) {
    drawWifiDisconnected(nowMs);
    if (ctx.quietMode) drawQuietMarker();
    show();
    return;
  }

  // ---------- API/data states ----------
  if (ctx.api == ApiState::Error) {
    drawApiError(nowMs);
    if (ctx.quietMode) drawQuietMarker();
    show();
    return;
  }

  if (ctx.api == ApiState::Fetching) {
    drawApiFetching(nowMs);
    if (ctx.quietMode) drawQuietMarker();
    show();
    return;
  }

  if (ctx.api == ApiState::Stale) {
    drawDataStale(nowMs);
    if (ctx.quietMode) drawQuietMarker();
    show();
    return;
  }

  // Short success pulses overlay normal behavior
  bool showedApiPulse = false;
  if (apiOkFlashStartMs_ > 0 && nowMs - apiOkFlashStartMs_ < 700) {
    drawApiOkPulse(nowMs);
    showedApiPulse = true;
  }

  bool showedWifiPulse = false;
  if (wifiConnectedFlashStartMs_ > 0 && nowMs - wifiConnectedFlashStartMs_ < 1200) {
    drawWifiConnectedFlash(nowMs);
    showedWifiPulse = true;
  }

  if (showedWifiPulse) {
    if (ctx.quietMode) drawQuietMarker();
    show();
    return;
  }

  // ---------- Aircraft / normal operation ----------
  if (ctx.specialLargeAircraft || ctx.traffic == TrafficAlert::Special) {
    drawSpecialAircraft(nowMs);
  } else if (ctx.hasAircraft) {
    if (ctx.traffic == TrafficAlert::Overhead || ctx.aircraftDistanceKm < 2.0f) {
      drawOverheadRipple(nowMs);
    } else {
      drawAircraftSector(ctx.aircraftBearingDeg, ctx.traffic, nowMs);
    }
  } else if (!showedApiPulse) {
    drawIdleGlow(nowMs);
  }

  // API OK pulse is small, so overlay it after aircraft/idle drawing.
  if (showedApiPulse) {
    drawApiOkPulse(nowMs);
  }

  if (ctx.quietMode) {
    drawQuietMarker();
  }

  show();
}

void LedStatus::flashWifiConnected(uint32_t nowMs) {
  wifiConnectedFlashStartMs_ = nowMs;
}

void LedStatus::flashApiOk(uint32_t nowMs) {
  apiOkFlashStartMs_ = nowMs;
}

void LedStatus::clear() {
  fill_solid(leds_, LED_COUNT, CRGB::Black);
}

void LedStatus::show() {
  FastLED.show();
}

int LedStatus::bearingToLed(float bearingDeg) const {
  while (bearingDeg < 0) bearingDeg += 360.0f;
  while (bearingDeg >= 360.0f) bearingDeg -= 360.0f;

  int index = (int)roundf((bearingDeg / 360.0f) * LED_COUNT) % LED_COUNT;
  index = (index + LED_INDEX_OFFSET) % LED_COUNT;
  if (index < 0) index += LED_COUNT;
  return index;
}

void LedStatus::fadeAll(uint8_t amount) {
  for (uint8_t i = 0; i < LED_COUNT; i++) {
    leds_[i].fadeToBlackBy(amount);
  }
}

void LedStatus::setSector(int center, int radius, const CRGB& color, uint8_t falloff) {
  for (int d = -radius; d <= radius; d++) {
    int idx = (center + d + LED_COUNT) % LED_COUNT;
    CRGB c = color;
    uint8_t fade = abs(d) * falloff;
    c.fadeToBlackBy(fade);
    leds_[idx] += c;
  }
}

void LedStatus::drawQuietMarker() {
  // Purple dot at bottom / 6 o'clock.
  leds_[12] += CRGB(55, 0, 70);
}

void LedStatus::drawBoot(uint32_t nowMs) {
  int pos = (nowMs / 55) % LED_COUNT;
  setSector(pos, 1, CRGB(80, 80, 80), 90);
}

void LedStatus::drawWifiConnecting(uint32_t nowMs) {
  int pos = (nowMs / 75) % LED_COUNT;
  setSector(pos, 2, CRGB(0, 0, 120), 70);
}

void LedStatus::drawWifiConnectedFlash(uint32_t nowMs) {
  uint32_t t = nowMs - wifiConnectedFlashStartMs_;
  bool on = ((t / 180) % 2) == 0;
  if (on) {
    fill_solid(leds_, LED_COUNT, CRGB(0, 90, 0));
  } else {
    fill_solid(leds_, LED_COUNT, CRGB::Black);
  }
}

void LedStatus::drawWifiDisconnected(uint32_t nowMs) {
  // Broken rotating red arc.
  int pos = (nowMs / 90) % LED_COUNT;
  for (int i = 0; i < LED_COUNT; i++) {
    bool inArc = ((i - pos + LED_COUNT) % LED_COUNT) < 8;
    bool broken = (i % 3) != 0;
    if (inArc && broken) leds_[i] = CRGB(100, 0, 0);
  }
}

void LedStatus::drawCaptivePortal(uint32_t nowMs) {
  bool phase = ((nowMs / 400) % 2) == 0;
  for (int i = 0; i < LED_COUNT; i++) {
    bool firstHalf = i < LED_COUNT / 2;
    if (firstHalf == phase) {
      leds_[i] = CRGB(0, 0, 70);
    } else {
      leds_[i] = CRGB(40, 40, 40);
    }
  }
}

void LedStatus::drawApiFetching(uint32_t nowMs) {
  int pos = (nowMs / 45) % LED_COUNT;
  setSector(pos, 1, CRGB(0, 95, 120), 80);
}

void LedStatus::drawApiError(uint32_t nowMs) {
  bool redPhase = ((nowMs / 220) % 2) == 0;
  CRGB c = redPhase ? CRGB(120, 0, 0) : CRGB(120, 55, 0);
  for (int i = 0; i < LED_COUNT; i += 2) {
    leds_[i] = c;
  }
}

void LedStatus::drawApiOkPulse(uint32_t nowMs) {
  // Small confirmation pulse at bottom.
  uint32_t t = nowMs - apiOkFlashStartMs_;
  uint8_t brightness = 0;
  if (t < 350) {
    brightness = map(t, 0, 350, 20, 110);
  } else {
    brightness = map(t, 350, 700, 110, 0);
  }

  CRGB c = CRGB(0, brightness, 0);
  leds_[11] += c / 3;
  leds_[12] += c;
  leds_[13] += c / 3;
}

void LedStatus::drawDataStale(uint32_t nowMs) {
  // Slowly shift between green and amber.
  uint8_t wave = beatsin8(8, 0, 100);
  CRGB c = blend(CRGB(0, 35, 0), CRGB(90, 45, 0), wave);
  fill_solid(leds_, LED_COUNT, c);
}

void LedStatus::drawIdleGlow(uint32_t nowMs) {
  uint8_t breathe = beatsin8(6, 5, 22);
  fill_solid(leds_, LED_COUNT, CRGB(0, breathe, 0));
}

void LedStatus::drawAircraftSector(float bearingDeg, TrafficAlert level, uint32_t nowMs) {
  int center = bearingToLed(bearingDeg);
  CRGB color = CRGB(0, 35, 0);
  int radius = 1;
  uint8_t falloff = 90;

  switch (level) {
    case TrafficAlert::Distant:
      color = CRGB(0, 20, 0);
      radius = 1;
      falloff = 110;
      break;

    case TrafficAlert::Nearby:
      color = CRGB(0, 80, 0);
      radius = 2;
      falloff = 70;
      break;

    case TrafficAlert::Close: {
      uint8_t pulse = beatsin8(35, 60, 130);
      color = CRGB(0, pulse / 2, pulse);
      radius = 2;
      falloff = 55;
      break;
    }

    case TrafficAlert::VeryClose: {
      uint8_t pulse = beatsin8(50, 80, 170);
      color = CRGB(pulse, 35, 0);
      radius = 3;
      falloff = 45;
      break;
    }

    default:
      color = CRGB(0, 45, 0);
      radius = 1;
      falloff = 85;
      break;
  }

  // Dim ambient background so direction remains obvious.
  fill_solid(leds_, LED_COUNT, CRGB(0, 4, 0));
  setSector(center, radius, color, falloff);
}

void LedStatus::drawOverheadRipple(uint32_t nowMs) {
  // Red/white expanding ring feel.
  uint8_t phase = (nowMs / 90) % 4;
  for (int i = 0; i < LED_COUNT; i++) {
    if ((i + phase) % 4 == 0) {
      leds_[i] = CRGB(150, 0, 0);
    } else if ((i + phase) % 4 == 1) {
      leds_[i] = CRGB(80, 80, 80);
    } else {
      leds_[i] = CRGB(20, 0, 0);
    }
  }
}

void LedStatus::drawEmergency(uint32_t nowMs) {
  bool on = ((nowMs / 120) % 2) == 0;
  fill_solid(leds_, LED_COUNT, on ? CRGB(180, 0, 0) : CRGB::Black);
}

void LedStatus::drawSpecialAircraft(uint32_t nowMs) {
  // Purple/cyan slow majestic sweep for A380/B747/rare aircraft.
  int pos = (nowMs / 100) % LED_COUNT;
  fill_solid(leds_, LED_COUNT, CRGB(4, 0, 8));
  setSector(pos, 3, CRGB(80, 0, 130), 45);
  setSector((pos + 12) % LED_COUNT, 2, CRGB(0, 70, 100), 60);
}
