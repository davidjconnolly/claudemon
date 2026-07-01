// ui_common.h — shared landscape drawing helpers (320x240).
#pragma once
#include <TFT_eSPI.h>
#include <time.h>
#include "config.h"
#include "theme.h"
#include "globals.h"
#include "offline_ind.h"

namespace ui {

static constexpr int HEADER_H   = 20;
static constexpr int CONTENT_Y  = HEADER_H + 4;     // 24
static constexpr int CONTENT_B  = NAV_Y - 2;        // bottom of content

inline bool every(unsigned long& last, unsigned long ms) {
  if (millis() - last >= ms || last == 0) { last = millis(); return true; }
  return false;
}

// Live countdown (seconds) to an absolute reset epoch; <0 if unknown. Computed at
// render time so it ticks down smoothly between polls instead of going stale.
inline long secsTo(long epoch) { return epoch > 0 ? epoch - (long)time(nullptr) : -1; }

// Top-right status dot: green online, yellow stale, red offline. A single static
// 6px square — no blinking glyph (detailed health lives on the web /status page).
// Sits left of the gear/close icon.
inline void drawStatus() {
  static offline_ind::State drawn = (offline_ind::State)255;
  offline_ind::State s = offline_ind::state();
  if (s != drawn) {
    drawn = s;
    uint16_t c = (s == offline_ind::ONLINE) ? COL_GOOD
               : (s == offline_ind::STALE)  ? COL_ACCENT : COL_WARN;
    tft.fillRect(SCREEN_W - 34, 6, 6, 6, c);
  }
}

// Top-right tap target shared by the gear (open settings) and X (close settings).
static constexpr int CHROME_ZONE_X = SCREEN_W - 24;   // hit when tx >= this
static constexpr int CHROME_ZONE_Y = 22;              //        and ty <= this

// Gear icon (rects only, so the host renderer draws it too) — "open settings".
// A ~16px 8-tooth cog with a hollow centre so it reads clearly as a gear.
inline void drawGear() {
  int gx = SCREEN_W - 13, gy = 10; uint16_t c = COL_ACCENT;
  tft.fillRect(gx - 5, gy - 5, 10, 10, c);      // body
  tft.fillRect(gx - 2, gy - 2, 4, 4, COL_BG);   // hole
  tft.fillRect(gx - 2, gy - 8, 4, 3, c);        // N tooth
  tft.fillRect(gx - 2, gy + 5, 4, 3, c);        // S
  tft.fillRect(gx - 8, gy - 2, 3, 4, c);        // W
  tft.fillRect(gx + 5, gy - 2, 3, 4, c);        // E
  tft.fillRect(gx - 7, gy - 7, 3, 3, c);        // NW
  tft.fillRect(gx + 4, gy - 7, 3, 3, c);        // NE
  tft.fillRect(gx - 7, gy + 4, 3, 3, c);        // SW
  tft.fillRect(gx + 4, gy + 4, 3, 3, c);        // SE
}

// X icon — "close settings".
inline void drawClose() {
  int cx = SCREEN_W - 13, cy = 10; uint16_t c = COL_ACCENT;
  for (int i = -5; i <= 5; i++) { tft.fillRect(cx + i, cy + i, 2, 2, c); tft.fillRect(cx + i, cy - i, 2, 2, c); }
}

inline void drawHeader(const char* title) {
  tft.fillRect(0, 0, SCREEN_W, HEADER_H, COL_BG);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextSize(2);
  tft.drawString(title, 6, 2, 1);
  tft.setTextSize(1);
  tft.drawFastHLine(0, HEADER_H, SCREEN_W, COL_TRACK);
}

// Horizontal progress bar with track + optional right-side label.
inline void drawBarH(int x, int y, int w, int h, int pct, uint16_t fill) {
  if (pct < 0) pct = 0; if (pct > 100) pct = 100;
  int fw = (pct * w) / 100;
  tft.fillRect(x, y, w, h, COL_TRACK);
  if (fw > 0) tft.fillRect(x, y, fw, h, fill);
}

// Bar-graph sparkline scaled to its own max.
inline void drawSparkline(int x, int y, int w, int h, const uint16_t* v, int n, uint16_t color) {
  uint32_t mx = 1;
  for (int i = 0; i < n; i++) if (v[i] > mx) mx = v[i];
  tft.fillRect(x, y, w, h, COL_BG);
  int bw = w / n; if (bw < 1) bw = 1;
  for (int i = 0; i < n; i++) {
    int bh = (int)((uint32_t)v[i] * h / mx);
    if (bh < 1 && v[i] > 0) bh = 1;
    tft.fillRect(x + i * bw, y + h - bh, (bw > 1 ? bw - 1 : 1), bh, color);
  }
  tft.drawFastHLine(x, y + h, w, COL_TRACK);
}

// Right-aligned label helper.
inline void label(const char* s, int x, int y, uint16_t fg, uint8_t size = 1, uint8_t datum = TL_DATUM) {
  tft.setTextDatum(datum);
  tft.setTextColor(fg, COL_BG);
  tft.setTextSize(size);
  tft.drawString(s, x, y, 1);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
}

// "no data" centered hint for pages whose source isn't configured.
inline void noData(const char* line1, const char* line2) {
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setTextSize(1);
  tft.drawString(line1, CENTER_X, CENTER_Y - 6, 2);
  if (line2) tft.drawString(line2, CENTER_X, CENTER_Y + 12, 1);
  tft.setTextDatum(TL_DATUM);
}

} // namespace ui
