// ui_system.h — device info page, landscape single column.
#pragma once
#include "ui_common.h"
#include "globals.h"
#include <WiFi.h>

namespace page_system {

inline void row(int& y, const char* label, const String& value) {
  ui::label(label, 6, y, COL_DIM, 1);
  ui::label(value.c_str(), SCREEN_W - 6, y, COL_ACCENT, 1, TR_DATUM);
  y += 11;
  tft.drawFastHLine(6, y, SCREEN_W - 12, COL_TRACK);
  y += 5;
}

inline void run() {
  if (!modeChanged) { ui::drawStatus(); return; }
  modeChanged = false;
  tft.fillScreen(COL_BG);
  ui::drawHeader("SYSTEM");
  ui::drawStatus();

  int y = 26;
  row(y, "CHIP", String(ESP.getChipModel()) + " r" + ESP.getChipRevision() + " " + ESP.getChipCores() + "c");
  row(y, "CPU", String(ESP.getCpuFreqMHz()) + " MHz");
  char fb[24];
  snprintf(fb, sizeof fb, "%.1f / %d MB", ESP.getSketchSize() / 1048576.0, (int)(ESP.getFlashChipSize() / 1048576));
  row(y, "FLASH USED", String(fb));
  row(y, "HEAP USED", String((ESP.getHeapSize() - ESP.getFreeHeap()) / 1024) + "K / " + String(ESP.getHeapSize() / 1024) + "K");
  row(y, "PANEL", "ILI9341 320x240");
  row(y, "TOUCH", "XPT2046");

  // wifi with signal bars
  ui::label("WIFI", 6, y, COL_DIM, 1);
  int32_t rssi = WiFi.RSSI();
  int bars = (rssi > -50) ? 5 : (rssi > -60) ? 4 : (rssi > -70) ? 3 : (rssi > -80) ? 2 : 1;
  for (int i = 0; i < 5; i++)
    tft.fillRect(120 + i * 8, y, 6, 6, (i < bars) ? COL_ACCENT : COL_TRACK);
  ui::label((String(rssi) + "dBm").c_str(), SCREEN_W - 6, y, COL_ACCENT, 1, TR_DATUM);
  y += 11; tft.drawFastHLine(6, y, SCREEN_W - 12, COL_TRACK); y += 5;

  row(y, "IP", WiFi.localIP().toString());

  unsigned long sec = millis() / 1000;
  int d = sec / 86400, h = (sec % 86400) / 3600, m = (sec % 3600) / 60;
  String up = (d > 0 ? String(d) + "d " : "") + String(h) + "h " + String(m) + "m";
  row(y, "UPTIME", up);

  String src = (g_data.subEnabled && g_data.adminEnabled) ? "OAuth + Admin"
             : g_data.subEnabled   ? "OAuth (usage)"
             : g_data.adminEnabled ? "Admin (cost)" : "none";
  row(y, "SOURCES", src);
  row(y, "FW", "v" CLAUDEMON_VERSION);
}

} // namespace page_system
