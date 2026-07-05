// CYD-Claudemon — landscape Claude usage/cost monitor for the ESP32-2432S028R.
//
// Daemon-free: the device talks to the Anthropic API directly.
//   * Console Usage & Cost Admin API (admin key)  -> COST / MODELS / TREND pages
//   * Subscription rate-limit windows (OAuth)      -> USAGE (session/weekly) page
// Either or both credentials may be configured; pages appear only for the data
// the device can actually fetch.  Build with -DFAKE_DATA for the Wokwi emulator.
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Wire.h>
#include "time.h"

#include "config.h"
#include "theme.h"
#include "globals.h"
#include "display_pm.h"
#include "offline_ind.h"
#include "poller.h"
#include "ui_common.h"
#include "ui_nav.h"
#include "ui_session.h"
#include "ui_cost.h"
#include "ui_models.h"
#include "ui_spark.h"
#include "ui_clock.h"
#include "ui_system.h"
#include "ui_settings.h"

#ifndef FAKE_DATA
#include "applog.h"
#include "web_config.h"
#endif

// Optional local dev credentials (gitignored src/secrets.h; copy from
// secrets.h.example). Only seeds empty credential slots — NVS / the web form
// always wins. Absent on CI and for other users; never committed.
#if defined(__has_include)
#  if __has_include("secrets.h")
#    include "secrets.h"
#  endif
#endif
#if !defined(FAKE_DATA) && !defined(WOKWI)
#include <WiFiManager.h>   // captive-portal onboarding — real hardware only
#endif

// --- Global state (declared extern in globals.h) ---
TFT_eSPI    tft = TFT_eSPI();
Preferences prefs;
SPIClass    touchSPI(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

uint8_t pages[PAGE__COUNT];
int     pageCount = 0;
int     currentPage = 0;
unsigned long modeTimer = 0;
bool    modeChanged = true;

// Settings is a separate screen (a gear opens it, an X closes it) — not part of
// the page carousel. These track that overlay + the top-chrome redraw.
static bool g_settingsOpen = false;
static bool settingsEnter  = false;
static int  navDrawn       = -1;
static bool chromeDirty    = true;

uint8_t displayRotation = ROT_LANDSCAPE;

AppData g_data;
Diag    g_diag;
String  oauthToken, oauthRefresh, adminKey;
int64_t oauthExpiresMs = 0;
double  budgetWeeklyUsd = 0;
uint16_t pageMask = 0xFFFF;   // which optional pages are shown (settings-toggled)
bool    otaAuto = false;      // auto-install newer firmware (opt-in; default off)

#ifndef FAKE_DATA
volatile bool g_otaWebRequest = false;   // web "Check for update" — check only (no install)
volatile bool g_otaInstallReq = false;   // web "Update now" — flash the already-found update
volatile bool g_otaChecking   = false;   // true while a web-initiated check runs (drives the UI spinner)
String        g_otaUrl;                  // download URL of the last-found update ("" = none known)
// Set from the WiFi event task on STA disconnect; logged by the loop-side
// watchdog (applog isn't safe to call from another task).
volatile uint8_t g_wifiLostReason = 0;
#endif

unsigned long animNow() { return millis(); }

// --- Touch mapping for landscape (panel native portrait, ts.setRotation(0)) ---
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
inline int mapTouchX(const TS_Point& p) {
  int v = (displayRotation == ROT_LANDSCAPE)
        ? map(p.y, TOUCH_RAW_MIN, TOUCH_RAW_MAX, 0, SCREEN_W)
        : map(p.y, TOUCH_RAW_MAX, TOUCH_RAW_MIN, 0, SCREEN_W);
  return clampi(v, 0, SCREEN_W - 1);
}
inline int mapTouchY(const TS_Point& p) {
  int v = (displayRotation == ROT_LANDSCAPE)
        ? map(p.x, TOUCH_RAW_MAX, TOUCH_RAW_MIN, 0, SCREEN_H)
        : map(p.x, TOUCH_RAW_MIN, TOUCH_RAW_MAX, 0, SCREEN_H);
  return clampi(v, 0, SCREEN_H - 1);
}

// --- Touch source abstraction: XPT2046 (resistive, SPI) on real hardware;
//     FT6206 (capacitive, I2C) on the Wokwi board-ili9341-cap-touch part, which
//     unlike wokwi-ili9341 is clickable in the simulator. Both feed the same
//     handleTouch() gesture logic below. ---
#ifdef WOKWI
static const int WOKWI_TOUCH_SDA = 27, WOKWI_TOUCH_SCL = 22;
// Read the FT6206 (I2C 0x38); returns panel-native coords (x:0..239, y:0..319).
inline bool ft6206Read(int& px, int& py) {
  Wire.beginTransmission(0x38);
  Wire.write(0x02);                          // TD_STATUS register
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(0x38, 5) != 5) return false;
  uint8_t n  = Wire.read() & 0x0F;           // number of active touch points
  uint8_t xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
  if (n == 0) return false;
  px = ((xh & 0x0F) << 8) | xl;
  py = ((yh & 0x0F) << 8) | yl;
  return true;
}
inline bool tpTouched() { int x, y; return ft6206Read(x, y); }
// Map panel-native coords -> landscape screen coords. If taps land off, this is
// the spot to calibrate (swap/invert the two axes).
inline bool tpPoint(int& sx, int& sy) {
  int px, py;
  if (!ft6206Read(px, py)) return false;
  if (displayRotation == ROT_LANDSCAPE) { sx = py;                sy = SCREEN_H - 1 - px; }
  else                                  { sx = SCREEN_W - 1 - py; sy = px;                }
  sx = clampi(sx, 0, SCREEN_W - 1); sy = clampi(sy, 0, SCREEN_H - 1);
  return true;
}
#else
inline bool tpTouched() { return ts.touched(); }
inline bool tpPoint(int& sx, int& sy) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  sx = mapTouchX(p); sy = mapTouchY(p);
  return true;
}
#endif

