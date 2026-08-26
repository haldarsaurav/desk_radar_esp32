# MUC Desk Radar

<p align="center">
  <img src="docs/renders/rev5/munich/radar.png" width="420" alt="Real MUC Desk Radar screen showing live aircraft around Freising">
</p>

<p align="center">
  <strong>A tiny, physical window into the sky above Munich.</strong>
</p>

MUC Desk Radar is a standalone Wi-Fi aircraft tracker built around an ESP32-S3 and a 240 × 240 round display. It turns live ADS-B positions, airport weather, orbital data and its own learned traffic history into a quiet desk instrument.

There is no phone app, subscription, account or home server. Once Wi-Fi is configured, the device talks directly to public data sources and draws everything locally.

It answers the simple question that started the project—**“What is that plane?”**—but it also answers more interesting ones:

- Is that aircraft arriving, departing, holding, circling, surveying or going around?
- What will pass closest to home if every aircraft keeps its present course?
- Which contact is the most unusual—not merely the nearest?
- Is the sky genuinely busier than it normally is on this weekday and hour?
- Which runway direction is probably active?
- Where is the ISS in my sky, and when is its next useful pass?

> This repository is the public, presentation-only home of the project: explanations, real renders, HTML guides, PDFs, wiring information and enclosure concepts. It intentionally contains **no firmware source and no private configuration**.

---

## At a glance

| Item | Detail |
|---|---|
| Display | 1.28-inch GC9A01 round IPS, 240 × 240 pixels |
| Controller | ESP32-S3-WROOM-1 N16R8, dual core, 16 MB flash, 8 MB PSRAM |
| Local controls | One MODE button; all configuration is through a captive web page |
| Main aircraft refresh | Approximately every 8 seconds |
| Drawing rate | Approximately 40 frames per second |
| Stored contacts | Up to the nearest 96 contacts from the shared aircraft request |
| Radar AUTO ladder | 10, 15, 20, 30, 50, 75, 100, 150 or 250 km |
| Behavior memory | Up to 64 aircraft tracks × 48 position fixes |
| Traffic memory | 60 one-minute samples plus a learned 7 × 24-hour weekly model |
| Special alerts | Squawk 7500, 7600 and 7700 |

---

## Real screens from the firmware

These are not UI mock-ups. They were rendered by the same page-drawing code used on the physical display.

| Live radar | Munich Airport | Traffic now |
|---|---|---|
| <img src="docs/renders/rev5/munich/radar.png" width="240" alt="Munich radar page"> | <img src="docs/renders/rev5/munich/airport.png" width="240" alt="Munich airport page"> | <img src="docs/renders/rev5/munich/traffic.png" width="240" alt="Munich traffic page"> |
| Nearest aircraft | Coolest aircraft | Space |
| <img src="docs/renders/rev5/munich/nearest.png" width="240" alt="Nearest aircraft page"> | <img src="docs/renders/rev5/munich/coolest.png" width="240" alt="Coolest aircraft page"> | <img src="docs/renders/rev5/munich/space.png" width="240" alt="ISS and launches page"> |
| Weather | Detected patterns | Learned trends |
| <img src="docs/renders/rev5/munich/weather.png" width="240" alt="Weather page"> | <img src="docs/renders/rev5/munich/patterns.png" width="240" alt="Flight patterns page"> | <img src="docs/renders/rev5/munich/trends.png" width="240" alt="Traffic trends page"> |
| System health | About | Connection recovery |
| <img src="docs/renders/rev5/munich/system.png" width="240" alt="System page"> | <img src="docs/renders/rev5/munich/about.png" width="240" alt="About page"> | <img src="docs/renders/rev5/munich/connection.png" width="240" alt="Connection recovery page"> |

---

## The idea: a tiny spotting assistant

Most flight trackers are maps on a phone or browser. This is deliberately a physical object: closer to a miniature cockpit instrument than another app.

The circular screen is small, so the design favors information that works at a glance:

- arrowheads show aircraft direction;
- color shows altitude;
- motion leaders hint at speed;
- real trails show what a contact has actually done;
- short labels mark detected behavior;
- airport geometry is drawn at the correct orientation and scale;
- the most important values sit outside the moving radar layer, where sweep graphics cannot cover them.

The radar does not treat every aircraft equally. A routine airliner is useful, but a helicopter orbiting overhead, a heavy aircraft, a military flight, a fresh contact, a go-around or an emergency should win attention immediately.

---

## How a position becomes a symbol

For nearby aircraft, latitude and longitude are converted into a local east/north plane centered on home:

- north distance ≈ latitude difference × 111.195 km;
- east distance ≈ longitude difference × 111.195 km × cos(home latitude);
- range = √(east² + north²);
- true bearing = atan2(east, north), normalized to 0–359°.

The local approximation is fast and accurate at desk-radar ranges. Long-distance calculations, including ISS ground distance, use a great-circle/Haversine model with an Earth radius of 6,371 km.

