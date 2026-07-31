// poller.h — owns refresh timing for both sources and writes into g_data.
//
// Fixed cadences: the subscription is probed every POLL_SUB_MS and the admin API
// every POLL_ADMIN_MS (one "today" call + one rotating heavier call per tick, with
// a 429 backoff). There is no "are you working right now" inference — a polled
// rate-limit header can't tell reading from typing, so we don't pretend to.
//
// Due work is QUEUED, and tick() drains at most ONE item per loop() iteration.
// Every request blocks the UI task, and handleTouch() only samples the panel at
// the top of loop(), so a tap that starts and ends inside a blocking request is
// dropped entirely. Draining all three items back-to-back made that dead window
// three timeouts long — the 15-30 s "frozen stylus"; one per iteration lets
// touch run in between. (The subscription item is one request in the healthy
// case; it still chains internally when it has to fail over between strategies.)
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

// Work queued by the cadence check, drained one HTTP call per tick().
static constexpr uint8_t WORK_ADMIN_TODAY = 1 << 0;
static constexpr uint8_t WORK_ADMIN_SLOW  = 1 << 1;
static constexpr uint8_t WORK_SUB         = 1 << 2;
static uint8_t queued = 0;

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

  // --- Queue whatever is due. Admin (Usage & Cost API, ~1/min recommended)
  //     schedules the "today" call plus ONE rotating heavier call — never a
  //     burst of all of them — and backs off for a few minutes on a 429. ---
  if (g_data.adminEnabled && millis() >= adminBackoffUntil &&
      (firstAdmin || millis() - tAdmin >= POLL_ADMIN_MS)) {
    tAdmin = millis();
    firstAdmin = false;
    queued |= WORK_ADMIN_TODAY | WORK_ADMIN_SLOW;
  }
  if (subDue()) {
    tSub = millis();
    firstSub = false;
    queued |= WORK_SUB;
  }

  // Sample the touch gate EVERY tick, not just when there is work to hold back:
  // it tracks press/release transitions, so it has to see the release. Calling
  // it only under `queued` let its internal clock go stale across an idle gap —
  // after the stuck-latch fallback fired, a later genuine tap would be measured
  // against the old timestamp, read as already-expired, and get no grace window
  // at all. Sampling unconditionally is a volatile-bool read; it costs nothing.
  bool busy = touchBusy();

  // Hold the queue while the screen is being touched. A poll started under the
  // stylus swallows the whole gesture; the data is minutes-stale anyway, so
  // deferring it by a few loop iterations costs nothing and keeps taps live.
  if (queued && !busy) {
    bool anyOk = false;
    if (queued & WORK_ADMIN_TODAY) {                 // --- exactly one call ---
      queued &= ~WORK_ADMIN_TODAY;
      if (admin::fetchToday(g_data.cost)) anyOk = true;
      if (g_diag.adminLastCode == 429) { adminBackoffUntil = millis() + ADMIN_BACKOFF_MS; queued &= ~WORK_ADMIN_SLOW; }
    } else if (queued & WORK_ADMIN_SLOW) {
      queued &= ~WORK_ADMIN_SLOW;
      bool got = (slowPhase == 0) ? admin::fetchCostMonth(g_data.cost, g_data.spark)
               : (slowPhase == 1) ? admin::fetchHourlySpark(g_data.spark)
                                  : admin::fetchWeekTokens(g_data.cost);
      if (got) anyOk = true;
      slowPhase = (slowPhase + 1) % 3;
      if (g_diag.adminLastCode == 429) adminBackoffUntil = millis() + ADMIN_BACKOFF_MS;
    } else if (queued & WORK_SUB) {
      // One reading can need several requests (upgrade retry -> known-good ->
      // rediscovery). pollStep() does one and reports whether more remain; keep
      // the item queued so the chain resumes on the NEXT loop iteration. Same
      // wall-clock as the old back-to-back chain, but touch runs in between.
      bool more = false;
      if (sub::pollStep(g_data.sub, more)) { g_diag.subLastOkMs = millis(); anyOk = true; }
      if (!more) queued &= ~WORK_SUB;
    }
    if (anyOk) offline_ind::recordSuccess();
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
      // Only 401/403 is actually an OAuth problem. Other non-2xx codes are
      // transient 5xx / -11 network blips, now logged with precise per-strategy
      // context by data_sub's miss line; don't double-log them here (and the
      // "check OAuth token" hint would be misleading), keeping the small ring
      // buffer legible for diagnosis.
      else if (g_diag.subLastCode == 401 || g_diag.subLastCode == 403)
        applog::add("usage: HTTP %d (check OAuth token)", g_diag.subLastCode);
    }
  }
}
#endif

} // namespace poller

// Budget alert: weekly spend has crossed the configured threshold.
inline bool budgetExceeded() {
  return budgetWeeklyUsd > 0 && g_data.cost.valid && g_data.cost.costWeekUsd >= budgetWeeklyUsd;
}
