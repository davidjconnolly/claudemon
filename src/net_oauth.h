// net_oauth.h — on-device refresh of the Claude Code subscription OAuth token.
//
// The old daemon never refreshed; it re-read ~/.claude/.credentials.json which
// Claude Code keeps fresh. Daemon-free, the device must refresh itself: when the
// access token is near expiry we POST the refresh token to the OAuth endpoint and
// persist the new pair.
//
// NOTE: this whole subscription path is UNDOCUMENTED (endpoint, client id, and
// the grant shape may change). It degrades gracefully — on failure the Session
// page shows a "reconfigure token" hint and the rest of the firmware is fine.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "config.h"
#include "net_http.h"
#include "applog.h"

namespace oauth {

// Refresh `accessToken` in place using `refreshToken`. Returns true on success
// and updates accessToken/refreshToken/expiresMs + persists them to NVS.
inline bool refresh(String& accessToken, String& refreshToken, int64_t& expiresMs) {
  if (refreshToken.isEmpty()) return false;

  JsonDocument req;
  req["grant_type"]    = "refresh_token";
  // std::string so ArduinoJson copies the token into the document — a bare
  // c_str() would store an unowned pointer into refreshToken's buffer.
  req["refresh_token"] = std::string(refreshToken.c_str());
  req["client_id"]     = CLAUDE_CODE_CLIENT_ID;
  // serializeJson truncates silently on overflow -> malformed JSON and an
  // inscrutable refresh failure; check the measured size first.
  char body[768];
  if (measureJson(req) >= sizeof(body)) {
    applog::add("oauth: refresh request too large");
    return false;
  }
  serializeJson(req, body, sizeof(body));

  net::Header hdrs[] = {{"Content-Type", "application/json"}};
  String resp;
  int code = net::post(OAUTH_TOKEN_URL, hdrs, 1, body, resp);
  if (code != 200) {
    applog::add("oauth: refresh failed HTTP %d", code);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, resp.c_str())) return false;
  String newAccess  = doc["access_token"]  | "";
  if (newAccess.isEmpty()) return false;
  String newRefresh = doc["refresh_token"] | "";
  long   expiresIn  = doc["expires_in"]    | 0;   // seconds

  accessToken = newAccess;
  if (!newRefresh.isEmpty()) refreshToken = newRefresh;
  if (expiresIn > 0) expiresMs = (int64_t)time(nullptr) * 1000LL + (int64_t)expiresIn * 1000LL;

  Preferences p; p.begin(NVS_NS, false);
  p.putString("oauth_at", accessToken);
  p.putString("oauth_rt", refreshToken);
  p.putLong64("oauth_exp", expiresMs);
  p.end();
  applog::add("oauth: token refreshed");
  return true;
}

// Refresh if the token expires within `skewMs` (default 5 min).
inline bool refreshIfNeeded(String& accessToken, String& refreshToken,
                            int64_t& expiresMs, int64_t skewMs = 300000LL) {
  if (accessToken.isEmpty()) return false;
  int64_t nowMs = (int64_t)time(nullptr) * 1000LL;
  if (expiresMs > 0 && nowMs + skewMs < expiresMs) return true;  // still valid
  return refresh(accessToken, refreshToken, expiresMs);
}

} // namespace oauth
