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

The current firmware is a focused 7-page carousel:

1. `RADAR` - calm live aircraft radar around the configured home position.
2. `SPECIAL` - emergencies, helicopters, and notable traffic near home/MUC.
3. `NEAREST` - nearest aircraft detail, path, bearing, and closest point of approach.
4. `MUC MAP` - Munich Airport runway close-up with live traffic and wind.
5. `MUC OPS` - next two likely arrivals and next two likely departures.
6. `SPOTTER` - coolest aircraft in the current traffic set, with a short reason.
7. `MUC WX` - current EDDM weather from AviationWeather.

Why:

- The old airport page was trying to show runway geometry, aircraft, arrival/departure counts, and text all at once.
- Splitting radar, special traffic, nearest-aircraft tracking, airport map, airport operations, spotter context, and airport weather makes each page less crowded.
- The small round display needs rigid page roles to stay readable.
- A temporary 10-page API carousel existed earlier, but `OPEN SKY`, `MUC FIELD`, and `MUC STATUS` were removed because they added clutter, repeated data, or depended on limited paid/free-tier APIs.

## Radar Page Changes

### Range Label

- The main radar scope is back to a compact `RADAR_RANGE_KM = 20`.
- With the 20 km scope, the outer ring represents 20 km and farther contacts become blue rim squares.

Why:

- 30 km showed more traffic, but it made the home page too busy.
- A 20 km scope matches the compact radar aesthetic better.

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
- After the handwritten-notes polish pass, brace balance still matched (`opens=328`, `closes=328`), `pageDur` still has 7 entries for 7 pages, and `git diff --check` reported no whitespace errors.
- Checked that stale on-screen strings such as `NEARBY`, `watching...`, `scanning`, `none inferred`, and `HEAVY TRAFFIC` are not present in the firmware UI.
- After the manual page-button pass, brace balance still matched (`opens=336`, `closes=336`), `pageDur` still has 7 entries for 7 pages, and `git diff --check` still reported no whitespace errors.

Not completed:

- Full Arduino compile was not possible from this environment because `arduino-cli` was not available on PATH, in the usual Arduino IDE install locations, or in the checked Arduino data folder.
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

## Historical API Expansion

This was an earlier experiment and is now superseded by the focused 7-page carousel. It added one visible page or page role for each flight-data source discussed at that time:

1. `ADS-B.lol` - keeps feeding the main `RADAR`, `FLIGHT`, `TRACK`, `MUC MAP`, `MUC NEXT`, and `SPOTTER` pages with nearby aircraft position data.
2. `ADSBdb` - keeps feeding route and airline context on the `FLIGHT`, `MUC NEXT`, and `SPOTTER` pages.
3. `AviationWeather` - still feeds the current `MUC WX` page for EDDM wind, visibility, temperature, QNH, cloud, weather, runway estimate, and clipped raw METAR line.
4. `OurAirports` - was tried as a `MUC FIELD` page with static airport/runway facts; it was removed from the current carousel because the facts do not change.
5. `OpenSky` - was tried as an `OPEN SKY` state-vector count page; it was removed because it duplicated nearby traffic data and could be heavy for ESP32-C3 heap.
6. `aviationstack` - was tried as an optional `MUC STATUS` delay/cancel page; it was removed because the free tier is too small to be useful as an always-cycling device page.

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

## Handwritten Notes / Real Device Polish Pass

This pass used the real GC9A01 photos and handwritten notes as the source of truth. The main goal was not to add more data; it was to make the existing 7 pages look calmer, more readable, and more like a polished mini aviation instrument.

Changes made:

- Added circle-aware centred text. Top and bottom rows now clip to the actual safe chord of the round display, so long spotter/weather/card headings do not get cut off by the circular glass.
- Added optional compass chrome. Map-like pages can keep the bezel/range styling, while pages that do not need compass labels can clear/redraw without reintroducing `N/S/E/W` collisions.
- Radar page:
  - changed the traffic chip from `NEARBY` to `PLANES`,
  - moved the radar rings slightly inward,
  - made the 30 km ring dashed and more subtle,
  - changed ring labels to `10km`, `20km`, and `30km`,
  - labels only the nearest representative aircraft in each range band, with collision checks before drawing.
- Flight detail page:
  - removed the fake airline-logo circle,
  - added a slim airline-brand accent line,
  - kept type/name, callsign, airline, route, distance, altitude, speed, bearing, vertical speed, track, squawk, and look direction in a cleaner card hierarchy.
