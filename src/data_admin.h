// data_admin.h — Console Usage & Cost Admin API source (admin key).
// Fully documented endpoints; this is the reliable path. Each call is small
// and the poller staggers them to respect the ~1/min recommendation.
//
//   today usage   -> usage_report/messages, 1d bucket, [start-of-day, now]
//   hourly spark  -> usage_report/messages, 1h buckets, last 24h
//   week tokens   -> usage_report/messages, 1d buckets, last 7d
//   cost + models -> cost_report, 1d buckets, [start-of-month, now], by model
//
// Token/amount field names are centralised so they're easy to correct against a
// real response (see README — validate parsing once on real data).
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "globals.h"
#include "net_http.h"

namespace admin {

inline String base(const char* path) { return String("https://") + ANTHROPIC_HOST + path; }

inline int callGet(const String& url, JsonDocument& doc) {
  net::Header hdrs[] = {
    {"x-api-key", adminKey},
    {"anthropic-version", ANTHROPIC_VERSION_HDR},
    {"User-Agent", "CYD-Claudemon/" CLAUDEMON_VERSION},
  };
  String body;
  int code = net::get(url, hdrs, 3, body);
  g_diag.adminLastCode  = code;
  g_diag.adminLastTryMs = millis();
  if (code == 200) {
    DeserializationError e = deserializeJson(doc, body);
    if (e) {
      Serial.printf("[admin] json: %s\n", e.c_str());
      snprintf(g_diag.adminLastErr, sizeof g_diag.adminLastErr, "json: %s", e.c_str());
      return -2;
    }
    g_diag.adminLastErr[0] = 0;
    g_diag.adminLastOkMs = millis();
  } else {
    Serial.printf("[admin] GET %d: %s\n", code, body.c_str());
    // Prefer the human-readable error.message ("invalid x-api-key") over raw JSON.
    JsonDocument ed; const char* msg = nullptr;
    if (!deserializeJson(ed, body)) msg = ed["error"]["message"] | (const char*)nullptr;
    snprintf(g_diag.adminLastErr, sizeof g_diag.adminLastErr, "%s", msg ? msg : body.c_str());
  }
  return code;
}

// Sum all token kinds in one usage result row.
inline uint64_t rowTokens(JsonObjectConst r) {
  uint64_t in  = r["uncached_input_tokens"]    | (uint64_t)0;
  uint64_t out = r["output_tokens"]            | (uint64_t)0;
  uint64_t cr  = r["cache_read_input_tokens"]  | (uint64_t)0;
  uint64_t cc  = r["cache_creation_input_tokens"] | (uint64_t)0;
  if (cc == 0 && r["cache_creation"].is<JsonObjectConst>()) {
    JsonObjectConst o = r["cache_creation"];
    cc = (o["ephemeral_5m_input_tokens"] | (uint64_t)0)
       + (o["ephemeral_1h_input_tokens"] | (uint64_t)0);
  }
  return in + out + cr + cc;
}

// --- Pagination + date helpers. The usage/cost API returns only ~7 time buckets
//     per page (with a next_page token); reading just the first page silently
//     drops everything older/newer, so each fetch below must follow pagination. ---

// Minimal URL-encode of a next_page token used as a query value (base64 may
// contain + / =).
inline String pageEncode(const String& s) {
  String o; o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '+')      o += "%2B";
    else if (c == '/') o += "%2F";
    else if (c == '=') o += "%3D";
    else               o += c;
  }
  return o;
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
// Day index (days since epoch) from an ISO-8601 "YYYY-MM-DD…" string; -1 on fail.
inline long isoDayIndex(const char* s) {
  int y, m, d;
  if (!s || sscanf(s, "%d-%d-%d", &y, &m, &d) != 3) return -1;
  return civilDays(y, m, d);
}

// Fetch every page of a usage/cost query, invoking onBucket(bucket) for each time
// bucket across all pages. Returns true if at least the first page returned 200.
template <typename Fn>
inline bool fetchPaged(const String& baseUrl, Fn onBucket) {
  String page; bool ok = false;
  for (int guard = 0; guard < 24; guard++) {       // hard cap on pages
    String url = baseUrl;
    if (page.length()) url += "&page=" + pageEncode(page);
    JsonDocument doc;
    if (callGet(url, doc) != 200) return ok;
    ok = true;
    for (JsonObjectConst b : doc["data"].as<JsonArrayConst>()) onBucket(b);
    bool more = doc["has_more"] | false;
    const char* np = doc["next_page"] | (const char*)nullptr;
    if (more && np && np[0]) page = np; else break;
  }
  return ok;
}

