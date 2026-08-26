# MUC Desk Radar

This is my tiny live aircraft radar for the desk. 🛩️

I built it because I live around Freising, close enough to Munich Airport that the sky is never really empty. Sometimes there is a normal Lufthansa arrival, sometimes a cargo jet, sometimes a helicopter, sometimes something much more interesting. I wanted a small object on my table that could quietly answer the question:

**"What is that plane?"**

So this became a round little radar screen watching the sky above Freising and Munich Airport / MUC / EDDM.

---

## What it is 🧭

MUC Desk Radar is a standalone desk gadget that shows live aircraft traffic on a round screen. No phone, no app, no server — the device talks to free public APIs directly over Wi-Fi.

It is not just a list of flights. It behaves more like a tiny spotting assistant:

- 🏠 it shows aircraft around my local sky
- 🛬 it tracks Munich Airport arrivals and departures
- 🎯 it marks aircraft by direction, altitude and importance
- 📍 it shows the nearest aircraft with distance, altitude, speed and closest approach
- ⭐ it has a "coolest aircraft" page for rare or unusual traffic
- 🛰️ it tracks the ISS and counts down to the next rocket launch
- 🔍 it notices what aircraft are *doing* — holding, going around, circling
- 📈 it learns what a normal week looks like and tells you when today is not
- 🌦️ it decodes Munich weather into something readable

The nice thing is that it makes the sky feel physical. I can hear something outside, glance at the radar, and usually get a pretty good idea of what it is.

---

## Why I think it is cool ✨

Most flight trackers live on phones, browsers or huge maps. This one is different because it is a real object. It sits on the desk like a tiny cockpit instrument.

The display is only 240 × 240 pixels, so everything had to be designed for quick glances. Big callsigns, simple colours, tiny symbols, a round radar scope, and pages that show only what matters.

My favourite part is that it does not treat every aircraft equally. A regular short-haul airliner is useful to see, but an A380, a military transport, a helicopter orbiting overhead, an aircraft stuck in a holding pattern or an emergency squawk should stand out immediately.

---

## The pages 📄

Press the MODE button to move through them. Any page can be switched off in the settings.

| Page | What it shows |
|---|---|
| **RADAR** | The scope. Range rings, a slow sweep, aircraft as little arrows coloured by altitude, each trailing the track it has flown |
| **AIRPORT** | The runways drawn to scale with the traffic on top — you can see which way the field is landing |
| **TRAFFIC** | How busy the sky is right now, broken down by climbing, descending, high, on the ground, rotary, military, with the last hour as a sparkline |
| **NEAREST** | The closest aircraft in detail: airline, type, registration, route, and closest point of approach |
| **COOLEST** | The most interesting aircraft up there, not just the closest |
| **SPACE** | Where the ISS is in the sky with its ground track drawn behind and ahead of it, when it next comes over, and a countdown to the next four launches |
| **WEATHER** | Munich's METAR decoded — wind rose, visibility, cloud, pressure |
| **PATTERNS** | What aircraft are actually doing: holding, go-arounds, circling helicopters, survey flights |
| **TRENDS** | Today's traffic against what this device has learned is normal for this hour |
| **SYSTEM** | Frame rate, memory, uptime, Wi-Fi, how reliable the data feed has been |
| **ABOUT** | Chevrons in orbit, a slow ping going out, and the build credit. Switchable off in the settings |

There is also a full-screen alert if anything squawks 7500, 7600 or 7700.

**Two circles, and they are not the same circle.** The *range* is what fits on
the radar page — a drawing decision, and the one the auto-ranger moves. *Your
sky* is a fixed radius, 100 km by default, that decides what counts as "around
here": the traffic count, what Nearest and Coolest are allowed to reach for, and
the sky the device learns week by week for Trends. Every page that measures a
circle now prints which circle it measured, so the two can never quietly
disagree.

---

## Hardware 🔌

Two parts, plus a button.

