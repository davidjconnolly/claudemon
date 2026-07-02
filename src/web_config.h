// web_config.h — on-device HTTP server for status + (re)configuration over WiFi.
//
// The captive portal (WiFiManager) is for first-time WiFi onboarding only. Once
// the device is on your network, this always-on server lets you:
//   * read live data-source health + a recent-activity log (so failures like a
//     401 are visible over WiFi without a serial cable), and
//   * update credentials / timezone / budget without a factory reset.
//
// Reconfiguring or updating firmware does NOT erase settings: creds live in the
// NVS partition, which neither a reboot nor an OTA flash touches.
//
// cyd build only (compiled out of FAKE/emulator). LAN-only and unauthenticated —
// anyone on the network can read masked config and change settings; treat the
// device as holding sensitive credentials (same posture as the open setup AP).
#pragma once
#ifndef FAKE_DATA

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "config.h"
#include "globals.h"
#include "data_models.h"
#include "display_pm.h"
#include "offline_ind.h"
#include "applog.h"

// Defined in main.cpp (they drive the TFT/touch globals that live there).
bool otaFetchLatest(String& tag, String& url);
void gotoPage(int delta);             // change the page shown on the device/sim
extern volatile bool g_otaWebRequest;
extern volatile bool g_otaChecking;   // true while a web-initiated OTA check runs

namespace webcfg {

static WebServer server(80);

// Common timezones as POSIX TZ strings (the device has no zoneinfo DB).
struct TzOpt { const char* label; const char* tz; };
static const TzOpt TZONES[] = {
  {"Toronto / Eastern (EST/EDT)", "EST5EDT,M3.2.0,M11.1.0"},
  {"Halifax / Atlantic",          "AST4ADT,M3.2.0,M11.1.0"},
  {"Chicago / Central",           "CST6CDT,M3.2.0,M11.1.0"},
  {"Denver / Mountain",           "MST7MDT,M3.2.0,M11.1.0"},
  {"Phoenix (no DST)",            "MST7"},
  {"Los Angeles / Pacific",       "PST8PDT,M3.2.0,M11.1.0"},
  {"UTC",                         "UTC0"},
  {"London (GMT/BST)",            "GMT0BST,M3.5.0/1,M10.5.0"},
  {"Berlin / Paris (CET)",        "CET-1CEST,M3.5.0,M10.5.0/3"},
  {"Sydney (AEST/AEDT)",          "AEST-10AEDT,M10.1.0,M4.1.0/3"},
};
static const int NTZ = sizeof(TZONES) / sizeof(TZONES[0]);

// ---- tiny HTML helpers ----
inline String esc(const String& s) {
  String o; o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '<')      o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '"') o += "&quot;";
    else               o += c;
  }
  return o;
}
inline String mask(const String& s) {
  if (s.isEmpty()) return "(not set)";
  if (s.length() <= 12) return "set (" + String(s.length()) + ")";
  return esc(s.substring(0, 8)) + "…" + esc(s.substring(s.length() - 4)) +
         " (" + String(s.length()) + ")";
}
inline String ageStr(unsigned long ms) {
  if (ms == 0) return "never";
  unsigned long a = (millis() - ms) / 1000;
  if (a < 90)   return String(a) + "s ago";
  if (a < 5400) return String(a / 60) + "m ago";
  return String(a / 3600) + "h ago";
}
inline String otaStatusFriendly() {
  if (g_diag.otaMsg[0] == 0)               return "not checked yet";
  if (String(g_diag.otaMsg) == "check HTTP 404") return "no published release yet";
  return String(g_diag.otaMsg);
}
// Overall data-freshness, mirroring the on-device corner dot (green/yellow/red).
inline String connHtml() {
  switch (offline_ind::state()) {
    case offline_ind::ONLINE: return "<span class=ok>online</span>";
    case offline_ind::STALE:  return "<span class=warn>stale</span> &middot; last reading "
                                     + ageStr(g_diag.subLastOkMs ? g_diag.subLastOkMs : g_diag.adminLastOkMs);
    default:                  return "<span class=bad>offline</span>";
  }
}
inline String jsonEsc(const String& s) {
  String o; for (size_t i = 0; i < s.length(); i++) { char c = s[i]; if (c == '"' || c == '\\') o += '\\'; o += c; }
  return o;
}
inline const char* pageName(int p) {
  switch (p) {
    case PAGE_SESSION:  return "USAGE";
    case PAGE_COST:     return "COST";
    case PAGE_MODELS:   return "MODELS";
    case PAGE_SPARK:    return "TREND";
    case PAGE_CLOCK:    return "CLOCK";
    case PAGE_SYSTEM:   return "SYSTEM";
    case PAGE_SETTINGS: return "SETTINGS";
    default:            return "?";
  }
}
inline String pageLabel() {
  return String(pageName(activePage())) + " (" + String(currentPage + 1) + "/" + String(pageCount) + ")";
}

