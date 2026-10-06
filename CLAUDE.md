# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Pebble smartwatch app (SDK v3) for HVV (Hamburger Verkehrsverbund) transit departures. Displays real-time departures for a configurable station using the GTI API (gti.geofox.de). Targets platforms: aplite, basalt, diorite, emery, gabbro.

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

### Emery (Pebble Time 2) Scaling

Emery has a 200x228 display (~1.4x basalt's 144x168). UI elements use `#ifdef PBL_PLATFORM_EMERY` to define scaled sizes — roughly 1.5x for dimensions and one font size up (GOTHIC_18→GOTHIC_24, GOTHIC_14→GOTHIC_18). Pebble's built-in Gothic fonts have varying amounts of internal top padding at different sizes, so vertical nudge values (`_Y_NUDGE`) need per-platform tuning. Always test on both basalt and emery emulators when changing layout constants.

### Round (gabbro)

Gabbro is round, 260x260, color, with a touchscreen. It shares emery's size tables (`#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)`); round-specific behaviour keys on `PBL_ROUND`, never platform names.

- Departure list: each row is inset with `round_inset()` (`src/c/modules/round.c`) from its on-screen position, so badge and minutes stay inside the circle; rows with no room left draw only their background. Extra bottom padding lets the last row reach the middle. `round_inset()` returns 0 on rectangular screens, so the rectangular path is unchanged.
- Station list: centre-focused round `MenuLayer`, selected row taller (68 px with the distance/services line, 44 px for one-line rows like favorites) than neighbours (32 px). **MenuLayer caches the new selection's position using the old row's selected height, leaving a gap, and reloading during its selection animation crashes.** So on round the window handles up/down/select itself, tracks the selection (`s_round_sel`), sizes rows from it, reloads, then jumps the menu's selection without animation.
- Touch navigation (`app_touch_navigation_enable`, under `#ifdef PBL_TOUCH`) is a watch setting, default on, applied in `settings.c`. The emulator cannot inject touch; test on a device.
- Tapping a departure (touch watches, while touch navigation is on) opens its route. The departure window reads taps from the raw touch stream (`touch_service_subscribe`) while the system touch bridge keeps scrolling the list; the bridge alone doesn't say where a tap landed. To open a route in the emulator, temporarily bind SELECT to `prv_open_route_at()` and revert before commit.
- Test settings in the emulator with `pebble send-app-message --emulator <p> --int <key>=<value>`, numeric keys from `build/js/message_keys.json`.

Tag platform-specific image resources with `~bw` or `~color` suffixes.

### Gotchas worth remembering

- **AppMessage buffer sizes.** `app_message_*_size_maximum()` returns ~8 KB on every platform, but aplite/diorite have only ~15 KB of app heap total. Calling `app_message_open(maximum, maximum)` silently fails with `APP_MSG_INVALID_STATE` (32768). Pick sizes that fit the actual messages — see `comm.c` for the per-platform conditional.
- **Watch identifier.** The new PebbleKit JS shipping in the CoreDevices mobile app no longer exposes a `serialNumber` field on `Pebble.getActiveWatchInfo()` (regardless of watch firmware). Use `Pebble.getWatchToken()` instead — it returns a 32-char lowercase hex string that's MD5(serial + appUuid + salt), stable per-device, and app-scoped.
- **Emulator vs proxy mode.** `Pebble.getWatchToken()` returns nothing on QEMU emulators, so proxy mode falls through to demo. To exercise the proxy path on an emulator, temporarily patch `getWatchToken()` in `api.js` to return a fixed valid hex value when `info.model.indexOf('qemu') >= 0` — and revert before commit.
- **Gray colors dither on B&W.** `GColorDarkGray` and `GColorLightGray` render as a checkerboard on aplite/diorite, which makes any white text on top unreadable. Always wrap in `PBL_IF_COLOR_ELSE(gray, GColorBlack)` (or similar) for fills behind text.
- **App-store screenshots are NOT in the `.pbw`.** Sideloading a build never carries screenshots. The `package.json` `pebble.screenshots` array is a legacy field; modern submission is via `pebble publish` or upload at dev-portal.rebble.io.
- **waf `find_or_declare` resolves into `build/`, not the source tree.** When generating a file the JS bundler needs to pick up at `src/pkjs/**/*.js`, write directly to `os.path.join(ctx.path.abspath(), 'src', 'pkjs', '<name>.js')` instead.
- **Proxy whitelist is restrictive on purpose.** Whenever you add a new GTI endpoint call, change a request body shape, or start consuming a new response field on the JS side, the proxy will silently strip it — the feature will work in BYO mode and break in proxy mode (confusingly). **Tell the user**: the proxy repo at `/home/ave/Projects/pebble-hvv-proxy/` needs the new endpoint added to its route table or the new field added to `whitelistRequest` / `whitelistResponse` in `src/whitelist.ts`, and the proxy needs to be redeployed before the watch app change ships.

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
pebble install --emulator basalt   # Install to emulator (aplite, basalt, diorite, emery, gabbro)
pebble install --phone <IP>        # Install to phone
pebble logs                        # View app logs (run right after install to not miss logs)
pebble screenshot --emulator basalt  # Take screenshot (saves to cwd, delete after viewing)

# Emulator button control (buttons: up / down / select / back)
pebble emu-button --emulator basalt click up                              # Single press
pebble emu-button --emulator basalt click --repeat 3 --interval 100 down  # Multi press
```

When testing via emulator: build, install, then take a screenshot to verify visuals. Run `pebble logs` immediately after install to capture JS logs. Screenshots save to the working directory — delete them after viewing.

The build system uses waf (`wscript`). C sources are globbed from `src/c/**/*.c`, JS from `src/pkjs/**/*.js`.

## Architecture

### C side (watch)
- `src/c/main.c` — App lifecycle, 30s refresh timer, orchestration
- `src/c/windows/departure_window.c/.h` — ScrollLayer-based departure list UI; on touch watches, tapping a row opens its route
- `src/c/windows/route_window.c/.h` — Touch watches only: a departure's route (stops, times, delays, vehicle position worked out from expected times), refreshed every minute. Selecting a stop shows that stop's departures in place of the departure list (Back then goes to the station list)
- `src/c/modules/data.c/.h` — Departure data model (TransitType enum, Departure struct, persistent storage)
- `src/c/modules/comm.c/.h` — AppMessage handling (receive departures, send requests to JS)
- `src/c/modules/icons.c/.h` — Programmatic transit type icon drawing (no bitmap resources)
- `src/c/modules/course.c/.h` — Stops of the shown route (`PBL_TOUCH` only, up to 64)
- `src/c/modules/round.c/.h` — `round_inset()` for fitting rows to round screens
- `src/c/modules/settings.c/.h` — Watch-side settings persisted in Storage: bold departure text (emery, gabbro), direction arrows (default on), touch navigation (default on) and arrival vibration (touch watches, default off), sent from Clay as `CONFIG_BOLD_TEXT` / `CONFIG_DIRECTION_ARROWS` / `CONFIG_TOUCH_NAV` / `CONFIG_ARRIVAL_VIBE`

### JS side (phone)
- `src/pkjs/index.js` — Clay config init, AppMessage bridge, demo data, response parsing. Keeps the departure list it last sent so a `REQUEST_COURSE` (station, row index, line) maps back to a trip
- `src/pkjs/course.js` — Pure route helpers: GTI dateTime building/parsing with Hamburg's UTC offset, `departureCourse` response to stop list, demo route
- `src/pkjs/api.js` — Single `request(endpoint, body, cb)` entry point. Picks one of three adapters via `pickAdapter()`:
  - **gti** (BYO): HMAC-signs and calls `gti.geofox.de` directly when both `gti_user` and `gti_password` are in localStorage
  - **proxy** (default for shipped builds): calls the Cloudflare Worker at `PROXY_API_BASE` with `Authorization: Bearer <PROXY_SECRET>` + `X-Watch-Token: <token>` headers, when build-time config is present and `Pebble.getWatchToken()` returns a valid token
  - **demo**: short-circuits to local fixtures (`DEMO_NEARBY`, `DEMO_DEPARTURES`)
- `src/pkjs/clay_config.js` — Clay configuration page (favorites, BYO credentials in advanced section, watch token display)
- `src/pkjs/build_config.js` — Generated by `wscript` at build time from `PROXY_API_BASE` / `PROXY_SECRET` env vars (gitignored). Empty strings = demo mode.
- `src/pkjs/hmac.js` — HMAC-SHA1 + Base64 for the BYO path

### Config & build
- `package.json` — Pebble app manifest (UUID, platforms, message keys, Clay dependency). Clay is `@rebble/clay`, Rebble's maintained fork; the original `pebble-clay` (last released 2022) has no gabbro/flint binaries and fails the build
- `wscript` — waf build configuration

### Data flow
1. Watch sends `REQUEST_DEPARTURES` (carrying the station name) via AppMessage when the departure window appears and every 30s after
2. JS receives request and dispatches via `api.request(...)` to one of the three adapter modes (see above)
3. JS sends departure data back via AppMessage (DEP_COUNT, DEP_LINE[0..9], DEP_TYPE[0..9], etc.)
4. Watch parses message, updates data model, refreshes MenuLayer

### Data path

Default for the published `.pbw` is **proxy** — requests go to the Cloudflare Worker at `pebble-hvv-api.ave.zone`, which signs with the maintainer's HVV credentials and forwards to `gti.geofox.de`. **BYO mode** signs HMAC-SHA1 directly from the phone using user-entered credentials. **Demo mode** never makes an HTTP request. The watch C side and the JS response parser are mode-agnostic — the proxy returns GTI-shape JSON (just whitelisted to a subset of fields).

Used response fields: `results[].name/type/distance/serviceTypes` (from `checkName`); `departures[].line.name/type.{shortInfo,longInfo}/direction`, `direction`, `directionId`, `timeOffset`, `delay`, `cancelled`, plus `serviceId`, `line.id/dlid`, `station.id/combinedName` and the top-level `time` to identify a trip (from `departureList`, requested with `version: 63`); `courseElements[].fromStation/toStation.{name,combinedName,id}` (`combinedName`, e.g. "Ahrensburg, Rosenhof", looks a stop up again: bare names are ambiguous outside Hamburg), `depTime`, `arrTime`, `depDelay`, `arrDelay`, `fromCancelled`, `toCancelled` (from `departureCourse`, where times are planned and delays are in seconds on top).

## Conventions

- C functions use `prv_` prefix for private/static functions
- Static globals use `s_` prefix
- Message keys for C/JS communication are declared in `package.json` under `pebble.messageKeys`
- Transit types: BUS=0, SBAHN=1, UBAHN=2, FERRY=3, UNKNOWN=4 (shared between C enum and JS constants)
- Icons are drawn programmatically (no bitmap resources) using HVV departure board shapes/colors
