// ui_models.h — per-model cost/token breakdown (admin Usage & Cost API).
#pragma once
#include "ui_common.h"
#include "globals.h"

namespace page_models {

static unsigned long tick = 0;
// The model breakdown only changes on a poll (minutes apart), so redraw the list
// only when it actually changes — never on a fixed timer.
static int  lastView = -1, lCount = -1;
static long lCents[6] = {0};

inline void run() {
  if (modeChanged) {
    tft.fillScreen(COL_BG);
    ui::drawHeader("MODELS");
    modeChanged = false; tick = 0; lastView = -1;
  }
  ui::drawStatus();
  if (!ui::every(tick, 500)) return;

  CostUsage& c = g_data.cost;
  int view = !g_data.adminEnabled ? 0 : !c.valid ? 1 : (c.modelCount == 0 ? 2 : 3);

  bool changed = (view != lastView);
  if (view == 3) {
    if (c.modelCount != lCount) changed = true;
    for (int i = 0; i < c.modelCount && i < 6; i++)
      if ((long)(c.models[i].costUsd * 100 + 0.5) != lCents[i]) changed = true;
  }
  if (!changed) return;
  lastView = view;
  tft.fillRect(0, ui::HEADER_H + 1, SCREEN_W, NAV_Y - ui::HEADER_H - 1, COL_BG);
  if (view == 0) { ui::noData("Model view needs", "an admin key (settings)"); return; }
  if (view == 1) { ui::noData("reading models...", nullptr); return; }
  if (view == 2) { ui::noData("no model usage", "in the last 7 days"); return; }

  lCount = c.modelCount;
  double mx = 0.0001;
  for (int i = 0; i < c.modelCount; i++) if (c.models[i].costUsd > mx) mx = c.models[i].costUsd;

  int y = 26;
  for (int i = 0; i < c.modelCount && y < ui::CONTENT_B - 24; i++) {
    lCents[i] = (long)(c.models[i].costUsd * 100 + 0.5);
    ui::label(c.models[i].name, 6, y, COL_ACCENT, 1);
    ui::label(fmtUsd(c.models[i].costUsd).c_str(), SCREEN_W - 6, y, offline_ind::tintColor(COL_ACCENT), 1, TR_DATUM);
    int pct = (int)(c.models[i].costUsd * 100 / mx);
    ui::drawBarH(6, y + 11, SCREEN_W - 12, 8, pct, offline_ind::tintColor(COL_ACCENT2));
    ui::label((fmtTokens(c.models[i].tokens) + " tok").c_str(), 6, y + 21, COL_DIM, 1);
    y += 32;
  }
}

} // namespace page_models