static const char* CSS =
  "body{font-family:system-ui,-apple-system,sans-serif;background:#111;color:#ddd;margin:0;padding:16px;max-width:680px}"
  "h1{color:#f5a623;font-size:20px}h2{color:#f5a623;font-size:15px;margin-top:22px;border-bottom:1px solid #333;padding-bottom:4px}"
  "table{width:100%;border-collapse:collapse}td{padding:4px 6px;border-bottom:1px solid #222;font-size:14px;word-break:break-word}"
  "td:first-child{color:#888;width:42%}.ok{color:#3c3}.bad{color:#e44}.warn{color:#f5a623}.muted{color:#666;font-size:12px}"
  "label{display:block;margin:12px 0 4px;color:#888;font-size:13px}"
  "input,select{width:100%;box-sizing:border-box;padding:8px;background:#1a1a1a;border:1px solid #333;color:#ddd;border-radius:4px;font-size:14px}"
  "pre{background:#0a0a0a;border:1px solid #222;border-radius:4px;padding:8px;font-size:12px;white-space:pre-wrap;overflow-wrap:anywhere;color:#cfcfcf}"
  "code{background:#222;padding:1px 4px;border-radius:3px;font-size:12px;overflow-wrap:anywhere}"
  "details{margin:8px 0}summary{color:#f5a623;cursor:pointer;font-size:13px}"
  "input[type=checkbox]{width:auto;margin-right:6px;vertical-align:-2px}.rm{color:#888;font-size:12px;margin-top:6px}"
  "button,a.btn{display:inline-block;margin-top:12px;margin-right:6px;padding:7px 12px;background:#f5a623;color:#111;border:0;border-radius:4px;font-size:14px;font-weight:bold;text-decoration:none;cursor:pointer}"
  "button:disabled{opacity:.5;cursor:default}"
  ".spin{display:inline-block;width:11px;height:11px;border:2px solid #555;border-top-color:#f5a623;border-radius:50%;animation:sp .7s linear infinite;vertical-align:-1px}@keyframes sp{to{transform:rotate(360deg)}}"
  "a{color:#f5a623}";

inline String head(const char* title) {
  String h = "<!doctype html><meta charset=utf-8>"
             "<meta name=viewport content='width=device-width,initial-scale=1'>";
  h += "<title>"; h += title; h += "</title><style>"; h += CSS; h += "</style>";
  return h;
}

// One data-source health row. ok429 = treat HTTP 429 as healthy (true for the
// subscription probe, where 429 still carries valid rate-limit headers).
inline String srcRow(const char* label, bool enabled, int code, unsigned long lastOk,
                     bool ok429, const char* err = "") {
  String h = "<tr><td>"; h += label; h += "</td><td>";
  if (!enabled)      return h + "<span class=muted>off</span></td></tr>";
  if (code == 0)     return h + "<span class=warn>starting…</span></td></tr>";
  bool ok = (code == 200) || (ok429 && code == 429);
  h += ok ? "<span class=ok>ok</span>" : "<span class=bad>failing</span>";
  h += " &middot; HTTP " + String(code) + " &middot; last ok " + ageStr(lastOk);
  if (!ok && err && err[0]) h += "<br><span class=bad>" + esc(err) + "</span>";
  return h + "</td></tr>";
}