// --- Page registry: only show pages whose data source is available. SETTINGS is
//     NOT here — it's a separate gear-opened overlay, not a carousel page. ---
void buildPages() {
  pageCount = 0;
  if (g_data.subEnabled)   pages[pageCount++] = PAGE_SESSION;
  if (g_data.adminEnabled) { pages[pageCount++] = PAGE_COST; pages[pageCount++] = PAGE_MODELS; pages[pageCount++] = PAGE_SPARK; }
  if (pageMask & (1 << PAGE_CLOCK))  pages[pageCount++] = PAGE_CLOCK;
  if (pageMask & (1 << PAGE_SYSTEM)) pages[pageCount++] = PAGE_SYSTEM;
  if (pageCount == 0) pages[pageCount++] = PAGE_CLOCK;   // always have at least one page
  if (currentPage >= pageCount) currentPage = 0;
}

void gotoPage(int delta) {
  currentPage = (currentPage + delta + pageCount) % pageCount;
  modeChanged = true; modeTimer = millis();
}

void autoCycleNext() {
  currentPage = (currentPage + 1) % pageCount;
  modeChanged = true; modeTimer = millis();
}

void factoryReset() {
#if !defined(FAKE_DATA) && !defined(WOKWI)
  WiFiManager wm; wm.resetSettings();   // clear stored WiFi (real hardware only)
#endif
#ifndef FAKE_DATA
  applog::clearAll();                   // the log holds SSID/IP details — wipe it too
#endif
  prefs.begin(NVS_NS, false); prefs.clear(); prefs.end();
  ESP.restart();
}

