// fake_data.h — scripted usage/cost curves for FAKE_DATA (Wokwi) builds.
// No network, no credentials: lets every page be verified in the emulator.
#pragma once
#include <math.h>
#include <time.h>
#include "data_models.h"
#include "config.h"

namespace fake {

inline void fill(AppData& d) {
  d.subEnabled   = true;
  d.adminEnabled = true;
  unsigned long t = millis() / 1000;

  // --- Subscription windows: session sawtooths 0..92% over 5 min then resets;
  //     weekly climbs slowly. "active" while the session is mid-ramp. ---
  int phase = t % 300;
  d.sub.valid          = true;
  d.sub.sessionPct     = (phase * 92) / 300;
  d.sub.weeklyPct      = 38 + (int)((t / 45) % 24);
  d.sub.sessionResetAt = (long)time(nullptr) + (300 - phase);
  d.sub.weeklyResetAt  = (long)time(nullptr) + (3 * 86400 + 4 * 3600 + (300 - phase));
  d.sub.limited        = d.sub.sessionPct >= 90;
  d.sub.lastUpdated    = millis();

  // --- Console cost/tokens ---
  double wk = 28.0 + 6.0 * sin(t / 90.0) + (t % 600) / 50.0;
  d.cost.valid        = true;
  d.cost.costTodayUsd = 3.2 + 2.0 * fabs(sin(t / 60.0));
  d.cost.costWeekUsd  = wk;
  d.cost.costMonthUsd = wk * 3.4;
  d.cost.tokensToday  = 900000ULL + (uint64_t)(400000.0 * fabs(sin(t / 50.0)));
  d.cost.tokensWeek   = 9400000ULL;
  d.cost.inputToday   = d.cost.tokensToday * 35 / 100;
  d.cost.outputToday  = d.cost.tokensToday * 20 / 100;
  d.cost.cacheReadToday   = d.cost.tokensToday * 40 / 100;
  d.cost.cacheCreateToday = d.cost.tokensToday * 5 / 100;
  d.cost.cacheHitPct  = 71;
  d.cost.periodEndSec = (long)(11 * 86400 + 6 * 3600);
  d.cost.lastUpdated  = millis();

  static const char* names[4] = {"opus-4-8", "sonnet-4-6", "haiku-4-5", "fable-5"};
  double shares[4] = {0.62, 0.27, 0.07, 0.04};
  d.cost.modelCount = 4;
  for (int i = 0; i < 4; i++) {
    strncpy(d.cost.models[i].name, names[i], sizeof(d.cost.models[i].name) - 1);
    d.cost.models[i].costUsd = d.cost.costTodayUsd * shares[i];
    d.cost.models[i].tokens  = (uint64_t)(d.cost.tokensToday * shares[i]);
  }

  // --- Sparklines ---
  d.spark.valid = true;
  for (int i = 0; i < 24; i++)
    d.spark.hourly[i] = (uint16_t)(30 + 70 * fabs(sin((i + t / 30.0) / 3.0)));
  for (int i = 0; i < 7; i++)
    d.spark.daily[i] = (uint16_t)(280 + 180 * fabs(sin((i + t / 120.0) / 2.0)));
}

} // namespace fake
