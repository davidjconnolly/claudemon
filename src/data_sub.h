// data_sub.h — subscription rate-limit window source (OAuth token).
//
// Reads the 5h/7d utilization windows (plus the model-scoped weekly window and
// plan label on Max plans). There is no documented read-only way to get these,
// so we try strategies cheapest-first and remember the first that works:
//
//   0) usage endpoint      — the (undocumented) GET the claude.ai usage page
//                            reads. Free, and does NOT open/refresh a 5h
//                            session. Also the only source of the model-scoped
//                            weekly window (e.g. "Fable"). Shape confirmed
//                            live 2026-07; guarded so a shape change just
//                            falls through to the probes below.
//   1) count_tokens probe  — free, separate limit; MAY return the unified
//                            headers without opening a session.
//   2) messages probe      — costs ~1 token and opens/refreshes a 5h session;
//                            the proven fallback (this is what the daemon did).
//
// The poller gates *when* we probe (adaptive cadence) to minimise session opens.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include "config.h"
#include "globals.h"
#include "net_http.h"
#include "net_oauth.h"

namespace sub {

// true after a 401/403 — the UI shows a "reconfigure token" hint.
static bool authError = false;
// remembered working strategy: -1 unknown, 0 usage endpoint, 1 count_tokens, 2 messages
static int  working = -1;

static const char* RL[5] = {
  "anthropic-ratelimit-unified-5h-utilization",
  "anthropic-ratelimit-unified-5h-reset",
  "anthropic-ratelimit-unified-7d-utilization",
  "anthropic-ratelimit-unified-7d-reset",
  "anthropic-ratelimit-unified-status",
};

inline int pctFromFraction(const String& v) {
  if (v.isEmpty()) return -1;
  float f = v.toFloat();
  // Match claude.ai's usage page, which rounds the utilization fraction to the
  // nearest whole percent (round half up). Ceil (+0.999) over-reported by 1% on
  // any fractional value — that's why the device read a point high. Any residual
  // ±1 vs the live dashboard is polling latency (we sample this header ~once a
  // minute while the dashboard updates continuously), not a rounding mismatch.
  int p = (v.indexOf('.') >= 0) ? (int)(f * 100.0f + 0.5f) : (int)f;
  return constrain(p, 0, 100);
}
// The reset headers are absolute unix-epoch seconds. Store the epoch itself (not
// a countdown): a countdown frozen between polls makes an absolute "resets at"
// display drift forward with the wall clock until the next poll re-syncs it.
inline long epochFromHeader(const String& v) {
  if (v.isEmpty()) return 0;
  long e = atol(v.c_str());
  return e > 0 ? e : 0;
}

// Returns true if the response carried the unified headers.
inline bool parseHeaders(String* h, SessionUsage& u) {
  int s = pctFromFraction(h[0]);
  int w = pctFromFraction(h[2]);
  if (s < 0 && w < 0 && h[4].isEmpty()) return false;
  if (s >= 0) u.sessionPct = s;
  if (w >= 0) u.weeklyPct  = w;
  long sa = epochFromHeader(h[1]); if (sa > 0) u.sessionResetAt = sa;
  long wa = epochFromHeader(h[3]); if (wa > 0) u.weeklyResetAt  = wa;
  u.limited     = (h[4] == "limited");
  u.valid       = true;
  u.lastUpdated = millis();
  return true;
}

// One probe attempt. Returns HTTP status; fills `u` if unified headers present.
inline int probe(const char* path, const String& body, SessionUsage& u, bool& gotHeaders) {
  gotHeaders = false;
  String url = String("https://") + ANTHROPIC_HOST + path;
  net::Header hdrs[] = {
    {"Authorization",    String("Bearer ") + oauthToken},
    {"anthropic-version", ANTHROPIC_VERSION_HDR},
    {"Content-Type",      "application/json"},
  };
  String collected[5], respBody;
  int code = net::request("POST", url, hdrs, 3, body, respBody, RL, 5, collected);
  if (code == 200 || code == 429) gotHeaders = parseHeaders(collected, u);
  return code;
}

static const char* COUNT_BODY = "{\"model\":\"claude-haiku-4-5\",\"messages\":[{\"role\":\"user\",\"content\":\".\"}]}";
static const char* MSG_BODY   = "{\"model\":\"claude-haiku-4-5\",\"max_tokens\":1,\"messages\":[{\"role\":\"user\",\"content\":\".\"}]}";

// Strategy 0: the usage endpoint. Response shape (confirmed live 2026-07):
// top-level five_hour/seven_day objects (utilization is a PERCENT float, not a
// 0..1 fraction like the headers) plus a limits[] array. Model-scoped weekly
// buckets (the "Fable" bar on Max plans) appear ONLY in limits[] as
// kind=="weekly_scoped" with scope.model.display_name — the top-level
// seven_day_<model> fields were all null on a plan that shows a Fable bar.
inline int probeUsage(SessionUsage& u, bool& got) {
  got = false;
  String url = String("https://") + ANTHROPIC_HOST + OAUTH_USAGE_PATH;
  net::Header hdrs[] = {
    {"Authorization",  String("Bearer ") + oauthToken},
    {"anthropic-beta", OAUTH_BETA_HDR},
  };
  String body;
  int code = net::get(url, hdrs, 2, body);
  if (code != 200) return code;

  JsonDocument doc;
  if (deserializeJson(doc, body.c_str())) return code;  // got stays false -> fall through

  bool sawSession = false, sawWeekly = false, sawScoped = false, lim = false;
  for (JsonObjectConst l : doc["limits"].as<JsonArrayConst>()) {
    const char* kind = l["kind"] | "";
    int  pct = l["percent"] | -1;
    long rst = net::parseIso8601Utc(l["resets_at"] | "");
    if (pct < 0) continue;
    pct = constrain(pct, 0, 100);
    if (strcmp(kind, "session") == 0) {
      u.sessionPct = pct; if (rst > 0) u.sessionResetAt = rst; sawSession = true;
    } else if (strcmp(kind, "weekly_all") == 0) {
      u.weeklyPct = pct;  if (rst > 0) u.weeklyResetAt = rst;  sawWeekly = true;
    } else if (strcmp(kind, "weekly_scoped") == 0) {
      const char* name = l["scope"]["model"]["display_name"] | "Model";
      snprintf(u.scopedLabel, sizeof(u.scopedLabel), "%s", name);
      u.scopedPct = pct;  if (rst > 0) u.scopedResetAt = rst;  sawScoped = true;
    }
    if ((l["percent"] | 0) >= 100) lim = true;   // pre-clamp value
  }
  // Robustness: fill any window missing from limits[] from the top-level
  // five_hour/seven_day objects (utilization is already a percent there) —
  // per-window, so a partial limits[] can't pass a stale window off as fresh.
  if (!sawSession) {
    JsonObjectConst fh = doc["five_hour"];
    if (!fh.isNull()) {
      int p = constrain((int)(fh["utilization"].as<float>() + 0.5f), 0, 100);
      u.sessionPct = p; if (p >= 100) lim = true;
      long r = net::parseIso8601Utc(fh["resets_at"] | ""); if (r > 0) u.sessionResetAt = r;
      sawSession = true;
    }
  }
  if (!sawWeekly) {
    JsonObjectConst sd = doc["seven_day"];
    if (!sd.isNull()) {
      int p = constrain((int)(sd["utilization"].as<float>() + 0.5f), 0, 100);
      u.weeklyPct = p; if (p >= 100) lim = true;
      long r = net::parseIso8601Utc(sd["resets_at"] | ""); if (r > 0) u.weeklyResetAt = r;
      sawWeekly = true;
    }
  }
  if (!sawSession && !sawWeekly) return code;    // unrecognised shape -> fall through

  // The endpoint has no explicit "limited" flag like the unified-status header;
  // a window at 100% is the same signal. (severity values beyond "normal" are
  // unconfirmed, so we don't key off them.)
  u.limited     = lim;
  u.hasScoped   = sawScoped;                     // clears if the plan loses the bar
  u.valid       = true;
  u.lastUpdated = millis();
  got = true;
  return code;
}

// --- Plan label (e.g. "MAX 5x") from the profile endpoint. -----------------

// "default_claude_max_5x" + "claude_max" -> "MAX 5x". Falls back to the raw
// tier string if the shape is unrecognised, so a new plan still shows something.
inline void planLabel(const char* tier, const char* type, char* out, size_t n) {
  const char* base = strstr(type, "max")  ? "MAX"
                   : strstr(type, "pro")  ? "PRO"
                   : strstr(type, "free") ? "FREE" : "";
  const char* mult = nullptr;                    // trailing "_<digits>x" segment
  const char* seg  = strrchr(tier, '_');
  if (seg && isdigit((unsigned char)seg[1])) {
    const char* p = seg + 1;
    while (isdigit((unsigned char)*p)) p++;
    if (*p == 'x' && p[1] == '\0') mult = seg + 1;
  }
  if (base[0] && mult) snprintf(out, n, "%s %s", base, mult);
  else if (base[0])    snprintf(out, n, "%s", base);
  else                 snprintf(out, n, "%s", tier);
}

// Fetch the plan label once a day (retry hourly on failure). Cheap single GET;
// purely cosmetic, so failures never affect the usage reading.
inline void refreshPlan(SessionUsage& u) {
  static unsigned long lastMs = 0, intervalMs = 0;   // 0 = due now
  if (intervalMs != 0 && millis() - lastMs < intervalMs) return;
  lastMs = millis();
  intervalMs = 60UL * 60UL * 1000UL;                 // assume failure; 1h retry

  String url = String("https://") + ANTHROPIC_HOST + OAUTH_PROFILE_PATH;
  net::Header hdrs[] = {
    {"Authorization",  String("Bearer ") + oauthToken},
    {"anthropic-beta", OAUTH_BETA_HDR},
  };
  String body;
  if (net::get(url, hdrs, 2, body) != 200) return;
  JsonDocument doc;
  if (deserializeJson(doc, body.c_str())) return;
  const char* tier = doc["organization"]["rate_limit_tier"]   | "";
  const char* type = doc["organization"]["organization_type"] | "";
  if (!tier[0] && !type[0]) return;
  planLabel(tier, type, u.plan, sizeof(u.plan));
  intervalMs = 24UL * 60UL * 60UL * 1000UL;          // success; re-check daily
}

// Run one reading. Ensures a fresh token first. Returns true on success.
inline bool poll(SessionUsage& u) {
  if (oauthToken.isEmpty()) return false;
  oauth::refreshIfNeeded(oauthToken, oauthRefresh, oauthExpiresMs);

  auto tryStrategy = [&](int s) -> int {
    bool got = false;
    int code = (s == 0) ? probeUsage(u, got)
             : (s == 1) ? probe(COUNT_TOKENS_PATH, COUNT_BODY, u, got)
                        : probe(MESSAGES_PATH,    MSG_BODY,   u, got);
    g_diag.subLastCode = code;
    if (code == 401 || code == 403) { authError = true; return -code; }
    authError = false;
    // `got` covers both a parsed 200 and (for the probes) a 429 whose unified
    // headers were still present.
    if (got) { working = s; return 1; }
    return 0;
  };

  auto runStrategies = [&]() -> bool {
    if (working >= 0) {                   // use the known-good strategy
      int r = tryStrategy(working);
      if (r == 1) return true;
      if (r < 0)  return false;           // auth error
      working = -1;                       // it stopped working; rediscover
    }
    for (int s = 0; s <= 2; s++) {        // discover cheapest working strategy
      int r = tryStrategy(s);
      if (r == 1) return true;
      if (r < 0)  return false;
    }
    return false;
  };

  bool ok = runStrategies();
  // A 401/403 means the access token was rejected even though our expiry clock
  // thought it was fine (the token can be rotated out from under us by the
  // Claude Code that shares this login). Force one refresh and retry before
  // giving up, so the device self-heals whenever its refresh token is still good.
  if (!ok && authError && oauth::refresh(oauthToken, oauthRefresh, oauthExpiresMs))
    ok = runStrategies();

  if (ok) refreshPlan(u);                 // cosmetic; never affects the reading

  u.authError = authError;                // surface for the UI banner
  return ok;
}

} // namespace sub