// --- Boot splash ---
void bootSplash(const char* line) {
  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_ACCENT, COL_BG);
  tft.setTextSize(3);
  tft.drawString("CLAUDEMON", CENTER_X, CENTER_Y - 16, 1);
  tft.setTextSize(1);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.drawString(line, CENTER_X, CENTER_Y + 18, 1);
  tft.setTextDatum(TL_DATUM);
}

void checkOTA();
#ifndef FAKE_DATA
bool otaFetchLatest(String& tag, String& url);
void otaDownloadFlash(const String& url);
#endif

void setup() {
  Serial.begin(115200);
#ifndef FAKE_DATA
  applog::init();   // mount the flash log first so every boot (and its reset reason) is recorded
#endif
  tft.init();
#ifdef WOKWI
  tft.invertDisplay(false);  // Wokwi's ILI9341 model is not pre-inverted...
#else
  tft.invertDisplay(true);   // ...but the real CYD panel needs colour inversion.
#endif
  display_pm::init(prefs);
#ifdef WOKWI
  Wire.begin(WOKWI_TOUCH_SDA, WOKWI_TOUCH_SCL);   // FT6206 capacitive touch (Wokwi board)
#else
  touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(0);
#endif

  prefs.begin(NVS_NS, false);
  oauthToken     = prefs.getString("oauth_at", "");
  oauthRefresh   = prefs.getString("oauth_rt", "");
  oauthExpiresMs = prefs.getLong64("oauth_exp", 0);
  adminKey       = prefs.getString("admin_key", "");
  String tz      = prefs.getString("tz", "UTC0");
  displayRotation = prefs.getUChar("rot", ROT_LANDSCAPE);
  if (displayRotation != ROT_LANDSCAPE && displayRotation != ROT_LANDSCAPE_F) displayRotation = ROT_LANDSCAPE;
  budgetWeeklyUsd = prefs.getDouble("budget", 0);
  pageMask        = prefs.getUShort("pgmask", 0xFFFF);
  otaAuto         = prefs.getBool("ota_auto", false);
  prefs.end();

  // Seed any credential left unset in NVS from the optional dev secrets.h.
#ifdef DEV_OAUTH_AT
  if (oauthToken.isEmpty())   oauthToken   = DEV_OAUTH_AT;
#endif
#ifdef DEV_OAUTH_RT
  if (oauthRefresh.isEmpty()) oauthRefresh = DEV_OAUTH_RT;
#endif
#ifdef DEV_ADMIN_KEY
  if (adminKey.isEmpty())     adminKey     = DEV_ADMIN_KEY;
#endif
#ifdef DEV_TZ
  if (tz == "UTC0")           tz           = DEV_TZ;
#endif

  tft.setRotation(displayRotation);
  bootSplash("starting up");

#ifdef WOKWI
  // Wokwi: no captive portal — join the simulator's open AP. The Private IoT
  // Gateway gives this Internet access (real Anthropic API in live builds) and
  // forwards the web server to http://localhost:9080/. No XPT2046 in Wokwi, so
  // auto-cycle the pages (navigate/configure via the web UI instead).
  WiFi.begin("Wokwi-GUEST", "");
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) delay(250);
  #ifdef FAKE_DATA
    g_data.subEnabled = g_data.adminEnabled = true;   // scripted curves, no network
    display_pm::cycSec = 6;                            // auto-cycle (no web nav here)
  #else
    // Live: real API over the simulated WiFi. Credentials come from NVS — set
    // them at http://localhost:9080/config (persist across the sim's soft reboots).
    // Navigate the display from the web /status Prev/Next buttons (no touch in Wokwi).
    g_data.subEnabled   = !oauthToken.isEmpty();
    g_data.adminEnabled = !adminKey.isEmpty();
  #endif
