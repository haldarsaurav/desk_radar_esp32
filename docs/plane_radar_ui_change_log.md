# Plane Radar UI Change Log

This document records the firmware UI changes made to `firmware/plane_radar_v1/plane_radar_v1.ino`, what each change was meant to solve, and the current verification status.

## Changed Firmware File

- `firmware/plane_radar_v1/plane_radar_v1.ino`

## Main Goals

- Make the radar sweep look smoother and stop it from jagging text.
- Improve text layout so numbers, callsigns, routes, and labels do not overlap on the 240x240 round display.
- Make the Munich Airport pages more useful and more readable.
- Add more live-feeling motion between ADS-B API refreshes.
- Make the spotter page explain why a flight is interesting.
- Add comments explaining why blocks exist, not just what each line does.

## FlightScnr Inspiration

The FlightScnr project was used as UI inspiration, especially its sparse radar display, range rings, heading-aligned aircraft pips, compact flight detail pages, route lookups, and cached callsign details.

Reference: <https://github.com/yashmulgaonkar/FlightScnr>

No FlightScnr code was copied into this firmware. The useful ideas were adapted to the much smaller GC9A01 240x240 round TFT and the existing single-file Arduino sketch.

## Page System

The firmware changed from the original compact carousel to 10 pages:

1. `RADAR` - live aircraft around the configured home position.
2. `FLIGHT` - nearest airborne aircraft detail card.
3. `TRACK` - nearest aircraft trail and closest point of approach.
4. `MUC MAP` - Munich Airport runway close-up.
5. `MUC OPS` / `MUC NEXT` - next likely arrivals, departures, and one cool MUC-area aircraft.
6. `MUC METAR` - current EDDM weather from AviationWeather.
7. `MUC FIELD` - Munich Airport field/runway facts from OurAirports-style source data.
8. `OPEN SKY` - MUC-area OpenSky state-vector counts.
9. `MUC STATUS` - optional aviationstack MUC departure delay/cancellation sample.
10. `SPOTTER` - coolest aircraft in the current traffic set.

Why:

- The old airport page was trying to show runway geometry, aircraft, arrival/departure counts, and text all at once.
- Splitting airport map, airport operations, airport weather, airport facts, OpenSky counts, and optional status data makes each page less crowded.
- The small round display needs rigid page roles to stay readable.

## Radar Page Changes

### Range Label

- The first page range label is back to a simple `20km`.
- With the default `RADAR_RANGE_KM = 30`, the rings represent roughly 10 km, 20 km, and 30 km.

Why:

- `RNG 20km` was clearer but added clutter.
- A short `20km` label matches the compact radar aesthetic better.

### Sweep Animation

- Replaced the expanding ping-circle sweep with a rotating radar beam.
- The beam skips protected text zones.
- The beam is drawn pixel-by-pixel so it does not erase or jag text.

Why:

- The old expanding ring erased with background color and cut through callsigns and numbers.
- Repainting all labels every sweep frame caused visible jitter.
- A rotating beam feels more like a radar and can avoid text areas.

### Live Aircraft Motion

- Aircraft positions are projected forward between API fetches using:
  - latest east/north offset,
  - ground speed,
  - track heading,
  - elapsed time since last fetch.
- Projection age is capped so stale data cannot drift forever.
- Aircraft now get a tiny track line in front of the triangle.

Why:

- ADS-B data arrives in bursts, so icons looked frozen between fetches.
- Dead-reckoning makes arrows feel live without increasing API usage.

### Page Dot Jitter

- Page dots are no longer redrawn during every radar sweep frame.
- Radar traffic radius is pulled inward so aircraft erasing does not hit the page-dot strip.

Why:

- Redrawing dots during animation made them look like they were jittering.
- Moving traffic should not share pixels with fixed navigation chrome.

### Bezel / Edge Ring

- The bezel was moved slightly inward from the physical display edge.
- The bezel is redrawn after radar traffic so blip erasing does not leave small gaps.

Why:

- Some GC9A01 modules clip near the outermost pixels.
- Aircraft near the rim could erase small chunks of the edge ring.

