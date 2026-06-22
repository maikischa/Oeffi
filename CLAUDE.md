# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

ESP32 firmware for a public-transport departure board ("Öffi") running on the **Cheap Yellow Display** (CYD) — a 2.8" ESP32-WROOM all-in-one board sold as "185-ESP32-2.8-HuangBan". Pulls realtime departures from **multiple providers** (Wiener Linien + ÖBB) and shows them on a landscape amber-on-black board. PlatformIO + Arduino + TFT_eSPI + ArduinoJson.

Inspired by `coppermilk/wiener_linien_esp32_monitor` (built for the LILYGO T-Display-S3). The display is a clean rewrite for the CYD's 320×240 landscape ST7789 screen.

### Architecture — three decoupled layers

| Layer | Files | Responsibility |
|---|---|---|
| **Orchestration** | `main.cpp` | WiFi, NTP, source registry, fetch/merge/sort loop, **no-internet → setup fallback**. No TFT or API code. |
| **Data / providers** | `departures.h` / `.cpp` | `DepartureSource` interface + concrete sources + HTTP/JSON helpers. No display code. |
| **Display** | `display.h` / `.cpp` | Owns the `TFT_eSPI`, palette, per-style row renderers, and the WiFi-setup screen (incl. QR via `ricmoo/QRCode`). No network code. |
| **Portal** | `portal.h` / `.cpp` | `WebServer` + captive-portal `DNSServer` + mDNS: the `Oeffi-Setup` provisioning AP and the always-on `oeffi.local` config page. Persists via `settings.*`; no display code. |
| **Settings** | `settings.h` / `.cpp` | Tiny typed key/value store over NVS (`Preferences`, namespace `oeffi`). Holds WiFi creds; seeded from `config.h` so a pre-flashed config still works. |

A provider's `fetch()` appends normalised `Departure` records (`line, towards, countdown, style, planned, actual, delayed`). `main.cpp` calls `fetch()` on each registered source, merges, sorts by `countdown`, and hands the vector to `displayBoard()`.

The seam between data and display is **`RowStyle`** (an enum on each `Departure`). The display maps it to a renderer via the `kRenderers[]` table in `display.cpp` — there is **no `if (provider == ...)` anywhere**.

**To add a provider:**
1. Write a `FooSource : DepartureSource` in `departures.{h,cpp}` (model on the existing two); tag its departures with a `RowStyle`.
2. Add `FOO_*` settings to `config.h` and a `#if FOO_ENABLED` block in `registerSources()` (`main.cpp`).
3. Reuse an existing `RowStyle` for its look, **or** add a `RowStyle` value + a `renderFoo()` + one row in `kRenderers[]` (`display.cpp`) for a new look.

Steps 1–2 are all that's needed if the provider reuses an existing style. **To remove a provider:** set its `*_ENABLED` to 0 (or delete its source + registry block).

Implemented sources ([src/departures.cpp](src/departures.cpp)):

| Source | Endpoint | Countdown |
|---|---|---|
| `WienerLinienSource` | `wienerlinien.at/ogd_realtime/monitor?...&rbl=<id>` | direct `countdown` field; deep JSON needs an ArduinoJson **filter** + `NestingLimit(20)` |
| `OebbSource` | ÖBB Scotty liveticker: `fahrplan.oebb.at/bin/stboard.exe/dn?L=vs_scotty.vs_liveticker` | computed from the board's local `da`+`ti`/`rt.dlt` via `mktime()`, so **needs NTP + Vienna TZ** |

