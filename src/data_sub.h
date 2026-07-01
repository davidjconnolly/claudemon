// data_sub.h — subscription rate-limit window source (OAuth token).
//
// Reads the unified 5h/7d utilization+reset headers. There is no documented
// read-only way to get these, so we try strategies cheapest-first and remember
// the first that actually returns the unified headers:
//
//   A) count_tokens probe  — free, separate limit; MAY return the headers without
//                            opening a session (the hoped-for zero-cost path).
//   B) messages probe      — costs ~1 token and opens/refreshes a 5h session;
//                            the proven fallback (this is what the daemon did).
//
// (A future strategy 0 — the undocumented usage endpoint the web page uses —
//  would slot in front of these once its shape is confirmed; see README.)
//
// The poller gates *when* we probe (adaptive cadence) to minimise session opens.
#pragma once

#include <Arduino.h>
#include "config.h"
#include "globals.h"
#include "net_http.h"
#include "net_oauth.h"

namespace sub {

// true after a 401/403 — the UI shows a "reconfigure token" hint.
static bool authError = false;
// remembered working strategy: -1 unknown, 0 count_tokens, 1 messages
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
  // Round UP (ceil) to match how claude.ai shows "% used": a usage bar shouldn't
  // under-report how close you are to a limit. (+0.999 = ceil for positive values.)
  int p = (v.indexOf('.') >= 0) ? (int)(f * 100.0f + 0.999f) : (int)f;
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

// Run one reading. Ensures a fresh token first. Returns true on success.
inline bool poll(SessionUsage& u) {
  if (oauthToken.isEmpty()) return false;
  oauth::refreshIfNeeded(oauthToken, oauthRefresh, oauthExpiresMs);

  auto tryStrategy = [&](int s) -> int {
    bool got = false;
    int code = (s == 0) ? probe(COUNT_TOKENS_PATH, COUNT_BODY, u, got)
                        : probe(MESSAGES_PATH,    MSG_BODY,   u, got);
    g_diag.subLastCode = code;
    if (code == 401 || code == 403) { authError = true; return -code; }
    authError = false;
    if (code == 200 && got) { working = s; return 1; }
    if (code == 429)        { working = s; return 1; }  // limited, but headers valid
    return 0;
  };

  auto runStrategies = [&]() -> bool {
    if (working >= 0) {                   // use the known-good strategy
      int r = tryStrategy(working);
      if (r == 1) return true;
      if (r < 0)  return false;           // auth error
      working = -1;                       // it stopped working; rediscover
    }
    for (int s = 0; s <= 1; s++) {        // discover cheapest working strategy
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

  u.authError = authError;                // surface for the UI banner
  return ok;
}

} // namespace sub