// limit = max buckets per page (API caps it at 31); request the whole span in one
// page where it fits, and fetchPaged() still follows next_page if it doesn't.
inline String usageUrl(time_t startT, time_t endT, const char* bucket,
                       const char* group = nullptr, int limit = 0) {
  String u = base(USAGE_REPORT_PATH);
  u += "?starting_at=" + net::iso8601(startT);
  u += "&ending_at="  + net::iso8601(endT);
  u += "&bucket_width="; u += bucket;
  if (group) { u += "&group_by[]="; u += group; }
  if (limit > 0) { u += "&limit="; u += limit; }
  return u;
}

// --- Today's tokens + cache split (1 day bucket). ---
inline bool fetchToday(CostUsage& c) {
  time_t now = time(nullptr);
  JsonDocument doc;
  if (callGet(usageUrl(net::startOfDayUtc(now), now, "1d"), doc) != 200) return false;

  uint64_t in = 0, out = 0, cr = 0, cc = 0;
  for (JsonObjectConst b : doc["data"].as<JsonArrayConst>())
    for (JsonObjectConst r : b["results"].as<JsonArrayConst>()) {
      in  += r["uncached_input_tokens"]    | (uint64_t)0;
      out += r["output_tokens"]            | (uint64_t)0;
      cr  += r["cache_read_input_tokens"]  | (uint64_t)0;
      cc  += r["cache_creation_input_tokens"] | (uint64_t)0;
    }
  c.inputToday = in; c.outputToday = out;
  c.cacheReadToday = cr; c.cacheCreateToday = cc;
  c.tokensToday = in + out + cr + cc;
  uint64_t denom = cr + in;                    // cache hit ratio of input
  c.cacheHitPct = denom ? (int)((cr * 100) / denom) : 0;
  c.valid = true; c.lastUpdated = millis();
  return true;
}

// --- Hourly token totals for the last 24h -> sparkline (values in k-tokens). ---
inline bool fetchHourlySpark(Sparkline& s) {
  time_t now = time(nullptr);
  uint16_t buf[24] = {0};
  int i = 0;
  bool ok = fetchPaged(usageUrl(now - 24 * 3600, now, "1h", nullptr, 24), [&](JsonObjectConst b) {
    uint64_t tot = 0;
    for (JsonObjectConst r : b["results"].as<JsonArrayConst>()) tot += rowTokens(r);
    if (i < 24) { uint64_t k = tot / 1000; buf[i++] = (k > 65535) ? 65535 : (uint16_t)k; }
  });
  if (!ok) return false;
  for (int k = 0; k < 24; k++) s.hourly[k] = buf[k];
  s.valid = true;
  return true;
}

// --- Last 7 days of tokens -> weekly total. ---
inline bool fetchWeekTokens(CostUsage& c) {
  time_t now = time(nullptr);
  uint64_t tot = 0;
  bool ok = fetchPaged(usageUrl(now - 7 * 86400, now, "1d", nullptr, 8), [&](JsonObjectConst b) {
    for (JsonObjectConst r : b["results"].as<JsonArrayConst>()) tot += rowTokens(r);
  });
  if (!ok) return false;
  c.tokensWeek = tot;
  return true;
}