#else
  bootSplash("connect to AP: CYD-Claudemon");
  WiFiManager wm;
  WiFiManagerParameter pAt("oauth_at", "Subscription OAuth access token", oauthToken.c_str(), 1600);
  WiFiManagerParameter pRt("oauth_rt", "Subscription OAuth refresh token", oauthRefresh.c_str(), 600);
  WiFiManagerParameter pAk("admin_key", "Admin API key (sk-ant-admin01...)", adminKey.c_str(), 220);
  // Timezone as a friendly dropdown (same list the web /config page uses) instead
  // of a raw POSIX text box. WiFiManager can't render a <select> for a param, so
  // this <select> is display-only; its onchange copies the chosen POSIX string
  // into the hidden 'tz' input, which is the field WiFiManager reads back. If the
  // select is left untouched (or JS is off) the hidden input keeps the current
  // value, so nothing is lost.
  bool tzMatched = false;
  for (int i = 0; i < webcfg::NTZ; i++) if (tz == webcfg::TZONES[i].tz) tzMatched = true;
  String tzSelect = "<label for='tzsel'>Timezone</label>"
                    "<select id='tzsel' onchange=\"document.getElementById('tz').value=this.value\">";
  if (!tzMatched)   // keep an existing custom/unknown value selectable at the top
    tzSelect += "<option value='" + webcfg::esc(tz) + "' selected>(current) " + webcfg::esc(tz) + "</option>";
  for (int i = 0; i < webcfg::NTZ; i++) {
    tzSelect += "<option value='" + String(webcfg::TZONES[i].tz) + "'";
    if (tzMatched && tz == webcfg::TZONES[i].tz) tzSelect += " selected";
    tzSelect += ">" + String(webcfg::TZONES[i].label) + "</option>";
  }
  tzSelect += "</select>";
  WiFiManagerParameter pTzSel(tzSelect.c_str());                                       // dropdown (display only)
  WiFiManagerParameter pTz("tz", NULL, tz.c_str(), 48, "type='hidden'", WFM_NO_LABEL); // value read-back
  wm.addParameter(&pAt); wm.addParameter(&pRt); wm.addParameter(&pAk);
  wm.addParameter(&pTzSel); wm.addParameter(&pTz);
  wm.setConfigPortalTimeout(300);
  if (!wm.autoConnect("CYD-Claudemon")) {
    bootSplash("WiFi failed - restarting");
    delay(3000); ESP.restart();
  }
  // Persist any updated values.
  oauthToken   = pAt.getValue();
  oauthRefresh = pRt.getValue();
  adminKey     = pAk.getValue();
  String newTz = pTz.getValue(); if (newTz.length()) tz = newTz;
  prefs.begin(NVS_NS, false);
  prefs.putString("oauth_at", oauthToken);
  prefs.putString("oauth_rt", oauthRefresh);
  prefs.putString("admin_key", adminKey);
  prefs.putString("tz", tz);
  prefs.end();
  g_data.subEnabled   = !oauthToken.isEmpty();
  g_data.adminEnabled = !adminKey.isEmpty();
#endif

  configTime(0, 0, "pool.ntp.org", "time.google.com");
  setenv("TZ", tz.c_str(), 1);
  tzset();

  bootSplash("connected");
  delay(600);
#ifndef FAKE_DATA
  // Harden the connection: keep the radio awake (modem sleep makes the device
  // intermittently unreachable on some APs) and make auto-reconnect explicit.
  // The loop watchdog below handles the cases auto-reconnect gets stuck on.
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    g_wifiLostReason = info.wifi_sta_disconnected.reason;
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  applog::add("wifi: connected %s, ip %s, %d dBm", WiFi.SSID().c_str(),
              WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
  #ifndef WOKWI
  checkOTA();         // boot update prompt — hardware only (skips the Wokwi TLS hang)
  #endif
  webcfg::begin();    // status + reconfiguration server on the LAN (http://claudemon.local)
#endif

  buildPages();
  poller::init();
  randomSeed(esp_random());
  modeTimer = millis();
  modeChanged = true;
  tft.fillScreen(COL_BG);
}

