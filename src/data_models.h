#pragma once
#include <stdint.h>
#include <Arduino.h>

// ---------------------------------------------------------------------------
// Data model shared between the data sources (subscription + admin) and the UI.
// Two independent sources fill two independent structs; each page renders
// whichever it needs and shows a "no data" hint when its source is absent.
// ---------------------------------------------------------------------------

// Subscription rate-limit windows (from the unified 5h/7d rate-limit headers,
// read via the OAuth token). This is the "claude.ai usage page" view.
struct SessionUsage {
  bool  valid          = false;  // we have at least one good reading
  int   sessionPct     = 0;      // 0..100 utilization of the 5h session window
  int   weeklyPct      = 0;      // 0..100 utilization of the 7d weekly window
  long  sessionResetAt  = 0;     // unix epoch (s) the 5h session window resets (0=unknown)
  long  weeklyResetAt   = 0;     // unix epoch (s) the 7d weekly window resets  (0=unknown)
  bool  limited        = false;  // status == "limited"
  bool  authError      = false;  // last probe got 401/403 even after a forced refresh
  long  lastUpdated    = 0;      // millis() of last good reading
};

// One model's slice of cost/usage (admin Usage & Cost API, group_by=model).
struct ModelCost {
  char     name[24] = {0};
  double   costUsd  = 0;
  uint64_t tokens   = 0;
};

// Console org token + cost accounting (from the Usage & Cost API, admin key).
struct CostUsage {
  bool     valid        = false;
  double   costTodayUsd = 0;
  double   costWeekUsd  = 0;
  double   costMonthUsd = 0;
  uint64_t tokensToday  = 0;
  uint64_t tokensWeek   = 0;
  uint64_t inputToday   = 0;
  uint64_t outputToday  = 0;
  uint64_t cacheReadToday   = 0;
  uint64_t cacheCreateToday = 0;
  int      cacheHitPct  = 0;     // cacheRead / (cacheRead + uncachedInput) * 100
  long     periodEndSec = 0;     // seconds until end of billing month
  ModelCost models[6];
  int      modelCount   = 0;
  long     lastUpdated  = 0;
};

// Trend buffers for the sparkline page (values in US cents to fit uint16_t).
struct Sparkline {
  bool     valid       = false;
  uint16_t hourly[24]  = {0};    // last 24 hours, 1h buckets
  uint16_t daily[7]    = {0};    // last 7 days, 1d buckets
};

// Everything the UI reads. One global instance, owned by the poller.
struct AppData {
  SessionUsage sub;
  CostUsage    cost;
  Sparkline    spark;
  bool subEnabled   = false;     // OAuth token configured -> session page live
  bool adminEnabled = false;     // admin key configured  -> cost pages live
};

// Lightweight runtime diagnostics, surfaced on the web /status page so failures
// (e.g. an admin 401) can be read over WiFi instead of needing a serial cable.
struct Diag {
  int           adminLastCode  = 0;     // last admin HTTP status (0=none, <0=conn err)
  char          adminLastErr[96] = {0}; // snippet of the last admin error body
  unsigned long adminLastOkMs  = 0;     // millis() of last admin 200 (0=never)
  unsigned long adminLastTryMs = 0;     // millis() of last admin attempt
  int           subLastCode    = 0;     // last subscription probe status
  unsigned long subLastOkMs    = 0;     // millis() of last good sub reading
  int           otaCheckCode   = 0;     // last GitHub releases check status
  char          otaMsg[48]     = {0};   // "up to date" / "vX available" / error
};