Each valid contact is checked for plausible coordinates, altitude and speed. Between network updates, the last good fix is carried forward using track and groundspeed, so an aircraft moves smoothly instead of jumping once every eight seconds.

### Altitude color

Color is continuous: values are interpolated between these stops.

| Altitude | Color | Meaning on the display |
|---:|---|---|
| 0 ft | Orange | Ground or very low |
| 4,000 ft | Yellow | Low traffic |
| 12,000 ft | Green | Climbing/descending traffic |
| 24,000 ft | Cyan | High traffic |
| 34,000 ft | Blue | Typical cruise |
| 45,000 ft | Violet | Very high |

The palette makes height readable without spending precious pixels on a label beside every target.

### Velocity leaders

A short line in front of each aircraft communicates speed. The projection time grows with radar range:

**leader time = clamp(range in km × 1.2, 10 seconds, 90 seconds)**

Projected distance is groundspeed converted from knots to km/h, multiplied by the leader time. The final line is capped at 30% of the scope radius, so fast aircraft remain legible without drawing across the whole screen.

### Trails that show actual movement

The sweep has phosphor-style persistence, but aircraft trails are not decorative screen burn. They come from retained position history:

- up to 48 fixes are retained for a tracked aircraft;
- the radar uses the newest 14 points;
- the visible trail is capped at 22 pixels;
- very short sub-pixel segments are skipped;
- older segments fade smoothly.

The cap is in screen pixels rather than kilometers, so trails remain useful at every zoom level. Their time span depends on the live polling cadence; at the normal aircraft refresh rate they represent several minutes of recent movement.

This matters because a 450-knot aircraft on a 50 km scope moves only about 0.17 display pixels during a short phosphor decay. A frame-fade alone can show a rotating sweep, but it cannot show an honest aircraft path.

---

## Four radii, two centers

The device deliberately separates **what is drawn**, **what counts as local**, **what an airport page needs**, and **what must be downloaded**.

| Quantity | Center | Purpose | Changes what? |
|---|---|---|---|
| Radar range | Home | Size of the visible radar scope | Drawing only; AUTO may change it |
| Activity radius | Home | Definition of “your sky” | Traffic, Coolest, Patterns and Trends |
| Airport map range | Reference airport | Area visible around the runway diagram | Airport page only |
| Fetch radius | Shifted request center | One shared request large enough to contain both home and airport circles | Network coverage |

This presentation uses Freising as home and Munich Airport/MUC/EDDM as the reference airport.

The shared aircraft request is shifted and widened when necessary so both circles fit inside it. That avoids two simultaneous live-aircraft requests while still letting a distant home and airport have independent views.

This separation is also why AUTO zoom never silently changes the statistical meaning of “local traffic.” Trends remain comparable even while the picture zooms in and out.

---

## AUTO radar range

AUTO tries to keep the radar useful without nervous zooming.

### The range ladder

It chooses only from:

**10 → 15 → 20 → 30 → 50 → 75 → 100 → 150 → 250 km**

The user may set lower and upper bounds. The default AUTO window is 20–100 km.

### What it measures

Once per minute, AUTO counts airborne contacts inside every allowed rung. It chooses the smallest range containing at least six airborne aircraft. If no allowed rung reaches six, the widest allowed range becomes the target.

AUTO evaluates the complete airborne picture, even when the display is temporarily filtered to military-only traffic. A visual filter therefore cannot make the physical zoom hunt.

### Why it changes slowly

The first valid evaluation—after the feed has had time to settle—may adopt the ideal range immediately. After that:

- the same ideal must be requested for 10 consecutive evaluations, roughly 10 minutes;
- at least one hour must have passed since the previous range change;
- the range moves only one rung at a time;
- stale data or an emergency alert suspends the decision.

The result feels like an instrument adapting to the day, not a camera constantly zooming. The screen prints **AUTO** whenever this mode is active.

AUTO changes only the visible radar range. It does not change the fixed activity radius, the airport map or the learned traffic baseline.

---

## Every page, in detail

### RADAR — the live sky

<p align="center"><img src="docs/renders/rev5/munich/radar.png" width="360" alt="Radar page with range rings, trails and behavior labels"></p>

The main page is centered on home. Range rings provide scale, aircraft arrows show true track, and altitude color, velocity leaders and trails reveal three different dimensions without clutter.

The default sweep turns at about 6 rpm—one revolution every 10 seconds. It is rendered beneath the information layer, so the glow cannot erase a label or page status. Detected behaviors appear as short labels such as HOLD, CIRC, SURV or G/A.

### AIRPORT — runway geometry and operations

<p align="center"><img src="docs/renders/rev5/munich/airport.png" width="360" alt="Munich Airport view"></p>

This page is centered on the reference airport, not on home. Runways are drawn using their real headings, lengths and relative positions. Aircraft are classified as arrivals, departures or ground traffic.

