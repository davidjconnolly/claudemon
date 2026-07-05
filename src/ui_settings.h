// ui_settings.h — on-device settings, landscape (320x240).
// Hold-drag sliders ported from ohmyclawd; landscape ORIENTATION (NORMAL = rot 1,
// FLIPPED = rot 3). Credentials + weekly budget are set on the web /config page,
// not here (budget was intentionally removed to avoid two places for it).
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "theme.h"
#include "display_pm.h"
#include "globals.h"   // PageId, pageMask, currentPage/pageCount, buildPages, modeChanged
#ifndef FAKE_DATA
#include "applog.h"    // VIEW LOGS pages through the flash-persisted log
#endif

namespace settings_ui {

static bool          needsFullRedraw  = true;
static bool          rowsDirty        = true;
static unsigned long resetHoldStartMs = 0;
static bool          resetActive      = false;
static int           resetFilledPx    = 0;
static bool          dirty            = false;

static uint8_t pBri, pQhStart, pQhEnd, pQhMode, pCyc, pRot;
static uint16_t pPageMask;

static constexpr int FULLW       = SCREEN_W;
static constexpr int ROW0_Y      = 21;
static constexpr int ROW_H       = 15;
static constexpr int LABEL_X     = 10;
static constexpr int VALUE_X     = SCREEN_W - 10;
static constexpr int RULE_INDENT = 6;
static constexpr int NUM_ROWS    = 11;
static constexpr int ROW_PG_CLK  = 6;   // show CLOCK
static constexpr int ROW_PG_SYS  = 7;   // show SYSTEM
static constexpr int ROW_LOGS    = 8;   // open the log viewer
static constexpr int ROW_RESET   = 9;
static constexpr int ROW_SAVE    = 10;

inline bool pgOn(int pageId)        { return pPageMask & (1 << pageId); }
inline void pgToggle(int pageId)    { pPageMask ^= (1 << pageId); }
inline const char* onOff(bool b)    { return b ? "ON" : "OFF"; }

inline int rowY(int i) { return ROW0_Y + i * ROW_H; }
inline int rowFromY(int y) {
  if (y < ROW0_Y) return -1;
  int i = (y - ROW0_Y) / ROW_H;
  return (i >= NUM_ROWS) ? -1 : i;
}
inline bool isSlider(int r) { return r == 0 || r == 1 || r == 2 || r == 4; }

inline String briLabel()   { return String(pBri) + "%"; }
inline const char* qhmLabel(){ return pQhMode == 0 ? "OFF" : pQhMode == 1 ? "DIM" : "SLEEP"; }
inline String cycLabel()   { return pCyc == 0 ? "OFF" : "< " + String(pCyc) + "s >"; }
inline const char* orientLabel(){ return pRot == ROT_LANDSCAPE ? "NORMAL" : "FLIPPED"; }

inline void drawRow(int i, const char* label, const String& value, bool hl) {
  int y = rowY(i);
  uint16_t bg = hl ? COL_ACCENT : COL_BG;
  tft.fillRect(0, y, FULLW, ROW_H - 1, bg);
  tft.setTextSize(1);
  tft.setTextColor(hl ? COL_BG : COL_DIM, bg);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(label, LABEL_X, y + 5, 1);
  tft.setTextColor(hl ? COL_BG : COL_ACCENT, bg);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(value, VALUE_X, y + 5, 1);
  tft.drawFastHLine(RULE_INDENT, y + ROW_H - 1, FULLW - 2 * RULE_INDENT, COL_TRACK);
  tft.setTextDatum(TL_DATUM);
}

inline void flashRow(int i, const char* label, const String& value) { drawRow(i, label, value, true); delay(120); }

#ifndef FAKE_DATA
// --- log viewer (VIEW LOGS row): pages through the flash-persisted log, all
//     lines since the last rotation. Tap top half = older page, bottom half =
//     newer, X = back to the settings list. ---
static bool logView  = false;
static bool logDirty = false;
static int  logTotal = 0;      // lines in the current log segment
static int  logTop   = 0;      // file line index of the first row shown
static constexpr int LOG_TOP_Y    = 21;
static constexpr int LOG_LINE_H   = 10;
static constexpr int LOG_PER_PAGE = (SCREEN_H - LOG_TOP_Y - 12) / LOG_LINE_H;
static char logPage[LOG_PER_PAGE][applog::LEN];

inline void openLogView() {
  logTotal = applog::fileLines();
  logTop   = logTotal > LOG_PER_PAGE ? logTotal - LOG_PER_PAGE : 0;   // newest page first
  logView  = true; logDirty = true;
}

inline void logViewTap(int ty) {
  int maxTop = logTotal > LOG_PER_PAGE ? logTotal - LOG_PER_PAGE : 0;
  int nt = logTop + (ty < CENTER_Y ? -LOG_PER_PAGE : LOG_PER_PAGE);
  if (nt < 0)      nt = 0;
  if (nt > maxTop) nt = maxTop;
  if (nt != logTop) { logTop = nt; logDirty = true; }
}

inline void renderLogView() {
  if (!logDirty) return;
  logDirty = false;
  tft.fillScreen(COL_BG);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  char hdr[40];
  int last = logTop + LOG_PER_PAGE; if (last > logTotal) last = logTotal;
  snprintf(hdr, sizeof hdr, "LOG  %d-%d / %d", logTotal ? logTop + 1 : 0, last, logTotal);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.drawString(hdr, 6, 6, 1);
  tft.drawFastHLine(0, 18, SCREEN_W, COL_TRACK);

  int n = applog::readFileLines(logTop, LOG_PER_PAGE, logPage);
  char tmp[54];                              // 53 chars fit the 320px width
  tft.setTextColor(COL_TEXT, COL_BG);
  for (int i = 0; i < n; i++) {
    snprintf(tmp, sizeof tmp, "%s", logPage[i]);
    tft.drawString(tmp, 2, LOG_TOP_Y + i * LOG_LINE_H, 1);
  }
  tft.setTextColor(COL_DIM, COL_BG);
  if (n == 0) tft.drawString("log empty", 2, LOG_TOP_Y, 1);
  tft.drawString("tap top: older   tap bottom: newer   X: back", 2, SCREEN_H - 9, 1);
}
#endif // !FAKE_DATA

// X tapped while the settings overlay is open. Returns true if it was consumed
// (log viewer -> back to the list); false means the caller closes settings.
inline bool handleCloseTap() {
#ifndef FAKE_DATA
  if (logView) { logView = false; needsFullRedraw = rowsDirty = true; return true; }
#endif
  return false;
}

// --- sliders ---
static constexpr unsigned long SLIDER_HOLD_MS = 500UL;
static constexpr unsigned long RESET_HOLD_MS  = 3000UL;
static int sliderRow = -1, sliderStartX = -1, sliderLastVal = 0;
static unsigned long sliderHoldStart = 0;
static bool sliderEngaged = false;

inline int sliderMax(int r)  { return r == 0 ? 100 : (r == 1 || r == 2) ? 23 : 250; }
inline int sliderStep(int r) { return r == 4 ? 5 : 1; }
inline int sliderVal(int r)  { return r == 0 ? pBri : r == 1 ? pQhStart : r == 2 ? pQhEnd : pCyc; }
inline void sliderSet(int r, int v) {
  if (r == 0) { if (v < 10) v = 10; pBri = v; display_pm::previewActive = true; display_pm::targetDuty = (v * 255) / 100; }
  else if (r == 1) pQhStart = v;
  else if (r == 2) pQhEnd = v;
  else if (r == 4) pCyc = v;
  dirty = true;
}

inline void drawSliderBar(int r) {
  int y = rowY(r), maxV = sliderMax(r), v = sliderVal(r);
  int filled = maxV ? (v * FULLW) / maxV : 0;
  tft.fillRect(0, y, FULLW, ROW_H - 1, COL_BG);
  tft.fillRect(0, y, filled, ROW_H - 1, COL_ACCENT);
  tft.setTextSize(1);
  tft.setTextColor(COL_BG, COL_ACCENT);
  tft.setTextDatum(TL_DATUM);
  const char* nm = r == 0 ? "BRIGHTNESS" : r == 1 ? "QUIET START" : r == 2 ? "QUIET END" : "AUTO-CYCLE";
  tft.drawString(nm, LABEL_X, y + 5, 1);
  String lab;
  if (r == 0) lab = String(v) + "%";
  else if (r == 4) lab = v == 0 ? "OFF" : String(v) + "s";
  else lab = String(v) + ":00";
  tft.setTextDatum(TR_DATUM);
  tft.drawString(lab, VALUE_X, y + 5, 1);
  tft.drawFastHLine(RULE_INDENT, y + ROW_H - 1, FULLW - 2 * RULE_INDENT, COL_TRACK);
  tft.setTextDatum(TL_DATUM);
}

inline void save();

inline void handleTap(TFT_eSPI&, int ty, int tx) {
#ifndef FAKE_DATA
  if (logView) { logViewTap(ty); return; }
#endif
  int r = rowFromY(ty);
  if (r < 0) return;
  if (r != ROW_RESET) { resetActive = false; resetFilledPx = 0; }
  switch (r) {
    case 3: pQhMode = (pQhMode + 1) % 3; dirty = true; flashRow(3, "QUIET MODE", qhmLabel()); break;
    case 5: pRot = (pRot == ROT_LANDSCAPE) ? ROT_LANDSCAPE_F : ROT_LANDSCAPE; dirty = true; flashRow(5, "ORIENTATION", orientLabel()); break;
    case ROW_PG_CLK: pgToggle(PAGE_CLOCK);  dirty = true; flashRow(ROW_PG_CLK, "SHOW CLOCK",   onOff(pgOn(PAGE_CLOCK))); break;
    case ROW_PG_SYS: pgToggle(PAGE_SYSTEM); dirty = true; flashRow(ROW_PG_SYS, "SHOW SYSTEM",  onOff(pgOn(PAGE_SYSTEM))); break;
#ifndef FAKE_DATA
    case ROW_LOGS: openLogView(); break;
#endif
    case ROW_SAVE: if (dirty) { flashRow(ROW_SAVE, "SAVE", "SAVED!"); save(); } break;
    default: break;  // sliders + reset are hold-driven
  }
  needsFullRedraw = true; rowsDirty = true;
}

inline void handleHoldTick(TFT_eSPI&, int ty, int tx, unsigned long elapsed) {
#ifndef FAKE_DATA
  if (logView) return;   // the viewer is tap-only
#endif
  if (sliderEngaged && sliderRow >= 0) {
    int delta = tx - sliderStartX, maxV = sliderMax(sliderRow), step = sliderStep(sliderRow);
    int vd = (delta * maxV) / (FULLW - 60);
    vd = (vd / step) * step;
    int nv = sliderLastVal + vd;
    if (sliderRow == 1 || sliderRow == 2) nv = ((nv % 24) + 24) % 24;
    else { if (nv < 0) nv = 0; if (nv > maxV) nv = maxV; if (sliderRow == 0 && nv < 10) nv = 10; }
    if (nv != sliderVal(sliderRow)) { sliderSet(sliderRow, nv); drawSliderBar(sliderRow); }
    return;
  }
  if (resetActive) {
    unsigned long held = millis() - resetHoldStartMs; if (held > RESET_HOLD_MS) held = RESET_HOLD_MS;
    int filled = (int)((held * FULLW) / RESET_HOLD_MS);
    if (filled != resetFilledPx) {
      resetFilledPx = filled;
      int y = rowY(ROW_RESET);
      tft.fillRect(0, y, FULLW, ROW_H - 1, COL_BG);
      tft.fillRect(0, y, resetFilledPx, ROW_H - 1, COL_WARN);
      tft.setTextSize(1); tft.setTextColor(COL_TEXT, COL_BG); tft.setTextDatum(TL_DATUM);
      tft.drawString("RESET", LABEL_X, y + 5, 1);
      tft.setTextDatum(TR_DATUM); tft.drawString("hold...", VALUE_X, y + 5, 1);
      tft.setTextDatum(TL_DATUM);
    }
    return;
  }
  int r = rowFromY(ty);
  if (isSlider(r)) {
    if (sliderRow != r) { sliderRow = r; sliderHoldStart = millis(); sliderEngaged = false; }
    else if (!sliderEngaged && millis() - sliderHoldStart >= SLIDER_HOLD_MS) {
      sliderEngaged = true; sliderStartX = tx; sliderLastVal = sliderVal(r); drawSliderBar(r);
    }
  } else if (r == ROW_RESET) {
    resetActive = true; resetHoldStartMs = millis() - elapsed; sliderRow = -1;
  } else sliderRow = -1;
}

inline bool consumeResetIfTriggered() {
  if (resetActive && millis() - resetHoldStartMs >= RESET_HOLD_MS) { resetActive = false; resetFilledPx = 0; return true; }
  return false;
}

inline void cancelHold() {
  if (resetActive || sliderRow >= 0) { resetActive = false; resetFilledPx = 0; sliderRow = -1; sliderEngaged = false; needsFullRedraw = true; rowsDirty = true; }
}

inline void enter() {
  needsFullRedraw = rowsDirty = true; resetActive = false; resetFilledPx = 0; dirty = false;
  pBri = display_pm::briPct; pQhStart = display_pm::qhStart; pQhEnd = display_pm::qhEnd;
  pQhMode = display_pm::qhMode; pCyc = display_pm::cycSec;
  pRot = displayRotation;
  pPageMask = pageMask;
}

inline void exit() {
  resetActive = false; sliderRow = -1; sliderEngaged = false;
#ifndef FAKE_DATA
  logView = false;
#endif
  if (dirty) { display_pm::previewActive = false; dirty = false; }
}

inline void save() {
  display_pm::setBrightness(pBri);
  display_pm::setQuietHours(pQhStart, pQhEnd, pQhMode);
  display_pm::setCycle(pCyc);
  displayRotation = pRot;
  bool pagesChanged = (pageMask != pPageMask);
  pageMask = pPageMask;
  Preferences p; p.begin(NVS_NS, false);
  p.putUChar("rot", displayRotation);
  p.putUShort("pgmask", pageMask);
  p.end();
  tft.setRotation(displayRotation);
  display_pm::previewActive = false;
  dirty = false;
  if (pagesChanged) {            // apply page show/hide immediately (buildPages clamps currentPage)
    buildPages();
    modeChanged = true;
  }
}

inline void render(TFT_eSPI&, bool full) {
#ifndef FAKE_DATA
  if (logView) { if (full) logDirty = true; renderLogView(); return; }
#endif
  if (full || needsFullRedraw) {
    tft.fillScreen(COL_BG);
    tft.setTextSize(2); tft.setTextColor(COL_ACCENT, COL_BG); tft.setTextDatum(TL_DATUM);
    tft.drawString("SETTINGS", 6, 2, 1);
    tft.setTextSize(1); tft.setTextColor(COL_DIM, COL_BG); tft.setTextDatum(TR_DATUM);
    tft.drawString("hold * to adjust", SCREEN_W - 26, 6, 1);   // leave room for the close X
    tft.setTextDatum(TL_DATUM);
    needsFullRedraw = false;
  }
  if (rowsDirty || full) {
    char buf[12];
    drawRow(0, "BRIGHTNESS *", briLabel(), false);
    snprintf(buf, sizeof(buf), "%02d:00", pQhStart); drawRow(1, "QUIET START *", buf, false);
    snprintf(buf, sizeof(buf), "%02d:00", pQhEnd);   drawRow(2, "QUIET END *", buf, false);
    drawRow(3, "QUIET MODE", qhmLabel(), false);
    drawRow(4, "AUTO-CYCLE *", cycLabel(), false);
    drawRow(5, "ORIENTATION", orientLabel(), false);
    drawRow(ROW_PG_CLK, "SHOW CLOCK",   onOff(pgOn(PAGE_CLOCK)),  false);
    drawRow(ROW_PG_SYS, "SHOW SYSTEM",  onOff(pgOn(PAGE_SYSTEM)), false);
    drawRow(ROW_LOGS,   "VIEW LOGS",    ">",                      false);
    if (resetActive) {
      int y = rowY(ROW_RESET);
      tft.fillRect(0, y, FULLW, ROW_H - 1, COL_BG);
      tft.fillRect(0, y, resetFilledPx, ROW_H - 1, COL_WARN);
    } else drawRow(ROW_RESET, "RESET", "hold 3s", false);
    int sy = rowY(ROW_SAVE);
    tft.fillRect(0, sy, FULLW, ROW_H - 1, dirty ? COL_ACCENT : COL_BG);
    tft.setTextSize(1); tft.setTextColor(dirty ? COL_BG : COL_DIM, dirty ? COL_ACCENT : COL_BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("SAVE", CENTER_X, sy + ROW_H / 2 - 1, 1);
    tft.setTextDatum(TL_DATUM);
    rowsDirty = false;
  }
}

} // namespace settings_ui
