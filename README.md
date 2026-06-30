# Öffi

A real-time public-transport departure board for the **ESP32 "Cheap Yellow Display" (CYD)**.
It shows the next departures for your stops on the 2.8" screen and refreshes automatically.

Built-in data providers:

- **Wiener Linien** — Vienna trams, U-Bahn and buses (countdown in minutes)
- **ÖBB** — Austrian railways / S-Bahn (planned + real-time clock times, ÖBB board look)

Both run at once and are merged into one board, sorted by soonest departure. Adding
another transit provider is a small, well-defined job — see [Adding a provider](#adding-a-provider).

Everything is configured **on the device** through a phone-friendly web portal — WiFi
(with a scannable QR code to join), which stops to show, and the board settings. There is
no config file and no recompiling to change any of it.

> "Öffi" is the Austrian/German colloquial word for public transport
> (*öffentliche Verkehrsmittel*) — trams, trains, buses, the lot.

![Öffi departure board on the Cheap Yellow Display](docs/preview.png)

---

## Hardware

| | |
|---|---|
| Board | ESP32 "Cheap Yellow Display" (CYD), 2.8" — sold as `ESP32-2432S028` / `185-ESP32-2.8-HuangBan` |
| MCU | ESP32-WROOM (dual-core, WiFi) |
| Display | 2.8" 240×320 SPI TFT, **ST7789** controller (some listings say ILI9341 — it is ST7789) |
| Touch | XPT2046 resistive — drives the on-screen settings menu |

No wiring required — the display, MCU and USB-serial are all on the one board. Just a USB cable.

## Software

- [PlatformIO](https://platformio.org/) (CLI or the VS Code extension)
- Arduino framework for ESP32
- Libraries (installed automatically by PlatformIO): [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI),
  [ArduinoJson](https://arduinojson.org/), [QRCode](https://github.com/ricmoo/QRCode),
  [XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen)

All display/pin settings are passed as `build_flags` in [`platformio.ini`](platformio.ini) —
you do **not** need to edit TFT_eSPI's `User_Setup.h`.

---

## Quick start

```bash
# 1. Clone
git clone https://github.com/maikischa/Oeffi.git
cd Oeffi

# 2. Build, flash and watch the serial log — there's nothing to configure first
pio run --target upload
pio device monitor
```

There is **no config file to edit** — WiFi, providers (which stops to show) and system
settings are all set up on the device itself via the web portal (see below). The board
boots with no provider configured and shows "No departures" until you add one.

## First boot — WiFi setup

On first boot (or whenever it has no working WiFi), the board can't reach the internet yet, so
it puts **itself** into setup mode:

1. The screen shows a **WiFi Setup** page with a **QR code** and the network name `Oeffi-Setup`.
2. On your phone, **scan the QR code** to join the `Oeffi-Setup` network automatically
   (it's an open network — no password). Or join it manually from your WiFi list.
3. A **captive-portal page opens by itself** (the same one is at `http://192.168.4.1/`).
   Pick your home network from the list, type its password, and tap **save**.
4. The board reboots and connects. With no provider configured yet, it shows
   "No departures" — open `http://oeffi.local/providers` (or scan the network again on a
   phone) and add at least one stop; saving there reboots the board and it starts showing
   departures. Both WiFi credentials and provider config are stored in the ESP32's flash
   (NVS), so they persist across every future boot.

**No working internet?** Some open/"free" WiFi networks (hotel, transit, café) let a device
*join* but block everything until you accept terms in a browser — which the ESP32 can't do. The
board detects this (the NTP clock never syncs) and **drops back to the WiFi Setup screen** with a
*"No internet"* note, so you can pick a different network. It does **not** sit on a blank board.

### Changing settings later

While the board is running it serves a config page at **`http://oeffi.local/`**
(or its IP — shown in the serial log, and briefly on the display after connecting), with
three subpages — saving on any of them reboots the board to apply the change:

| Page | What you can change |
|---|---|
| **`/wifi`** | Switch to a different network, or **forget** the current one (reboots back into setup mode). |
| **`/providers`** | Enable/disable Wiener Linien and ÖBB, set their stops/stations, line and direction filters. Editing any field auto-enables that provider. |
| **`/system`** | How many departures to show at once (3–4 looks best) and the refresh interval (seconds). |

Don't remember the address? **Tap the screen** — a gear icon appears in the corner; tap it for a
QR code straight to the config page.

## Configuration

There is **nothing to configure at build time** — everything lives in the on-device web
portal above and persists in the ESP32's flash (NVS). The only compile-time items left are
the hardware/display `build_flags` in [`platformio.ini`](platformio.ini), which you only
touch if your CYD variant differs (see [Troubleshooting](#troubleshooting)).

---

## How it works

Three decoupled layers handle the live board, plus a small WiFi-provisioning side:

```
main.cpp        Orchestration: WiFi connect, NTP, source registry, fetch → merge
                → sort, and the on-screen touch UI. Falls back to setup if there's
                no internet. Knows nothing about TFT or transit APIs.

departures.*    Data layer: the DepartureSource interface and the concrete
                providers (WienerLinienSource, OebbSource) + HTTP/JSON helpers.
                Knows nothing about the display.

display.*       Presentation: owns the TFT, the palette, the row renderers, the
                setup/QR screens and the on-board tool button. No network code.

touch.*         Reads the XPT2046 touchscreen on its own SPI bus and reports taps
                mapped to screen pixels. No display or network code.

portal.*        Web portal: the "Oeffi-Setup" captive portal (first run) and the
                always-on config page at oeffi.local (with /wifi, /providers and
                /system subpages). Persists via settings.*.

settings.*      Tiny key/value store over the ESP32's flash (NVS) — holds every
                user setting (WiFi, providers, board/refresh). There is no
                config file; this is the single source of truth.
```

Each provider's `fetch()` appends normalised `Departure` records. `main.cpp` merges all
sources, sorts by countdown, and hands the list to `displayBoard()`. The only thing tying
data to presentation is `RowStyle` — an enum on each `Departure` that the display maps to a
renderer via a small dispatch table. There is no `if (provider == …)` anywhere in the UI.

### Startup flow

```
boot → load settings → WiFi credentials? ──no──► WiFi Setup screen + captive portal
                          │ yes                       (save → reboot)
                          ▼
                       connect ──fail──► (same setup screen)
                          │ ok
                          ▼
                       NTP sync ──fail──► WiFi Setup screen ("No internet")
                          │ ok                (open/captive-portal WiFi → pick another)
                          ▼
                       fetch → merge → sort → draw board   (repeat every refresh interval)
```

### Data sources

- **Wiener Linien** uses the official OGD real-time monitor
  (`wienerlinien.at/ogd_realtime/monitor`), which returns a `countdown` directly.
- **ÖBB** uses ÖBB's own Scotty "liveticker" station board (`fahrplan.oebb.at`) — no API key,
  no proxy. Station names are resolved to IDs via the Scotty station finder, and the optional
  destination filter uses Scotty's native `dirInput` parameter. ÖBB clock times are computed
  from the board's local Vienna time, which is why a working NTP sync is required.

## Adding a provider

1. Add a `class FooSource : public DepartureSource` in `departures.h` / `departures.cpp`
   (model it on the two existing sources). In its `fetch()`, push `Departure` records and
   tag each with a `RowStyle`.
2. Add named accessors (`fooEnabled()`, etc.) to `settings.h`/`.cpp`, an
   `if (fooEnabled())` block in `registerSources()` in `main.cpp`, and a config
   form on the `/providers` page in `portal.cpp`.
3. Reuse an existing `RowStyle` for the row look — **or**, for a new look, add a `RowStyle`
   value, a `renderFoo()` function, and one entry in the `kRenderers[]` table in `display.cpp`.

Steps 1–2 are all you need if the new provider reuses an existing style.

## Project layout

```
platformio.ini          Board, libraries, TFT_eSPI build flags (only build-time config)
src/
  main.cpp              setup()/loop(), WiFi, NTP, source registry, touch UI, setup fallback
  departures.h/.cpp     DepartureSource interface + providers + HTTP helpers
  display.h/.cpp        TFT, palette, row renderers, setup/QR screens + tool button
  touch.h/.cpp          XPT2046 touchscreen → screen-pixel taps
  portal.h/.cpp         Captive-portal provisioning + oeffi.local config server
  settings.h/.cpp       All user settings store — WiFi, providers, system (ESP32 flash / NVS)
CLAUDE.md               Architecture notes & gotchas for contributors
```

## Troubleshooting

- **Stuck on the WiFi Setup screen** — the network has no usable internet (often an open/
  captive-portal WiFi). Scan the QR / join `Oeffi-Setup` and pick a different network.
- **Forgot which network it's on / want to change it** — open `http://oeffi.local/wifi`
  to change or forget it. If `oeffi.local` doesn't resolve, use the IP from the serial log.
- **Board shows "No departures"** — no provider is configured yet (or both got disabled);
  open `http://oeffi.local/providers` and add at least one stop.
- **Display upside-down** — change `tft.setRotation(1)` to `3` in `displayInit()` (`display.cpp`).
- **Wrong colours / mirrored** — this CYD variant is ST7789 with BGR order; the flags in
  `platformio.ini` (`ST7789_DRIVER`, `TFT_RGB_ORDER=TFT_BGR`, `TFT_INVERSION_OFF`) handle that.
- **`IO 21 is not set as GPIO` at boot** — harmless TFT_eSPI backlight message.
- **ÖBB rows missing** — the Scotty backend occasionally errors; the board degrades gracefully
  to the other providers and recovers on the next refresh.

## Contributing

Issues and pull requests are welcome — especially new transit providers. Please keep the
layer separation: no network code in `display.*`, no TFT code in `departures.*`/`portal.*`.

## License

[MIT](LICENSE) — do what you like, keep the copyright notice.

## Acknowledgements

- Inspired by [coppermilk/wiener_linien_esp32_monitor](https://github.com/coppermilk/wiener_linien_esp32_monitor).
- Wiener Linien open data (Stadt Wien) and ÖBB Scotty for the real-time data.