// ---- status page ----
inline String statusHtml() {
  String h = head("CYD-Claudemon");
  h += "<h1>CYD-Claudemon</h1>";

  size_t total = ESP.getHeapSize(), freeh = ESP.getFreeHeap();
  h += "<h2>Device</h2><table>";
  h += "<tr><td>Firmware</td><td>v" CLAUDEMON_VERSION "</td></tr>";
  h += "<tr><td>Address</td><td>" + WiFi.localIP().toString() + " &middot; claudemon.local</td></tr>";
  h += "<tr><td>WiFi</td><td>" + esc(WiFi.SSID()) + " &middot; " + String(WiFi.RSSI()) + " dBm</td></tr>";
  h += "<tr><td>Connection</td><td>" + connHtml() + "</td></tr>";
  h += "<tr><td>RAM used</td><td>" + String((total - freeh) / 1024) + " / " + String(total / 1024) + " KB</td></tr>";
  h += "<tr><td>Uptime</td><td>" + String(millis() / 60000) + " min</td></tr>";
  h += "</table>";

  h += "<h2>Display</h2><table>";
  h += "<tr><td>Current page</td><td id=pg>" + esc(pageLabel()) + "</td></tr></table>";
  h += "<p><button class=btn onclick=\"nav(-1)\">&#9664; Prev</button> "
       "<button class=btn onclick=\"nav(1)\">Next &#9654;</button> "
       "<span class=muted>changes the page shown on the device/sim (Wokwi has no touchscreen)</span></p>";

  h += "<h2>Data sources</h2><table>";
  h += srcRow("Subscription (USAGE)",       g_data.subEnabled,   g_diag.subLastCode,   g_diag.subLastOkMs,   true);
  h += srcRow("Admin (COST/MODELS/TREND)",  g_data.adminEnabled, g_diag.adminLastCode, g_diag.adminLastOkMs, false, g_diag.adminLastErr);
  h += "</table>";
  if (g_data.subEnabled && (g_diag.subLastCode == 401 || g_diag.subLastCode == 403)) {
    h += "<p class=bad><b>&#9888; USAGE token expired &mdash; the device tried to refresh and could not.</b></p>";
    h += "<p class=muted>The subscription OAuth token is the same login as the Claude Code on your "
         "computer, and Claude uses single-use (rotating) refresh tokens: when your Claude Code "
         "refreshes, the copy stored on the device is invalidated, so this can recur. To restore "
         "USAGE, re-enter the current tokens at <a href=/config>/config</a> &mdash; from "
         "<code>~/.claude/.credentials.json</code> &rarr; <code>claudeAiOauth</code>, or the macOS "
         "Keychain item <code>Claude Code-credentials</code>.</p>";
  }

  h += "<h2>Firmware updates</h2><table>";
  h += "<tr><td>Status</td><td id=otastat>" + esc(otaStatusFriendly()) + "</td></tr>";
  h += "<tr><td>Auto-install</td><td>" + String(otaAuto ? "<span class=ok>on</span>" : "off") +
       " &middot; <a href=/settings>change</a></td></tr>";
  h += "</table>";

  h += "<h2>Recent activity</h2>";
  if (applog::size() == 0) h += "<p class=muted>no events yet</p>";
  else {
    h += "<pre>";
    for (int i = applog::size() - 1; i >= 0; i--) { h += esc(applog::at(i)); h += "\n"; }
    h += "</pre>";
  }

  h += "<p><a class=btn id=bcfg href=/config>Edit configuration</a> "
       "<a class=btn href=/settings>Device settings</a> "
       "<button class=btn id=bupd onclick=\"checkUpd()\">Check for update</button></p>";
  h += "<p class=muted>LAN-only and unauthenticated &mdash; anyone on this network can change settings.</p>";
  // Browser-side: spin the status + disable buttons while the device checks GitHub,
  // then poll /otastatus for the result. All work is in the browser; the device just
  // serves a tiny JSON flag.
  h += "<script>"
       "function nav(d){fetch('/nav?d='+d).then(function(r){return r.text();}).then(function(t){document.getElementById('pg').textContent=t;});}"
       "function checkUpd(){"
       "var u=document.getElementById('bupd'),c=document.getElementById('bcfg'),s=document.getElementById('otastat');"
       "u.disabled=true;c.style.pointerEvents='none';c.style.opacity=.5;"
       "s.innerHTML='checking <span class=spin></span>';"
       "fetch('/update').catch(function(){});var n=0;"
       "var iv=setInterval(function(){n++;"
       "fetch('/otastatus').then(function(r){return r.json();}).then(function(j){"
       "if(!j.checking){clearInterval(iv);s.textContent=j.status;u.disabled=false;"
       "c.style.pointerEvents='';c.style.opacity='';}}).catch(function(){"
       "clearInterval(iv);s.textContent='device may be updating - watch the screen, refresh shortly';});"
       "if(n>25){clearInterval(iv);u.disabled=false;c.style.pointerEvents='';c.style.opacity='';}},1000);}</script>";
  return h;
}

