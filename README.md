# MUC Desk Radar

This is my tiny live aircraft radar for the desk.

I built it because I live around Freising, close enough to Munich Airport that the sky is never really empty. Sometimes there is a normal Lufthansa arrival, sometimes a cargo jet, sometimes a helicopter, sometimes something much more interesting. I wanted a little object on my table that could quietly answer the question:

**"What is that plane?"**

So this became a small round radar screen powered by an ESP32-C3 and a 1.28 inch circular display. It tracks live aircraft near my Freising area and also keeps an eye on Munich Airport / MUC / EDDM.

Real build photos will be added later. For now, this repository is the public project page for the idea, the features, and the finished behaviour.

---

## What it is

MUC Desk Radar is a standalone desk gadget that shows real aircraft traffic on a round screen.

It does not need a phone app, laptop window, browser tab, or external server running next to it. Once powered, it pulls live public aviation data, draws the aircraft on the display, rotates through different pages, and highlights the aircraft that are actually worth looking at.

The fun part is that it is not just a dot map. It tries to behave more like a tiny spotting assistant.

It can show:

- aircraft near my Freising location
- aircraft around Munich Airport
- arrivals and departures at MUC
- nearby aircraft with distance, speed, altitude, and direction
- helicopters
- rare or interesting aircraft
- emergency squawks
- Munich weather from METAR
- a live estimate of where to look when something is passing nearby

---

## Why I Think It Is Cool

Most flight trackers are made for phones or big screens. This one is different because it is a physical object. It just sits there and makes the airspace feel visible.

I like that it turns normal background traffic into something you can glance at. If a plane passes over Freising, the radar card can show what it is, how far away it is, whether it is climbing or descending, and roughly when it will be closest.

It also watches Munich Airport separately, so it is not only about my immediate sky. The device can show MUC arrivals, departures, ground movement, weather, runway-style layout, and airport traffic in one small round display.

The best moments are when the "coolest aircraft" page catches something unusual. A big cargo plane, a military callsign, a special type, a low overhead pass, or just a plane that makes you go "wait, what is that?"

---

## The Pages

The device has six main pages. It rotates through them by itself, but it can also be changed manually with the button.

| Page | What it does |
|---|---|
| **Home Radar** | Shows live traffic around my Freising area on a north-up radar screen. Aircraft move smoothly instead of just jumping between updates. |
| **MUC Map** | Shows Munich Airport traffic with the real airport orientation in mind, including arrivals, departures, ground traffic, wind, and temperature. |
| **Traffic Brief** | A quick summary of what matters right now: nearest aircraft, coolest aircraft, helicopters, emergency traffic, and general activity. |
| **Nearest Aircraft** | A full tracking card for the closest airborne aircraft, with callsign, aircraft type, airline/route when available, speed, altitude, distance, climb/descent, and closest approach. |
| **Coolest Aircraft** | Tracks the most interesting aircraft currently visible and explains why it was picked. |
| **MUC Weather** | Decodes Munich Airport METAR into readable weather: wind, visibility, clouds, temperature, pressure, and flight category. |

---

## Freising + Munich Tracking

The radar is built around two views of the sky:

**My local sky near Freising**  
This is the personal part. It watches the airspace around where I actually am, so the aircraft on the screen are aircraft I might really hear or see outside.

**Munich Airport / MUC / EDDM**  
This is the airport part. Munich is close enough to make the traffic interesting, and busy enough that there is almost always something happening. The MUC page is built for arrivals, departures, airport weather, and runway-style movement rather than just random dots.

I am not publishing exact private coordinates or personal network details here. The public repo is meant to show the project, not expose my setup.

---

## Hardware

The build is intentionally small:

| Part | Why it is there |
|---|---|
| **ESP32-C3 Super Mini** | Small microcontroller board that runs the whole thing. |
| **1.28 inch round GC9A01 display** | The circular radar face. |
| **USB-C power** | Keeps it simple as a desk device. |

That is basically the whole physical idea: a small board, a round screen, and a case. I want it to feel like a little instrument, not a messy electronics demo.

---

## Things It Can Notice

The radar does more than just list aircraft.

It can pick out:

- the nearest aircraft
- the most interesting aircraft
- helicopters
- aircraft passing close to my location
- flights arriving into Munich
- flights departing from Munich
- aircraft on or near the airport
- rare aircraft types
- military or government-style callsigns
- emergency squawks
- weather changes at Munich Airport

The "coolest aircraft" logic is one of my favourite parts. A normal airliner is fine, but an A380, An-124, C-17, special callsign, helicopter, low pass, or emergency situation should not be treated like just another dot.

---

## What I Am Keeping Private

This repository is a showcase, not a full firmware dump.

I am intentionally not publishing:

- firmware files
- personal configuration
- exact private location
- network names or passwords
- API-style secrets
- unfinished local build notes

The public version is for showing what the project does and why I built it. The private version stays on my machine.

---

## Current Status

The project is already working as a live desk radar. It can run through the six pages, track aircraft around Freising, watch Munich Airport traffic, decode MUC weather, and highlight interesting planes automatically.

Next things I want to add here:

- real photos of the finished device
- maybe a short video of the pages rotating
- cleaner product-style screenshots
- notes from using it day to day

---

## Why This Exists

Because planes are more fun when the sky has subtitles.

And because a tiny round screen showing live Munich traffic from my desk is exactly the kind of unnecessary thing I wanted to build.

*Built around Freising and Munich Airport. Made for my desk, but hopefully fun for other aviation nerds too.*
