// poller.h — owns refresh timing for both sources and writes into g_data.
//
// Fixed cadences: the subscription is probed every POLL_SUB_MS and the admin API
// every POLL_ADMIN_MS (one "today" call + one rotating heavier call per tick, with
// a 429 backoff). There is no "are you working right now" inference — a polled
// rate-limit header can't tell reading from typing, so we don't pretend to.
#pragma once

#include <Arduino.h>
#include "config.h"
#include "globals.h"
#include "offline_ind.h"

#ifdef FAKE_DATA
  #include "fake_data.h"
#else
  #include "applog.h"
  #include "data_admin.h"
  #include "data_sub.h"
#endif

namespace poller {

static unsigned long tAdmin = 0, tSub = 0;
static bool          firstAdmin = true, firstSub = true;
static unsigned long adminBackoffUntil = 0;   // set ahead on a 429 to stop hammering
static int           slowPhase = 0;           // rotates the heavier admin calls
static constexpr unsigned long ADMIN_BACKOFF_MS = 3UL * 60UL * 1000UL;

inline bool timeSynced() { return time(nullptr) > 1700000000; }

inline void init() {
#ifndef FAKE_DATA
  g_data.subEnabled   = !oauthToken.isEmpty();
  g_data.adminEnabled = !adminKey.isEmpty();
#endif
}

#ifdef FAKE_DATA
inline void tick() {
  static unsigned long t = 0;
  if (millis() - t < 1000 && t != 0) return;
  t = millis();
  fake::fill(g_data);
  offline_ind::recordSuccess();
}
#else

// Subscription probe on a fixed cadence.
inline bool subDue() {
  if (!g_data.subEnabled) return false;
  if (firstSub) return true;
  return (millis() - tSub) >= POLL_SUB_MS;
}

inline void tick() {
  if (!timeSynced()) return;
  bool anyOk = false;

  // --- Admin (Usage & Cost API, ~1/min recommended). Per tick do the "today"
  //     call plus ONE rotating heavier call — never a burst of all of them — and
  //     back off for a few minutes if the API returns 429 (rate limited). ---
  if (g_data.adminEnabled && millis() >= adminBackoffUntil &&
      (firstAdmin || millis() - tAdmin >= POLL_ADMIN_MS)) {
    tAdmin = millis();
    if (admin::fetchToday(g_data.cost)) anyOk = true;
    bool got = (slowPhase == 0) ? admin::fetchCostMonth(g_data.cost, g_data.spark)
             : (slowPhase == 1) ? admin::fetchHourlySpark(g_data.spark)
                                : admin::fetchWeekTokens(g_data.cost);
    if (got) anyOk = true;
    slowPhase = (slowPhase + 1) % 3;
    firstAdmin = false;
    if (g_diag.adminLastCode == 429) adminBackoffUntil = millis() + ADMIN_BACKOFF_MS;
  }

  // --- Subscription: fixed-cadence probe. ---
  if (subDue()) {
    tSub = millis();
    firstSub = false;
    if (sub::poll(g_data.sub)) { g_diag.subLastOkMs = millis(); anyOk = true; }
  }

  // Event log: record source state transitions only (not every poll).
  if (g_data.adminEnabled) {
    static int prevAdmin = -999;
    if (g_diag.adminLastCode != prevAdmin) {
      prevAdmin = g_diag.adminLastCode;
      if (g_diag.adminLastCode == 200) applog::add("admin: ok");
      else applog::add("admin: HTTP %d %s", g_diag.adminLastCode, g_diag.adminLastErr);
    }
  }
  if (g_data.subEnabled) {
    static int prevSub = -999;
    if (g_diag.subLastCode != prevSub) {
      prevSub = g_diag.subLastCode;
      if (g_diag.subLastCode == 200 || g_diag.subLastCode == 429) applog::add("usage: ok");
      else if (g_diag.subLastCode != 0) applog::add("usage: HTTP %d (check OAuth token)", g_diag.subLastCode);
    }
  }

  if (anyOk) offline_ind::recordSuccess(); else offline_ind::recordFailure();
}
#endif

} // namespace poller

// Budget alert: weekly spend has crossed the configured threshold.
inline bool budgetExceeded() {
  return budgetWeeklyUsd > 0 && g_data.cost.valid && g_data.cost.costWeekUsd >= budgetWeeklyUsd;
}