The likely active landing end is chosen from METAR wind: the runway direction pointing most nearly into the wind wins. If usable wind data is missing, the device can infer a likely direction from descending aircraft near the field and marks that answer in amber to show that it is inferred rather than reported.

Ground symbols are intentionally different:

- amber chevron: moving on a runway;
- violet stub: taxiing;
- dim violet dot: parked or nearly stationary.

### TRAFFIC — what the sky is doing now

<p align="center"><img src="docs/renders/rev5/munich/traffic.png" width="360" alt="Traffic page showing counts, direction rose and one-hour history"></p>

TRAFFIC summarizes the fixed activity circle:

- airborne now;
- climbing and descending;
- above 30,000 ft;
- on the ground;
- helicopters;
- military aircraft.

An eight-sector direction rose shows where traffic is distributed around home. The one-hour sparkline is built from 60 one-minute buckets. Multiple live samples during each minute are accumulated and committed as a rounded mean, so one noisy fetch does not become the entire minute.

The current ten-minute mean is compared with the preceding ten-minute mean to color the short-term trend.

### NEAREST — the closest contact and its future miss distance

<p align="center"><img src="docs/renders/rev5/munich/nearest.png" width="360" alt="Nearest aircraft detail page"></p>

NEAREST chooses the retained airborne contact with the smallest current distance from home, then adds enrichment such as type, registration, operator and route when available.

It also calculates closest point of approach (CPA). In a local east/north plane:

- position vector p = (east, north);
- velocity vector v comes from track and groundspeed;
- time to CPA = −(p · v) / (v · v);
- CPA distance = |p + v × time|.

If speed is 1 knot or less, or the mathematical CPA lies in the past, there is no future CPA to show. The detail page displays it only when the predicted closest point is less than 30 minutes away.

CPA assumes constant course and constant groundspeed. It is a useful short-range projection, not an air-traffic-control prediction.

<details>
<summary><strong>Exact selection boundary</strong></summary>

In the current firmware, NEAREST searches all airborne contacts retained from the shared fetch. Unlike COOLEST, TRAFFIC, PATTERNS and TRENDS, its selector is not clipped again to the activity radius. This detail matters mainly when the home and reference-airport circles are far apart.

</details>

### COOLEST — interesting beats merely close

<p align="center"><img src="docs/renders/rev5/munich/coolest.png" width="360" alt="Coolest aircraft detail page"></p>

COOLEST searches the fixed activity circle and gives every airborne contact an interest score:

**score = 2 × behavior interest + type bonuses + rarity bonuses + altitude / 20,000**

| Signal | Score added |
|---|---:|
| Emergency descent | 200 |
| Go-around | 190 |
| Holding pattern | 140 |
| Survey pattern | 130 |
| Circling | 120 |
| Military | 100 |
| Rotorcraft | 70 |
| Never seen since boot | 45 |
| Seen fewer than three times | 15 |
| Heavy aircraft category | 20 |
| Fast climb | 50 |
| Approach | 24 |
| Departure | 20 |
| Altitude | Small tie-break: about 2 points at 40,000 ft |

A brand-new contact receives both the “never seen” and “rare” bonuses on its first evaluation, for 60 points total.

Rarity is deliberately lightweight: a 512-slot in-memory table indexed from the aircraft address. It resets on reboot. If two addresses collide in one slot, the new one is treated as familiar rather than inventing a false rarity event. That can miss an occasional novel aircraft, but it avoids falsely announcing a routine contact as special.

### PATTERNS — detecting what an aircraft is doing

<p align="center"><img src="docs/renders/rev5/munich/patterns.png" width="360" alt="Patterns page showing go-around, holding, survey and circling detections"></p>

The detector does not decide from a single arrow. It builds track statistics from recent fixes:

- signed and absolute accumulated turn;
- actual path length and straight-line displacement;
- bounding-box span;
- minimum and maximum altitude;
- distance trend from home;
- heading reversals greater than 140°;
- vertical rate, speed, sample count and elapsed time.

The principal detectors use these conditions:

| Behavior | Main evidence required |
|---|---|
| Holding | At least 8 samples and 60 s; ≥120 kt; at least 300° net turn; span ≤22 km; altitude spread ≤2,500 ft; displacement ≤35% of flown path; fewer than 2 reversals |
| Circling | At least 6 samples and 45 s; at least 300° turn; span ≤6 km; speed ≤160 kt; fewer than 2 reversals |
| Survey | At least 12 samples and 120 s; at least 3 heading reversals; path ≥8 km; displacement ≤30% of path; altitude spread ≤2,000 ft |
| Go-around | At least 12 samples; a genuine low point followed by ≥700 ft climb after ≥700 ft descent; current climb ≥300 ft/min; low point near airport ≤4,000 ft, elsewhere ≤2,500 ft |
| Emergency descent | Current vertical rate ≤−3,500 ft/min and more than 3,000 ft lost in the history window |