- Track page:
  - removed the word `YOU`,
  - replaced it with a small white current-location dot,
  - removed compass letters from the page so `N` cannot collide with the aircraft name,
  - made Munich Airport more prominent with an amber mini-runway marker,
  - added the aircraft type/name above the route when there is room.
- MUC airport map:
  - removed compass letters from the airport close-up,
  - redrew runways as darker rectangular slabs with light edges,
  - added label background pads for runway names,
  - added a geometric runway-proximity check so aircraft on/very near the runway draw as yellow aircraft markers,
  - kept purple for ground traffic, green for arrivals, red for departures, and blue squares for out-of-range airport traffic.
- MUC traffic board:
  - moved the whole board slightly lower,
  - kept exactly two arrivals and two departures,
  - replaced the empty row wording with `standby`,
  - preserved the mini airport-display split-board structure.
- Spotter page:
  - shortened the top reason/status badge to compact strings like `A380 MUC 4M`, `ARR MUC 2M`, `DEP LEAVING MUC`, `EMERG 5KM AWAY`, or `SPOT 12KM AWAY`,
  - this avoids top-edge clipping while still explaining why the aircraft is interesting.
- Weather page:
  - changed the header to a stronger `MUC WX` title with `EDDM METAR` subtitle,
  - respaced wind, visibility, cloud, temp, QNH, weather, runway, status, and raw METAR rows,
  - narrowed the raw METAR lines near the bottom of the circle to avoid edge clipping.

## Manual Page Button

Added a physical page button for the desk device:

- New config option: `PAGE_BUTTON_PIN` in `config.example.h`.
- Default pin: GPIO `2`.
- Wiring model: active-low momentary pushbutton, one side to GPIO `2`, the other side to `GND`.
- Firmware uses `INPUT_PULLUP`, so no external resistor is required for a basic button.
- Short press:
  - advances to the next available page,
  - skips pages that currently have no useful data,
  - enters manual hold mode so the carousel stays on the selected page.
- Long press:
  - resumes the automatic page carousel.
- Manual hold also suppresses the automatic spotter alert jump, so a manually selected page really stays selected.

Why:

- The display is now useful enough that the user may want to inspect one page instead of waiting for the timed carousel.
- A single button keeps the hardware simple while still allowing both manual navigation and return-to-auto behavior.

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

- Chrome rule established: bezel ring lives only on map-like pages. Compass letters are now limited to the RADAR page; TRACK and MUC MAP use the ring without compass letters to prevent title/label collisions.
- Page order changed: SPOTTER moved to slot 6, MUC WX is now the last page.
- New palette entry `C_PURPLE` for stationary/ground traffic.

RADAR page:

- The rotating sweep beam was REMOVED (it caused most perceived jitter and fought with labels). Traffic now advances calmly by dead reckoning every ~1.2 s (`RADAR_STEP_MS`).
- Rings re-aligned to exactly 1/3, 2/3, 3/3 of the traffic radius with per-ring km labels on the NE diagonal (old rings were subtly misaligned with the projection).
- Reference-photo style stacked labels (callsign / type / altitude) on the nearest 3 aircraft, side-aware placement, clamped to the inner disc, each with its own erase blip (`pushBlip`, `MAX_BLIPS`).
- Colour-coded traffic counter chip at the top (`N PLANES`: green quiet, amber normal, red busy) replaces the old destination readout.
- Out-of-range contacts are now blue rim squares (consistent with the map page).

FLIGHT + SPOTTER pages:

- `flightCard` is now a full-panel card: no ring/compass, badge at top, callsign, slim airline-colour accent line, airline name, route + cities, divider, 4 stat rows, LOOK direction strip.

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

## v13 Polish (Handwritten Notes + Device Photos)

Driven by the user's handwritten review notes and breadboard photos of v12.

Fixed bugs found during review:

- NEAREST page header/footer clear bands were two large `fillRect`s whose corners reached r≈137 — past the 116 px bezel — silently eating the edge ring on every repaint (same bug class `clearInner()` fixed before). Replaced with stacked "pyramid" rects sized to the chord available at each row.
- The "@@@@@@@@" callsign on the traffic board photo: the feed occasionally carries non-ASCII bytes that render as solid blocks. `copyStr()` now filters to printable ASCII for every string in the system.

Radar page (notes p1):

- Scope widened 92 → 98 px so the gap to the edge is smaller; the bezel itself is now compact (one thin ring + twelve 30° ticks instead of a 36-tick double ring) and cheap enough to repaint every live frame.
- All three range rings are dashed, each with a small 10/20/30 km number on the NE diagonal on background pads (matches the sketch).

Flight/spotter card (notes p2 + p6):

