/*
  display_test_v1.ino
  GC9A01 round display bring-up test for the ESP32-C3 Plane Radar project.

  Wiring (matches docs/wiring_notes.md and docs/display_wiring_guide.html):
    VCC -> 3V3   GND -> GND
    SCL -> GPIO4   SDA -> GPIO3
    DC  -> GPIO10  CS  -> GPIO1   RST -> GPIO0

  Library required: "Adafruit GC9A01A" (installs Adafruit GFX automatically)

  What you should see:
    1. Red, green, blue full-screen flashes (proves data lines work)
    2. A mock "plane info" page with a compass ring
*/

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_GC9A01A.h>

#define TFT_SCLK 4
#define TFT_MOSI 3
#define TFT_DC   10
#define TFT_CS   1
#define TFT_RST  0

Adafruit_GC9A01A tft(TFT_CS, TFT_DC, TFT_RST);

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("GC9A01 display test starting...");

  // Route hardware SPI to our custom pins (ESP32-C3)
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  tft.begin();
  tft.setRotation(0);
  Serial.println("Display initialised.");

  // --- Step 1: color flashes ---
  tft.fillScreen(GC9A01A_RED);   delay(600);
  tft.fillScreen(GC9A01A_GREEN); delay(600);
  tft.fillScreen(GC9A01A_BLUE);  delay(600);
  tft.fillScreen(GC9A01A_BLACK);

  // --- Step 2: mock plane info page ---
  drawPlanePage();
  Serial.println("Test page drawn. If you can read it, wiring is good!");
}

void drawPlanePage() {
  tft.fillScreen(GC9A01A_BLACK);

  // Compass ring
  tft.drawCircle(120, 120, 118, GC9A01A_DARKGREY);
  tft.drawCircle(120, 120, 117, GC9A01A_DARKGREY);
  tft.setTextColor(GC9A01A_CYAN);
  tft.setTextSize(1);
  tft.setCursor(116, 8);   tft.print("N");
  tft.setCursor(116, 224); tft.print("S");
  tft.setCursor(8, 116);   tft.print("W");
  tft.setCursor(226, 116); tft.print("E");

  // Title
  tft.setTextColor(GC9A01A_YELLOW);
  tft.setTextSize(2);
  tft.setCursor(52, 40);
  tft.print("PLANE RADAR");

  // Mock aircraft info
  tft.setTextColor(GC9A01A_WHITE);
  tft.setTextSize(3);
  tft.setCursor(56, 78);
  tft.print("DLH453");

  tft.setTextSize(2);
  tft.setTextColor(GC9A01A_GREEN);
  tft.setCursor(60, 118); tft.print("DIST 12.4km");
  tft.setCursor(60, 144); tft.print("ALT  2850m");
  tft.setCursor(60, 170); tft.print("SPD  420kt");

  tft.setTextSize(1);
  tft.setTextColor(GC9A01A_DARKGREY);
  tft.setCursor(74, 205);
  tft.print("display test v1 - mock data");
}

void loop() {
  // Blink a small heartbeat dot so you can tell the sketch is alive
  static bool on = false;
  static uint32_t last = 0;
  if (millis() - last > 1000) {
    last = millis();
    on = !on;
    tft.fillCircle(120, 60, 4, on ? GC9A01A_ORANGE : GC9A01A_BLACK);
  }
}
