#pragma once
#include <TFT_eSPI.h>

// Semantic palette (RGB565). Orange/cyan/grey on black, matching the CYD look.
#define COL_BG      TFT_BLACK
#define COL_ACCENT  TFT_ORANGE
#define COL_ACCENT2 TFT_CYAN
#define COL_DIM     TFT_DARKGREY
#define COL_TEXT    TFT_WHITE
#define COL_WARN    TFT_RED
#define COL_GOOD    0x07E0      // green
#define COL_TRACK   0x2104      // empty-bar track (very dark grey)
#define COL_DARKRED 0x6800

// Bar fill colour by utilization: green < 50 < orange < 80 < red.
inline uint16_t barColor(int pct) {
  if (pct >= 80) return COL_WARN;
  if (pct >= 50) return COL_ACCENT;
  return COL_GOOD;
}

// Compact USD formatting: "$4.82", "$38.1", "$1.2k".
inline String fmtUsd(double v) {
  char buf[16];
  if (v >= 1000.0)      snprintf(buf, sizeof(buf), "$%.1fk", v / 1000.0);
  else if (v >= 100.0)  snprintf(buf, sizeof(buf), "$%.0f", v);
  else                  snprintf(buf, sizeof(buf), "$%.2f", v);
  return String(buf);
}

// Compact token counts: "942", "1.2k", "9.4M", "1.1B".
inline String fmtTokens(uint64_t t) {
  char buf[16];
  if (t >= 1000000000ULL) snprintf(buf, sizeof(buf), "%.1fB", t / 1e9);
  else if (t >= 1000000ULL) snprintf(buf, sizeof(buf), "%.1fM", t / 1e6);
  else if (t >= 1000ULL)    snprintf(buf, sizeof(buf), "%.1fk", t / 1e3);
  else                      snprintf(buf, sizeof(buf), "%llu", (unsigned long long)t);
  return String(buf);
}

// "2h 14m", "47m", "3d 4h" from seconds.
inline String fmtDuration(long sec) {
  if (sec <= 0) return "now";
  long d = sec / 86400, h = (sec % 86400) / 3600, m = (sec % 3600) / 60;
  char buf[20];
  if (d > 0)      snprintf(buf, sizeof(buf), "%ldd %ldh", d, h);
  else if (h > 0) snprintf(buf, sizeof(buf), "%ldh %02ldm", h, m);
  else            snprintf(buf, sizeof(buf), "%ldm", m);
  return String(buf);
}