void renderActive() {
  switch (activePage()) {
    case PAGE_SESSION: page_session::run(); break;
    case PAGE_COST:    page_cost::run();    break;
    case PAGE_MODELS:  page_models::run();  break;
    case PAGE_SPARK:   page_spark::run();   break;
    case PAGE_CLOCK:   page_clock::run();   break;
    case PAGE_SYSTEM:  page_system::run();  break;
    default: break;
  }
}

void handleTouch() {
  int sx, sy;
  if (!tpPoint(sx, sy)) return;
  if (display_pm::isSleeping()) {
    display_pm::wake(10000);
    while (tpTouched()) delay(20);
    delay(200); return;
  }
  int startX = sx, lastX = sx, lastY = sy;
  unsigned long t0 = millis();

  if (g_settingsOpen) {
    // Settings overlay: hold-drag sliders + guarded RESET; tap a row to toggle;
    // tap the top-right X to close. (No global hold-to-wipe anywhere.)
    while (tpPoint(sx, sy)) {
      lastX = sx; lastY = sy;
      settings_ui::handleHoldTick(tft, sy, sx, millis() - t0);
      if (settings_ui::consumeResetIfTriggered()) factoryReset();
      delay(20);
    }
    unsigned long elapsed = millis() - t0;
    int dx = lastX - startX;
    settings_ui::cancelHold();
    if (elapsed < 300 && abs(dx) < 20) {
      if (lastX >= ui::CHROME_ZONE_X && lastY <= ui::CHROME_ZONE_Y) {   // X -> close
        // The log viewer consumes the first X (back to the list); otherwise close.
        if (!settings_ui::handleCloseTap()) {
          settings_ui::exit(); g_settingsOpen = false; modeChanged = true; chromeDirty = true;
        }
      } else {
        settings_ui::handleTap(tft, lastY, lastX);
      }
    }
    delay(180);
    return;
  }

  // Content page.
  while (tpPoint(sx, sy)) { lastX = sx; lastY = sy; delay(20); }
  unsigned long elapsed = millis() - t0;
  int dx = lastX - startX;

  if (elapsed < 500 && abs(dx) > 40) {            // swipe -> change page
    gotoPage(dx > 0 ? -1 : 1);
  } else if (elapsed < 300 && abs(dx) < 20) {     // tap
    if (lastX >= ui::CHROME_ZONE_X && lastY <= ui::CHROME_ZONE_Y) {  // gear -> open settings
      g_settingsOpen = true; settingsEnter = true;
    } else {
      // Anywhere else: left half = previous, right half = next.
      gotoPage(lastX < CENTER_X ? -1 : 1);
    }
  }
  delay(180);
}