If none of those shapes is present, simpler state labels can describe a fast climb, approach, departure, high-level overflight or ordinary level flight.

This is behavior inference from public position data. It can identify a convincing shape; it cannot know the crew’s clearance or intent.

### TRENDS — learning what “normal” means here

<p align="center"><img src="docs/renders/rev5/munich/trends.png" width="360" alt="Trends page comparing current traffic with learned normal"></p>

The radar learns a separate baseline for every weekday and hour: **7 days × 24 hours = 168 buckets**.

Each bucket stores a running mean, variance accumulator and sample count using Welford’s numerically stable update. A verdict appears only after at least three observations for the current weekday/hour.

The comparison is a z-score:

**z = (current airborne count − learned mean) / standard deviation**

To stop a nearly constant quiet hour from producing absurd sensitivity, the standard deviation has a floor of:

**1 + 15% of the learned mean**

| z-score | Display verdict |
|---:|---|
| Above +2 | Much busier |
| +1 to +2 | Busier |
| −1 to +1 | Normal |
| −2 to −1 | Quieter |
| Below −2 | Much quieter |

Each bucket retains up to 12 effective observations; after that, old influence decays gently. This makes the model resemble a rolling seasonal memory rather than an eternal average. The learned model is saved in non-volatile storage and survives reboot.

Because Trends always measures the fixed activity radius, an AUTO zoom change never corrupts the comparison.

### SPACE — ISS position, pass estimate and launches

<p align="center"><img src="docs/renders/rev5/munich/space.png" width="360" alt="Space page with ISS sky position and launch countdown"></p>

The ISS position is refreshed about once per minute. Ground distance uses Haversine geometry; azimuth uses the initial great-circle bearing and handles date-line wrapping.

The dome is a sky view:

- outer ring = horizon;
- center = zenith;
- radial position = dome radius × (1 − elevation / 90°).

The station is considered visibly favorable only when all three are true:

- elevation is above 10°;
- the ISS data says the station is sunlit;
- the observer’s location is in darkness.

The trail and forward leader use the live orbital speed and ground distance to estimate angular motion. The visual arc is clamped so it remains useful both near and below the horizon.

#### How the next-pass estimate works

After two valid position fixes establish a heading, the device:

1. assumes a circular orbit at the reported altitude;
2. derives an orbital period from the live speed and orbital circumference;
3. propagates the great-circle position in one-minute steps;
4. subtracts Earth’s sidereal rotation, 360° per 1,436.07 minutes;
5. searches the next 1,440 minutes for elevation above 10°;
6. records pass start and estimated maximum elevation.

The derived orbital period must be between 60 and 200 minutes. The model intentionally ignores orbital precession, so the result is an approximate spotting aid rather than an ephemeris.

The lower panel rotates through the next four launches from The Space Devs, with a live T− countdown before launch and T+ age after the scheduled time.

### WEATHER — METAR translated for a glance

<p align="center"><img src="docs/renders/rev5/munich/weather.png" width="360" alt="Decoded airport weather page"></p>

The airport METAR is refreshed about every 10 minutes and shown with the age of the actual observation. The page presents:

- VFR, MVFR, IFR or LIFR flight category;
- wind direction and speed on a wind rose;
- visibility and cloud information;
- temperature and dew point;
- pressure;
- a near-saturation warning when temperature and dew point are within roughly 2°C.

The same wind direction feeds the airport page’s probable active-runway calculation.

### SYSTEM — proving the instrument is healthy

<p align="center"><img src="docs/renders/rev5/munich/system.png" width="360" alt="System diagnostics page"></p>

SYSTEM exposes the details that make a small network appliance trustworthy: Wi-Fi state, active aircraft feed, fetch success, frame time, memory, uptime and data freshness.

The aircraft parser validates incoming values and keeps the last good picture if a fetch fails. The service can fall back from adsb.lol to adsb.fi, apply longer backoff after server refusal or rate limiting, and later probe the preferred feed again.

### ABOUT — the quiet signature page

<p align="center"><img src="docs/renders/rev5/munich/about.png" width="360" alt="Animated About page"></p>

ABOUT is deliberately playful: orbiting chevrons, a slow radar ping and the build credit. Like every normal page, it can be disabled in settings.

---

## Special states

| Recovering a connection | Emergency squawk |
|---|---|
| <img src="docs/renders/rev5/munich/connection.png" width="280" alt="Connection recovery screen"> | <img src="docs/renders/rev5/munich/emergency.png" width="280" alt="Emergency squawk screen"> |

The connection screen makes first setup and network recovery understandable without a serial console.

An airborne contact squawking one of the international emergency codes takes over the full display:

| Squawk | Meaning |
|---:|---|
| 7500 | Unlawful interference / hijack |
| 7600 | Radio communication failure |
| 7700 | General emergency |

