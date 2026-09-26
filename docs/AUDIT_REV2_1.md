# Revision 2.1 logic audit

**26 September 2026 · Project: Saurav Haldar · Baseline: revision 2.0 (`9535025`)**

The baseline was formerly labelled product 2.3 / firmware 5.7.1. This release resets
current labels to 2.1 and preserves the historical comments and Git history.

## Scope and results

The audit traced startup, settings, the two tasks and shared state, each page's dispatch,
network scheduling and parsers, recovery, learning, geometry and setup UI. Both site builds
receive the same implementation. Munich has only MUC/EDDM; Estonia has only Tallinn/EETN
and Tartu/EETU. Old picker selections cannot recreate removed pages or network requests.

This is a source audit with targeted executable regressions, not exhaustive proof of every
input or interleaving. Hardware endurance, radio behavior and low-memory failure injection
on the ESP32 remain physical checks. Host transport tests simulate HTTPClient; they do not
exercise mbedTLS or the radio driver.

## Changes by file (relative to each sketch)

| Files | Finding and revision 2.1 behavior |
| --- | --- |
| `config.example.h`, local `config.h`, `src/airports.cpp`, `src/airports.h` | Munich's picker is disabled and its four other table entries removed. Estonia remains fixed Tallinn + Tartu. Credentials stay local. |
| `src/settings.cpp` | Defaults now pass the same validation as saved settings. Fixed airport identities override old NVS/API input; obsolete extra masks are cleared. Tartu takes the validated current map range. Saved timezones survive loading. Unknown page bits, absent/disabled start pages, unsupported unit/clock modes, unterminated names and arbitrary LED pins are sanitized. The read-only load path no longer tries to delete an NVS key. Settings schema stays 2, preserving credentials and learning. |
| `*.ino` | Check mutex creation before use; create/check the render task before launching networking, entering setup on those failures. The startup status read takes the state mutex. Disabled or stale emergency alerts no longer prevent button/automatic page changes. Revision history is recorded in the sketch header. |
| `src/net.cpp` | HTTP body reads have both idle and total-duration limits. Each aircraft attempt resets its failure classification. Fallback deadlines compare safely across timer wrap, and retry backoff starts after request completion. Offline status/LED and feed age update immediately. The renderer ages feed snapshots during blocked network work, so stale alerts expire. Auto-range samples are discarded across outages. Query longitude uses a wrapped midpoint without dividing by a polar longitude scale. |
| `src/portal.cpp` | Wi-Fi scan and join run through polling rather than an 18-second blocking HTTP handler. Scan JSON escapes arbitrary SSID characters. Geolocation uses bounded storage/read time and validates coordinates. A new SSID does not inherit another network's password. JSON save/test bodies must be objects. |
| `src/portal_page.h` | Requests have deadlines; pending scan and Wi-Fi results are polled. A failed retest clears prior location/results. SSIDs and location text cannot inject HTML. Unconfirmed saves no longer claim success. |
| `src/model.h`, `src/pages.cpp`, `src/page_data.cpp` | Emergency indices must refer to a retained contact; stale data cannot draw a live emergency overlay. Detail-page selections are checked before indexing. |
| `src/dbglog.cpp`, `src/dbglog.h`, `src/net.cpp` | Log body sends use nonblocking sockets with a shared 10-second limit, including partial sends; request draining is bounded. File-log lock acquisition is limited to 20 ms so concurrent render logs fall back to Serial. A failed log mutex allocation disables FATFS logging. |
| `src/geo.h` | Date-line offsets take the short displacement; antipodal haversine rounding cannot take a negative square root. |
| `src/baseline.cpp` | Nonfinite samples are ignored. NVS blobs are checked for exact size, magic, finite values and bounded counters before replacing the live model. Null serialization inputs fail safely. |
| `src/health.h` | Accumulated success/failure totals use 64-bit addition before percentage calculation. |
| `src/app_info.h` | Screen, setup, boot and HTTP labels share revision 2.1. The User-Agent no longer claims a fixed request rate while the interval is configurable. |
| `tools/*_check`, root `tools/run_checks.py` | Added production settings tests, tests of the actual extracted HTTP helper with simulated transport, corrupt baseline/geometry/alert bounds checks and fixed-site asynchronous browser regressions. |
| Root `.github/workflows/checks.yml` | Run host suites, variant parity, pin checks, browser regressions and both pinned ESP32-S3 builds on GitHub pushes/PRs using public example configurations. Physical tests remain separate. |
| Root docs and rights notices | Revision tables, credit to Saurav Haldar and measured 0.6 W running cost; public showcase remains Munich-focused. |