void loop() {
  handleTouch();   // XPT2046 (SPI) on hardware; FT6206 (I2C) on the Wokwi cap-touch
                   // board. Both are deterministic — no phantom-touch problem.
  display_pm::tick();
  offline_ind::update();
  poller::tick();

#ifndef FAKE_DATA
  // --- WiFi watchdog: auto-reconnect can wedge after an AP hiccup (observed as
  //     grey bars + red dot + device unreachable until power-cycled). Log the
  //     drop with its reason code, nudge a reconnect every 30 s, and reboot
  //     after 5 minutes down as the last resort — all flash-logged, so the
  //     story survives the reboot. ---
  static unsigned long wifiDownAt = 0, wifiRetryAt = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (!wifiDownAt) {
      wifiDownAt = wifiRetryAt = millis();
      applog::add("wifi: lost (reason %d)", (int)g_wifiLostReason);
      WiFi.reconnect();
    } else if (millis() - wifiRetryAt >= 30000UL) {
      wifiRetryAt = millis();
      WiFi.reconnect();
    }
    if (millis() - wifiDownAt >= 5UL * 60UL * 1000UL) {
      applog::add("wifi: down 5 min - restarting");
      delay(200);
      ESP.restart();
    }
  } else if (wifiDownAt) {
    wifiDownAt = 0;
    applog::add("wifi: reconnected, ip %s, %d dBm",
                WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
  }

  webcfg::handle();
  if (g_otaWebRequest) {                 // web "Check for update" — check only, never install
    g_otaWebRequest = false;
    String tag, url;
    bool avail = otaFetchLatest(tag, url);
    applog::add("ota: %s", g_diag.otaMsg);
    g_otaUrl = avail ? url : "";         // remember (or clear) the found update for "Update now"
    g_otaChecking = false;
  }
  if (g_otaInstallReq) {                 // web "Update now" — flash the update the check found
    g_otaInstallReq = false;
    if (g_otaUrl.length()) otaDownloadFlash(g_otaUrl);   // reboots on success
  }
  static unsigned long tOtaCheck = 0;    // periodic check -> note availability, or auto-install
  if (millis() - tOtaCheck > 6UL * 3600UL * 1000UL) {
    tOtaCheck = millis();
    String tag, url;
    if (otaFetchLatest(tag, url)) {
      applog::add("ota: %s", g_diag.otaMsg);
      g_otaUrl = url;                    // surface it on the web UI as "Update now"
      if (otaAuto) {                     // opt-in auto-update: flash the newer release now
        applog::add("ota: auto-installing v%s", tag.c_str());
        otaDownloadFlash(url);           // reboots on success
      }
    }
  }
#endif

  // Settings overlay takes over the screen (no carousel, no auto-cycle) until closed.
  if (g_settingsOpen) {
    if (settingsEnter) { settings_ui::enter(); settings_ui::render(tft, true); settingsEnter = false; }
    else               { settings_ui::render(tft, false); }
    ui::drawStatus();
    ui::drawClose();
    navDrawn = -1;                 // force chrome redraw when we return to a page
    return;
  }

  uint32_t cycleMs = display_pm::getCycleIntervalMs();
  if (cycleMs > 0 && millis() - modeTimer > cycleMs) autoCycleNext();

  renderActive();

  if (navDrawn != currentPage || chromeDirty) {
    nav::draw(); ui::drawGear(); navDrawn = currentPage; chromeDirty = false;
  }
}

// --- OTA: GitHub releases -> on-device flash (landscape progress UI) ---
#ifndef FAKE_DATA

// Query GitHub for the latest release. Returns true (+ tag/url) only if it is
// newer than the running build and ships a claudemon-firmware.bin asset. Records
// the outcome in g_diag.otaMsg/otaCheckCode for the web /status page.
bool otaFetchLatest(String& tagOut, String& urlOut) {
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, "https://api.github.com/repos/" OTA_REPO "/releases/latest")) {
    snprintf(g_diag.otaMsg, sizeof g_diag.otaMsg, "connect failed");
    return false;
  }
  http.addHeader("User-Agent", "CYD-Claudemon");
  int code = http.GET();
  g_diag.otaCheckCode = code;
  if (code != 200) {
    http.end();
    snprintf(g_diag.otaMsg, sizeof g_diag.otaMsg, "check HTTP %d", code);
    return false;
  }
  JsonDocument doc; deserializeJson(doc, http.getString()); http.end();

  String tag = doc["tag_name"] | "";
  if (tag.startsWith("v")) tag = tag.substring(1);
  if (tag.isEmpty()) { snprintf(g_diag.otaMsg, sizeof g_diag.otaMsg, "no release found"); return false; }
  if (tag == CLAUDEMON_VERSION) {
    snprintf(g_diag.otaMsg, sizeof g_diag.otaMsg, "up to date (v%s)", CLAUDEMON_VERSION);
    return false;
  }
  String url;
  for (JsonObject a : doc["assets"].as<JsonArray>())
    if (String(a["name"] | "") == "claudemon-firmware.bin") { url = a["browser_download_url"].as<String>(); break; }
  if (url.isEmpty()) {
    snprintf(g_diag.otaMsg, sizeof g_diag.otaMsg, "v%s: no .bin asset", tag.c_str());
    return false;
  }
  snprintf(g_diag.otaMsg, sizeof g_diag.otaMsg, "v%s available", tag.c_str());
  tagOut = tag; urlOut = url;
  return true;
}