During an emergency alert, ordinary page cycling and AUTO range changes are suppressed so the warning remains stable.

---

## Complete visual language

The display uses the same visual grammar on every page. Once these marks are familiar, most screens can be read without searching for a legend.

### General colors

| Color | Meaning |
|---|---|
| Cyan | The main live value: the number answering the page’s question |
| Pale cyan | A secondary live value |
| Green | Descending, arriving or healthy |
| Amber | Climbing, departing, holding, inferred information or something worth attention |
| Red | Emergency, go-around, severe descent, or the fixed north marker |
| Violet | Ground traffic or a circling behavior |
| Lilac | Space/ISS information or a survey pattern |
| Orange | Military flag supplied by the aircraft feed |
| Grid green | Non-data furniture: rings, ticks, runway outlines and scale |
| Grey | Labels, units and explanatory text |

Aircraft altitude coloring is a separate continuous gradient described earlier. When that option is enabled, the aircraft body uses altitude color while behavior rings, labels and page statistics retain the semantic colors above.

### Shapes and marks

| Mark | Exact meaning |
|---|---|
| Pointed chevron | Airborne aircraft; the point follows the reported true track |
| Fading track behind a chevron | Retained recent position fixes—where the aircraft actually went |
| Thin line ahead | Velocity leader—where present heading and groundspeed would carry it |
| Violet dot | Stationary or nearly stationary ground contact |
| Violet dot with stub | Moving ground contact/taxiing |
| Faint mark pinned to the rim | Known contact outside the drawn scope but inside the fetched picture |
| Colored ring around aircraft | Detected holding, circling, survey, go-around or emergency descent |
| Red triangle at top | True north |
| Dim rotating radial line | Decorative radar sweep; it does not trigger detections |
| Bright traveling rim arc | Health ring and proof that the display loop is alive |
| Small pips near the bottom | Position in the enabled page sequence |

### Health colors

| Health color | Meaning |
|---|---|
| Violet | Boot has begun |
| Blue | Connecting or actively fetching |
| Amber | Warning, retry, stale information or degraded feed |
| Cyan | Network/data initialization progressing |
| Green | Normal operation and fresh data |
| Pulsing red | Emergency aircraft alert |

---

## Every on-screen term

### RADAR terms

| Term or number | Meaning |
|---|---|
| Number at top | Airborne aircraft inside the currently drawn radar range |
| Number at left | Aircraft climbing inside that same drawn range |
| Number at right | Aircraft descending inside that same drawn range |
| Outer “km” label | Current radar edge; AUTO changes this value only |
| Three rings | ¼, ½ and ¾ of the outer radar range |
| AUTO | Automatic range selection is enabled |
| Callsign tag | Radio callsign, not necessarily the passenger-facing flight number |
| HOLD / CIRC / SURV / G/A | Holding, circling, survey pattern or go-around |
| Clock | Local time in the selected 12- or 24-hour format |
| Site label | The short home/airport identity chosen for this build |

### AIRPORT terms

| Term or mark | Meaning |
|---|---|
| MUC / EDDM | Munich Airport’s presentation label and ICAO weather station |
| Runway numbers | Magnetic runway direction rounded to the nearest ten degrees |
| ARR | Descending/approaching aircraft associated with the field |
| DEP | Climbing/departing aircraft associated with the field |
| Active runway highlight | Runway end most nearly facing the reported METAR wind |
| Amber inferred direction | Wind was unusable, so nearby descending traffic supplied the estimate |
| Ground chevron | Moving on a runway |
| Ground stub | Taxiing |
| Ground dot | Parked or nearly stationary |
| MILITARY / HEAVY / ROTARY | Most notable nearby airport contact category |

The airport callout gives priority to operationally interesting traffic: emergency squawk +120, go-around +70, military +60, heavy category +45, high-performance category +30 and rotorcraft +25. A score of at least 40 earns the box, and the strongest candidate is shown. Its reason label follows strict priority: EMERGENCY, GO-AROUND, MILITARY, ROTARY, HEAVY, then HIGH PERF.

### TRAFFIC terms

| Term | Meaning |
|---|---|
| AIRBORNE WITHIN … KM | Live airborne count inside the fixed activity radius |
| CLIMBING | Positive vertical-rate contacts |
| DESCENDING | Negative vertical-rate contacts |
| OVER FL300 | Aircraft above flight level 300 / 30,000 ft |
| ON GROUND | Contacts reporting ground state |
| ROTARY | Helicopters/rotorcraft identified from aircraft category |
| MILITARY | Contacts explicitly flagged military by the feed |
| MOSTLY N/NE/E/SE/S/SW/W/NW | Busiest of eight bearing sectors around home |
| One-hour line | 60 one-minute mean traffic samples |
| collecting… | Not enough complete clock minutes yet |
| no clock yet | NTP time is not available, so minute buckets cannot be committed |

