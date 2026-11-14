// net_http.h — thin HTTPS helper around WiFiClientSecure + HTTPClient.
// Calls are sequential (the poller never overlaps them), so allocating a fresh
// TLS client per request keeps heap pressure bounded.
#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

namespace net {

struct Header { const char* name; String value; };

// Response-body read limits. IDLE is the longest gap between bytes; TOTAL caps
// the whole body however slowly it trickles in. Our bodies are a few KB of JSON
// that arrive in well under a second.
static constexpr unsigned long BODY_IDLE_MS  = 8000;
static constexpr unsigned long BODY_TOTAL_MS = 15000;
// Default is 120 s, and HTTPClient's connect/read timeouts don't reach it.
static constexpr unsigned long TLS_HANDSHAKE_S = 10;

// Read a response body into `out` with a hard deadline. Returns false on a stall,
// on the total cap, or when the peer closes before `expected` bytes arrive.
//
// This replaces HTTPClient::getString(), which loops on
// `while (connected() && bytes remain) delay(1);` with NO timeout. A peer that
// goes silent mid-body without a FIN/RST leaves connected() true. getString()
// then spins until lwIP's first TCP keepalive probe at 2 h (TCP_KEEPIDLE_DEFAULT)
// draws a RST. That froze the whole UI task (screen, touch, web server and the
// WiFi watchdog) for exactly 2 h on 2026-11-13, 20:38 to 22:38, with no reboot.
//
// `expected` is the Content-Length, or -1 when the body is delimited by the
// connection closing (what we get under HTTP/1.0 with no length header).
template <typename S>
inline bool readBody(S& s, long expected, String& out,
                     unsigned long idleMs = BODY_IDLE_MS,
                     unsigned long totalMs = BODY_TOTAL_MS) {
  out = "";
  char buf[257];
  long got = 0;
  unsigned long start = millis(), lastData = start;
  while (expected < 0 || got < expected) {
    unsigned long now = millis();
    if (now - lastData >= idleMs || now - start >= totalMs) return false;
    int n = s.available();
    if (n > 0) {
      if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
      if (expected > 0 && n > expected - got) n = (int)(expected - got);
      int r = s.read((uint8_t*)buf, (size_t)n);
      if (r > 0) {
        buf[r] = '\0';                  // bodies are JSON text, so no embedded NULs
        out += buf;
        got += r;
        lastData = millis();
        continue;
      }
    } else if (!s.connected()) {
      return expected < 0;              // close ends an unsized body; a sized one is short
    }
    delay(2);
  }
  return true;
}

// Performs an HTTPS request. Returns the HTTP status code, or a negative
// HTTPClient error. Response body -> outBody. Optionally collects named
// response headers into `collected` (same order as collectNames).
inline int request(const char* method, const String& url,
                   const Header* hdrs, int nHdrs,
                   const String& body, String& outBody,
                   const char** collectNames = nullptr, int nCollect = 0,
                   String* collected = nullptr) {
  WiFiClientSecure client;
  client.setInsecure();                 // see README: TLS is not pinned
  client.setHandshakeTimeout(TLS_HANDSHAKE_S);
  HTTPClient http;
  // These bound how long loop() is blocked, and therefore how long touch input
  // is dead: every request runs on the UI task, and a tap that starts and ends
  // inside a request is never sampled. A healthy call to the API answers in
  // ~1-2 s, so the old 15 s ceiling bought nothing and cost up to 15 s of
  // unresponsive screen per stalled connection (the poller runs several per
  // minute — that is the 15-30 s "frozen stylus" window). Keep them tight and
  // let the next poll retry instead.
  http.setConnectTimeout(4000);
  http.setTimeout(8000);
  http.setReuse(false);
  // HTTP/1.0 rules out a chunked reply, so the body is either Content-Length
  // bytes or everything up to the close, and readBody() can read it raw.
  // (Anthropic answers 1.0 with a close-delimited body; GitHub sends a length.)
  http.useHTTP10(true);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return -1;

  for (int i = 0; i < nHdrs; i++) http.addHeader(hdrs[i].name, hdrs[i].value);
  if (collectNames && nCollect > 0) http.collectHeaders(collectNames, nCollect);

  int code;
  if (strcmp(method, "POST") == 0)
    code = http.POST((uint8_t*)body.c_str(), body.length());
  else
    code = http.GET();

  // GET()/POST() already parsed the headers, so collecting them below still
  // works after a timed-out body.
  if (code > 0 && !readBody(client, http.getSize(), outBody)) {
    outBody = "";
    code = HTTPC_ERROR_READ_TIMEOUT;    // -11: callers already treat it as a network blip
  }
  if (collectNames && collected)
    for (int i = 0; i < nCollect; i++) collected[i] = http.header(collectNames[i]);

  http.end();
  return code;
}

inline int get(const String& url, const Header* hdrs, int nHdrs, String& outBody) {
  return request("GET", url, hdrs, nHdrs, "", outBody);
}

inline int post(const String& url, const Header* hdrs, int nHdrs,
                const String& body, String& outBody) {
  return request("POST", url, hdrs, nHdrs, body, outBody);
}

// --- Time helpers (NTP must be configured first) ---

// ISO-8601 UTC, e.g. 2026-06-28T00:00:00Z, for an absolute epoch.
inline String iso8601(time_t t) {
  struct tm g; gmtime_r(&t, &g);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &g);
  return String(buf);
}

// Days since 1970-01-01 for a civil UTC date (Howard Hinnant's algorithm).
inline long civilDays(int y, int m, int d) {
  y -= (m <= 2);
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097L + doe - 719468L;
}

// "YYYY-MM-DDTHH:MM:SS[.frac][Z|+00:00]" -> unix epoch seconds; 0 on failure.
// Fractional seconds and the offset are ignored — the endpoints we read always
// return UTC (+00:00), so no offset math is needed (mktime/timegm are unreliable
// on ESP32, hence the civil-days route).
inline long parseIso8601Utc(const char* s) {
  int y, mo, d, h, mi, se;
  if (!s || sscanf(s, "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
  return civilDays(y, mo, d) * 86400L + h * 3600L + mi * 60L + se;
}

// Midnight UTC of the day containing `t`.
inline time_t startOfDayUtc(time_t t) { return t - (t % 86400); }

} // namespace net
