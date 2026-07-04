# MUC Desk Radar

This is my tiny live aircraft radar for the desk. 🛩️

I built it because I live around Freising, close enough to Munich Airport that the sky is never really empty. Sometimes there is a normal Lufthansa arrival, sometimes a cargo jet, sometimes a helicopter, sometimes something much more interesting. I wanted a small object on my table that could quietly answer the question:

**"What is that plane?"**

So this became a round little radar screen powered by an ESP32-C3 and a 1.28 inch circular display. It watches aircraft near my Freising area and also keeps an eye on Munich Airport / MUC / EDDM.

Real build photos will be added later. For now, this is the project page for what the device does, what the pages show, and why I think it is such a fun little desk instrument.

---

## What it is

MUC Desk Radar is a standalone desk gadget that shows live aircraft traffic on a round screen.

It is not just a list of flights. It behaves more like a tiny spotting assistant:

- it shows aircraft around my local Freising sky
- it tracks Munich Airport arrivals and departures
- it marks aircraft by direction, type, and importance
- it shows the nearest aircraft with distance, altitude, speed, and closest approach
- it has a special "coolest aircraft" page for rare or unusual traffic
- it decodes Munich weather into something readable
- it rotates through the pages by itself, but still has a button if I want to pause or browse

The nice thing is that it makes the sky feel physical. I can hear something outside, glance at the radar, and usually get a pretty good idea of what it is.

---

## Why I Think It Is Cool ✨

Most flight trackers live on phones, browsers, or huge maps. This one is different because it is a real object. It sits on the desk like a tiny cockpit instrument.

The display is only 240 x 240 pixels, so everything had to be designed for quick glances. Big callsigns, simple colours, tiny symbols, a round radar scope, and pages that show only what matters.

My favourite part is that it does not treat every aircraft equally. A regular short-haul airliner is useful to see, but an A380, An-124, C-17, helicopter, special callsign, low overhead pass, or emergency squawk should stand out immediately. That is what the "coolest aircraft" logic is for.

---

## The Pages

The radar has six main pages. They rotate automatically, so the device feels alive even when I am not touching it.

| Page | What you see |
|---|---|
| **Home Radar** | A north-up radar scope centred around my Freising area. Aircraft slide across range rings, with a small centre dot for my location and a north marker at the top. |
| **MUC Map** | A Munich Airport view with the real runway direction in mind. It shows arrivals, departures, ground traffic, wind, temperature, and the next interesting MUC movements. |
| **Traffic Brief** | A compact "what matters right now" page: nearest aircraft, coolest aircraft, nearest helicopter, emergency traffic, and how busy the local sky is. |
| **Nearest Aircraft** | A full tracking card for the closest airborne aircraft: callsign, aircraft type, airline/route when known, speed, altitude, distance, climb/descent, and closest approach. |
| **Coolest Aircraft** | Similar to the nearest page, but locked onto the most interesting aircraft in range. It also gives a short reason for why it was picked. |
| **MUC Weather** | Munich Airport weather decoded from METAR: wind, visibility, clouds, temperature, pressure, weather state, runway estimate, and flight condition. |

---

## Page Details

### 1. Home Radar 🟢

This is the main "what is above me?" page.

It shows aircraft around my Freising area on a circular radar scope. The radar is north-up, so aircraft positions make sense when I compare them to the real sky outside.

What it shows:

- range rings for the local airspace
- live aircraft positions
- smooth movement between data updates, so aircraft glide instead of jumping
- a centre marker for my location
- a red north marker at the top
- total nearby traffic count
- Munich-style arrival and departure counters when aircraft look like they are heading toward or away from MUC

This is the page I would glance at if I hear a plane and want to know where it is.

### 2. MUC Map 🔴🟢

This page is for Munich Airport.

Instead of showing only my local sky, it focuses on MUC / EDDM. The runway layout is drawn as a simplified airport map, with the parallel Munich runways represented in their real orientation.

What it shows:

- airport traffic around Munich
- arrivals in green
- departures in red
- ground traffic in purple
- aircraft physically on the runway as yellow runway blocks
- farther airport traffic as blue rim dots
- wind and temperature from Munich weather
- DEP / GND / ARR counters
- next arrival and next departure labels when available

I like this page because it makes Munich feel like a tiny live airport board, but on a round watch-sized screen.

### 3. Traffic Brief 📋

This is the quick summary page.

It does not try to show everything. It just answers: "what should I care about right now?"

Rows on this page:

- **NEAR**: the closest airborne aircraft
- **COOL**: the aircraft with the highest coolness score
- **HELI**: the nearest helicopter
- **EMG**: any aircraft showing an emergency squawk

If the sky is boring, this page stays calm. If something special appears, it becomes the fast way to notice it.

### 4. Nearest Aircraft 📍

This page follows the closest airborne aircraft.

It shows a card with the useful spotting details:

- callsign
- aircraft type
- airline or operator when known
- route when available
- altitude
- speed
- distance from me
- climb or descent trend
- closest point of approach
- a red rim blip showing roughly where to look

The closest point of approach is one of the best features. It can tell me that a plane is not just nearby now, but that it will pass closest in a short time.

### 5. Coolest Aircraft ⭐

This is the fun page.

The device scores aircraft and picks the most interesting one currently visible. It might be the rarest aircraft type, a special callsign, a military transport, a helicopter, a low pass, or an aircraft involved in something urgent.

