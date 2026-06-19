# Ship Radar

![ESP32 Ship Radar](Photo.jpg)

Descended from MatixYO's [ESP32 Plane Radar](https://github.com/MatixYo/ESP32-Plane-Radar) project, adapted from ADS-B aircraft tracking to AIS ship tracking with [AISStream](https://aisstream.io/).

Firmware for the **ESP32-2424S012** board: an ESP32-C3 based module with an integrated **1.2" round GC9A01** display (240x240). Shows a circular **AIS ship radar** around your configured location, with **WiFiManager** for first-time setup.

## What it does

1. **Wi‑Fi setup** (if needed) — captive portal on AP **`ShipRadar-Setup`**
2. **Radar** — live vessels from [AISStream](https://aisstream.io/) on a sonar-style grid

After Wi‑Fi and an AISStream API key are saved, the device reconnects automatically; the radar runs in the main loop with a persistent AIS websocket. AISStream messages are accepted as text or binary websocket frames.

## Controls (BOOT, GPIO 9, active LOW)

| Action | Effect |
|--------|--------|
| **Short tap** | Cycle range preset (5 → 10 → 15 → 25 km); saved to flash |
| **Hold 3 s** | Clear Wi‑Fi, location, AIS key, and units; reboot into setup portal |

During setup you can also hold BOOT at power-on to force a credential reset (same as the long press).

## Wi‑Fi setup portal

Before first setup, create a free AISStream API key at [aisstream.io](https://aisstream.io/). The radar cannot receive vessel data until this key is entered in the setup portal.

**First-time setup** (no saved Wi‑Fi):

1. Connect to **`ShipRadar-Setup`**
2. Open **`http://ship-radar.local`** (preferred) or **`http://192.168.4.1`** — both are shown on the yellow setup screen; captive portal may open automatically
3. Set home Wi‑Fi, radar location, and AISStream API key, then save

**Reconfigure anytime** (after the device is on your network):

1. Open **`http://ship-radar.local`** or **`http://<device-ip>`** (e.g. from your router or serial log at boot)
2. Change Wi‑Fi, location, AISStream API key, units, or runway overlay; save

The same portal runs on the setup AP and on the device’s LAN IP while connected to Wi‑Fi. mDNS hostname is `ship-radar` → **ship-radar.local** (`kPortalHostname` in `config.h`). Some clients resolve `.local` slowly; use the IP if needed.

**Custom fields** (stored in NVS):

| Field | Purpose |
|-------|---------|
| **Latitude / Longitude** | Radar center and AISStream bounding-box position (defaults in `config.h` until set) |
| **AISStream API key** | Required key created at [aisstream.io](https://aisstream.io/), saved for the websocket subscription |
| **Display distances in miles** | Ring scale label in **mi** instead of **km** (e.g. `6mi` vs `10km`) |
| **Show airport runways** | Major-airport runway overlay on the radar (off to hide) |

After a reset, the device reboots and shows the setup screen immediately (no “Connecting” loop on stale credentials).

Default radar location in `include/config.h` is **51.03515, 1.55343**. Saved portal coordinates override this default.

## Radar display

### Grid

- Dark blue background, subdued green rings and crosshairs
- White **N / S / E / W** at the bezel; range label on the **east** spoke (ring 3 = ¾ of outer radius)
- White center dot

Layout and colors: `include/ui/radar_theme.h`.

### Range presets

| Ring 3 label | Outer radius (vessel scale) |
|------------|-------------------------------|
| 5 km / 3 mi | ~6.7 km |
| 10 km / 6 mi | ~13.3 km (default) |
| 15 km / 9 mi | ~20 km |
| 25 km / 16 mi | ~33.3 km |

Preset and miles/km choice persist across reboot (`planeradar` NVS namespace).

### Airport Overlay

- Optional legacy major-airport runway overlay from the original project; disabled by default for marine use
- Teal runway lines with one ICAO label per airport (e.g. `KJFK`); toggle in the Wi‑Fi setup portal
- Update the embedded list: `python3 scripts/build_large_airports.py`

### Vessels

- **Inside the outer ring** — red ship-shaped heading marker, magenta course/speed vector (clipped at the ring), vessel name / type / speed tags
- **Outside the ring** (still within the AISStream bounding box) — small **red dot on the screen rim** at the correct bearing (direction cue; not distance-accurate past the ring)
- **Tags** — placed toward the **center**: west (left) → tag on the **right** of the symbol; east (right) → tag on the **left**

As range decreases (or vessels approach), targets move inward; beyond-ring dots become full symbols when they cross the outer ring.

### AISStream

- Source: `wss://stream.aisstream.io/v0/stream`
- Subscription bounding box: derived from `ui::radar::fetchRadiusKm()` around the configured latitude/longitude
- API key: entered in the captive/LAN portal and stored in NVS
- Incoming AIS JSON is parsed from both websocket text and binary frames
- Serial health line every ~30 s: `ais: health key=yes ws=connected frames=... vessels=... last=... status="..."`
- Display refresh fallback: `kAisDisplayRefreshIntervalMs` in `config.h`

## Configuration

Edit **`include/config.h`** for hardware and behavior:

| Area | Keys / notes |
|------|----------------|
| Portal | `kPortalApName`, `kPortalIp`, `kPortalHostname` / `kPortalHostUrl` (mDNS; needs `-DWM_MDNS` in `platformio.ini`) |
| Wi‑Fi timing | connect attempts, reconnect grace, portal timeout (`0` = no timeout) |
| BOOT | `kBootPin`, `kBootResetHoldMs`, `kBootTapMinMs` |
| Display SPI | pins, `kDisplayInvert`, `kDisplayRgbOrder`, `kDisplaySpiWriteHz` |
| Default location | `kDefaultRadarLat`, `kDefaultRadarLon` (until portal overrides) |
| AIS | `kAisDisplayRefreshIntervalMs` |

Useful serial monitor messages:

- `ais: websocket connected` / `ais: websocket disconnected`
- `ais: subscribed bbox [...] [...]`
- `ais: health key=yes ws=connected frames=123 vessels=12 last=0s status=""`
- `AIS ERR: ...` or `ais: JSON parse error: ...` if AISStream returns an error or malformed payload

Range presets: `include/ui/radar_range.h` (`kRangePresets`).

## Project layout

```
include/
  config.h
  hardware/
    lgfx_config.hpp
    display.h
    display_font.h
  data/
    large_airports.h
  ui/
    radar_theme.h
    radar_range.h
    radar_display.h
    runway_overlay.h
    status_screens.h
  services/
    wifi_setup.h
    radar_location.h
    ais_client.h
data/
  ui_font.vlw              — embedded smooth UI font (Noto Sans Bold)
scripts/
  build_large_airports.py
src/
  main.cpp
  data/
    large_airports_data.cpp
  hardware/
  ui/
  services/
```

## Board

Target board: **ESP32-2424S012**.

The GC9A01 display is integrated on the board, so no separate display wiring is needed. Display pin mapping is defined in `include/config.h` and `include/hardware/lgfx_config.hpp`.

BOOT is the user/input button on GPIO **9**.

## Build

```bash
pio run -t upload
pio device monitor
```

- PlatformIO env: **`supermini`** (kept from the original project; targets the ESP32-C3 on the ESP32-2424S012)
- Serial: **115200** baud
- USB CDC on boot enabled in `platformio.ini`

### Web-flashable release image

Single `.bin` for [esptool-js](https://espressif.github.io/esptool-js/) and similar tools (ESP32-C3, 4 MB, flash at **0x0**):

```bash
chmod +x scripts/merge-firmware.sh   # once
./scripts/merge-firmware.sh
```

Writes `release/ship-radar-merged.bin`. Skip rebuild if firmware is already built:

```bash
./scripts/merge-firmware.sh --no-build
```

Or via PlatformIO only (output: `.pio/build/supermini/firmware-merged.bin`):

```bash
pio run -e supermini
pio run -t merge -e supermini
```

Put the board in download mode (hold **BOOT**, tap **RESET**), then flash with Chrome/Edge over USB.

### CI and releases (GitHub Actions)

| Workflow | When | Output |
|----------|------|--------|
| [Build](.github/workflows/build.yml) | Push / PR to `main` | Artifact `ship-radar-supermini` (merged + split `.bin` files, ~90 days) |
| [Release](.github/workflows/release.yml) | Git tag `v*` (e.g. `v1.0.0`) | GitHub Release asset `ship-radar-v1.0.0.bin` + `.sha256` |

To ship a version users can download:

```bash
git tag v1.0.0
git push origin v1.0.0
```

The release workflow builds firmware in CI and attaches the merged image to the release. Download from **Releases** on GitHub, then flash at **0x0** (ESP32-C3, 4 MB).

## Dependencies

- [LovyanGFX](https://github.com/lovyan03/LovyanGFX)
- [WiFiManager](https://github.com/tzapu/WiFiManager)
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
- [WebSockets](https://github.com/Links2004/arduinoWebSockets)