## Text Layout Stability

### Fixed Text Boxes

Added helper functions:

- `fitCopy(...)`
- `printFit(...)`
- improved `centerText(...)`

Why:

- Long callsigns, route names, airline names, aircraft types, or numbers can otherwise run into neighboring fields.
- Every displayed text string now has a maximum character/pixel box.
- If a value is too long, it clips instead of breaking the layout.

### Flight Card Rows

- `statRow(...)` now uses fixed boxes for both columns.

Why:

- The detail and spotter pages were already the best-looking pages.
- Fixing `statRow(...)` makes those pages more robust without changing their visual style.

## Closest Approach Page

- Closest point of approach text now uses `CPA`.
- Format is now like `CPA 3.2km in 4m20s`.
- If the aircraft is moving away, the display says `away now`.

Why:

- The old `closest ... in 3:42` was ambiguous.
- `CPA` clarifies that the distance is predicted closest distance to the device/user, in kilometers.
- Time is shown as minutes and seconds.

## Munich Airport Runway Page

### Larger Runway Close-Up

- `MUC_SCALE` was increased so the runways dominate the page.
- The page is now a close-up airport view rather than a broad home-to-airport map.
- Runways are drawn as thick angled slabs instead of thin lines.
- Runways are labeled:
  - `08L/26R`
  - `08R/26L`

Why:

- The previous runways were too small.
- Endpoint runway numbers overlapped.
- A close-up airport view is more useful on a tiny screen than a geographically broad map.

### Live Airport Traffic

- MUC traffic uses the same dead-reckoning projection as the radar page.
- Low/interesting traffic gets heading strokes.
- Aircraft outside the close-up are clamped to the map edge instead of disappearing immediately.

Why:

- The runway page should feel live even when aircraft are still on approach or climbout.
- Clamped edge traffic gives a useful "something is inbound/outbound from this direction" cue.

### ARR / DEP Counts

- Counts remain on the runway page but are smaller and less likely to overlap.

Why:

- Big count text collided with other page elements.
- Counts are useful, but the runway view should be visually dominant.

## Munich Arrivals / Departures Page

The airport operations page was rebuilt as `MUC NEXT`.

It now has fixed sections:

- `ARRIVALS`
  - `A1`
  - `A2`
- `DEPARTURES`
  - `D1`
  - `D2`
- `COOL`
  - one most interesting MUC-area aircraft

Each arrival/departure row shows:

- callsign,
- distance to MUC,
- ETA-style minutes for arrivals when available,
- route code if the route API knows it, for example `FRA > MUC` or `MUC > LHR`,
- otherwise aircraft type / altitude / climb-descent detail.

Why:

- The previous `ARR1/ARR2/DEP1/DEP2` layout was still too cramped.
- Section headings and fixed row boxes make the page read more like a compact flight board.
- Route codes answer "where is it coming from?" and "where is it going?"

## Arrival / Departure Detection

The firmware does not get a complete official airport schedule from the free ADS-B feed. It estimates airport operations from live aircraft state.

Arrival scoring uses:

- distance to MUC,
- altitude,
- vertical rate,
- track direction toward MUC,
- ground speed / ETA.

Departure scoring uses:

- distance to MUC,
- altitude,
- vertical rate,
- track direction away from MUC,
- ground status when available.

Why:

- ADS-B position data can show likely arrivals and departures even without a full schedule feed.
- The scoring prefers obvious finals and climbouts but still picks best nearby candidates if the strict heuristic misses.

## Route Lookup Cache

The route cache was expanded:

- nearest flight slot,
- spotter slot,
- MUC board slots.

Route calls now use bounded string copy for safety.

Why:

- The MUC page needs route details for multiple callsigns, not just the nearest aircraft.
- Bounded copies avoid corrupting memory if a callsign is weird or longer than expected.

Route API timeout was reduced.

Why:

- Route details are helpful, but the ESP32 should not appear frozen while waiting on several route lookups.

## Plane Spotter Page

