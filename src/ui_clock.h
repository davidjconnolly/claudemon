// ui_clock.h — big landscape digital clock + second-progress bar.
#pragma once
#include "ui_common.h"
#include "time.h"

namespace page_clock {

static int lsec = -1;

inline void run() {
  if (modeChanged) { tft.fillScreen(COL_BG); ui::drawHeader("CLOCK"); modeChanged = false; lsec = -1; }
  ui::drawStatus();

  struct tm ti;
  if (!getLocalTime(&ti)) { if (lsec != -2) { ui::noData("waiting for time...", nullptr); lsec = -2; } return; }
  if (ti.tm_sec == lsec) return;
  lsec = ti.tm_sec;

  char tB[8];
  sprintf(tB, (ti.tm_sec % 2) ? "%02d %02d" : "%02d:%02d", ti.tm_hour, ti.tm_min);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextSize(6);
  tft.drawString(tB, CENTER_X, 92, 1);

  char dy[24], dt[24];
  strftime(dy, sizeof(dy), "%A", &ti);
  strftime(dt, sizeof(dt), "%b %d, %Y", &ti);
  tft.setTextSize(2);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString(dy, CENTER_X, 150, 1);
  tft.drawString(dt, CENTER_X, 176, 1);

  int barW = 240, x = (SCREEN_W - barW) / 2, filled = ti.tm_sec * barW / 60;
  tft.fillRect(x, 200, barW, 6, COL_TRACK);
  tft.fillRect(x, 200, filled, 6, COL_ACCENT);

  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
}

} // namespace page_clock