### NEAREST and COOLEST terms

| Term | Meaning |
|---|---|
| Large callsign | Selected aircraft identity |
| Airline/operator | Enriched operator when the lookup source knows it |
| Type | Aircraft model/type description |
| Registration | Civil registration/tail number |
| Route | Known origin and destination from enrichment |
| Distance | Current great-circle/local-plane distance from home |
| Bearing/cardinal direction | True bearing and its readable compass sector |
| ALT | Barometric altitude in feet |
| GS | Groundspeed in knots |
| VS | Vertical speed in feet per minute |
| CPA | Predicted closest point of approach if course and speed remain constant |
| T− / minutes | Time until that predicted CPA |
| MIL | Military flag from the live feed |
| COOLEST reason | Strongest reason the contact won: behavior, military, rotorcraft, rarity or heavy type |

### SPACE terms

| Term | Meaning |
|---|---|
| AZ | True azimuth from home to the ISS |
| EL | Elevation above the observer’s horizon |
| DIST | Great-circle ground distance |
| Horizon ring | 0° elevation |
| Center of dome | Zenith / 90° elevation |
| VISIBLE | Above 10°, station sunlit and home dark |
| NEXT PASS | First predicted crossing above 10° in the next 24 hours |
| MAX EL | Highest estimated elevation during that pass |
| T− | Time remaining until a scheduled launch |
| T+ | Time elapsed since its scheduled launch time |
| NO PASS 24H | No qualifying pass found within the search window |

### WEATHER terms

| Term | Meaning |
|---|---|
| METAR | Standard coded airport weather observation |
| Age | Time since the observation itself, not time since the device downloaded it |
| VFR | Visual conditions |
| MVFR | Marginal visual conditions |
| IFR | Instrument conditions |
| LIFR | Low instrument conditions |
| WIND | Direction wind comes from and speed in knots |
| VIS | Reported visibility |
| TEMP / DEW | Air temperature and dew point |
| Near-dew warning | Temperature and dew point separated by roughly 2°C or less |
| QNH | Sea-level pressure in hectopascals |
| Clouds | Reported cloud amount/base decoded from the METAR |
| NO METAR | No usable report is currently available for EDDM |

### PATTERNS terms

| Term | Meaning |
|---|---|
| HOLDING | Repeated coordinated turn/racetrack-like containment |
| CIRCLING | Tight, slower orbit in a small area |
| SURVEY PATTERN | Multiple reversals and long path with little net displacement |
| GO-AROUND | Descent to a real low point followed by a sustained climb |
| EMERGENCY DESCENT | Extreme descent rate plus significant altitude loss |
| FAST CLIMB | More than 2,500 ft/min below 20,000 ft |
| APPROACH | Descending, below 15,000 ft and getting closer inside 45 km |
| DEPARTURE | Climbing, below 15,000 ft and getting farther inside 45 km |
| OVERFLIGHT | Above 24,000 ft and approximately level |
| LEVEL | No stronger behavior classification applies |

### TRENDS terms

| Term | Meaning |
|---|---|
| LEARNING | Fewer than three observations exist for this weekday/hour bucket |
| Normal | z-score from −1 to +1 |
| Busier / Quieter | z-score beyond ±1 |
| Much busier / Much quieter | z-score beyond ±2 |
| Baseline | Learned mean for the same weekday and hour |
| History line | Last 24 observed hourly values |
| Progress | Coverage of the weekly model, not a network-download percentage |

### SYSTEM terms

| Term | Meaning |
|---|---|
| FETCH OK | Percentage of aircraft polls that returned a usable payload |
| FEED | adsb.lol primary or adsb.fi fallback |
| Data age | Elapsed time since the last good aircraft picture |
| Wi-Fi / RSSI | Network name and received signal strength |
| Frame time / FPS | Display-rendering performance |
| RAM | Internal memory available to the time-critical renderer/network stack |
| PSRAM | External memory used for larger histories and supporting data |
| Uptime | Time since boot |
| Status/last result | Current network task state and most recent request outcome |

### ABOUT and page pips

ABOUT contains the owner credit and animation only; it does not affect aircraft processing. The bottom pips are a navigation indicator. Disabled pages disappear from the sequence, so the pips describe the active set rather than a fixed page number.

---

## Everything configurable

The captive setup page exposes the controls that genuinely affect the current hardware. Settings that could not work on this seven-pin display—backlight brightness, night dimming, runtime SPI speed and RGB-order changes—were deliberately removed.