- The top badge now says why the aircraft is interesting, for example:
  - `WHY: EMERGENCY SQUAWK`
  - `WHY: SUPERJUMBO A380`
  - `WHY: MUC FINAL INBOUND`
  - `WHY: LOW OVERHEAD PASS`
- The awkward small footer line was removed.

Why:

- The page should answer "why is this the one to look at?"
- The old footer did not fit cleanly and looked visually broken.

## Coolness Scoring

The spotter score now considers:

- emergency squawks,
- rare / large aircraft types,
- military and government callsign prefixes,
- very low nearby passes,
- fast low traffic,
- MUC final inbound,
- MUC climbout.

Why:

- The "coolest" aircraft should update dynamically when something genuinely interesting appears.
- An emergency or rare aircraft should beat ordinary nearby traffic.

## Removed / Avoided UI Text

Removed or avoided visible text that was confusing or cluttered:

- `scanning...`
- `none inferred`
- `inferred from ADS-B`
- `not official schedule`
- the malformed bottom `HEAVY TRAFFIC` footer

Why:

- These were either visually awkward, too wordy, or not helpful on the device.
- The UI should show useful traffic state, not implementation caveats.

## Verification So Far

Completed:

- Static brace count checks passed after edits.
- Checked for stale unwanted strings such as `scanning`, `none inferred`, and ADS-B disclaimer footer text.
- Checked that page dots are no longer redrawn during the radar sweep loop.
- After the API-page expansion, the brace count still matched and the page-switch cases were checked for the new 10-page carousel.
- Confirmed the on-screen spotter fallback no longer uses the awkward `HEAVY TRAFFIC` footer wording.

Not completed:

- Full Arduino compile was not possible from this environment because `arduino-cli` was not available on PATH or in the usual install locations.
- Hardware/display visual verification still needs to be done on the actual GC9A01 device.

## Known Follow-Up Items

- Compile in Arduino IDE or install/use `arduino-cli` for a real build check.
- Flash to the ESP32-C3 and visually inspect:
  - page-dot stability,
  - radar sweep smoothness,
  - MUC runway scale,
  - MUC NEXT row spacing,
  - route text clipping,
  - outer bezel continuity.
- If route lookups feel slow in real use, reduce how many MUC rows prefetch routes per page visit.
- If the runway page still feels too crowded, consider splitting it into:
  - runway-only live map,
  - separate approach/departure mini-board.

## Later Polish Pass

After the first UI pass, another round of changes was made based on visual feedback from the device:

- Bottom page indicators were removed completely. They sat too close to the moving radar/rim area and made the whole UI feel jittery.
- The compass letters were moved inward and given tiny background pads so `N/S/E/W` do not collide with the bezel, crosshair, or sweep.
- The main radar color language changed:
  - green = aircraft likely arriving to MUC,
  - red = aircraft likely departing from MUC,
  - yellow = ordinary in-range traffic,
  - blue = out-of-range rim traffic.
- The airport runway page now uses the same intent colors:
  - green dots = arrivals,
  - red dots = departures,
  - blue dots = stationary/ground traffic.
- The runway page now uses colored dots with short heading tails instead of tiny triangles, because dots read more cleanly over the enlarged runway drawing.
- The runway page count numbers were removed and replaced with a tiny `ARR / DEP / GND` color legend.
- The MUC arrivals/departures page was rebuilt again as a two-column board:
  - left column: arrivals,
  - right column: departures,
  - two rows each,
  - route or aircraft details on a second line.
- A temporary `TRAFFIC` page was tried as a FlightScnr-inspired compact aircraft list, but it was later removed because it felt redundant with the radar, flight detail, and spotter pages.
- Text-heavy pages no longer repaint on every ADS-B fetch. The live radar and runway pages still update, but detail/board pages redraw when opened or page-switched to reduce visible refresh flicker.

## Latest Layout Correction

The newest feedback pass made these additional corrections:

