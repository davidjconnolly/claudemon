#pragma once
#include <Arduino.h>

// Build version (set in platformio.ini; falls back for ad-hoc builds).
#ifndef CLAUDEMON_VERSION
#define CLAUDEMON_VERSION "dev"
#endif

// NVS namespace for all persisted settings/creds.
#define NVS_NS "claudemon"

// --- XPT2046 touch SPI wiring on the CYD (separate bus from the TFT) ---
#define TOUCH_IRQ  36
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK  25
#define TOUCH_CS   33

// --- Landscape geometry (panel is native 240x320; we rotate to 320x240) ---
static constexpr int      SCREEN_W   = 320;
static constexpr int      SCREEN_H   = 240;
static constexpr int      CENTER_X   = SCREEN_W / 2;   // 160
static constexpr int      CENTER_Y   = SCREEN_H / 2;   // 120

// Two landscape rotations: 1 = USB on the right, 3 = USB on the left.
static constexpr uint8_t  ROT_LANDSCAPE   = 1;
static constexpr uint8_t  ROT_LANDSCAPE_F = 3;

// --- Bottom navigation bar ---
static constexpr int NAV_H        = 18;
static constexpr int NAV_Y        = SCREEN_H - NAV_H;  // 222
static constexpr int NAV_BTN_W    = 26;
static constexpr int NAV_LEFT_X   = 0;
static constexpr int NAV_RIGHT_X  = SCREEN_W - NAV_BTN_W;

// --- Touch calibration (raw ADC -> pixels). Tweak per panel if needed. ---
static constexpr int TOUCH_RAW_MIN = 300;
static constexpr int TOUCH_RAW_MAX = 3900;

// --- Polling cadence (ms) ---
static constexpr unsigned long POLL_ADMIN_MS = 60000;  // Usage&Cost API: data ~5min stale, <=1/min
static constexpr unsigned long POLL_SUB_MS   = 60000;  // subscription rate-limit probe, fixed 60s.
                                                       // Each probe is ~1 token (or a free count_tokens),
                                                       // so a tight cadence keeps the % bars fresh cheaply.

// --- Anthropic endpoints ---
#define ANTHROPIC_HOST        "api.anthropic.com"
#define ANTHROPIC_VERSION_HDR "2023-06-01"
#define USAGE_REPORT_PATH     "/v1/organizations/usage_report/messages"
#define COST_REPORT_PATH      "/v1/organizations/cost_report"
#define COUNT_TOKENS_PATH     "/v1/messages/count_tokens"
#define MESSAGES_PATH         "/v1/messages"
#define OAUTH_TOKEN_URL       "https://console.anthropic.com/v1/oauth/token"
// Claude Code's public OAuth client id (UNDOCUMENTED — verify against your own
// ~/.claude/.credentials.json refresh flow before trusting on-device refresh).
#define CLAUDE_CODE_CLIENT_ID "9d1c250a-e61b-44d9-88ed-5944d1962f5e"

// GitHub repo for OTA (update to your fork before first release).
#define OTA_REPO "davidjconnolly/claudemon"
