// net_http.h — thin HTTPS helper around WiFiClientSecure + HTTPClient.
// Calls are sequential (the poller never overlaps them), so allocating a fresh
// TLS client per request keeps heap pressure bounded.
#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

namespace net {

struct Header { const char* name; String value; };

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
  HTTPClient http;
  http.setTimeout(15000);
  http.setReuse(false);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return -1;

  for (int i = 0; i < nHdrs; i++) http.addHeader(hdrs[i].name, hdrs[i].value);
  if (collectNames && nCollect > 0) http.collectHeaders(collectNames, nCollect);

  int code;
  if (strcmp(method, "POST") == 0)
    code = http.POST((uint8_t*)body.c_str(), body.length());
  else
    code = http.GET();

  if (code > 0) outBody = http.getString();
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