- Main radar and nearest-flight detail pages now stay on screen longer than the other pages.
- The main radar flight readout moved to the top of the page.
- Aircraft arrows are now hidden when they would enter the top flight-readout band.
- The radar sweep redraws the top readout after each sweep step so the sweep/grid/aircraft cannot sit on top of the flight text.
- The redundant `TRAFFIC` carousel page was removed.
- The MUC arrivals/departures page was simplified again into a cleaner split board:
  - big `ARR` column,
  - big `DEP` column,
  - two cells per side,
  - one small `COOL` row at the bottom.

## Destination-Only Radar Tag

The main radar top readout was simplified again:

- It now shows only a destination-style tag such as `TO MUC`.
- The old callsign plus altitude/speed line was removed from the radar page.
- The tag is refreshed on page/data updates, not every sweep frame.
- The radar sweep and aircraft arrows are shielded from the tag area.

Why:

- The earlier top rectangle still appeared to jitter because it was being cleared and repainted during the sweep.
- The radar page should remain visually live; detailed flight information belongs on the flight-detail page.

## MUC Board Distance Label

The departure cell value that looked like `1.2` is now labeled as `1.2km`.

Meaning:

- On departure rows, that number is distance from Munich Airport.
- Arrival rows show an ETA-style minute value when available.

## Airport Data / API Expansion

The latest pass adds one visible page or page role for each flight-data source discussed in the chat:

1. `ADS-B.lol` - keeps feeding the main `RADAR`, `FLIGHT`, `TRACK`, `MUC MAP`, `MUC NEXT`, and `SPOTTER` pages with nearby aircraft position data.
2. `ADSBdb` - keeps feeding route and airline context on the `FLIGHT`, `MUC NEXT`, and `SPOTTER` pages.
3. `AviationWeather` - adds the `MUC METAR` page for EDDM wind, visibility, temperature, QNH, and a clipped raw METAR line.
4. `OurAirports` - adds the `MUC FIELD` page with Munich Airport identifiers, elevation, runway names, and runway dimensions.
5. `OpenSky` - adds the `OPEN SKY` page with MUC-area state-vector counts, including total aircraft, ground traffic, heavy aircraft, fast/high-performance traffic, and rotorcraft.
6. `aviationstack` - adds the optional `MUC STATUS` page with a labeled sample of MUC departures, including delayed, cancelled, and active counts when `AVIATIONSTACK_API_KEY` is configured.

Why:

- The user wanted more Munich-airport-related pages and more useful airport data, not just the same traffic page repeated in a different shape.
- Each source has different strengths, so the UI now separates live position, route context, weather, field facts, OpenSky aggregate counts, and optional commercial status data.
- The ESP32 display is small, so each API page shows only a few high-value fields instead of trying to render a large airport dashboard.

## API Notes And Limits

- AviationWeather Data API: free/no-key aviation weather endpoint used for EDDM METAR. The official API supports METAR, TAF, station info, airport info, and other aviation weather products. It asks clients to keep requests limited and says endpoints should not be consumed more frequently than once per minute per thread.
- OurAirports: open/public-domain airport data source with downloadable airport and runway datasets. It is excellent for static field/runway facts, not live status.
- OpenSky REST API: supports `/states/all` with a bounding box and optional extended aircraft category data. Anonymous access has daily credit limits, so the firmware uses a small Munich bounding box and a slow refresh. OpenSky arrival/departure flight-history endpoints are batch-updated and are not suitable as a live MUC schedule page.
- aviationstack: optional key-based API. The free tier is small, so the firmware does not call it constantly. The page is labeled as a `sample of 50`, because free/paginated API results are not guaranteed to represent the complete airport operation.
- ADS-B.lol and ADSBdb remain the core free/no-key live traffic and route-context sources already used by the project.

References:

- AviationWeather Data API: <https://aviationweather.gov/data/api/>
- OurAirports data downloads: <https://ourairports.com/data/>
- OurAirports Munich Airport data: <https://ourairports.com/airports/EDDM/>
- OurAirports Munich runway data: <https://ourairports.com/airports/EDDM/runways.html>
- OpenSky REST API: <https://openskynetwork.github.io/opensky-api/rest.html>
- aviationstack pricing / free-tier notes: <https://aviationstack.com/pricing>
- FlightScnr inspiration project: <https://github.com/yashmulgaonkar/FlightScnr>