// ---- config form ----
inline String configHtml() {
  Preferences p; p.begin(NVS_NS, true);
  String tz = p.getString("tz", "UTC0");
  p.end();
  bool matched = false;
  for (int i = 0; i < NTZ; i++) if (tz == TZONES[i].tz) matched = true;

  String h = head("Configure");
  h += "<h1>Configure</h1><form method=POST action=/save>";
  h += "<p class=muted>Secret fields are blank &mdash; leave a field blank to keep its current value. "
       "Saving reboots the device; <b>your settings are preserved</b> (as they are across a firmware update).</p>";
  h += "<label>Admin API key &mdash; current: " + mask(adminKey) + "</label>"
       "<input name=admin_key autocomplete=off placeholder='sk-ant-admin01-…'>";
  if (!adminKey.isEmpty())
    h += "<label class=rm><input type=checkbox name=admin_clear>remove this key (turns off the cost pages)</label>";
  h += "<details><summary>Where do I get the Admin key?</summary><p class=muted>"
       "A <b>distinct</b> key (prefix <code>sk-ant-admin01-</code>) from your normal "
       "<code>sk-ant-api03-</code> keys. Create it at "
       "<a href='https://platform.claude.com/settings/admin-keys' target=_blank rel=noopener>"
       "platform.claude.com/settings/admin-keys</a> "
       "(Console &rarr; Settings &rarr; <b>Admin keys</b> &mdash; open the link directly; it may not be in the "
       "sidebar). Needs an org <b>admin</b> role. No org / individual account &rarr; not available, and the cost "
       "pages just stay empty.</p></details>";

  h += "<label>OAuth access token &mdash; current: " + mask(oauthToken) + "</label>"
       "<input name=oauth_at autocomplete=off placeholder='sk-ant-oat01-…'>";
  h += "<label>OAuth refresh token &mdash; current: " + mask(oauthRefresh) + "</label>"
       "<input name=oauth_rt autocomplete=off placeholder='sk-ant-ort01-…'>";
  if (!oauthToken.isEmpty() || !oauthRefresh.isEmpty())
    h += "<label class=rm><input type=checkbox name=oauth_clear>remove both OAuth tokens (turns off USAGE)</label>";
  h += "<details><summary>Where do I get the OAuth tokens?</summary><p class=muted>"
       "Your Claude Code subscription credentials, on the computer where Claude Code is signed in.<br>"
       "File: <code>~/.claude/.credentials.json</code> &rarr; <code>claudeAiOauth.accessToken</code> / "
       "<code>.refreshToken</code> &mdash; or macOS Keychain item <code>Claude Code-credentials</code>.<br>"
       "Quick (macOS): <code>python3 -c \"import json,os;o=json.load(open(os.path.expanduser("
       "'~/.claude/.credentials.json')))['claudeAiOauth'];print(o['accessToken']);print(o['refreshToken'])\"</code>"
       "</p></details>";

  h += "<label>Timezone</label><select name=tz_preset>";
  for (int i = 0; i < NTZ; i++) {
    h += "<option value='" + String(TZONES[i].tz) + "'";
    if (matched && tz == TZONES[i].tz) h += " selected";
    h += ">" + String(TZONES[i].label) + "</option>";
  }
  h += "<option value=''"; if (!matched) h += " selected"; h += ">Custom (use field below)</option></select>";
  h += "<label class=muted>Custom POSIX TZ &mdash; overrides the dropdown if set</label>"
       "<input name=tz_custom placeholder='e.g. EST5EDT,M3.2.0,M11.1.0'";
  if (!matched) h += " value='" + esc(tz) + "'";
  h += ">";

  h += "<label>Weekly budget USD (0 = off)</label><input name=budget value='" + String(budgetWeeklyUsd, 2) + "'>";
  h += "<p><button type=submit>Save &amp; reboot</button> &nbsp; <a href=/>cancel</a></p></form>";
  return h;
}