## Reviewed paths retained

- **Memory and rendering:** internal DMA sprite; PSRAM world snapshots and JSON; font fallback;
  45,056-byte net / 8,192-byte render stacks; snapshot bounds; page transition buffer fallback.
- **Aircraft:** missing coordinates; finite/range clamps; nearest-contact capacity; ground/air
  classification; callsign cleanup; enrichment lookup; emergency and interest selection.
- **Optional sources:** feature/page masks; METAR station routing; ISS fix validation and bounded
  prediction; launch parsing; stale remote-airport age; main-feed priority under low memory.
- **Network recovery:** joining and reconnecting; missing PSRAM; empty/malformed/truncated replies;
  HTTP 401/403/429; fallback and quiet periods; bounded bounce/restart counts; healthy rearming.
- **Learning and UI:** stale/time-discontinuous buckets; both unit modes; first boot, saved settings,
  schema mismatch, page masks, long/short button paths, portal flags, learning/factory resets.

## Executed verification

The final results are recorded here before publication. Logs and firmware artifacts remain in
ignored `build/rev21/` and `build/checks_munich/`, `build/checks_estonia/`.

- Host suites: 18 per variant, including actual production settings, HTTP-body and log-send code.
- Browser: fixed Munich and Tallinn/Tartu controls, legacy generic UI regression coverage,
  pending Wi-Fi success/failure, request abort, malicious SSID/location text, stale location reset and unconfirmed-save handling passed.
- Variant parity and both pin maps passed.
- Real page code: 840 crash-regression draws per compiled variant completed; full-page fixtures
  exercised holding, circling, go-around and survey detection. Both site fixtures and credit screens rendered and inspected.
- ESP32-S3 build: final run pending.
- GitHub Actions: workflow added; a remote run is separate from the local evidence above.

## Physical closeout and known limits

No board was flashed, and no 24–48-hour hardware soak was performed. Verify page cycling and
SPACE → WEATHER, Wi-Fi loss/recovery, setup and saved settings on each device. Preserve the
complete panic/reset log if a restart recurs. A successful build cannot prove steady-state
heap stability or resolve power-supply/radio faults.

Diagnostic log bodies stop after 10 seconds, so slow clients can receive a partial log.
During downloads, contended log lines remain on Serial rather than flash.

The portal's optional IP-location lookup can still briefly occupy the portal loop after
joining. Its browser poll allows 30 seconds for this slower result; ordinary requests use
an 8-second deadline. The HTTP transport has library-level connect/handshake timeouts plus the
new body deadline; TLS retries can delay new data while the independent renderer stays active.
TLS currently uses `setInsecure()` for public feeds. Certificate validation was not added.

The rendering harness uses synthetic state and can exercise extra-field slots that the shipped
configurations cannot select. The generic airport-picker browser cases are legacy regression
fixtures, not airports available on either device. Fixed-site settings tests cover the actual
compiled configurations. Default visible page counts are 10 (Munich) and 12 (Estonia); enabling
ABOUT adds one. Shared arrays retain their existing capacity.

The alternative PlatformIO partition lacks an `ffat` label, so persistent logs fall back to
Serial there. Reproduce the documented Arduino core/partition for FATFS logs. The ISS predictor
is approximate and external feed availability/coverage is outside this firmware's control.