The page shows the same practical data as the nearest card, but the reason line is different. It might say the aircraft is a rare type, an emergency squawk, a Munich arrival, a departure, or a helicopter nearby.

### 6. MUC Weather 🌦️

This page turns Munich METAR weather into a small airport weather board.

It shows:

- wind
- visibility
- cloud layer
- temperature
- pressure
- weather state
- estimated active runway direction from the wind
- flight condition from good to bad

The flight condition is colour-coded, so it is easy to read at a glance: green is good, amber is marginal, and red means poor conditions.

---

## Symbols And Colours

The screen has its own little grammar. Once you know the colours, the pages become very fast to read.

| Symbol / colour | Meaning |
|---|---|
| **Triangle** | Normal fixed-wing aircraft, pointing in the direction it is moving. |
| **Star** ⭐ | A notable or cool aircraft. Usually something rare, special, or worth looking up from the desk for. |
| **Rotor-style mark** 🚁 | Helicopter. These get separated because they behave differently and are fun to catch locally. |
| **Red "!" pill** | Emergency squawk. This gets priority over everything else. |
| **Yellow runway block** | Aircraft on a runway at Munich. A bigger block means a heavier aircraft. |
| **Blue rim dot** | Aircraft near the edge of the airport map view. It is nearby enough to matter, but outside the main drawn area. |
| **Red number / marker** | Departure-style traffic. On the MUC page this means aircraft moving away from Munich. |
| **Green number / marker** | Arrival-style traffic. On the MUC page this means aircraft moving toward Munich. |
| **Purple dot / number** | Ground traffic at Munich. Taxiing, parked, or slow airport movement. |
| **White number** | Total aircraft count. |
| **Amber text** | The main highlighted thing on the page, like the page title or selected callsign. |
| **Cyan text** | Useful supporting information, like routes, closest approach, or weather/runway details. |

The idea is that I should not have to read every label. Red, green, purple, yellow, star, rotor, and emergency all mean something immediately.

---

## The Cool Aircraft Logic

This is the part that makes it feel less like a normal tracker and more like a tiny plane-spotter.

Every aircraft gets a rough "coolness" score. The score can come from the aircraft type, the callsign, or the situation.

Examples of aircraft that score high:

- **A380**: superjumbo, always worth noticing
- **An-124**: rare heavy cargo beast
- **C-17 / C-5 / C-130 / A400M**: military transport aircraft
- **B747 / 747-8**: still special enough to deserve attention
- **Eurofighter / F-16 / F-35**: military fast jets
- **MD-11 / A340-500 / A340-600**: rare types that are not everyday traffic anymore
- **B787 / A330**: not as rare, but still more interesting than a tiny common narrowbody

Callsigns can also make an aircraft interesting:

- government flights
- military callsigns
- NATO / tanker-style callsigns
- special transport callsigns
- unusual operators around Munich

Then there are situation bonuses:

- low overhead pass
- fast and low aircraft
- aircraft lining up for Munich
- aircraft climbing out of Munich
- helicopter nearby
- emergency squawk

If the score is high enough, the aircraft gets a star. If it is extremely interesting or urgent, the page can jump to show it instead of waiting for the normal carousel.

That is the behaviour I wanted: normal traffic stays readable, but genuinely interesting traffic gets to interrupt.

---

## Freising + Munich Tracking

The radar is built around two views of the sky.

**My local sky near Freising**  
This is the personal part. It watches the airspace around where I actually am, so the aircraft on the screen are often aircraft I can hear or see outside.

**Munich Airport / MUC / EDDM**  
This is the airport part. Munich is close enough to make the traffic interesting, and busy enough that there is almost always something happening. The MUC page is built for arrivals, departures, airport weather, runway movement, and traffic flow rather than just random dots.

Together, those two views make the device feel useful: local enough for my window, but airport-aware enough to understand Munich traffic.

---

## How It Works

The device reads live public aviation and weather data, then turns it into a display that makes sense on a very small round screen.

The basic flow is:

- get live aircraft positions around the local area and Munich
- calculate distance, bearing, altitude, speed, and direction
- estimate where aircraft move between updates so the display feels smooth
- decide whether aircraft look like arrivals, departures, ground traffic, helicopters, or special traffic
- fetch route/operator information when available
- decode Munich Airport weather into readable rows
- choose which aircraft deserves the "nearest" and "coolest" pages
- draw everything with simple symbols and strong colours

The hard part was not only getting the data. The harder part was making it readable on a tiny circular display. Text near the edge gets clipped, labels can overlap, and too many aircraft can turn into visual noise. So the pages are deliberately compact: small symbols, short labels, colour meaning, and only the most useful aircraft getting full detail.

---

## Hardware

The build is small.

| Part | Why it is there |
|---|---|
| **ESP32-C3 Super Mini** | The small board running the whole device. |
| **1.28 inch round GC9A01 display** | The circular radar face. |
| **USB-C power** | Simple desk power. |

That is basically the whole physical idea: a small board, a round screen, and a case. I want it to feel like a little instrument, not a messy electronics demo.

---

## What You See On The Desk

Most of the time, it quietly rotates:

Home radar, Munich airport, traffic brief, nearest aircraft, coolest aircraft, weather, then back around again.

If nothing special is happening, it is just a calm little live radar. If something fun appears, it can surface that aircraft by itself. That is what makes it addictive: the device is not only showing data, it is choosing what might be worth looking at.

*Built around Freising and Munich Airport.* ✈️
