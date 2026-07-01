// ui_spark.h — trend page: 24h token sparkline + 7d cost sparkline.
#pragma once
#include "ui_common.h"
#include "globals.h"

namespace page_spark {

static unsigned long tick = 0;
// Sparkline buffers change only on a poll; redraw only when they actually differ.
static int      lastView = -1;
static uint16_t lH[24] = {0}, lD[7] = {0};

inline void run() {
  if (modeChanged) {
    tft.fillScreen(COL_BG);
    ui::drawHeader("TREND");
    modeChanged = false; tick = 0; lastView = -1;
  }
  ui::drawStatus();
  if (!ui::every(tick, 500)) return;

  Sparkline& s = g_data.spark;
  int view = !g_data.adminEnabled ? 0 : !s.valid ? 1 : 2;

  bool changed = (view != lastView) ||
                 (view == 2 && (memcmp(lH, s.hourly, sizeof lH) || memcmp(lD, s.daily, sizeof lD)));
  if (!changed) return;
  lastView = view;
  tft.fillRect(0, ui::HEADER_H + 1, SCREEN_W, NAV_Y - ui::HEADER_H - 1, COL_BG);
  if (view == 0) { ui::noData("Trend view needs", "an admin key (settings)"); return; }
  if (view == 1) { ui::noData("reading trend...", nullptr); return; }

  memcpy(lH, s.hourly, sizeof lH); memcpy(lD, s.daily, sizeof lD);

  uint32_t hMax = 0; for (int i = 0; i < 24; i++) if (s.hourly[i] > hMax) hMax = s.hourly[i];
  uint32_t dMax = 0; for (int i = 0; i < 7;  i++) if (s.daily[i]  > dMax) dMax = s.daily[i];

  ui::label("24h tokens", 6, 26, COL_ACCENT, 1);
  ui::label((fmtTokens((uint64_t)hMax * 1000) + " peak/h").c_str(), SCREEN_W - 6, 26, COL_DIM, 1, TR_DATUM);
  ui::drawSparkline(6, 40, SCREEN_W - 12, 64, s.hourly, 24, offline_ind::tintColor(COL_ACCENT));

  ui::label("7d cost", 6, 120, COL_ACCENT2, 1);
  ui::label((fmtUsd(dMax / 100.0) + " peak/d").c_str(), SCREEN_W - 6, 120, COL_DIM, 1, TR_DATUM);
  ui::drawSparkline(6, 134, SCREEN_W - 12, 64, s.daily, 7, offline_ind::tintColor(COL_ACCENT2));
}

} // namespace page_spark