| Setup area | What can be changed | Effect |
|---|---|---|
| Wi-Fi | Scanned/manual 2.4 GHz network, password, connection test, hostname | Moves the radar to a network without reflashing |
| Home | Latitude, longitude and time zone | Center for radar, distance, bearing, traffic, Trends and solar calculations |
| Radar range | Fixed 10–150 km slider | Outer ring when AUTO is off |
| AUTO | On/off, minimum and maximum range | Lets traffic density choose a rung inside the allowed window |
| Airport map | 5–60 km | Scale of the Munich Airport page |
| Your sky | 5–400 km activity radius | Fixed meaning of local traffic for Traffic, Coolest, Patterns and Trends |
| Sweep | 2–20 rpm | Visual sweep speed only |
| Phosphor | On/off and long/medium/short/very-short decay | Persistence of the CRT-style drawing layer |
| Velocity leaders | On/off | Future-position direction/speed cue |
| Altitude color | On/off | Warm-low to cool-high aircraft symbols |
| Labels | 0, 1, 2, 3, 5 or 8 | Maximum simultaneous callsign tags |
| Pages | Enable/disable, starting page | RADAR always remains enabled |
| Auto-cycle | On/off and 4–120 second dwell | Automatic page advance |
| Transition | Instant, fade, slide or iris | Animation between pages |
| Screen | 0°, 90°, 180° or 270° rotation; color inversion | Physical orientation/panel correction |
| Status LED | Enabled, alerts-only, brightness and supported board pin | Mirrors the health ring or stays dark until needed |
| Aircraft refresh | 2–30 seconds | Live feed polling; 8 seconds is the recommended balance |
| Fetch radius | 20–250 km | Base area requested before automatic widening |
| Enrichment | On/off | Aircraft type, registration, operator and route lookups |
| Space data | On/off | ISS and launch requests/page data |
| Emergency alerts | On/off | Full-screen 7500/7600/7700 interruption |
| Military-only | On/off | Hides civil contacts from the view; AUTO still measures the full sky |
| Learned baseline | Restart learning | Clears Trends history without erasing Wi-Fi |
| Personal | Owner name and 12/24-hour clock | ABOUT credit and radar clock |
| Maintenance | Erase all settings | Returns the device to first-run Wi-Fi setup |

The reference airport and its runway geometry are intentionally fixed in the build. They are not casual setup fields: changing them incorrectly would make weather, bearings and runway drawings disagree.

### Button and automatic navigation

| Action | Result |
|---|---|
| Short MODE press | Advance to the next enabled page |
| Hold MODE for about 1.2–2 seconds | Restart into the setup portal |
| Quick double RESET | Alternate route into setup |
| Auto-cycle enabled | Advance after the selected dwell time |
| Emergency active | Freeze normal navigation on the warning |

Do not hold BOOT/MODE while applying power. GPIO0 is also the programming strap; holding it during reset asks the ESP32-S3 to wait for flashing, leaving the display black until a normal restart.

### Units

The current display deliberately uses one consistent aviation-oriented mixture:

- distance in kilometers;
- altitude in feet;
- speed in knots;
- vertical speed in feet per minute;
- pressure in hectopascals;
- temperature in degrees Celsius;
- bearings and runway orientation in degrees.

---

## Connection and recovery messages

| Message | Meaning | Normal action |
|---|---|---|
| joining | Connecting to the saved router | Wait |
| connected | Network association succeeded | None |
| network not found | SSID absent, mistyped or unavailable | Reopen setup and select it |
| wrong password? | Router rejected authentication | Re-enter the password |
| no answer from AP | Network is visible but did not answer | Improve Wi-Fi range |
| connection lost | A previously working link dropped | Device reconnects itself |
| reconnecting | Automatic recovery is running | Wait |
| join timed out | Router did not finish association | Wait or reopen setup |
| no credentials | No Wi-Fi has been saved | Join DESK-RADAR-xxxx |
| net task up | Networking task started | None |
| reading baseline | Loading learned traffic history | None |
| allocating buffers | Reserving network/parser memory | None |
| out of memory | Required working memory could not be reserved | Reflash/check build configuration |
| PSRAM off in build | External RAM support was disabled when flashed | Reflash with PSRAM enabled |
| collecting… | Waiting for enough time-based samples | Wait |
| NO METAR | Weather report unavailable | Check later/verify airport source |
| LEARNING | Trends does not yet have enough matching weekday/hour history | Let it run |

One missed aircraft poll is treated as noise. The device keeps the last valid picture, reports age honestly and warns only after repeated misses make the feed stale.

---

## Aviation glossary

| Term | Plain-language meaning |
|---|---|
| ADS-B | Automatic Dependent Surveillance–Broadcast: aircraft transmitting identity, position and motion |
| Callsign | Radio identity used by air traffic control; related to but not always equal to the ticketed flight number |
| Squawk | Four-digit transponder code |
| Knot | One nautical mile per hour, approximately 1.852 km/h |
| Flight level | Pressure altitude in hundreds of feet; FL300 means 30,000 ft |
| METAR | Standard airport weather observation |
| QNH | Pressure reduced to sea level for altimeter setting |
| VFR / IFR | Visual / instrument flight rules and their associated weather categories |
| CPA | Closest point of approach under constant-course, constant-speed projection |
| Go-around | A landing approach discontinued into a climb |
| Hold | Repeating racetrack-like path while waiting or sequencing |
| Heavy | Wake-turbulence category for large aircraft such as the 747, 777 or A380 |
| Rotorcraft | Helicopter or other rotary-wing aircraft |
| True bearing | Direction relative to geographic north |
| Magnetic runway number | Runway heading relative to magnetic north, rounded to tens of degrees |
| NTP | Internet time synchronization used for clock, minute/hour buckets and solar calculations |

