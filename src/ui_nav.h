// ui_nav.h — bottom navigation bar: page dots + < > tap zones.
#pragma once
#include <TFT_eSPI.h>
#include "config.h"
#include "theme.h"
#include "globals.h"

namespace nav {

inline void draw() {
  tft.fillRect(0, NAV_Y, SCREEN_W, NAV_H, COL_BG);
  tft.drawFastHLine(0, NAV_Y, SCREEN_W, COL_TRACK);

  const int size = 6, gap = 6;
  int w = pageCount * size + (pageCount - 1) * gap;
  int x = (SCREEN_W - w) / 2;
  int y = NAV_Y + (NAV_H - size) / 2;
  for (int i = 0; i < pageCount; i++) {
    int bx = x + i * (size + gap);
    if (i == currentPage) tft.fillRect(bx, y, size, size, COL_ACCENT);
    else                  tft.drawRect(bx, y, size, size, COL_ACCENT);
  }
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.drawString("<", NAV_LEFT_X + NAV_BTN_W / 2, NAV_Y + NAV_H / 2, 2);
  tft.drawString(">", NAV_RIGHT_X + NAV_BTN_W / 2, NAV_Y + NAV_H / 2, 2);
  tft.setTextDatum(TL_DATUM);
}

// -1 = no nav hit, 0 = prev (<), 1 = next (>)
inline int hitTest(int tx, int ty) {
  if (ty < NAV_Y) return -1;
  if (tx < NAV_LEFT_X + NAV_BTN_W) return 0;
  if (tx >= NAV_RIGHT_X)           return 1;
  return -1;
}

} // namespace nav
