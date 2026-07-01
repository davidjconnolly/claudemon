// ui_cost.h — Console view: $ + tokens (today / week / month), cache, budget.
#pragma once
#include "ui_common.h"
#include "globals.h"

namespace page_cost {

static unsigned long tick = 0;
// Cached view + per-field values; repaint only the boxes that change.
static int      lastView = -1;
static long     lToday = -1, lWeek = -1, lMonth = -1;
static uint64_t lTokToday = ~0ULL, lTokWeek = ~0ULL, lTokDummy = 0;
static int      lCacheHit = -1, lBudgetMode = -1;
static long     lBWeek = -1, lBBudget = -1, lEndMin = -1, lProj = -1;
static uint8_t  lFoot = 255;

// One TODAY/WEEK/MONTH row: $ on the left (size 2), token count on the right.
// Each sub-field clears only its own box and only when its value changes.
inline void statField(int y, long& lUsd, uint64_t& lTok, double usd, uint64_t tok, bool showTok) {
  long cents = (long)(usd * 100 + 0.5);
  if (cents != lUsd) {
    lUsd = cents;
    tft.fillRect(6, y + 9, 150, 16, COL_BG);
    ui::label(fmtUsd(usd).c_str(), 6, y + 9, offline_ind::tintColor(COL_ACCENT), 2);
  }
  if (showTok && tok != lTok) {
    lTok = tok;
    tft.fillRect(180, y + 9, SCREEN_W - 180, 12, COL_BG);
    if (tok > 0) ui::label((fmtTokens(tok) + " tok").c_str(), SCREEN_W - 6, y + 13, COL_DIM, 1, TR_DATUM);
  }
}

inline void run() {
  if (modeChanged) {
    tft.fillScreen(COL_BG);
    ui::drawHeader("COST");
    modeChanged = false; tick = 0; lastView = -1;
  }
  ui::drawStatus();
  if (!ui::every(tick, 500)) return;

  int view = !g_data.adminEnabled ? 0 : !g_data.cost.valid ? 1 : 2;
  if (view != lastView) {
    lastView = view;
    tft.fillRect(0, ui::HEADER_H + 1, SCREEN_W, NAV_Y - ui::HEADER_H - 1, COL_BG);
    lToday = lWeek = lMonth = -1; lTokToday = lTokWeek = ~0ULL;
    lCacheHit = lBudgetMode = -1; lBWeek = lBBudget = lEndMin = lProj = -1; lFoot = 255;
    if (view == 0) { ui::noData("Cost view needs", "an admin key (settings)"); return; }
    if (view == 1) { ui::noData("reading cost...", nullptr); return; }
    ui::label("TODAY", 6, 26, COL_ACCENT2, 1);
    ui::label("THIS WEEK", 6, 62, COL_ACCENT2, 1);
    ui::label("THIS MONTH", 6, 98, COL_ACCENT2, 1);
    ui::label("cache hit", 6, 138, COL_DIM, 1);
  }
  if (view != 2) return;

  CostUsage& c = g_data.cost;
  statField(26, lToday, lTokToday, c.costTodayUsd, c.tokensToday, true);
  statField(62, lWeek,  lTokWeek,  c.costWeekUsd,  c.tokensWeek,  true);
  statField(98, lMonth, lTokDummy, c.costMonthUsd, 0, false);   // month token total not fetched

  // projected full-month spend = month-to-date / day-of-month * days-in-month (UTC,
  // matching the cost API's month window). Shown on the right of the MONTH row.
  {
    time_t now = time(nullptr); struct tm g; gmtime_r(&now, &g);
    static const int dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int dm = dim[g.tm_mon], yr = g.tm_year + 1900;
    if (g.tm_mon == 1 && (yr % 4 == 0 && (yr % 100 != 0 || yr % 400 == 0))) dm = 29;
    // Skip the projection on day 1 — extrapolating a full month from one day is
    // meaningless (it just reads today x 31).
    bool showProj = (g.tm_mday >= 2);
    double proj = c.costMonthUsd * dm / (g.tm_mday > 0 ? g.tm_mday : 1);
    long projCents = showProj ? (long)(proj * 100 + 0.5) : -2;
    if (projCents != lProj) {
      lProj = projCents;
      tft.fillRect(180, 107, SCREEN_W - 180, 12, COL_BG);
      if (showProj) ui::label(("proj " + fmtUsd(proj)).c_str(), SCREEN_W - 6, 111, COL_DIM, 1, TR_DATUM);
    }
  }

  if (c.cacheHitPct != lCacheHit) {
    lCacheHit = c.cacheHitPct;
    ui::drawBarH(70, 137, 150, 8, c.cacheHitPct, offline_ind::tintColor(COL_GOOD));
    char cb[8]; snprintf(cb, sizeof(cb), "%d%%", c.cacheHitPct);
    tft.fillRect(264, 138, SCREEN_W - 6 - 264, 8, COL_BG);
    ui::label(cb, SCREEN_W - 6, 138, COL_DIM, 1, TR_DATUM);
  }

  // budget bar OR month-end countdown
  int  mode = (budgetWeeklyUsd > 0) ? 1 : 0;
  long bw = (long)(c.costWeekUsd * 100), bb = (long)(budgetWeeklyUsd * 100), em = c.periodEndSec / 60;
  if (mode != lBudgetMode || (mode ? (bw != lBWeek || bb != lBBudget) : (em != lEndMin))) {
    lBudgetMode = mode; lBWeek = bw; lBBudget = bb; lEndMin = em;
    tft.fillRect(0, 156, SCREEN_W, 26, COL_BG);
    if (mode) {
      int pct = (int)(c.costWeekUsd * 100 / budgetWeeklyUsd);
      ui::label("budget", 6, 160, COL_DIM, 1);
      ui::drawBarH(70, 159, 150, 8, pct, barColor(pct));
      ui::label((fmtUsd(c.costWeekUsd) + "/" + fmtUsd(budgetWeeklyUsd)).c_str(), SCREEN_W - 6, 160, barColor(pct), 1, TR_DATUM);
    } else {
      ui::label(("billing month ends in " + fmtDuration(c.periodEndSec)).c_str(), 6, 160, COL_DIM, 1);
    }
  }

  // footer: only the reliable over-budget alert (no activity guessing)
  uint8_t foot = budgetExceeded() ? 1 : 0;
  if (foot != lFoot) {
    lFoot = foot;
    tft.fillRect(0, 188, SCREEN_W, 16, COL_BG);
    if (foot == 1) ui::label("OVER BUDGET", CENTER_X, 190, COL_WARN, 2, MC_DATUM);
  }
}

} // namespace page_cost
