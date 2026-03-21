# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Pebble smartwatch app (SDK v3) for HVV (Hamburger Verkehrsverbund) transit departures. Displays real-time departures for a configurable station using the GTI API (gti.geofox.de). Targets platforms: aplite, basalt, chalk, diorite, emery.

This is a watchapp (not a watchface).

## SDK Reference

Full documentation: https://developer.repebble.com/sdk/

C API docs: https://developer.rebble.io/docs/c/

### SDK Modules

The Pebble C SDK has six top-level modules:

- **Foundation** — App lifecycle, AppMessage/AppSync (phone<->watch communication), persistent Storage, Dictionary (data serialization), Timer/Wakeup scheduling, TickTimerService, DataLogging, event services (Accelerometer, Compass, Battery, Connection, Health), WatchInfo, Platform detection
- **Graphics** — Low-level drawing routines for the watch display
- **User Interface** — Window/WindowStack, layers (TextLayer, BitmapLayer, MenuLayer, SimpleMenuLayer, ScrollLayer, ActionBarLayer, StatusBarLayer, RotBitmapLayer), Clicks (button input), Animation/PropertyAnimation, Vibes, Light, NumberWindow, ActionMenu, UnobstructedArea
- **Smartstrap** — Communication with external smartstrap hardware
- **Worker** — Background task processing (AppWorker communicates with foreground app)
- **Standard C** — Standard C library functions adapted for Pebble

### Platform Differences

| Feature | Aplite, Diorite | Basalt, Chalk, Emery |
|---------|----------------------|----------------------|
| Colors | Black & white only | 64 colors |
| Display shape | Rectangular | Rectangular (Basalt, Emery) or Round (Chalk) |
| Resolution | 144×168 | Up to 200×228 |

Use preprocessor defines for conditional compilation: `PBL_COLOR`, `PBL_BW`, `PBL_RECT`, `PBL_ROUND`, `PBL_MICROPHONE`. Use `PBL_IF_COLOR_ELSE()` macro for inline color/BW branching.

Never hardcode screen dimensions — use `layer_get_bounds()` on the window's root layer and UnobstructedArea APIs.

Tag platform-specific image resources with `~bw` or `~color` suffixes.

### Battery Conservation

- Prefer `MINUTE_UNIT` over `SECOND_UNIT` for tick handlers
- Batch accelerometer samples to reduce wake frequency
- Set compass heading filters to ignore minor changes
- Cache data locally with Storage API to reduce Bluetooth usage
- Keep `SNIFF_INTERVAL_NORMAL` (low-power BT) as default; only switch for bursts
- Minimize vibration motor use and manual backlight activation

### Modular App Architecture

For non-trivial apps, split code into:
- `src/c/main.c` — High-level orchestration only
- `src/c/windows/` — One `.c`/`.h` pair per window, using `.load`/`.unload` handlers for lifecycle
- `src/c/modules/` — Reusable data/utility modules

Use `static` variables within modules for encapsulation. Expose only necessary functions via headers.

## Build Commands

```bash
pebble build                       # Build for all target platforms
pebble install --emulator basalt   # Install to emulator (aplite, basalt, chalk, diorite, emery)
pebble install --phone <IP>        # Install to phone
pebble logs                        # View app logs (run right after install to not miss logs)
pebble screenshot --emulator basalt  # Take screenshot (saves to cwd, delete after viewing)
```

When testing via emulator: build, install, then take a screenshot to verify visuals. Run `pebble logs` immediately after install to capture JS logs. Screenshots save to the working directory — delete them after viewing.

The build system uses waf (`wscript`). C sources are globbed from `src/c/**/*.c`, JS from `src/pkjs/**/*.js`.

## Architecture

### C side (watch)
- `src/c/main.c` — App lifecycle, 30s refresh timer, orchestration
- `src/c/windows/departure_window.c/.h` — MenuLayer-based departure list UI
- `src/c/modules/data.c/.h` — Departure data model (TransitType enum, Departure struct, persistent storage)
- `src/c/modules/comm.c/.h` — AppMessage handling (receive departures, send requests to JS)
- `src/c/modules/icons.c/.h` — Programmatic transit type icon drawing (no bitmap resources)

### JS side (phone)
- `src/pkjs/index.js` — Clay config init, GTI API calls, AppMessage bridge, demo data fallback
- `src/pkjs/clay_config.js` — Clay configuration page definition (station, API credentials)
- `src/pkjs/hmac.js` — Self-contained HMAC-SHA1 + Base64 for GTI API authentication

### Config & build
- `package.json` — Pebble app manifest (UUID, platforms, message keys, Clay dependency)
- `wscript` — waf build configuration

### Data flow
1. Watch sends `REQUEST_DEPARTURES` via AppMessage every 30s
2. JS receives request, calls GTI API (or sends demo data if no credentials)
3. JS sends departure data back via AppMessage (DEP_COUNT, DEP_LINE[0..9], DEP_TYPE[0..9], etc.)
4. Watch parses message, updates data model, refreshes MenuLayer

### GTI API
- Endpoint: POST `https://gti.geofox.de/gti/public/departureList`
- Auth: HMAC-SHA1 signed requests (username + password configured via Clay)
- Response fields used: `line.name`, `line.type`, `direction`, `timeOffset`, `delay`

## Conventions

- C functions use `prv_` prefix for private/static functions
- Static globals use `s_` prefix
- Message keys for C/JS communication are declared in `package.json` under `pebble.messageKeys`
- Transit types: BUS=0, SBAHN=1, UBAHN=2, FERRY=3, UNKNOWN=4 (shared between C enum and JS constants)
- Icons are drawn programmatically (no bitmap resources) using HVV departure board shapes/colors