## Whole Chat Summary

- First feedback was about the ping sweep making text jagged. The radar sweep was changed from an expanding ping circle to a rotating radar beam that skips protected text zones.
- The ambiguous closest-approach timing was clarified as `CPA` with kilometers and minutes/seconds.
- The main radar page was simplified: range label returned to `20km`, the top readout became destination-only, and aircraft colors now mean green inbound to MUC, red outbound from MUC, yellow ordinary traffic, and blue out-of-range or ground traffic.
- Bottom page indicators were removed because they jittered and competed with the round bezel.
- Compass labels were moved inward and padded so `N/S/E/W` do not overlap the radar graphics.
- The runway page was repeatedly enlarged and simplified. It now uses thick runway slabs, real runway labels, and live colored dots: green arrivals, red departures, blue ground/stationary aircraft.
- The arrival/departure page was rebuilt into a compact two-column `MUC NEXT` board with two arrivals, two departures, route context where available, and one cool MUC-area aircraft.
- The redundant traffic-list page was removed after it felt too similar to the other pages.
- The spotter page now explains why the highlighted aircraft matters, with reasons like emergency squawk, rare type, government/military callsign, MUC final inbound, MUC climbout, low pass, or fast low traffic.
- The code gained more detailed comments around layout protection, dead-reckoning, route caching, runway rendering, scoring, API caching, and why certain pages redraw less often.
- A changelog markdown file was added so the project records what changed, why it changed, what still needs testing, and which data sources are being used.

## v6 Refactor (Compile-Verified Pass)

The 10-page experimental build was reviewed, trimmed, compiled, and flashed. The carousel went from 10 pages back to 7 focused pages: RADAR, FLIGHT, TRACK, MUC MAP, MUC OPS, MUC WX, SPOTTER.

Removed:

- `OPEN SKY` page and its fetcher. It duplicated counts already derivable from the adsb.lol traffic set, anonymous OpenSky access is heavily rate-limited, and its unfiltered bounding-box JSON parse was the largest single heap risk in the sketch.
- `MUC STATUS` page, the aviationstack fetcher, and the `AVIATIONSTACK_API_KEY` config option. The free tier (~100 calls/month) cannot sustain a desk display, so the page mostly showed "ADD API KEY".
- `MUC FIELD` page. Its content was fully static text; after one viewing it only made the carousel longer. The facts remain in this changelog and in code comments.
- `drawMucOpRow(...)`, which was dead code after the board rebuild.

Changed:

- The radar paint loop no longer redraws the full bezel every 35 ms sweep frame. Traffic and blip-erase geometry stay inside the bezel radius, so the ~40 extra draw calls per frame were pure overhead.
- The radar destination tag falls back to the callsign instead of `TO ----` when the route API has no match.
- New alert latch: when any aircraft reaches cool score >= 95 (emergency squawk, A380, military transport, government callsign, etc.), the carousel jumps to the SPOTTER page immediately, once per callsign.

Verified:

- Compiled clean in Arduino IDE 2.x and flashed to the ESP32-C3 over COM5.
- Live data confirmed on-device after flash (aircraft fetch, route lookups, METAR).

## v7 Polish Pass (Product-Quality UI)

Code-only pass (compile/flash/push done by the user).

Bezel / jitter fixes:

- New `clearInner()`: pages now clear their content with a radius-105 circle instead of large `fillRect` calls. Any rect wide enough for content has corners beyond the bezel radius (e.g. corner at r=124 vs bezel at r=115) — this was the root cause of the outer ring getting visibly "eaten". Compass letters are re-drawn after each clear.
- Dead-reckoning is quantized to 0.4 s buckets, so aircraft hold stable pixel positions between visible steps instead of shimmering ±1 px on every 35 ms sweep frame.
- Radar altitude-tag erase blips are capped to the inner 75% of the radar so widened erases can never reach the bezel.

Radar page:

- Two-line shielded readout: destination tag plus an altitude + distance line.
- Tiny altitude tags on the four nearest aircraft (inner radar only).