---

## Hardware and wiring

The physical build is intentionally small: two main electronic parts and one button.

- ESP32-S3-WROOM-1 N16R8;
- GC9A01 1.28-inch round IPS display;
- onboard BOOT button as MODE;
- USB-C power.

### Seven display connections

| ESP32-S3 | Display | Purpose |
|---|---|---|
| GPIO12 | SCL | SPI clock |
| GPIO11 | SDA | SPI data |
| GPIO13 | DC | Data/command |
| GPIO10 | CS | Chip select |
| GPIO14 | RST | Reset |
| 3V3 | VCC | **3.3 V only—never 5 V** |
| GND | GND | Ground |

This seven-pin module has no BLK pin; its backlight is always on. MODE uses the board’s onboard BOOT button on GPIO0.

Keep the high-speed SPI wires under roughly 10 cm. Longer leads can produce sparkles and torn rows that look deceptively like a graphics bug.

### Why the rendering architecture matters

The complete 240 × 240 frame is composed off-screen, then sent to the GC9A01 as one image. This prevents visible partial redraws.

The framebuffer remains in internal SRAM so DMA can transfer it reliably. PSRAM is useful for larger history and lookup structures, but a display sprite allocated there can silently lose the fast DMA path.

Antialiased drawing targets the off-screen sprite, never the physical panel. The GC9A01 cannot be read back reliably for alpha blending, so attempting to smooth directly onto the display produces incorrect edges.

The network and display work are separated so a slow web request does not freeze a sweep. This is what allows a roughly 40 fps instrument to coexist with several slower internet feeds.

---

## Data sources and cadence

All sources are public and require no account for this personal project.

| Data | Primary source | Typical refresh | Use |
|---|---|---:|---|
| Live aircraft | [adsb.lol](https://adsb.lol/) | ~8 s | Positions, tracks, altitude, speed, squawk |
| Aircraft fallback | [adsb.fi](https://adsb.fi/) | On primary trouble | Continuity when the preferred feed fails |
| Aircraft enrichment | [adsbdb](https://www.adsbdb.com/) | On demand | Type, registration, airline and route |
| Airport weather | [Aviation Weather Center](https://aviationweather.gov/) | ~10 min | METAR, wind, visibility, flight category |
| ISS | [Where the ISS at?](https://wheretheiss.at/) | ~60 s | Live orbital position and velocity |
| Launches | [The Space Devs](https://thespacedevs.com/) | ~30 min | Next four launch events |

Volunteer ADS-B services can be incomplete or temporarily unavailable. The radar reports source age and fetch health rather than pretending that stale data is live.

---

## Using it

On first power-up, the radar creates a Wi-Fi network named **DESK-RADAR-xxxx**. Join it from a phone or laptop, choose the home network, enter its password and save.

After that:

- press MODE to move through enabled pages;
- hold MODE for about two seconds to reopen settings;
- choose which pages participate in the cycle;
- set radar, AUTO, activity and airport-map ranges;
- control refresh timing, alerts, clock, status light and personalization;
- erase all settings before transferring the device to somebody else.

The startup status light walks **violet → blue → amber → cyan → green**, giving a clue about where startup stopped even if the main display is blank.

The display module has no controllable backlight pin, so brightness and automatic night dimming are intentionally absent.

---

## Guides, printable material and visuals

- [Munich illustrated field guide](docs/desk_radar_field_guide.html)
- [Printable spotter’s field manual](docs/spotters_field_manual.pdf)
- [Enclosure concepts](design_concepts/)
- [All real firmware screen renders](docs/renders/rev5/)

The HTML field guide includes every page, every color and the meaning of the displayed numbers, using the Munich screen set.

---

## Repository boundary

This public repository contains only presentation material:

| Included here | Kept out |
|---|---|
| README and explanations | Firmware source |
| Real rendered screen images | Wi-Fi credentials |
| HTML guides and datasheets | Private configuration |
| PDFs and build material | Build scripts and executable code |
| Enclosure and product concepts | Personal location details beyond the stated city center |

The working firmware lives separately in the private [desk_radar_esp32_personal](https://github.com/haldarsaurav/desk_radar_esp32_personal) repository.

That separation is deliberate: this repository is what I can hand to a friend to explain **what the radar is, what it sees and how it thinks**, without publishing the code that runs it.
