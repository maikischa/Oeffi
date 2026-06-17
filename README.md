# Nahverkehrsanzeige

A real-time public-transport departure board for the **ESP32 "Cheap Yellow Display" (CYD)**.
It shows the next departures for your stops on the 2.8" screen and refreshes automatically.

Built-in data providers:

- **Wiener Linien** — Vienna trams, U-Bahn and buses (countdown in minutes)
- **ÖBB** — Austrian railways / S-Bahn (planned + real-time clock times, ÖBB board look)

Both run at once and are merged into one board, sorted by soonest departure. Adding
another transit provider is a small, well-defined job — see [Adding a provider](#adding-a-provider).

> The name is German for "local transport display".

---

## Hardware

| | |
|---|---|
| Board | ESP32 "Cheap Yellow Display" (CYD), 2.8" — sold as `ESP32-2432S028` / `185-ESP32-2.8-HuangBan` |
| MCU | ESP32-WROOM (dual-core, WiFi) |
| Display | 2.8" 240×320 SPI TFT, **ST7789** controller (some listings say ILI9341 — it is ST7789) |
| Touch | XPT2046 (present, not used yet) |

No wiring required — the display, MCU and USB-serial are all on the one board. Just a USB cable.

## Software

- [PlatformIO](https://platformio.org/) (CLI or the VS Code extension)
- Arduino framework for ESP32
- Libraries (installed automatically by PlatformIO): [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI), [ArduinoJson](https://arduinojson.org/)

All display/pin settings are passed as `build_flags` in [`platformio.ini`](platformio.ini) —
you do **not** need to edit TFT_eSPI's `User_Setup.h`.

---

## Quick start

```bash
# 1. Clone
git clone https://github.com/maikischa/Nahverkehrsanzeige.git
cd Nahverkehrsanzeige

# 2. Create your config from the template and edit it (WiFi + stops)
cp src/config.example.h src/config.h
$EDITOR src/config.h

# 3. Build, flash and watch the serial log
pio run --target upload
pio device monitor
```

The board connects to WiFi, syncs the clock over NTP, fetches departures and draws the board.

## Configuration

Everything is in `src/config.h` (your private copy of `src/config.example.h`). Highlights:

| Setting | Meaning |
|---|---|
| `WIFI_SSID` / `WIFI_PASS` | Your 2.4 GHz network (ESP32 has no 5 GHz). |
| `MAX_ROWS` | Departures shown at once (3–4 looks best). |
| `REFRESH_INTERVAL_MS` | How often to refresh (default 30 s). |
| `WL_ENABLED`, `RBL_IDS`, `LINE_FILTER` | Wiener Linien stop(s) — find your RBL at <https://till.mabe.at/rbl/>. |
| `OEBB_ENABLED`, `OEBB_STOPS` | ÖBB station name(s), e.g. `"Wien Mitte"` — resolved to IDs automatically. |
| `OEBB_TRAINS_ONLY` | Hide bus/tram/subway at ÖBB stops. |
| `OEBB_DESTINATION` | Optional: only trains heading via this station (incl. pass-through). |

Set `WL_ENABLED`/`OEBB_ENABLED` to `0` to turn a provider off entirely.

> **Your `config.h` is git-ignored** so your WiFi password never lands in a commit.

---

## How it works

Three decoupled layers:

```
main.cpp        Orchestration: WiFi, NTP, source registry, fetch → merge → sort.
                Knows nothing about TFT or transit APIs.

departures.*    Data layer: the DepartureSource interface and the concrete
                providers (WienerLinienSource, OebbSource) + HTTP/JSON helpers.
                Knows nothing about the display.

display.*       Presentation: owns the TFT, the palette and the row renderers.
                Knows nothing about the network.
```

Each provider's `fetch()` appends normalised `Departure` records. `main.cpp` merges all
sources, sorts by countdown, and hands the list to `displayBoard()`. The only thing tying
data to presentation is `RowStyle` — an enum on each `Departure` that the display maps to a
renderer via a small dispatch table. There is no `if (provider == …)` anywhere in the UI.

### Data sources

- **Wiener Linien** uses the official OGD real-time monitor
  (`wienerlinien.at/ogd_realtime/monitor`), which returns a `countdown` directly.
- **ÖBB** uses ÖBB's own Scotty "liveticker" station board (`fahrplan.oebb.at`) — no API key,
  no proxy. Station names are resolved to IDs via the Scotty station finder, and the optional
  destination filter uses Scotty's native `dirInput` parameter.

## Adding a provider

1. Add a `class FooSource : public DepartureSource` in `departures.h` / `departures.cpp`
   (model it on the two existing sources). In its `fetch()`, push `Departure` records and
   tag each with a `RowStyle`.
2. Add `FOO_*` settings to `config.example.h` and a `#if FOO_ENABLED` block in
   `registerSources()` in `main.cpp`.
3. Reuse an existing `RowStyle` for the row look — **or**, for a new look, add a `RowStyle`
   value, a `renderFoo()` function, and one entry in the `kRenderers[]` table in `display.cpp`.

Steps 1–2 are all you need if the new provider reuses an existing style.

## Project layout

```
platformio.ini          Board, libraries, TFT_eSPI build flags
src/
  config.example.h      Template — copy to config.h
  config.h              Your private settings (git-ignored)
  main.cpp              setup()/loop(), WiFi, NTP, source registry
  departures.h/.cpp     DepartureSource interface + providers + HTTP helpers
  display.h/.cpp        TFT, palette, row renderers
CLAUDE.md               Architecture notes & gotchas for contributors
```

## Troubleshooting

- **Display upside-down** — change `tft.setRotation(1)` to `3` in `displayInit()` (`display.cpp`).
- **Wrong colours / mirrored** — this CYD variant is ST7789 with BGR order; the flags in
  `platformio.ini` (`ST7789_DRIVER`, `TFT_RGB_ORDER=TFT_BGR`, `TFT_INVERSION_OFF`) handle that.
- **`IO 21 is not set as GPIO` at boot** — harmless TFT_eSPI backlight message.
- **ÖBB rows missing** — the Scotty backend occasionally errors; the board degrades gracefully
  to the other providers and recovers on the next refresh.

## Contributing

Issues and pull requests are welcome — especially new transit providers. Please keep the
three-layer separation (no network code in `display.*`, no TFT code in `departures.*`).

## License

[MIT](LICENSE) — do what you like, keep the copyright notice.

## Acknowledgements

- Inspired by [coppermilk/wiener_linien_esp32_monitor](https://github.com/coppermilk/wiener_linien_esp32_monitor).
- Wiener Linien open data (Stadt Wien) and ÖBB Scotty for the real-time data.
