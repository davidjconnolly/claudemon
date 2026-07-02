#pragma once
#include <TFT_eSPI.h>
#include <Preferences.h>
#include "data_models.h"

// --- Page identifiers. The active set is built at boot from which creds
//     exist, so the device only shows pages it can actually populate. ---
enum PageId {
  PAGE_SESSION = 0,  // subscription 5h/7d windows  (needs OAuth token)
  PAGE_COST,         // $ + tokens today/week/month (needs admin key)
  PAGE_MODELS,       // per-model breakdown          (needs admin key)
  PAGE_SPARK,        // 24h / 7d trend sparkline     (needs admin key)
  PAGE_CLOCK,        // digital clock                (always)
  PAGE_SYSTEM,       // device info                  (always)
  PAGE_SETTINGS,     // on-device settings           (always; skipped by auto-cycle)
  PAGE__COUNT
};

extern TFT_eSPI tft;
extern Preferences prefs;

// Active page list, built in setup().
extern uint8_t pages[PAGE__COUNT];
extern int     pageCount;
extern int     currentPage;        // index into pages[]
extern unsigned long modeTimer;
extern bool    modeChanged;

inline PageId activePage() { return (PageId)pages[currentPage]; }

// Landscape orientation: ROT_LANDSCAPE (1) or ROT_LANDSCAPE_F (3).
extern uint8_t displayRotation;

// App data, owned/refreshed by the poller.
extern AppData g_data;

// Runtime diagnostics for the web /status page (cyd build; harmless elsewhere).
extern Diag g_diag;

// Credentials + config (loaded from NVS / captive portal).
extern String oauthToken;        // subscription access token (optional)
extern String oauthRefresh;      // subscription refresh token (optional)
extern int64_t oauthExpiresMs;   // access-token expiry, ms since epoch
extern String adminKey;          // admin API key sk-ant-admin01... (optional)
extern double budgetWeeklyUsd;   // budget alert threshold in USD (0 = off)
extern uint16_t pageMask;        // bit (1<<PageId) set = that optional page is shown
extern bool  otaAuto;            // auto-install newer firmware (boot + ~6h check); default off

// True when budget alerts should fire (computed from cost vs budget).
bool budgetExceeded();

// (Re)builds the active page list from available creds + pageMask.
void buildPages();

unsigned long animNow();
