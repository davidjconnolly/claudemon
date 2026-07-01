```
 ██████ ██    ██ ██████      ██████ ██       █████  ██    ██ ██████  ███████ ███    ███  ██████  ███    ██
██       ██  ██  ██   ██    ██      ██      ██   ██ ██    ██ ██   ██ ██      ████  ████ ██    ██ ████   ██
██        ████   ██   ██    ██      ██      ███████ ██    ██ ██   ██ █████   ██ ████ ██ ██    ██ ██ ██  ██
██         ██    ██   ██    ██      ██      ██   ██ ██    ██ ██   ██ ██      ██  ██  ██ ██    ██ ██  ██ ██
 ██████    ██    ██████      ██████ ███████ ██   ██  ██████  ██████  ███████ ██      ██  ██████  ██   ████
```

**Landscape Claude usage & cost monitor for the ESP32-2432S028R (Cheap Yellow Display, 2.8").**

A daemon-free reimagining of [ohmyclawd](https://github.com/opariffazman/ohmyclawd): the CYD talks to the
Anthropic API **directly** — no host-side companion process. It renders in **landscape (320×240)** and
pulls from up to two sources, showing only the pages it can populate:

| Source | Auth | Pages |
|---|---|---|
| **Console Usage & Cost Admin API** | Admin key `sk-ant-admin01…` | `COST` · `MODELS` · `TREND` |
| **Subscription rate-limit windows** | Claude Code OAuth token | `USAGE` (5h session + 7d weekly + resets) |

Configure **either or both**. Add an admin key later and the cost pages light up automatically — no reflash.

---

## ⚠️ Read this first — which data you actually get

The two sources measure **different things** and don't overlap:

- **Usage & Cost API** ([docs](https://platform.claude.com/docs/en/manage-claude/usage-cost-api)) needs an
  **Admin key**, available only to **Console / Platform organizations** ("unavailable for individual accounts").
  It reports **tokens and $ cost** over time — **not** rate-limit "session %". Crucially, it only sees usage
  **billed to the org** (API keys / SDK / Claude Code run with `ANTHROPIC_API_KEY`). It does **not** see
  **Max-subscription** Claude Code usage.
- **Subscription windows** ("Session 68%, resets in 2h" / "Weekly 41%, resets Sun 11pm") come from the
  **unified rate-limit response headers**, readable only with the **subscription OAuth token**. This path is
  **undocumented** and may break. See [How the subscription path works](#how-the-subscription-path-works).

So: if your day-to-day Claude Code runs on a **Max subscription**, the `USAGE` page is what reflects it; the
`COST`/`MODELS`/`TREND` pages stay empty until you have API-key usage billed to a Console org.

---

## Features

- **Landscape dashboard** — `USAGE`, `COST`, `MODELS`, `TREND`, `CLOCK`, `SYSTEM`, plus a gear-opened `SETTINGS` overlay
- **Daemon-free** — direct HTTPS to `api.anthropic.com`; no host-side companion process
- **Progressive enhancement** — pages appear per configured credential
- **Reliable-only signals** — session/weekly **headroom** ("36% left"), month-cost **projection**, `RATE LIMITED` / `OVER BUDGET` / `OAUTH EXPIRED` — no "is Claude typing right now" guessing
- **Sparklines + per-model breakdown** — 24h token & 7d cost trends; cost/tokens by model (rolling 7 days)
- **Budget alert** — `OVER BUDGET` when weekly org spend crosses your threshold
- **Flicker-free** — per-field change detection (only redraws what actually changed)
- **Web config + status server** — `claudemon.local`: `/status`, editable `/config` and `/settings`, no reflash
- **OTA self-update, captive-portal setup, quiet hours, auto-cycle, brightness / orientation**
- **`FAKE_DATA` + Wokwi + host renderer** — verify every screen before flashing

## Hardware

| | |
|---|---|
| Board | ESP32-2432S028R (CYD 2.8") |
| Display | 2.8" ILI9341 320×240 TFT (driven landscape) |
| Touch | XPT2046 resistive |
| Connectivity | WiFi 2.4 GHz |

---

## Quick start

### 0. Install the toolchain

```bash
# PlatformIO (build/flash)
pip install -U platformio

# Wokwi: VS Code "Wokwi Simulator" extension, and/or the CLI for headless runs
curl -L https://wokwi.com/ci/install.sh | sh    # installs wokwi-cli
```

### 1. Verify in the emulator (no hardware, no credentials)

The `emulator` env builds with `-DFAKE_DATA`: scripted usage/cost curves, **no network calls, no keys**.
Touch isn't simulated by Wokwi's display part, so FAKE builds **auto-cycle** through every page.

```bash
pio run -e emulator                       # builds .pio/build/emulator/firmware.{bin,elf}
```

> **For layout, prefer the host renderer** — [`tools/render/build.sh`](tools/render/)
> draws every page to a pixel-accurate PNG instantly, no emulator needed. The Wokwi
> *VS Code extension* currently mis-renders landscape (a bundled-engine bug); use the
> **cloud/web** sim below for a faithful live view.

Then either:

- **VS Code:** open the folder, install the **Wokwi Simulator** extension, press **F1 → "Wokwi: Start Simulation"**
  (it reads `wokwi.toml` + `diagram.json`). ⚠️ Its bundled engine mis-renders the panel
  in landscape today — fine for logic, not for judging layout.
- **CLI / web (faithful engine):** the cloud sim renders correctly. For a headless screenshot:
  ```bash
  # --screenshot-time is required; --timeout must be ≥ it
  wokwi-cli --timeout 7000 --screenshot-time 6000 --timeout-exit-code 0 \
            --screenshot-part lcd --screenshot-file screen.png .
  ```

You should see the landscape dashboard cycling: `USAGE` → `COST` → `MODELS` → `TREND` → `CLOCK` → `SYSTEM`.
(`diagram.json` wires `wokwi-ili9341` to the CYD's SPI pins; Wokwi's public gateway gives the sim internet, so the clock syncs via NTP.)

> First Wokwi-CLI run needs a free token: `export WOKWI_CLI_TOKEN=…` from <https://wokwi.com/dashboard/ci>.

### 1b. Run the *real* firmware in Wokwi (live data over simulated WiFi)

The **`wokwi`** env is the real firmware — real Anthropic API **and** the web config/status server —
running in the simulator over Wokwi's virtual WiFi, so you can test live data **without flashing**.
(The `emulator` env above is offline/scripted; `wokwi` is live.)

> ⚠️ **This needs the Wokwi *Private* IoT Gateway, which is a paid feature (Wokwi Club).** The free
> Community License only has the **Public** gateway: flaky TLS (HTTPS to the API errors out) and
> **outbound-only** (so `http://localhost:9080` / `/config` is unreachable). On the free tier, prefer the
> **`emulator` build above for clickable UI testing** (scripted data, no gateway needed) and the **real
> device at `claudemon.local/config`** for real data (also no reflash). The live `wokwi` build below is
> only worthwhile with Wokwi Club.

```bash
pio run -e wokwi
```

Then in **VS Code** (with the Wokwi extension):

1. **F1 → "Enable Private Wokwi IoT Gateway"** — routes the sim's Internet through *your* network, so
   HTTPS to the API is reliable **and your keys never touch Wokwi's public cloud gateway**.
2. **F1 → "Wokwi: Start Simulation"**.
3. Open **<http://localhost:9080/>** — the gateway forwards port `9080` → the device's port `80`, so the
   web UI is right there. Go to **`/config`**, paste your `sk-ant-admin01-…` key, **Save**. Credentials
   persist across the sim's *soft* reboots (re-enter after a full stop/start).
4. Watch **`/status`** — `COST/MODELS/TREND` populate from the real API. The poller retries every 60 s, so
   an occasional Wokwi TLS hiccup self-heals.

**Navigating the display:** Wokwi's panel has **no touchscreen**, so the on-screen `<` `>` arrows can't be
tapped — that's a simulator limitation, not a bug. Use the **Prev / Next** buttons in the `/status` page's
**Display** section to change pages (works on real hardware too). Since you drive everything from the
browser, the extension's cosmetic display offset doesn't matter here.

> HTTPS in Wokwi works but is finicky on the **public** gateway (you may see `start_ssl_client` errors); the
> **Private Gateway** above is the reliable path. If it still fights you, the real device at
> `claudemon.local/config` tests live data with no reflash either.

### 2. Flash real hardware

```bash
pio run -e cyd -t upload          # set upload_port in platformio.ini if not /dev/ttyUSB0
pio device monitor                # 115200 baud
```

(Or grab `claudemon-firmware.bin` from Releases and `esptool.py write_flash 0x10000 claudemon-firmware.bin`.)

### 3. Configure on first boot

The CYD starts a WiFi AP **`CYD-Claudemon`**. Connect from your phone, the captive portal opens
(or browse `192.168.4.1`), pick your WiFi, then fill in **only what you have** (add the admin key
later and the cost pages light up — no reflash):

| Field | Powers | Where to get it |
|---|---|---|
| **OAuth access token** | `USAGE` | Claude Code subscription token — [see below](#oauth-access--refresh-tokens-usage-page) |
| **OAuth refresh token** | `USAGE` | same source — lets the device renew the access token itself |
| **Admin API key** | `COST` · `MODELS` · `TREND` | Console **Admin key** `sk-ant-admin01…` — [see below](#admin-api-key-cost--models--trend) |
| **Timezone** | clock / resets | POSIX `TZ` string — [see table below](#timezone-posix-tz) |

#### OAuth access + refresh tokens (`USAGE` page)

Your Claude Code OAuth credentials live in **one of two places** on macOS depending on version — try the
file first, then the Keychain. Run in a terminal and copy the two printed values into the portal:

```bash
# A) credentials file
python3 -c "import json,os;o=json.load(open(os.path.expanduser('~/.claude/.credentials.json')))['claudeAiOauth'];print('ACCESS :',o['accessToken']);print('REFRESH:',o['refreshToken'])"

# B) macOS Keychain (newer Claude Code) — or open Keychain Access.app and search 'Claude'
security find-generic-password -s "Claude Code-credentials" -w | python3 -c "import json,sys;o=json.load(sys.stdin)['claudeAiOauth'];print('ACCESS :',o['accessToken']);print('REFRESH:',o['refreshToken'])"
```

Access token looks like `sk-ant-oat01-…`, refresh like `sk-ant-ort01-…`. The access token expires every few
hours; the refresh token lets the device renew it on its own (that's why you enter both). Treat them as your
Claude identity — the device uses them for the (undocumented) subscription rate-limit probe.

#### Admin API key (`COST` / `MODELS` / `TREND`)

An **Organization Admin key** (`sk-ant-admin01-…`) — a **distinct** key from your normal `sk-ant-api03-…`
keys, only creatable by an **org member with the admin role**. It's what the
[Admin API](https://platform.claude.com/docs/en/manage-claude/admin-api) /
[Usage & Cost API](https://platform.claude.com/docs/en/manage-claude/usage-cost-api) require.

1. Sign in to the Claude Console (<https://platform.claude.com>) as an **org admin**.
2. Go straight to **Settings → Admin keys** — i.e. <https://platform.claude.com/settings/admin-keys>.
   ⚠️ This page is **separate from the normal "API keys" page and may not appear in the sidebar** — open the
   URL directly. (See [Create an Admin API key](https://platform.claude.com/docs/en/manage-claude/admin-api-keys).)
3. **Create key** → name it → copy the `sk-ant-admin01-…` secret immediately (shown once) → paste into the
   device. It lives in NVS / is entered over an open AP, so treat it as sensitive.

**Individual / personal account?** The Admin API is **unavailable** — `…/settings/admin-keys` returns
*Page not found*, by design. To enable it you must convert to a real **Organization** (Console →
**Settings → Organization**, add a member). Until then `COST/MODELS/TREND` simply can't populate — and
that's fine: `USAGE` (your subscription) still works. A key rejected with `invalid x-api-key` on the device's
web `/status` is just a non-admin `sk-ant-api03-…` key, which can't work here. (You can tick **remove this
key** on the web `/config` page to clear it and hide the cost pages.)

#### Timezone (POSIX `TZ`)

Use a [POSIX `TZ`](https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html) string (the device
has no zoneinfo DB, so an `America/Toronto`-style name won't work):

| Region | `TZ` |
|---|---|
| **Toronto / Eastern** | `EST5EDT,M3.2.0,M11.1.0` |
| Chicago / Central | `CST6CDT,M3.2.0,M11.1.0` |
| Denver / Mountain | `MST7MDT,M3.2.0,M11.1.0` |
| Los Angeles / Pacific | `PST8PDT,M3.2.0,M11.1.0` |
| London | `GMT0BST,M3.5.0/1,M10.5.0` |
| UTC (no DST) | `UTC0` |

Save → reboots and connects. Settings persist in NVS. To clear WiFi + credentials, use **RESET** (hold 3s) in
the Settings overlay, or the web `/config` page. (There is intentionally no global hold-to-wipe gesture.)

---

## Pages

| Page | Needs | Shows |
|---|---|---|
| `USAGE` | OAuth | Session % + "resets in 2h 14m"; Weekly % + "resets Sun 11:00 PM"; headroom ("36% left") / `RATE LIMITED` / `OAUTH EXPIRED` |
| `COST` | Admin | $ + tokens today / week / month + month **projection**; cache-hit %; budget bar or month-end countdown |
| `MODELS` | Admin | Per-model cost bars + tokens (rolling 7 days) |
| `TREND` | Admin | 24h token sparkline + 7d cost sparkline |
| `CLOCK` | — | Big landscape clock + second bar |
| `SYSTEM` | — | Chip / heap used / flash used / WiFi / IP / uptime / configured sources |

Navigate: **tap the left / right half** of the screen (or swipe) to change page; the nav-bar `<` `>` work too.
`SETTINGS` is a **separate screen** — tap the **gear** (top-right) to open it, the **✕** to close. Auto-cycle
rotates the content pages.

### Settings (gear → overlay; hold `*` rows 0.5s then drag; tap **SAVE**; ✕ to close)

Also editable from a browser at **`claudemon.local/settings`**.

| Row | Input | What |
|---|---|---|
| BRIGHTNESS * | drag | 10–100% |
| QUIET START/END * | drag | Quiet-hours window (cross-midnight OK) |
| QUIET MODE | tap | OFF / DIM / SLEEP |
| AUTO-CYCLE * | drag | 0 (off) – 250 s |
| ORIENTATION | tap | NORMAL (rot 1) / FLIPPED (rot 3) |
| SHOW CLOCK / SYSTEM | tap | Toggle those pages |
| RESET | hold 3s | Wipe WiFi + creds |
| SAVE | tap | Persist |

(The weekly **budget** and credentials / timezone live on `claudemon.local/config`, not here.)

---

## How it works

### Cost / Models / Trend (documented, reliable)

The poller calls the Usage & Cost Admin API and writes into the shared model:
- `usage_report/messages` (1d) for today's tokens + cache split,
- `usage_report/messages` (1h ×24) for the token sparkline,
- `usage_report/messages` (1d ×7) for weekly tokens,
- `cost_report` (1d, grouped by description) for $ today/week/month, per-model (rolling 7d), and the daily cost sparkline.

These endpoints are **paginated** (~7 buckets/page); the fetchers follow `next_page` (with `limit=31`) so a full
month isn't silently truncated — reading only the first page was why COST once showed `$0`. Calls are staggered
(one "today" call + one rotating heavier call per minute, with a 429 backoff) to respect the ~1/min guidance.
Cost amounts are **cents** per the docs (`"123.45"` → $1.23), verified against a live response.

### How the subscription path works

There's no documented read for the 5h/7d windows, so [`src/data_sub.h`](src/data_sub.h) tries strategies
cheapest-first and remembers the first that returns the unified headers:

1. **`count_tokens` probe** — free, separate limit; *may* return the headers without opening a session.
2. **`messages` probe** — costs ~1 token and opens/refreshes a 5h session (the proven fallback).

**The session caveat:** reading the windows requires a request, and a `messages` probe not already inside a
live session **opens one** (the free `count_tokens` probe doesn't). The device probes on a fixed **60 s**
cadence; each probe is ~1 token or free, so the cost is negligible. On a 401 it **force-refreshes** the OAuth
token and retries; if that still fails (the token is shared with your Claude Code, which rotates it) it shows
an `OAUTH TOKEN EXPIRED` banner on the device and web `/status` rather than silently showing stale data. Token
refresh runs on-device using the refresh token (see [`src/net_oauth.h`](src/net_oauth.h)).

> The `count_tokens`-returns-headers idea and a future zero-cost "usage endpoint" (the one the web usage page
> uses) are **spikes to confirm on real hardware** — if either works, the session is read for free and the
> probe is never needed.

---

## Security notes

- The **Admin key** lives in device NVS and is entered over an **open** setup AP. Prefer a **read-only/usage-scoped**
  admin key. Treat the device as holding a sensitive credential.
- TLS uses `setInsecure()` (no cert pinning) — same as the upstream OTA path. Fine for a hobby monitor; pin the
  root CA if you care.
- The subscription OAuth path is undocumented and uses a public client id — see [`src/config.h`](src/config.h).

## Project layout

```
src/
  config.h theme.h globals.h data_models.h     core + shared state
  net_http.h net_oauth.h                        TLS helper + on-device OAuth refresh
  data_admin.h data_sub.h fake_data.h poller.h  data sources + polling orchestration
  ui_*.h                                         landscape pages + nav + common helpers
  web_config.h                                  LAN status + editable config/settings server
  display_pm.h offline_ind.h                    power mgmt + offline state (adapted from ohmyclawd)
  secrets.h.example                             local-dev credentials template (copy to gitignored secrets.h)
  main.cpp                                        setup / captive portal / loop / OTA / touch
tools/render/                                    host renderer — every page to a PNG, no hardware
tools/probe.py                                   host API probe (reads secrets.h; never prints keys)
diagram.json  wokwi.toml                          Wokwi emulator config
platformio.ini                                    envs: cyd (hardware), emulator (FAKE_DATA), wokwi (live)
```

## Relationship to ohmyclawd

This began as a landscape, **daemon-free** reimagining of
[**ohmyclawd**](https://github.com/opariffazman/ohmyclawd) by **opariffazman** (MIT), and owes it the original
idea and product framing. It reuses ohmyclawd's power-management, offline-indicator and settings-slider code
plus that overall concept; the daemon-free direct-API data layer, the Usage & Cost API pages, the landscape
UI, and the web config/status server are new here (and the pixel mascot was removed).

## Disclaimer

Unofficial; not affiliated with Anthropic. Relies on documented (Usage & Cost) and **undocumented**
(rate-limit header) APIs that may change without notice. Credentials stored on-device; use at your own risk.

## License

[MIT](LICENSE) © David Connolly. Portions adapted from
[ohmyclawd](https://github.com/opariffazman/ohmyclawd) © opariffazman (MIT); the original copyright is preserved
in [`LICENSE`](LICENSE).