// Download the firmware at `url` and flash it with a progress bar; reboots on success.
void otaDownloadFlash(const String& url) {
  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM); tft.setTextColor(COL_ACCENT, COL_BG); tft.setTextSize(2);
  tft.drawString("UPDATING", CENTER_X, 40, 1);
  tft.setTextColor(COL_DIM, COL_BG); tft.setTextSize(1);
  tft.drawString("do not power off", CENTER_X, 66, 1);
  tft.setTextDatum(TL_DATUM);

  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return;
  http.addHeader("User-Agent", "CYD-Claudemon");
  if (http.GET() != 200) { http.end(); return; }
  int len = http.getSize();
  WiFiClient* stream = http.getStreamPtr();
  if (!Update.begin(len)) { http.end(); return; }

  uint8_t buf[1024]; int written = 0;
  int barX = 30, barY = 120, barW = SCREEN_W - 60, barH = 16;
  while (written < len) {
    int avail = stream->available();
    if (avail) {
      int r = stream->readBytes(buf, min((int)sizeof(buf), avail));
      Update.write(buf, r); written += r;
      int pct = (written * 100) / len;
      tft.fillRect(barX, barY, barW, barH, COL_TRACK);
      tft.fillRect(barX, barY, (pct * barW) / 100, barH, COL_ACCENT);
      tft.setTextDatum(MC_DATUM); tft.setTextColor(COL_TEXT, COL_BG); tft.setTextSize(1);
      tft.drawString(String(pct) + "%", CENTER_X, barY + barH + 14, 1);
      tft.setTextDatum(TL_DATUM);
    }
    delay(1);
  }
  http.end();
  if (Update.end(true)) { bootSplash("done - rebooting"); delay(1500); ESP.restart(); }
}

// Boot-time check with a touch Yes/No prompt (web /update and the periodic loop
// check reuse otaFetchLatest()/otaDownloadFlash() directly).
void checkOTA() {
  bootSplash("checking for updates");
  String tag, url;
  bool avail = otaFetchLatest(tag, url);
  applog::add("ota: %s", g_diag.otaMsg);
  if (!avail) return;

  if (otaAuto) {                         // opt-in auto-update: install without prompting
    applog::add("ota: auto-installing v%s", tag.c_str());
    otaDownloadFlash(url);               // reboots on success
    return;
  }

  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_ACCENT, COL_BG); tft.setTextSize(2);
  tft.drawString("UPDATE AVAILABLE", CENTER_X, 30, 1);
  tft.setTextColor(COL_DIM, COL_BG); tft.setTextSize(1);
  tft.drawString("v" CLAUDEMON_VERSION " -> v" + tag, CENTER_X, 56, 1);
  tft.fillRect(40, 90, 100, 50, COL_ACCENT);  tft.setTextColor(COL_BG, COL_ACCENT); tft.setTextSize(2); tft.drawString("YES", 90, 115, 1);
  tft.fillRect(180, 90, 100, 50, COL_DIM);    tft.setTextColor(COL_TEXT, COL_DIM); tft.drawString("NO", 230, 115, 1);
  tft.setTextDatum(TL_DATUM);

  unsigned long deadline = millis() + 15000; bool yes = false;
  while (millis() < deadline) {
    if (ts.touched()) {
      TS_Point p = ts.getPoint(); int tx = mapTouchX(p), ty = mapTouchY(p);
      if (ty >= 90 && ty <= 140) { if (tx >= 40 && tx <= 140) { yes = true; break; } if (tx >= 180 && tx <= 280) return; }
    }
    delay(40);
  }
  if (!yes) return;
  otaDownloadFlash(url);
}
#endif
