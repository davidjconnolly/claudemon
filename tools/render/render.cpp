// Host-side renderer for the CYD-Claudemon landscape UI.
// Compiles the real ui_*.h page code against a mock TFT_eSPI and dumps each
// page to a 320x240 PNG — no Wokwi, no hardware, instant.
#include "Arduino.h"
#include "config.h"
#include "theme.h"
#include "data_models.h"
#include "globals.h"
#include "offline_ind.h"
#include "fake_data.h"
#include "poller.h"        // budgetExceeded() + fake::fill via FAKE_DATA
#include "ui_common.h"
#include "ui_nav.h"
#include "ui_session.h"
#include "ui_cost.h"
#include "ui_models.h"
#include "ui_spark.h"
#include "ui_clock.h"
#include "ui_system.h"
#include "ui_settings.h"

#include <vector>
#include <cstdio>
#include <zlib.h>

// ---- globals declared extern in globals.h / Arduino.h / WiFi.h ----
TFT_eSPI    tft;
Preferences prefs;
EspClass    ESP;
WiFiClass   WiFi;
unsigned long g_millis = 210000;     // drives fake curves + sprite/anim timing

uint8_t pages[PAGE__COUNT];
int     pageCount = 0, currentPage = 0;
unsigned long modeTimer = 0;
bool    modeChanged = true;
uint8_t displayRotation = 1;
AppData g_data;
String  oauthToken, oauthRefresh, adminKey;
int64_t oauthExpiresMs = 0;
double  budgetWeeklyUsd = 0;
uint16_t pageMask = 0xFFFF;
unsigned long animNow() { return millis(); }

// Fixed wall-clock for the CLOCK page: Sun Jun 28 2026, 14:25:36.
bool getLocalTime(struct tm* info, uint32_t) {
  memset(info, 0, sizeof(*info));
  info->tm_sec = 36; info->tm_min = 25; info->tm_hour = 14;
  info->tm_mday = 28; info->tm_mon = 5; info->tm_year = 126; info->tm_wday = 0;
  info->tm_yday = 178; info->tm_isdst = 1;
  return true;
}

// ---- PNG writer (RGB565 -> RGB888, zlib IDAT) ----
static void wr32(FILE* f, uint32_t v) { uint8_t b[4]={(uint8_t)(v>>24),(uint8_t)(v>>16),(uint8_t)(v>>8),(uint8_t)v}; fwrite(b,1,4,f); }
static void chunk(FILE* f, const char* type, const uint8_t* data, uint32_t len) {
  wr32(f, len); fwrite(type, 1, 4, f);
  if (len) fwrite(data, 1, len, f);
  uLong c = crc32(0, (const Bytef*)type, 4);
  if (len) c = crc32(c, data, len);
  wr32(f, (uint32_t)c);
}
static void writePNG(const char* path, const uint16_t* fb, int W, int H) {
  std::vector<uint8_t> raw; raw.reserve((size_t)H * (1 + W * 3));
  for (int y = 0; y < H; y++) {
    raw.push_back(0);
    for (int x = 0; x < W; x++) {
      uint16_t c = fb[y * W + x];
      int r5 = (c >> 11) & 31, g6 = (c >> 5) & 63, b5 = c & 31;
      raw.push_back((uint8_t)((r5 * 255 + 15) / 31));
      raw.push_back((uint8_t)((g6 * 255 + 31) / 63));
      raw.push_back((uint8_t)((b5 * 255 + 15) / 31));
    }
  }
  uLong bound = compressBound(raw.size());
  std::vector<uint8_t> comp(bound);
  compress2(comp.data(), &bound, raw.data(), raw.size(), 9);
  comp.resize(bound);
  FILE* f = fopen(path, "wb");
  const uint8_t sig[8] = {137,80,78,71,13,10,26,10}; fwrite(sig,1,8,f);
  uint8_t ihdr[13] = {0};
  ihdr[0]=(W>>24);ihdr[1]=(W>>16);ihdr[2]=(W>>8);ihdr[3]=(uint8_t)W;
  ihdr[4]=(H>>24);ihdr[5]=(H>>16);ihdr[6]=(H>>8);ihdr[7]=(uint8_t)H;
  ihdr[8]=8; ihdr[9]=2;  // 8-bit, RGB
  chunk(f, "IHDR", ihdr, 13);
  chunk(f, "IDAT", comp.data(), (uint32_t)comp.size());
  chunk(f, "IEND", nullptr, 0);
  fclose(f);
  printf("wrote %s\n", path);
}

static void shot(int idx, const char* file, void (*run)()) {
  currentPage = idx;
  modeChanged = true;
  run();
  nav::draw();
  ui::drawGear();
  char path[512];
  snprintf(path, sizeof path, "out/%s.png", file);
  writePNG(path, tft.fb, 320, 240);
}

int main() {
  srand(20260628);
  g_data.subEnabled = g_data.adminEnabled = true;
  fake::fill(g_data);
  budgetWeeklyUsd = 50.0;            // exercise the COST budget bar
  offline_ind::recordSuccess();
  offline_ind::update();            // -> ONLINE (so accent colours aren't drained)

  pageCount = 6;
  pages[0]=PAGE_SESSION; pages[1]=PAGE_COST; pages[2]=PAGE_MODELS;
  pages[3]=PAGE_SPARK;   pages[4]=PAGE_CLOCK; pages[5]=PAGE_SYSTEM;

  shot(0, "1_usage",     []{ page_session::run(); });
  shot(1, "2_cost",      []{ page_cost::run(); });
  shot(2, "3_models",    []{ page_models::run(); });
  shot(3, "4_trend",     []{ page_spark::run(); });
  shot(4, "6_clock",     []{ page_clock::run(); });
  shot(5, "7_system",    []{ page_system::run(); });

  // settings overlay (separate screen: no nav bar, X to close instead of a gear)
  settings_ui::enter();
  settings_ui::render(tft, true);
  ui::drawStatus();
  ui::drawClose();
  writePNG("out/8_settings.png", tft.fb, 320, 240);

  // --- alert-state variants (red bars / RATE LIMITED / OVER BUDGET) ---
  g_data.sub.sessionPct = 96; g_data.sub.weeklyPct = 88;
  g_data.sub.limited = true;
  g_data.sub.sessionResetAt = (long)time(nullptr) + 1180;
  g_data.cost.costWeekUsd = 72.0;     // > budget 50 -> OVER BUDGET
  shot(0, "alt_usage",     []{ page_session::run(); });
  shot(1, "alt_cost",      []{ page_cost::run(); });

  // expired-OAuth banner on the USAGE page (with prior data, and with none)
  g_data.sub.limited = false;
  g_data.sub.authError = true;
  shot(0, "alt_usage_expired", []{ page_session::run(); });
  g_data.sub.valid = false;
  shot(0, "alt_usage_nodata", []{ page_session::run(); });

  // two-window USAGE layout (Pro/Free: no model-scoped weekly bar)
  fake::fill(g_data);
  g_data.sub.hasScoped = false;
  snprintf(g_data.sub.plan, sizeof(g_data.sub.plan), "PRO");
  shot(0, "alt_usage_two", []{ page_session::run(); });
  return 0;
}