// --- Cost report (month, by model): today/week/month $, per-model, daily spark.
//     Amounts are decimal strings in CENTS per the docs -> /100 for USD. ---
inline bool fetchCostMonth(CostUsage& c, Sparkline& s) {
  time_t now = time(nullptr);
  struct tm g; gmtime_r(&now, &g);
  int monthDays = g.tm_mday;                 // buckets in this month (1st..today) = fromEnd < monthDays
  // Query far enough back that "this week" always has a full rolling 7 days, even
  // early in a month (the cost_report is otherwise month-bounded and undercounts
  // the week on the 1st-7th). Month total / per-model stay scoped to this month.
  long daysBack = (monthDays - 1 > 7) ? (monthDays - 1) : 7;
  time_t rangeStart = net::startOfDayUtc(now) - daysBack * 86400;

  long todayIdx = civilDays(g.tm_year + 1900, g.tm_mon + 1, g.tm_mday);

  String url = base(COST_REPORT_PATH);
  url += "?starting_at=" + net::iso8601(rangeStart);
  url += "&ending_at="  + net::iso8601(now);
  url += "&group_by[]=description";
  url += "&limit=31";   // a full month in one page (API max); fetchPaged covers overflow

  double month = 0, week = 0, today = 0;
  // Per-model accumulation (top 6 by cost), this billing month only.
  struct Acc { char name[24]; double usd; uint64_t tok; };
  Acc acc[12]; int nAcc = 0;
  uint16_t daily[7] = {0};

  // Bucket each day by its real date (daysAgo), not list position — robust to
  // pagination and to any missing days the API may omit.
  bool ok = fetchPaged(url, [&](JsonObjectConst b) {
    long bidx = isoDayIndex(b["starting_at"] | (const char*)nullptr);
    long daysAgo = (bidx < 0) ? -1 : (todayIdx - bidx);
    bool inMonth = (daysAgo >= 0 && daysAgo < monthDays);
    double bucketUsd = 0;
    for (JsonObjectConst r : b["results"].as<JsonArrayConst>()) {
      double usd = (atof((r["amount"] | "0"))) / 100.0;   // cents -> USD
      bucketUsd += usd;
      if (daysAgo < 0 || daysAgo >= 7) continue;   // model breakdown = rolling 7 days
      const char* model = r["model"] | (r["description"] | "other");
      int f = -1;
      for (int k = 0; k < nAcc; k++) if (strncmp(acc[k].name, model, sizeof(acc[k].name)) == 0) { f = k; break; }
      if (f < 0 && nAcc < 12) { f = nAcc++; strncpy(acc[f].name, model, sizeof(acc[f].name) - 1); acc[f].name[sizeof(acc[f].name)-1]=0; acc[f].usd = 0; acc[f].tok = 0; }
      if (f >= 0) acc[f].usd += usd;
    }
    if (inMonth)                     month += bucketUsd;
    if (daysAgo >= 0 && daysAgo < 7)  week += bucketUsd;   // true rolling 7-day window
    if (daysAgo == 0)                today += bucketUsd;
    if (daysAgo >= 0 && daysAgo < 7) {                     // daily spark: last 7 days, cents
      long cents = (long)(bucketUsd * 100);
      if (cents < 0) cents = 0; if (cents > 65535) cents = 65535;
      daily[6 - daysAgo] = (uint16_t)cents;
    }
  });
  if (!ok) return false;

  c.costMonthUsd = month; c.costWeekUsd = week; c.costTodayUsd = today;

  // Top models by cost into c.models[6].
  for (int a = 0; a < nAcc; a++)
    for (int b2 = a + 1; b2 < nAcc; b2++)
      if (acc[b2].usd > acc[a].usd) { Acc t = acc[a]; acc[a] = acc[b2]; acc[b2] = t; }
  c.modelCount = min(nAcc, 6);
  for (int k = 0; k < c.modelCount; k++) {
    strncpy(c.models[k].name, acc[k].name, sizeof(c.models[k].name) - 1);
    c.models[k].name[sizeof(c.models[k].name)-1] = 0;
    c.models[k].costUsd = acc[k].usd;
    c.models[k].tokens  = acc[k].tok;
  }

  // billing period end ≈ end of this month
  static const int dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int y = g.tm_year + 1900, dm = dim[g.tm_mon];
  if (g.tm_mon == 1 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) dm = 29;
  // Midnight UTC of the 1st of this month (without timegm, unreliable on ESP32).
  time_t monthStart = net::startOfDayUtc(now) - (long)(monthDays - 1) * 86400;
  time_t monthEnd = monthStart + (long)dm * 86400;
  c.periodEndSec = (long)(monthEnd - now);

  for (int k = 0; k < 7; k++) s.daily[k] = daily[k];
  s.valid = true;
  c.valid = true; c.lastUpdated = millis();
  return true;
}

} // namespace admin