- Airline accent colour now comes from a proper brand table (~27 carriers: Ryanair navy, DHL yellow, Wizz pink, Lufthansa crane yellow, Condor yellow, easyJet orange, ...), keyed by callsign prefix.
- Whole card shifted a few px down so the top badge clears the glass curve.

NEAREST page (notes p3): "YOU" text removed earlier (white dot), MUC runway marker amber/prominent, aircraft type line present — confirmed; bezel fix above completes this page.

MUC MAP (notes p4): runway slabs are solid (centreline stroke removed — it read as an unfilled black line), runway designators moved next to the runway ends instead of on top of the slabs, and on-runway traffic is yellow with wake-class sizing (`isHeavyType`: widebodies/military transports draw visibly larger).

MUC OPS board (notes p5): board shifted lower with a title underline; garbage callsigns fixed by the sanitizer.

MUC WX (notes p7): "MUC WEATHER" size-2 header with underline, evenly spaced icon rows, field status in an outlined pill, raw METAR tail kept.

Not yet verified: compile + flash + on-device visual pass (user builds).

## rev1.0 (Product Restructure)

The firmware graduated from experimental builds to the final product layout. Full summary lives in the sketch header; highlights:

- New page order: HOME RADAR, TRAFFIC BRIEF, NEAREST TRACK, COOLEST TRACK, MUC MAP, WEATHER, LEGEND. The MUC OPS board and SPOTTER card pages were removed (their data lives on the map/brief/coolest pages now).
- Home radar: red compass-north arrow, DEP (red, west) / total (white, top) / ARR (green, east) counters, solid subtle inner rings + dashed outer ring, rim squares pushed to r=108, priority-based label picking (emergency > helicopter > special > MUC-related > nearest).
- TRAFFIC BRIEF: five compact entries with graceful fallbacks, no icons, no legend.
- Tracking pages share one renderer with a mini-map that is fully visible (the old full-screen rings were mostly hidden behind text bands — the "vanishing grid"). Nearest + Coolest each keep their own path-history trail.
- MUC MAP: configurable zoom (default 16 px/km, was 26), ARR/DEP/GND counter header + temp/wind line, projected path stubs on traffic, next-ARR/next-DEP rows at the bottom.
- Weather header shortened to "MUC" (the long title clipped on the glass).
- New LEGEND page rendered with the real symbol functions.
- Button rewritten around a CHANGE interrupt with press-length classification in the ISR: presses can no longer be lost inside blocking HTTP calls (the root cause of the "button stops working" reports). Long-press still resumes auto-scroll. `PAGE_BUTTON_ENABLED`, `PAGE_BUTTON_PIN` (GPIO9/BOOT supported), and `AUTO_SCROLL_ENABLED` are configurable.
- Every page redraws from cache after every successful fetch; fetch cadence and redraw cadence documented as separate concepts.
- Config template restructured as gift-ready ([REQUIRED] Wi-Fi / location / airport + optional tuning), with `#ifndef` defaults in the sketch so older config.h files still build.
- Added README.md (product) and docs/device_manual.html (printable wiring schematic + legend).

Not yet verified: compile + flash (user builds and pushes).

## v11 Symbol, Layout, And Button Polish

Driven by the latest device photos and handwritten notes.

RADAR page:

- Kept the clean `20km` range readout and removed bottom page indicators entirely.
- Restored visible `N/S/E/W` compass labels, placed close to the bezel but padded so they do not collide with aircraft labels.
- Added a compact traffic summary on the left side: green `ARR n`, red `DEP n`, and amber `OTH n`.
- Protected both the top aircraft-count chip and the new traffic summary from aircraft labels, so moving text no longer draws through those zones.
- Added consistent traffic symbols:
  - notable/cool aircraft draw as a star,
  - helicopters draw as a rotor/helicopter symbol,
  - normal arrivals/departures/overflights keep heading-aligned aircraft arrows,
  - out-of-range contacts remain blue rim squares.

Flight / Spotter cards:

- The airline name itself now uses the airline/callsign accent colour.
- The old separate accent line was removed so the card feels cleaner and wastes fewer pixels.
- Star/helicopter symbols appear near the callsign when the selected aircraft is notable or rotary-wing.

TRACK page:

- Added a live bearing/track line such as `BRG 126 SE  TRK 263 W`.
- Kept the current-position dot and MUC mini-runway but moved the text stack down to prevent top-edge clipping.
- The tracked aircraft marker now uses the same star/helicopter/arrow grammar as the other pages.

MUC MAP page:

- Runways are now solid dark-gray slabs with clean labels and lighter edge strokes.
- Removed the odd circles around aircraft markers.
- Aircraft on/near a runway draw as yellow aircraft symbols.
- Low live traffic uses the same symbol grammar: green arrivals, red departures, blue/purple stationary or out-of-view traffic, star for notable aircraft, rotor for helicopters.

MUC OPS page:

- Lowered and centered the arrival/departure board.
- Kept exactly two arrivals and two departures.
- Preserved route context where the cache knows it, with fixed-width row boxes to avoid number/callsign overlap.

MUC WX page:

- Added small color-coded row icons for wind, visibility, clouds, temperature, pressure, weather, and runway-in-use.
- Kept the METAR/status area compact so the bottom line no longer looks detached or clipped.

Manual page button:

- Short press still cycles to the next useful page and stays there.
- Long press resumes the automatic carousel.
- Debounce was tightened for a more reliable page press.
- Manual page changes now redraw immediately from cached data and do not trigger route/API prefetches, so pressing the button does not feel like a network wait.
- A GPIO2 wiring schematic was added at `docs/page_button_wiring_schematic.html`.

Verification status:

- Static source checks were run in Codex.
- `arduino-cli` is not installed in this workspace, so compile/flash still needs Arduino IDE verification on the real ESP32-C3.

## v12 Green Radar, Special Page, And Nearest Consolidation

Driven by the next device polish request.

Global:

- Updated the desk/home coordinates in both `config.example.h` and local `config.h`:
  - latitude `48.39576799207869`
  - longitude `11.773933419079786`
- Changed `RADAR_RANGE_KM` to `20.0` in both `config.example.h` and local `config.h` for a less cluttered home radar.
- Added named page IDs (`PAGE_RADAR`, `PAGE_SPECIAL`, `PAGE_NEAREST`, etc.) so the page carousel is easier to maintain.
- Changed the radar/grid palette from blue to a subtle classic radar green.
- Preserved the color grammar:
  - red = MUC departure / DEP,
  - green = MUC arrival / ARR,
  - amber = other traffic,
  - blue square = out-of-range edge contact,
  - star = special/notable aircraft,
  - helicopter symbol = helicopter,
  - purple = ground/stationary traffic,
  - red alert dot = emergency squawk.

Page structure:

- Removed the separate nearest-flight detail page from the carousel.
- Added `SPECIAL` immediately after `RADAR`.
- The page order is now: `RADAR`, `SPECIAL`, `NEAREST`, `MUC MAP`, `MUC OPS`, `SPOTTER`, `MUC WX`.
- `NEAREST` is the consolidated nearest-aircraft page: aircraft details, path, bearing/track, altitude/speed, route/airline, MUC marker, and CPA.

RADAR page:

- Reworked counters:
  - total airborne count is now only a number under `N`,
  - `DEP n` sits in the southwest radar area,
  - `ARR n` sits in the southeast radar area,
  - `OTH` was removed.
- Radar ring/grid labels are now optional during live frames. Full refreshes draw the km labels; live motion frames redraw only the subtle green geometry and the small counters.
- Out-of-range aircraft remain blue squares pinned to the radar rim at the correct bearing.
- Emergency, helicopter, and special-aircraft symbols are now drawn on the home radar page through the shared marker helper.

SPECIAL page:

- Shows up to four non-duplicate high-interest rows:
  - emergency near home,
  - helicopter near home,
  - special/cool aircraft near home,
  - special/cool aircraft near Munich.
- Each row includes a meaningful symbol, callsign, distance/bearing, and a short reason such as squawk, helicopter type, MUC arrival/departure, on-ground at MUC, or rare aircraft type.
- Includes a tiny bottom legend for DEP, ARR, OTH, EDGE, STAR, HELI, and GND.
- If nothing special is present, it falls back to `regular traffic` / `no special flights`.

NEAREST page:

- Replaces the old separate `FLIGHT` and `TRACK` pages.
- Adds a clear `NEAREST` header.
- Protects the top aircraft-detail band and bottom bearing/CPA band with background clears, so `MUC` and map graphics cannot overwrite the bearing line.
- Includes airline/route when cached, aircraft type/name, bearing/current track, altitude, speed, and closest point of approach.

Button / network responsiveness:

- Added shorter named HTTP timeouts for ADS-B, route lookup, and METAR requests.
- The main loop now avoids starting a new network request while the MODE button is currently held.
- Route prefetch checks the raw button state before and between lookup calls, so a button press can interrupt queued route work sooner.
- Manual page changes still redraw immediately from cached data and do not prefetch routes on the button press.

Verification status:

- Static checks were run in Codex after the change.
- `arduino-cli` is still unavailable in this shell, so Arduino IDE compile/flash remains the required hardware verification step.