Flight page:

- "NEAREST FLIGHT" heading removed; that row now shows the human-readable aircraft name via a new ICAO-code-to-name table (~56 types), with graceful fallback to the raw code.

Track page:

- Heading removed; callsign + route codes moved to the top band.
- Zoom widened (1.8x, min 12 km) so MUC arrivals/departures fit on screen.
- Munich Airport drawn in-view as mini runway strokes labeled "MUC".
- Labeled live line (`ALT .. SPD ..`) above the CPA readout.

Airport page:

- Styled header ("MUNICH AIRPORT" + EDDM line) that doubles as a live field-wind chip when a METAR is cached.

MUC OPS board:

- COOL footer removed (the spotter page owns that job); the board now shows exactly NEXT/THEN per side with three clean lines per cell: callsign, "in N min" / "N km out" / "on field", route or type+altitude fallback.

Weather page:

- "EDDM WEATHER" headline removed; six data rows now: WIND, VIS, CLOUD (lowest layer + ceiling detection), TEMP, QNH, WX (rain/snow/fog/etc. from present-weather groups).
- METAR-derived field status strip (NORMAL / MARGINAL / LOW VIS OPS) using standard visibility+ceiling bands. A fake 12 h delay percentage was deliberately NOT added: no free API provides trustworthy delay statistics.

Spotter page:

- "WHY:" prefix replaced by one concise line combining what the aircraft is and its live MUC status: inbound with ETA minutes, "AT MUC" for field traffic, "LEAVING MUC" for climbouts, or distance for notable passers-by.

Not yet verified (user to do): compile in Arduino IDE, flash, visual check of all seven pages.

## v8 Polish Pass (Calm Radar + Card Pages)

Driven by on-device feedback and a reference radar-scope photo.

Global:

- Chrome rule established: bezel ring + compass letters live only on map-like pages (RADAR, TRACK, MUC MAP). Text pages (FLIGHT, MUC OPS, SPOTTER, MUC WX) are bezel-less and use the full round panel.
- Page order changed: SPOTTER moved to slot 6, MUC WX is now the last page.
- New palette entry `C_PURPLE` for stationary/ground traffic.

RADAR page:

- The rotating sweep beam was REMOVED (it caused most perceived jitter and fought with labels). Traffic now advances calmly by dead reckoning every ~1.2 s (`RADAR_STEP_MS`).
- Rings re-aligned to exactly 1/3, 2/3, 3/3 of the traffic radius with per-ring km labels on the NE diagonal (old rings were subtly misaligned with the projection).
- Reference-photo style stacked labels (callsign / type / altitude) on the nearest 3 aircraft, side-aware placement, clamped to the inner disc, each with its own erase blip (`pushBlip`, `MAX_BLIPS`).
- Colour-coded traffic counter chip at the top ("N NEARBY": green quiet, amber normal, red busy) replaces the old destination readout.
- Out-of-range contacts are now blue rim squares (consistent with the map page).

FLIGHT + SPOTTER pages:

- `flightCard` is now a full-panel card: no ring/compass, badge at top, callsign, airline row with a coloured roundel carrying the callsign prefix (compact "tail logo" stand-in; colour stable per carrier), route + cities, divider, 4 stat rows, LOOK direction strip.

TRACK page:

- Flown path drawn as a connected fading polyline instead of loose dots.

MUC MAP page:

- Fixed the header overwriting the "N" compass label (header moved below the compass pad band).
- Bottom legend removed; marker grammar now: blue squares = clamped out-of-view traffic, purple circles = ground/stationary, green/red arrows = arrivals/departures with a highlight ring around the NEXT arrival and NEXT departure. High overflights are no longer drawn here.

MUC OPS page:

- Bezel-less full-width two-column board, wider cells, and a "data Ns ago" staleness footer.

MUC WX page:

- Bezel-less, tighter hierarchy, new RWY row estimating the runway direction in use from METAR wind (08 vs 26 operations), field-status strip retained.

Not yet verified: compile + flash + on-device visual pass.