// ---- device settings mirror (read-only) ----
// Shows the same settings as the on-device SETTINGS page, with a description of
// what each does. Adjust them by touch on the device (creds/timezone/budget are
// editable in the browser at /config).
inline String setRow(const char* name, const String& value, const char* desc) {
  return "<tr><td>" + String(name) + "</td><td><b>" + esc(value) +
         "</b><br><span class=muted>" + String(desc) + "</span></td></tr>";
}
inline String onOffStr(bool b) { return b ? "On" : "Off"; }

inline String settingsHtml() {
  using namespace display_pm;
  String h = head("Device settings");
  h += "<h1>Device settings</h1>";
  h += "<p class=muted>Live display &amp; behaviour settings (also adjustable by touch on the "
       "device). Saving applies immediately &mdash; no reboot. Credentials, timezone and the "
       "weekly budget are on the <a href=/config>configuration</a> page.</p>";
  h += "<form method=POST action=/savesettings>";

  h += "<h2>Display</h2>";
  h += "<label>Brightness % (10-100)</label>"
       "<input type=number name=bri min=10 max=100 value='" + String(briPct) + "'>";
  h += "<label>Quiet hours start (hour, 0-23)</label>"
       "<input type=number name=qhs min=0 max=23 value='" + String(qhStart) + "'>";
  h += "<label>Quiet hours end (hour, 0-23)</label>"
       "<input type=number name=qhe min=0 max=23 value='" + String(qhEnd) + "'>";
  h += "<label>Quiet mode</label><select name=qhm>";
  static const char* QM[] = {"Off (always on)", "Dim", "Sleep (tap to wake 10s)"};
  for (int i = 0; i < 3; i++)
    h += "<option value=" + String(i) + (qhMode == i ? " selected" : "") + ">" + QM[i] + "</option>";
  h += "</select><p class=muted>The daily do-not-disturb window for the screen.</p>";
  h += "<label>Orientation</label><select name=rot>"
       "<option value=1" + String(displayRotation == ROT_LANDSCAPE ? " selected" : "") + ">Normal (USB right)</option>"
       "<option value=3" + String(displayRotation == ROT_LANDSCAPE ? "" : " selected") + ">Flipped (USB left, 180&deg;)</option></select>";

  h += "<h2>Behaviour</h2>";
  h += "<label>Auto-cycle seconds (0 = off)</label>"
       "<input type=number name=cyc min=0 max=250 value='" + String(cycSec) + "'>"
       "<p class=muted>Seconds between automatic page advances; 0 keeps the current page.</p>";
  h += "<label class=rm><input type=checkbox name=ota_auto" + String(otaAuto ? " checked" : "") +
       ">Auto-install firmware updates</label>";
  h += "<p class=muted>When on, a newer release is installed automatically &mdash; at boot and on the "
       "~6-hourly check &mdash; instead of just being flagged. The device reboots to flash it; your "
       "settings are kept. Off by default.</p>";

  h += "<h2>Pages shown</h2>";
  h += "<label class=rm><input type=checkbox name=pg_clock" + String((pageMask & (1 << PAGE_CLOCK)) ? " checked" : "") + ">Clock page</label>";
  h += "<label class=rm><input type=checkbox name=pg_system" + String((pageMask & (1 << PAGE_SYSTEM)) ? " checked" : "") + ">System page</label>";
  h += "<p class=muted>USAGE / COST / MODELS / TREND appear automatically when their credential is set.</p>";

  h += "<p><button type=submit>Save</button> &nbsp; <a href=/>cancel</a></p></form>";
  return h;
}