- **ESP32-S3-WROOM-1 N16R8** — dual core, 8 MB PSRAM, 16 MB flash
- **GC9A01 1.28" round IPS display**, 240 × 240
- One button to GND for changing pages
- Powered over USB-C

Seven display connections:

| ESP32-S3 | Display | |
|---|---|---|
| GPIO12 | SCL | SPI clock |
| GPIO11 | SDA | SPI data |
| GPIO13 | DC | data/command |
| GPIO10 | CS | chip select |
| GPIO14 | RST | reset |
| 3V3 | VCC | **3.3 V only — never 5 V** |
| GND | GND | |

This seven-pin display has no BLK connection; its backlight is always on. MODE uses the board's onboard **BOOT button on GPIO0**.

Two things that will save you an evening: the display is **not 5 V tolerant**, and the SPI wires should be **under 10 cm** or the 80 MHz bus produces sparkle and torn rows that look exactly like a software bug.

---

## Using it 📶

The first time it powers on, the radar opens its own **`DESK-RADAR-xxxx`** Wi-Fi network. Join it from a phone or laptop, choose the home Wi-Fi, enter the password, and save. The device then connects directly to its public data sources; there is no companion app or account.

The Munich and Estonia devices each have their own fixed reference airport and runway geometry. The settings page controls the display pages, radar and activity ranges, visual behaviour, refresh timing, alerts, status light, clock and personalisation. The display has no controllable backlight, so there is no brightness or night-dimming setting.

Press MODE to browse. Hold it for about two seconds to reopen settings. The status light walks **violet → blue → amber → cyan → green** during startup, so the device can explain where it stopped even if the display is blank.

---

## Where the data comes from 🌐

All free, none of them need an account:

- **[adsb.lol](https://adsb.lol/)** — live aircraft positions
- **[adsb.fi](https://adsb.fi/)** — backup if the first one is having a bad day

Both are volunteer-run and free for personal, non-commercial use, and neither
needs an account — which is the point, on a device meant to outlive my attention.
The fallback used to be airplanes.live; it went feeder-only in April 2026 and
started returning 403 to everyone else.
- **adsbdb.com** — aircraft type, airline and route
- **aviationweather.gov** — Munich METAR
- **wheretheiss.at** — ISS position
- **The Space Devs** — upcoming launches

---

## A note on giving one away 🎁

The second one is built. It is going to a friend in **Tartu, Estonia**, watching **Tallinn Airport (EETN)** — which is why there are two firmware folders rather than one.

The person receiving it plugs it in, enters their own Wi-Fi, and it becomes *their* radar without ever seeing a line of code. Her exact address is not in this repository; the Estonia build uses Tartu as its home centre and Tallinn Airport as its fixed reference field.

**Before it leaves the house:** soak it for a day and check FETCH OK on the
System page is above 98%, then press **Erase all settings** in the setup page —
that clears the Wi-Fi password I used for testing. *Restart learning* is the
wrong button; it deliberately keeps the network details. Then power-cycle and
check it comes up asking for Wi-Fi, which is how she should find it.

There is a **[field guide](docs/desk_radar_field_guide_ee.html)** for her too — every page, every colour, and how each number is worked out, with real screenshots taken from the firmware's own drawing code, and a panel at the top explaining her two centres. [Mine is here](docs/desk_radar_field_guide.html); the two are the same document with different screenshots.

---

## What is in this repository 📁

```
docs/
  desk_radar_field_guide.html      illustrated Munich field guide
  desk_radar_field_guide_ee.html   illustrated Estonia field guide
  spotters_field_manual.pdf        printable spotting guide
  renders/                         product and screen visuals
design_concepts/                   enclosure concept images
```

This is deliberately the presentation repository: README, images, HTML guides and PDF material only. The two firmware projects live separately in the private [`desk_radar_esp32_personal`](https://github.com/haldarsaurav/desk_radar_esp32_personal) repository; no source code or private configuration is published here.
