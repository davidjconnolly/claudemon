// ui_session.h — subscription view: 5h session + 7d weekly windows with resets,
// plus the model-scoped weekly window (e.g. "Fable") and plan label when the
// plan has them. Mirrors the claude.ai usage page semantics.
#pragma once
#include <climits>
#include <ctype.h>
#include "ui_common.h"
#include "globals.h"

namespace page_session {

static unsigned long tick = 0;
// Cached view + field values; only repaint what actually changed (no full-page
// clear), so the screen doesn't flicker every second.
static int    lastView = -1;
static int    lSessPct = -1, lWeekPct = -1, lScopPct = -1;
static long   lSessSig = LONG_MIN, lWeekSig = LONG_MIN, lScopSig = LONG_MIN;
static long   lFoot = LONG_MIN;
static int    lastTint = -1;                              // offline_ind::state() the bars are tinted for
static char   lastPlan[16] = {0};                         // plan label drawn in the header
static char   drawnScoped[12] = {0};                      // scoped title drawn for this view

// "Sun 11:00 PM" for an absolute reset epoch (static, doesn't drift with the clock).
inline String absReset(long epoch) {
  if (epoch <= 0) return "—";
  time_t r = epoch;
  struct tm lt; localtime_r(&r, &lt);
  char buf[24];
  strftime(buf, sizeof(buf), "%a %I:%M %p", &lt);
  return String(buf);
}

// Repaint only the changed parts of one window (pct+bar share a value; reset line
// is independent, keyed by `resetSig`). Each field clears just its own small box.
// `compact` tightens the row so three windows fit above the footer.
inline void updateWindow(int y, int& lastPct, long& lastSig, int pct, long resetSig,
                         const String& resetText, bool compact) {
  int barY = y + (compact ? 18 : 20), barH = compact ? 14 : 16;
  int rstY = y + (compact ? 34 : 40);
  if (pct != lastPct) {
    lastPct = pct;
    char p[8]; snprintf(p, sizeof(p), "%d%%", pct);
    tft.fillRect(248, y, SCREEN_W - 248, 16, COL_BG);            // pct value box
    ui::label(p, SCREEN_W - 6, y, offline_ind::tintColor(barColor(pct)), 2, TR_DATUM);
    ui::drawBarH(6, barY, SCREEN_W - 12, barH, pct, offline_ind::tintColor(barColor(pct)));
  }
  if (resetSig != lastSig) {
    lastSig = resetSig;
    tft.fillRect(6, rstY, SCREEN_W - 12, 9, COL_BG);             // reset line box
    ui::label(resetText.c_str(), 6, rstY, COL_DIM, 1);
  }
}

inline void run() {
  if (modeChanged) {
    tft.fillScreen(COL_BG);
    ui::drawHeader("USAGE");
    modeChanged = false; tick = 0; lastView = -1; lastPlan[0] = 0;
  }
  ui::drawStatus();
  if (!ui::every(tick, 500)) return;        // check ~2x/s; only repaint what changed

  SessionUsage& u = g_data.sub;

  // Plan label ("MAX 5x") right of the page title; arrives async from the poller.
  if (strncmp(lastPlan, u.plan, sizeof(lastPlan)) != 0) {
    snprintf(lastPlan, sizeof(lastPlan), "%s", u.plan);
    tft.fillRect(88, 2, 110, 14, COL_BG);
    if (u.plan[0]) ui::label(u.plan, 88, 6, COL_DIM, 1);
  }

  bool three = u.valid && u.hasScoped;      // 3-window layout when a scoped bar exists
  int view = !g_data.subEnabled ? 0 : !u.valid ? (u.authError ? 2 : 1) : (three ? 4 : 3);

  // The scoped window's title is data-driven; force a relayout if it changes.
  if (three && strncmp(drawnScoped, u.scopedLabel, sizeof(drawnScoped)) != 0) lastView = -1;

  if (view != lastView) {                   // entering a new view: clear once, draw statics
    lastView = view;
    tft.fillRect(0, ui::HEADER_H + 1, SCREEN_W, NAV_Y - ui::HEADER_H - 1, COL_BG);
    lSessPct = lWeekPct = lScopPct = -1;
    lSessSig = lWeekSig = lScopSig = LONG_MIN; lFoot = LONG_MIN;
    if (view == 0) { ui::noData("Session view needs", "an OAuth token (settings)"); return; }
    if (view == 1) { ui::noData("reading session...", nullptr); return; }
    if (view == 2) {
      ui::label("OAUTH TOKEN EXPIRED", CENTER_X, CENTER_Y - 22, COL_WARN, 2, MC_DATUM);
      ui::label("saved subscription token was rejected", CENTER_X, CENTER_Y + 4, COL_DIM, 1, MC_DATUM);
      ui::label("reconfigure at claudemon.local/config", CENTER_X, CENTER_Y + 20, COL_ACCENT, 1, MC_DATUM);
      return;
    }
    if (view == 3) {                                             // static window titles
      ui::label("SESSION", 6, 30, COL_ACCENT, 2);
      ui::label("WEEKLY", 6, 108, COL_ACCENT2, 2);
    } else {
      char t[12];                                                // scoped title, uppercased
      for (size_t i = 0; i < sizeof(t); i++) { t[i] = toupper((unsigned char)u.scopedLabel[i]); if (!t[i]) break; }
      t[sizeof(t) - 1] = 0;
      snprintf(drawnScoped, sizeof(drawnScoped), "%s", u.scopedLabel);
      ui::label("SESSION", 6, 28, COL_ACCENT, 2);
      ui::label("WEEKLY", 6, 88, COL_ACCENT2, 2);
      ui::label(t, 6, 148, COL_ACCENT2, 2);
    }
  }
  if (view != 3 && view != 4) return;       // static screens already drawn

  // The pct labels + bars are tinted by data-freshness (grey until ONLINE). Force
  // a repaint when that state flips, otherwise a bar drawn grey at boot (before the
  // first successful poll registered) stays grey until the next pct change.
  int tint = (int)offline_ind::state();
  if (tint != lastTint) { lastTint = tint; lSessPct = lWeekPct = lScopPct = -1; }

  int yS = three ? 28 : 30, yW = three ? 88 : 108, footY = three ? 194 : 170;
  updateWindow(yS, lSessPct, lSessSig, u.sessionPct, ui::secsTo(u.sessionResetAt) / 60,
               "resets in " + fmtDuration(ui::secsTo(u.sessionResetAt)), three);
  updateWindow(yW, lWeekPct, lWeekSig, u.weeklyPct, u.weeklyResetAt,
               "resets " + absReset(u.weeklyResetAt), three);
  if (three)
    updateWindow(148, lScopPct, lScopSig, u.scopedPct, u.scopedResetAt,
                 "resets " + absReset(u.scopedResetAt), true);

  // footer: expired banner > rate-limited > headroom. While a 5h session is
  // meaningfully open (>1% used, so our own tiny poll doesn't count as "in
  // session") show session headroom — that's what bites first; else show
  // whichever weekly window (all-models or scoped) is closest to its limit.
  bool showWeekly = (u.sessionPct <= 1);
  int  wkPct = u.weeklyPct; const char* wkName = "weekly"; bool scopedWins = false;
  if (u.hasScoped && u.scopedPct > wkPct) { wkPct = u.scopedPct; wkName = u.scopedLabel; scopedWins = true; }
  int  headroom = 100 - (showWeekly ? wkPct : u.sessionPct);
  long foot = u.authError ? -3 : u.limited ? -2
            : (long)(showWeekly ? (scopedWins ? 2000 : 1000) : 0) + headroom;
  if (foot != lFoot) {
    lFoot = foot;
    tft.fillRect(0, footY, SCREEN_W, NAV_Y - footY, COL_BG);
    if (u.authError) {
      if (three) {                          // compact footer: two size-1 lines
        ui::label("OAUTH TOKEN EXPIRED - data stale", CENTER_X, footY + 8, COL_WARN, 1, MC_DATUM);
        ui::label("reconfigure at claudemon.local/config", CENTER_X, footY + 20, COL_ACCENT, 1, MC_DATUM);
      } else {
        ui::label("OAUTH TOKEN EXPIRED", CENTER_X, 174, COL_WARN, 2, MC_DATUM);
        ui::label("data stale - reconfigure tokens at", CENTER_X, 196, COL_DIM, 1, MC_DATUM);
        ui::label("claudemon.local/config", CENTER_X, 208, COL_ACCENT, 1, MC_DATUM);
      }
    } else if (u.limited) {
      ui::label("RATE LIMITED", CENTER_X, three ? footY + 12 : 190, COL_WARN, 2, MC_DATUM);
    } else {
      char b[40];
      snprintf(b, sizeof(b), "%d%% left (%s)", headroom, showWeekly ? wkName : "session");
      ui::label(b, CENTER_X, three ? footY + 12 : 192, headroom <= 15 ? COL_WARN : COL_GOOD, 2, MC_DATUM);
    }
  }
}

} // namespace page_session