**ÖBB via the Scotty liveticker** (not `transport.rest`): chosen after the public `v6.oebb.transport.rest` instance proved unreliable (its `oebb-hafas` lib fell out of sync with ÖBB's HAFAS backend → 502s). The liveticker is ÖBB's own production backend — no API key, no proxy. Details:
- **Station names → evaIds** are resolved at first fetch via `…/bin/ajax-getstop.exe/dn?S=<name>` (`resolveEva()`), cached. So `OEBB_STOPS`/`OEBB_DESTINATION` are human station names, not IDs.
- The body is a JS assignment (`journeysObj = {…};`); `parseEmbeddedJson()` parses from the first `{`.
- Fields: `pr` (line), `st` (direction, **HTML-entity encoded** → `decodeHtml()` transliterates umlauts to ASCII for the GFX fonts), `ti`/`da` (planned), `rt.dlt`/`rt.dlm` (realtime), `rt.status` (skip if cancelled).
- **Destination filter** (`OEBB_DESTINATION`): the native `&dirInput=<destEva>` param — one request, server-side, includes pass-through trains. (Empty = off. The `transport.rest` `?direction=`/`/journeys` filters were broken, hence this.)
- `productsFilter` bitmask: `1111110000000000` trains/S-Bahn only, else `1111111111111111`.

### Data flow

1. `settingsBegin()` + `registerSources()` (`main.cpp`) — open NVS, push `#if`-guarded static source instances to `g_sources`.
2. **Provisioning gate:** if `!wifiConfigured()` (no stored SSID) **or** `connectWiFi()` fails → `enterProvisioning()` brings up the `Oeffi-Setup` AP + captive portal and spins forever (the save handler reboots). `connectWiFi()` joins the SSID/pass from the settings store (`wifiSsid()`/`wifiPass()`), which falls back to `config.h`'s `WIFI_SSID`/`WIFI_PASS`.
3. `syncClock()` — NTP via `configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", ...)` so `mktime()`/`localtime()` match the ÖBB board's Vienna wall-clock times. **Returns `bool`**; a failed sync (clock still `< 1.7e9` after 15 s) is treated as **"no real internet"** (open/captive-portal WiFi associates but blocks NTP/HTTPS) → `enterProvisioning("Kein Internet…")` drops back to the setup screen. Also required before ÖBB countdowns work; `OebbSource::fetch()` bails if the clock isn't synced.
4. `portalStartConfigServer()` — once online, the always-on `oeffi.local` status/reconfigure page goes up; `portalHandle()` is pumped from `loop()`.
5. `fetchAll()` — iterates `g_sources`, merges into `g_departures`, sorts ascending by countdown.
6. `displayBoard()` (`display.cpp`) — draws `MAX_ROWS` rows; each row dispatched through `kRenderers[(int)style]`. `renderCountdown` = amber-on-black (line | direction | minutes); `renderClock` = ÖBB blue board (line + destination, planned/delayed clock times). Shared `drawDirection()`/`wrapText()` fit the direction to 2 lines. No header bar.
7. `loop()` pumps `portalHandle()` and re-fetches every `REFRESH_INTERVAL_MS`.

**Provisioning UX** (`display.cpp` `displaySetupScreen()` + `portal.cpp`): the setup screen is a two-column layout — instructions + `Oeffi-Setup` name on the left, a scannable **WiFi-join QR** on the right (`WIFI:T:nopass;S:<ssid>;;`, meta-chars escaped via `qrEscape()`; QR version 3 / `ECC_LOW` via `drawQr()`). Scanning joins the open AP; the captive portal then auto-opens the config page. The portal reuses one `WebServer`: `portalStartSetupAP()` (AP + DNS catch-all + OS connectivity-probe redirects) vs `portalStartConfigServer()` (STA + mDNS). **Don't call both** — `syncClock()` failure path enters provisioning *before* `portalStartConfigServer()`, so handlers are never double-registered.

### Key implementation notes

- **`httpGetRaw()` / `httpGetJson()`** ([departures.cpp](src/departures.cpp)) are the shared HTTP helpers. They read the **full body into a String before parsing** — streaming straight from `WiFiClientSecure` intermittently yields `IncompleteInput` because an empty TLS chunk looks like EOF to ArduinoJson. Buffering fixes it; RAM headroom is ample. `httpGetRaw` follows redirects (ÖBB serves `http`→`https`).
- If a source's `fetch()` returns false (e.g. ÖBB backend hiccup), the board **degrades gracefully** — that source is skipped and others still render.
- **History/avoid re-treading:** the ÖBB source first used `v6.oebb.transport.rest`, which proved unreliable — its `oebb-hafas` lib fell out of sync with ÖBB's HAFAS, returning `502 isHafasError` on `…/mgate.exe` for `departures`/`journeys`. Its `?direction=` and `/journeys` filters were also broken. We switched to ÖBB's own Scotty liveticker (current code). Don't reintroduce `transport.rest` without checking it's been fixed.

### Configuration

Build-time settings live in `src/config.h`: per-provider enable flags (`WL_ENABLED`, `OEBB_ENABLED`), `RBL_IDS` (Wiener Linien, find at https://till.mabe.at/rbl/), `OEBB_STOPS` (ÖBB station **names**, auto-resolved), `OEBB_DESTINATION` (optional ÖBB direction-filter station name), `OEBB_TRAINS_ONLY`, `OEBB_MAX_PER_STOP`, `MAX_ROWS`, `REFRESH_INTERVAL_MS`.

**WiFi is normally set on-device, not in `config.h`.** `WIFI_SSID`/`WIFI_PASS` default to `""`; the user provisions via the captive portal and creds persist in NVS (the settings store seeds from `config.h`, so a pre-flashed SSID still works as an override-free default). To wipe stored creds: the portal's "WLAN vergessen" button (`clearWifi()`), or re-flash with NVS erase. **`config.h` is git-ignored — keep any credentials out of public commits.**

## Build & flash commands

```bash
# Build
pio run

# Flash (PlatformIO auto-detects the serial port; pin it via upload_port in platformio.ini)
pio run --target upload

# Serial monitor (115200 baud)
pio device monitor

# Build + flash + monitor in one step
pio run --target upload && pio device monitor

# Clean build artifacts
pio run --target clean
```

## Hardware: Cheap Yellow Display (CYD) pin mapping

| Function | GPIO |
|---|---|
| Display MOSI | 13 |
| Display MISO | 12 |
| Display CLK | 14 |
| Display CS | 15 |
| Display DC | 2 |
| Display RST | — (tied to 3.3 V) |
| Backlight | 21 (active HIGH) |
| Touch MOSI | 32 |
| Touch MISO | 39 |
| Touch CLK | 25 |
| Touch CS | 33 |
| Touch IRQ | 36 |
| RGB LED R | 4 (active LOW) |
| RGB LED G | 16 (active LOW) |
| RGB LED B | 17 (active LOW) |
| LDR | 34 |

Display driver: **ST7789** (despite the "ILI9341" listing/silkscreen — confirmed via the user's working ESPHome config: `model: ST7789V`, `color_order: bgr`, `invert_colors: false`). 240 × 320 native. Touch controller: **XPT2046** (resistive, not yet used). The two SPI peripherals share the bus at different CS pins.

## TFT_eSPI configuration

All TFT_eSPI settings are passed via `build_flags` in [platformio.ini](platformio.ini) — do **not** create or edit a `User_Setup.h`. The key flag is `-DUSER_SETUP_LOADED` which prevents TFT_eSPI from loading its own default setup.

The board runs **landscape** via `tft.setRotation(1)` in `displayInit()` (320 × 240); use `3` if upside-down. Portrait is `0`/`2`. Driver/colour flags (`ST7789_DRIVER`, `TFT_RGB_ORDER=TFT_BGR`, `TFT_INVERSION_OFF`) are set in [platformio.ini](platformio.ini). The `TFT_eSPI` object lives in `display.cpp` only.

## Source layout

- `src/main.cpp` — `setup()`/`loop()`, WiFi connect, NTP, `registerSources()`, fetch/merge, provisioning + no-internet fallback. No TFT or API code.
- `src/departures.h` / `.cpp` — `RowStyle`, `Departure`, `DepartureSource` + `WienerLinienSource`/`OebbSource`, `httpGetRaw()`/`httpGetJson()`. No display code.
- `src/display.h` / `.cpp` — `displayInit/Status/Board/SetupScreen`, the `TFT_eSPI`, palette, `kRenderers[]` style→renderer table, and `drawQr()`. No network code.
- `src/portal.h` / `.cpp` — `portalStartSetupAP()` / `portalStartConfigServer()` / `portalHandle()`: the `WebServer`/`DNSServer`/mDNS captive portal + config page. No display code.
- `src/settings.h` / `.cpp` — NVS-backed settings store (`settingsBegin()`, typed getters/setters, `wifiSsid/Pass`, `setWifiCreds`, `clearWifi`).
- `src/config.h` — build-time settings (providers, stops); WiFi creds optional (default on-device).
- `platformio.ini` — single `[env:cyd]` environment; all TFT/hardware config lives here as `build_flags`; `lib_deps` adds `ricmoo/QRCode`.

## Debug output

Serial at 115200 baud (`pio device monitor`). Note: PlatformIO's bundled Python venv can break when Homebrew bumps `python@3.x` (dangling symlink); if `pio device monitor` or scripted pyserial reads fail, recreate a throwaway venv (`python3 -m venv … && pip install pyserial`) to read the port directly (`/dev/cu.usbserial-*`; auto-detected, varies per cable/host). The `IO 21 is not set as GPIO` warning at boot is just TFT_eSPI's backlight pin init — harmless.