// ---- handlers ----
inline void handleRoot()     { server.send(200, "text/html; charset=utf-8", statusHtml()); }
inline void handleConfig()   { server.send(200, "text/html; charset=utf-8", configHtml()); }
inline void handleSettings() { server.send(200, "text/html; charset=utf-8", settingsHtml()); }

// Apply device display/behaviour settings from the web form. Live — no reboot.
inline void handleSaveSettings() {
  using namespace display_pm;
  uint8_t nbri = briPct, nqs = qhStart, nqe = qhEnd, nqm = qhMode, ncyc = cycSec;
  if (server.hasArg("bri")) nbri = constrain((int)server.arg("bri").toInt(), 10, 100);
  if (server.hasArg("qhs")) nqs  = ((server.arg("qhs").toInt() % 24) + 24) % 24;
  if (server.hasArg("qhe")) nqe  = ((server.arg("qhe").toInt() % 24) + 24) % 24;
  if (server.hasArg("qhm")) nqm  = constrain((int)server.arg("qhm").toInt(), 0, 2);
  if (server.hasArg("cyc")) { int c = constrain((int)server.arg("cyc").toInt(), 0, 250); if (c > 0 && c < 5) c = 5; ncyc = c; }
  uint8_t nrot = (server.hasArg("rot") && server.arg("rot").toInt() == 3) ? ROT_LANDSCAPE_F : ROT_LANDSCAPE;
  uint16_t mask = pageMask;                          // checkbox present = on, absent = off
  if (server.hasArg("pg_clock"))  mask |=  (1 << PAGE_CLOCK);  else mask &= ~(1 << PAGE_CLOCK);
  if (server.hasArg("pg_system")) mask |=  (1 << PAGE_SYSTEM); else mask &= ~(1 << PAGE_SYSTEM);
  otaAuto = server.hasArg("ota_auto");               // checkbox present = auto-update on

  setBrightness(nbri);
  setQuietHours(nqs, nqe, nqm);
  setCycle(ncyc);
  displayRotation = nrot; tft.setRotation(nrot);
  bool pagesChanged = (mask != pageMask); pageMask = mask;
  Preferences p; p.begin(NVS_NS, false);
  p.putUChar("rot", displayRotation);
  p.putUShort("pgmask", pageMask);
  p.putBool("ota_auto", otaAuto);
  p.end();
  if (pagesChanged) buildPages();
  modeChanged = true;                                // redraw at the new rotation / page set

  server.sendHeader("Location", "/settings");
  server.send(303, "text/plain; charset=utf-8", "saved");
}

