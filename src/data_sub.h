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
#include "applog.h"

namespace sub {

// true after a 401/403 — the UI shows a "reconfigure token" hint.
static bool authError = false;
// remembered working strategy: -1 unknown, 0 usage endpoint, 1 count_tokens, 2 messages
static int  working = -1;
// While downgraded to a probe strategy, re-try the usage endpoint every N polls
// (~10 min at the 60 s cadence). Without this a single transient failure of the
// endpoint downgrades the device permanently: the known-good probe succeeds on
// every later poll, strategy 0 is never re-tried, and the model-scoped bar —
// which only strategy 0 carries — stays hidden until a reboot.
static constexpr uint8_t UPGRADE_RETRY_POLLS = 10;
static uint8_t downgradedPolls = 0;

// Hysteresis on the DOWNGRADE side: a known-good strategy must miss this many
// polls in a row before we abandon it. A single dropped TLS read / transient
// 5xx on strategy 0 would otherwise wipe the model-scoped ("Fable") bar and
// lock in an UPGRADE_RETRY_POLLS blackout, so one blip blanked the bar for
// ~10 min. Holding a few polls lets the next good read keep the bar alive.
static constexpr uint8_t DOWNGRADE_AFTER_MISSES = 3;
static uint8_t knownGoodMisses = 0;

// When strategy 0 stays unavailable long enough to downgrade (a 429 storm on the
// shared OAuth token, in practice), the header probes that take over carry NO
// model-scoped window — so the Fable bar would blank. But that window barely
// moves over the ~10-30 min a downgrade lasts, so we retain the last-known scoped
// reading and only drop it once it is this stale. Bounds the "frozen bar forever"
// risk that the old unconditional clear was guarding against; an authoritative
// strategy-0 200 that lacks the window still clears it immediately (see poll()).
static constexpr unsigned long SCOPED_STALE_MS = 2UL * 60 * 60 * 1000;   // 2 h

// Usage-miss logging is deduped by the HTTP code, not by "first miss of a
// streak": a streak whose code changes mid-way (e.g. 500 then -11 then the
// downgrade) must record BOTH codes — that transition is exactly the blind spot
// this logging closes — while a run of identical codes still writes one line so
// a sustained outage can't flood the small flash ring buffer. Reset on any
// success (re-arms for the next incident).
static int lastMissCode = 0;
inline bool newMissCode(int code) {
  if (code == lastMissCode) return false;
  lastMissCode = code;
  return true;
}

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
  // The headers carry no model-scoped window. Rather than blank the Fable bar on
  // every probe reading — a transient strategy-0 429 storm downgrades us here for
  // ~10-30 min, and the weekly-scoped window barely moves in that time — retain
  // the last-known value and only drop it once it goes stale (SCOPED_STALE_MS).
  // That bounds the "frozen forever" case the old unconditional clear guarded
  // against, while surviving the downgrades that were hiding the bar for real.
  if (u.hasScoped && millis() - u.scopedUpdatedAt >= SCOPED_STALE_MS) {
    u.hasScoped = false;
    u.scopedPct = 0; u.scopedResetAt = 0; u.scopedLabel[0] = 0;
  }
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
    // percent is an integer today, but parse it float-safe (like the sibling
    // utilization fields): an int-typed `| -1` gate would silently drop the
    // whole entry if the API ever emits 14.0 — and weekly_scoped has no
    // fallback source, so the Fable bar would just vanish.
    if (l["percent"].isNull()) continue;
    float raw = l["percent"].as<float>();
    int  pct = constrain((int)(raw + 0.5f), 0, 100);
    long rst = net::parseIso8601Utc(l["resets_at"] | "");
    if (strcmp(kind, "session") == 0) {
      u.sessionPct = pct; if (rst > 0) u.sessionResetAt = rst; sawSession = true;
    } else if (strcmp(kind, "weekly_all") == 0) {
      u.weeklyPct = pct;  if (rst > 0) u.weeklyResetAt = rst;  sawWeekly = true;
    } else if (strcmp(kind, "weekly_scoped") == 0) {
      const char* name = l["scope"]["model"]["display_name"] | "Model";
      snprintf(u.scopedLabel, sizeof(u.scopedLabel), "%s", name);
      u.scopedPct = pct;  if (rst > 0) u.scopedResetAt = rst;  sawScoped = true;
      u.scopedUpdatedAt = millis();       // reset the staleness clock (retain across downgrades)
    }
    if (raw >= 100.0f) lim = true;               // pre-clamp value
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

// One strategy attempt. Returns 1 = got a reading, 0 = soft miss, <0 = auth error.
inline int tryStrategy(SessionUsage& u, int s) {
  bool got = false;
  int code = (s == 0) ? probeUsage(u, got)
           : (s == 1) ? probe(COUNT_TOKENS_PATH, COUNT_BODY, u, got)
                      : probe(MESSAGES_PATH,    MSG_BODY,   u, got);
  g_diag.subLastCode = code;
  // A 403 from the usage endpoint is a SCOPE refusal, not a dead token: the
  // long-lived `claude setup-token` token is inference-only, so /api/oauth/usage
  // (and /profile) 403 while the messages probe still returns the unified
  // headers (verified live 2026-10-31). Treat it as a soft miss so the chain
  // falls through to the probes; a token that really is dead still trips the
  // auth path there, because the probes 401/403 too.
  bool scopeMiss = (s == 0 && code == 403);
  if (!scopeMiss && (code == 401 || code == 403)) { authError = true; return -code; }
  authError = false;
  // `got` covers both a parsed 200 and (for the probes) a 429 whose unified
  // headers were still present.
  if (got) {
    // Strategy changes are otherwise invisible: an intra-poll failover still
    // ends the poll with HTTP 200, so the transition-only event log never
    // fires. Log them here (this is how a vanished Fable bar gets diagnosed).
    if (working != s) applog::add("usage: strategy %d (was %d, HTTP %d)", s, working, code);
    working = s;
    knownGoodMisses = 0;              // any success resets the downgrade hysteresis
    lastMissCode = 0;                 // ...and re-arms the deduped miss log
    if (s == 0) downgradedPolls = 0;  // any path back to strategy 0 resets the retry clock
    return 1;
  }
  return 0;
}

// --- Resumable poll ---------------------------------------------------------
// One reading can need several sequential requests: the periodic strategy-0
// upgrade retry, the known-good attempt, then a 0->1->2 rediscovery if that
// attempt was the one that finally tripped the downgrade — up to five, plus the
// token refresh and the plan fetch. Run back-to-back on the UI task that is
// tens of seconds of blocked loop(), which is exactly the swallowed-tap problem
// the poller's one-item-per-iteration queue fixes for the admin calls.
//
// So the chain is a step machine instead: each pollStep() performs AT MOST one
// request and reports whether more remain. The poller re-queues immediately
// rather than waiting for the next cadence, so the chain still finishes in the
// same wall-clock time as before — just with handleTouch() sampled between
// requests instead of after all of them. No cadence, hysteresis, or logging
// behaviour changes; only where the yields are.
enum PollStep : uint8_t {
  PS_IDLE = 0,   // between polls
  PS_REFRESH,    // oauth::refreshIfNeeded
  PS_UPGRADE,    // periodic strategy-0 retry while downgraded
  PS_KNOWN,      // the known-good strategy
  PS_DISCOVER,   // 0 -> 1 -> 2 rediscovery, one per step
  PS_AUTHFIX,    // forced token refresh, then restart the chain once
  PS_PLAN,       // cosmetic plan label
  PS_DONE
};
static PollStep pollStep_    = PS_IDLE;
static uint8_t  discoverS    = 0;      // next strategy to try in PS_DISCOVER
static int      discoverCode = 0;      // strategy 0's code — the one worth logging
static bool     authRetried  = false;  // the forced-refresh retry is once per poll
static bool     pollOk       = false;  // result carried to PS_DONE

// Performs at most one request. `more` = call again to continue this reading;
// the return value is meaningful only once `more` comes back false.
inline bool pollStep(SessionUsage& u, bool& more) {
  more = false;
  if (oauthToken.isEmpty()) { pollStep_ = PS_IDLE; return false; }

  if (pollStep_ == PS_IDLE) {             // start of a new reading
    // Advance the upgrade-retry clock once per poll TICK — not per step, and
    // not inside the strategy chain, which the auth-error path restarts.
    if (working > 0) downgradedPolls++;
    authRetried = false; pollOk = false;
    discoverS = 0; discoverCode = 0;
    pollStep_ = PS_REFRESH;
  }

  for (;;) {                              // fall through request-free steps
    switch (pollStep_) {

    case PS_REFRESH:
      oauth::refreshIfNeeded(oauthToken, oauthRefresh, oauthExpiresMs);
      pollStep_ = PS_UPGRADE;
      more = true; return false;          // may have spent a request — yield

    case PS_UPGRADE: {
      // Downgraded? Periodically re-try the usage endpoint so a transient blip
      // doesn't hide the model-scoped bar until the next reboot. Runs BEFORE the
      // known-good strategy so a failed retry's HTTP code is overwritten by the
      // real result below.
      if (!(working > 0 && downgradedPolls >= UPGRADE_RETRY_POLLS)) { pollStep_ = PS_KNOWN; break; }
      downgradedPolls = 0;
      int r = tryStrategy(u, 0);
      if (r == 1) { pollOk = true; pollStep_ = PS_PLAN; more = true; return false; }
      if (r < 0)  { pollStep_ = PS_AUTHFIX;             more = true; return false; }
      // r == 0: the retry failed and its HTTP code is about to be overwritten
      // by the known-good strategy below. Log it here so a device stuck
      // downgraded (strategy 0 persistently failing) isn't diagnostically dark.
      if (newMissCode(g_diag.subLastCode))
        applog::add("usage: strategy 0 retry miss (HTTP %d)", g_diag.subLastCode);
      pollStep_ = PS_KNOWN; more = true; return false;
    }

    case PS_KNOWN: {
      if (working < 0) { pollStep_ = PS_DISCOVER; break; }
      int r = tryStrategy(u, working);
      if (r == 1) { pollOk = true; pollStep_ = PS_PLAN; more = true; return false; }
      if (r < 0)  { pollStep_ = PS_AUTHFIX;             more = true; return false; }
      // Soft failure. Don't abandon a known-good strategy on the first miss:
      // dropping strategy 0 wipes the model-scoped ("Fable") bar and blocks its
      // return for UPGRADE_RETRY_POLLS, so a single transient blip blanks the
      // bar for ~10 min. Hold for DOWNGRADE_AFTER_MISSES consecutive misses.
      // Log the miss (deduped by code) for ANY known-good strategy (0, 1 or 2).
      // A miss is otherwise invisible — a later strategy's HTTP 200 overwrites
      // subLastCode before the poller logs it, and the poller now records only
      // auth codes — so neither the vanishing Fable bar (strategy 0) nor a
      // probe-strategy blip could be diagnosed. Dedup-by-code (not first-miss)
      // so a streak whose code shifts mid-way records each distinct failure.
      if (newMissCode(g_diag.subLastCode))
        applog::add("usage: strategy %d miss (HTTP %d)", working, g_diag.subLastCode);
      // A scope 403 on strategy 0 isn't a transient blip, so skip the hysteresis
      // and rediscover now. If the token is actually dead the probes 401/403 in
      // that same pass and the force-refresh (PS_AUTHFIX) still runs promptly.
      // (Only strategy 0 reaches here with a 403; elsewhere it's an auth error.)
      bool scope403 = (g_diag.subLastCode == 403);
      if (++knownGoodMisses < DOWNGRADE_AFTER_MISSES && !scope403) { pollStep_ = PS_DONE; more = true; return false; }
      working = -1;                       // it really stopped working; rediscover
      pollStep_ = PS_DISCOVER; more = true; return false;
    }

    case PS_DISCOVER: {
      if (discoverS > 2) {
        // Every strategy failed — the discovery path (boot before any strategy is
        // known-good, or the rediscovery right after a downgrade). The other miss
        // logs don't cover this path and the poller records only auth codes, so a
        // real WiFi/TLS outage would otherwise leave the event log silent. Report
        // strategy 0's code (not the loop's last, which is the messages probe's) —
        // deduped like the other misses so a sustained -11/5xx storm writes one line.
        if (newMissCode(discoverCode))
          applog::add("usage: all strategies down (strategy 0 HTTP %d)", discoverCode);
        pollStep_ = PS_DONE; break;
      }
      int s = discoverS++;
      int r = tryStrategy(u, s);
      if (s == 0) discoverCode = g_diag.subLastCode;
      if (r == 1) { pollOk = true; pollStep_ = PS_PLAN; more = true; return false; }
      if (r < 0)  { pollStep_ = PS_AUTHFIX;             more = true; return false; }
      more = true; return false;          // stay here for the next strategy
    }

    case PS_AUTHFIX:
      // A 401/403 means the access token was rejected even though our expiry clock
      // thought it was fine (the token can be rotated out from under us by the
      // Claude Code that shares this login). Force one refresh and retry the whole
      // chain before giving up, so the device self-heals whenever its refresh
      // token is still good.
      if (authRetried || !authError) { pollStep_ = PS_DONE; break; }
      authRetried = true;
      if (!oauth::refresh(oauthToken, oauthRefresh, oauthExpiresMs)) {
        pollStep_ = PS_DONE; more = true; return false;
      }
      discoverS = 0; discoverCode = 0;
      pollStep_ = PS_UPGRADE; more = true; return false;

    case PS_PLAN:
      refreshPlan(u);                     // cosmetic; never affects the reading
      pollStep_ = PS_DONE;
      more = true; return false;

    case PS_DONE:
    default:
      pollStep_ = PS_IDLE;
      u.authError = authError;            // surface for the UI banner
      more = false;
      return pollOk;
    }
  }
}

// Blocking form: drive the whole chain to completion in one call. Keeps the
// original semantics for callers that don't care about yielding (the host
// tests, and anything that wants a reading synchronously).
inline bool poll(SessionUsage& u) {
  bool more = true, ok = false;
  while (more) ok = pollStep(u, more);
  return ok;
}

} // namespace sub
