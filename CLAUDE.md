# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

ESP32 firmware for a public-transport departure board ("Nahverkehrsanzeige") running on the **Cheap Yellow Display** (CYD) — a 2.8" ESP32-WROOM all-in-one board sold as "185-ESP32-2.8-HuangBan". Pulls realtime departures from **multiple providers** (Wiener Linien + ÖBB) and shows them on a landscape amber-on-black board. PlatformIO + Arduino + TFT_eSPI + ArduinoJson.

Inspired by `coppermilk/wiener_linien_esp32_monitor` (built for the LILYGO T-Display-S3). The display is a clean rewrite for the CYD's 320×240 landscape ST7789 screen.

### Architecture — three decoupled layers

| Layer | Files | Responsibility |
|---|---|---|
| **Orchestration** | `main.cpp` | WiFi, NTP, source registry, fetch/merge/sort loop. No TFT or API code. |
| **Data / providers** | `departures.h` / `.cpp` | `DepartureSource` interface + concrete sources + HTTP/JSON helpers. No display code. |
| **Display** | `display.h` / `.cpp` | Owns the `TFT_eSPI`, palette, and per-style row renderers. No network code. |

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

1. `registerSources()` (`main.cpp`) — `#if`-guarded static source instances pushed to `g_sources`.
2. `connectWiFi()` — joins the 2.4 GHz network from `config.h`.
3. `syncClock()` — NTP via `configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", ...)` so `mktime()`/`localtime()` match the ÖBB board's Vienna wall-clock times. Required before ÖBB countdowns work; `OebbSource::fetch()` bails if the clock isn't synced (`time(nullptr) < 1.7e9`).
4. `fetchAll()` — iterates `g_sources`, merges into `g_departures`, sorts ascending by countdown.
5. `displayBoard()` (`display.cpp`) — draws `MAX_ROWS` rows; each row dispatched through `kRenderers[(int)style]`. `renderCountdown` = amber-on-black (line | direction | minutes); `renderClock` = ÖBB blue board (line + destination, planned/delayed clock times). Shared `drawDirection()`/`wrapText()` fit the direction to 2 lines. No header bar.
6. `loop()` re-fetches every `REFRESH_INTERVAL_MS`.

### Key implementation notes

- **`httpGetRaw()` / `httpGetJson()`** ([departures.cpp](src/departures.cpp)) are the shared HTTP helpers. They read the **full body into a String before parsing** — streaming straight from `WiFiClientSecure` intermittently yields `IncompleteInput` because an empty TLS chunk looks like EOF to ArduinoJson. Buffering fixes it; RAM headroom is ample. `httpGetRaw` follows redirects (ÖBB serves `http`→`https`).
- If a source's `fetch()` returns false (e.g. ÖBB backend hiccup), the board **degrades gracefully** — that source is skipped and others still render.
- **History/avoid re-treading:** the ÖBB source first used `v6.oebb.transport.rest`, which proved unreliable — its `oebb-hafas` lib fell out of sync with ÖBB's HAFAS, returning `502 isHafasError` on `…/mgate.exe` for `departures`/`journeys`. Its `?direction=` and `/journeys` filters were also broken. We switched to ÖBB's own Scotty liveticker (current code). Don't reintroduce `transport.rest` without checking it's been fixed.

### Configuration

All user settings live in `src/config.h`: WiFi credentials, per-provider enable flags (`WL_ENABLED`, `OEBB_ENABLED`), `RBL_IDS` (Wiener Linien, find at https://till.mabe.at/rbl/), `OEBB_STOPS` (ÖBB station **names**, auto-resolved), `OEBB_DESTINATION` (optional ÖBB direction-filter station name), `OEBB_TRAINS_ONLY`, `OEBB_MAX_PER_STOP`, `MAX_ROWS`, `REFRESH_INTERVAL_MS`. **This file holds credentials — keep it out of any public commit.**

## Build & flash commands

```bash
# Build
pio run

# Flash (port is hardcoded in platformio.ini as /dev/cu.usbserial-11420)
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

- `src/main.cpp` — `setup()`/`loop()`, WiFi, NTP, `registerSources()`, fetch/merge. No TFT or API code.
- `src/departures.h` / `.cpp` — `RowStyle`, `Departure`, `DepartureSource` + `WienerLinienSource`/`OebbSource`, `httpGetRaw()`/`httpGetJson()`. No display code.
- `src/display.h` / `.cpp` — `displayInit/Status/Board`, the `TFT_eSPI`, palette, and `kRenderers[]` style→renderer table. No network code.
- `src/config.h` — all user settings + credentials.
- `platformio.ini` — single `[env:cyd]` environment; all TFT/hardware config lives here as `build_flags`.

## Debug output

Serial at 115200 baud (`pio device monitor`). Note: PlatformIO's bundled Python venv can break when Homebrew bumps `python@3.x` (dangling symlink); if `pio device monitor` or scripted pyserial reads fail, recreate a throwaway venv (`python3 -m venv … && pip install pyserial`) to read `/dev/cu.usbserial-11420` directly. The `IO 21 is not set as GPIO` warning at boot is just TFT_eSPI's backlight pin init — harmless.