// Change the page shown on the device/sim (Wokwi has no touch; also handy on HW).
inline void handleNav() {
  int d = server.hasArg("d") ? server.arg("d").toInt() : 0;
  if (d) gotoPage(d);
  server.send(200, "text/plain; charset=utf-8", pageLabel());
}

// Tiny JSON endpoint the browser polls while a check runs (drives the spinner).
inline void handleOtaStatus() {
  String j = "{\"checking\":";
  j += g_otaChecking ? "true" : "false";
  j += ",\"status\":\"" + jsonEsc(otaStatusFriendly()) + "\"}";
  server.send(200, "application/json", j);
}

inline void handleSave() {
  Preferences p; p.begin(NVS_NS, false);
  String v;
  // "remove" checkboxes win; otherwise a non-empty field replaces, blank keeps.
  if (server.hasArg("admin_clear"))      { adminKey = "";     p.putString("admin_key", ""); }
  else if (server.hasArg("admin_key") && (v = server.arg("admin_key")).length()) { v.trim(); adminKey = v; p.putString("admin_key", v); }
  if (server.hasArg("oauth_clear")) {
    oauthToken = ""; oauthRefresh = "";
    p.putString("oauth_at", ""); p.putString("oauth_rt", ""); p.putLong64("oauth_exp", 0);
  } else {
    if (server.hasArg("oauth_at") && (v = server.arg("oauth_at")).length()) { v.trim(); oauthToken   = v; p.putString("oauth_at", v); }
    if (server.hasArg("oauth_rt") && (v = server.arg("oauth_rt")).length()) { v.trim(); oauthRefresh = v; p.putString("oauth_rt", v); }
  }
  // Timezone: custom field wins, else the dropdown preset.
  String tzv;
  if (server.hasArg("tz_custom") && server.arg("tz_custom").length())  tzv = server.arg("tz_custom");
  else if (server.hasArg("tz_preset") && server.arg("tz_preset").length()) tzv = server.arg("tz_preset");
  if (tzv.length()) { tzv.trim(); p.putString("tz", tzv); }
  if (server.hasArg("budget")) { budgetWeeklyUsd = server.arg("budget").toDouble(); p.putDouble("budget", budgetWeeklyUsd); }
  p.end();

  String h = head("Saved");
  h.replace("<style>", "<meta http-equiv=refresh content='6;url=/'><style>");
  h += "<h1>Saved — rebooting…</h1><p class=muted>Settings are preserved. Returning to status in a few seconds.</p>";
  server.send(200, "text/html; charset=utf-8", h);
  delay(800);
  ESP.restart();
}

inline void handleUpdate() {
  g_otaChecking   = true;     // set first so the very first /otastatus poll sees it
  g_otaWebRequest = true;
  String h = head("Update");
  h.replace("<style>", "<meta http-equiv=refresh content='10;url=/'><style>");
  h += "<h1>Update check requested</h1>";
  h += "<p>The device is checking GitHub. If a newer release exists it will flash and reboot "
       "(watch the screen) &mdash; <b>your settings are preserved</b>. Otherwise it keeps running. "
       "Returning to status…</p>";
  server.send(200, "text/html; charset=utf-8", h);
}

inline void begin() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (MDNS.begin("claudemon")) MDNS.addService("http", "tcp", 80);
  server.on("/", handleRoot);
  server.on("/config", handleConfig);
  server.on("/settings", handleSettings);
  server.on("/savesettings", HTTP_POST, handleSaveSettings);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/update", handleUpdate);
  server.on("/otastatus", handleOtaStatus);
  server.on("/nav", handleNav);
  server.onNotFound([] { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); });
  server.begin();
  Serial.printf("[web] config server at http://%s/ (claudemon.local)\n",
                WiFi.localIP().toString().c_str());
}

inline void handle() { server.handleClient(); }

} // namespace webcfg
#endif // !FAKE_DATA
