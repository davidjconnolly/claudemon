// Host test for the subscription source (src/data_sub.h) — the plan matrix.
//
// Compiles the REAL data_sub.h / net_http.h / net_oauth.h against a canned
// HTTPClient transport (HTTPClient.h here) and asserts the parsed SessionUsage
// for each plan shape:
//
//   Max 5x  — session + weekly_all + weekly_scoped(Fable); plan "MAX 5x"
//   Max 20x — same, tier default_claude_max_20x; plan "MAX 20x"
//   Pro     — NO weekly_scoped entry -> scoped bar hidden; plan "PRO"
//   Free    — same shape as Pro; plan "FREE"
//
// plus the undocumented-endpoint failure modes: 404 / invalid JSON /
// unrecognised shape all fall through to the unified-header probes, and a
// profile failure never clobbers a previously fetched plan label.
//
//   ./build.sh    (compiles + runs; exit 0 = all checks pass)
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "Arduino.h"
#include "HTTPClient.h"

// --- globals the data layer links against (normally defined in main.cpp) ---
unsigned long g_millis = 1000;
EspClass ESP;
bool getLocalTime(struct tm*, uint32_t) { return false; }
std::vector<MockResponse> g_mockResponses;

#include "data_sub.h"

String  oauthToken;
String  oauthRefresh;
int64_t oauthExpiresMs = 0;
Diag    g_diag;

// --- tiny check harness ---
static int fails = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)
#define CHECK_STR(a, b) do { if (strcmp((a), (b)) != 0) { printf("FAIL %s:%d  \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); fails++; } } while (0)

// --- fixtures -----------------------------------------------------------

// limits[] usage response; scopedJson may be "" for plans without a scoped bar.
static std::string usageBody(int sess, int week, const char* scopedJson) {
  std::string s =
    "{\"five_hour\":{\"utilization\":" + std::to_string(sess) + ".0,"
    "\"resets_at\":\"2026-07-21T18:40:00.348133+00:00\"},"
    "\"seven_day\":{\"utilization\":" + std::to_string(week) + ".0,"
    "\"resets_at\":\"2026-07-27T03:00:00.348156+00:00\"},"
    "\"seven_day_opus\":null,\"seven_day_sonnet\":null,"
    "\"limits\":["
    "{\"kind\":\"session\",\"group\":\"session\",\"percent\":" + std::to_string(sess) + ","
    "\"severity\":\"normal\",\"resets_at\":\"2026-07-21T18:40:00.348133+00:00\",\"scope\":null,\"is_active\":true},"
    "{\"kind\":\"weekly_all\",\"group\":\"weekly\",\"percent\":" + std::to_string(week) + ","
    "\"severity\":\"normal\",\"resets_at\":\"2026-07-27T03:00:00.348156+00:00\",\"scope\":null,\"is_active\":false}";
  if (scopedJson[0]) s += std::string(",") + scopedJson;
  s += "]}";
  return s;
}

static std::string scopedLimit(const char* name, int pct) {
  return "{\"kind\":\"weekly_scoped\",\"group\":\"weekly\",\"percent\":" + std::to_string(pct) + ","
         "\"severity\":\"normal\",\"resets_at\":\"2026-07-27T03:00:00.348410+00:00\","
         "\"scope\":{\"model\":{\"id\":null,\"display_name\":\"" + std::string(name) + "\"},\"surface\":null},"
         "\"is_active\":false}";
}

static std::string profileBody(const char* type, const char* tier) {
  return "{\"account\":{\"has_claude_max\":true},"
         "\"organization\":{\"organization_type\":\"" + std::string(type) + "\","
         "\"rate_limit_tier\":\"" + std::string(tier) + "\"}}";
}

static void install(int usageCode, const std::string& usage,
                    int profileCode, const std::string& profile,
                    const std::map<std::string, std::string>& probeHeaders = {}) {
  g_mockResponses = {
    {"/api/oauth/usage",   usageCode,   usage},
    {"/api/oauth/profile", profileCode, profile},
    {"count_tokens",       probeHeaders.empty() ? 400 : 200, "{}", probeHeaders},
    {"/v1/messages",       probeHeaders.empty() ? 400 : 200, "{}", probeHeaders},
  };
}

// Advance the fake clock past the daily plan re-fetch interval.
static void advancePlanClock() { g_millis += 25UL * 3600UL * 1000UL; }

// Independent cross-check of the ISO-8601 parser against the host's timegm.
static long hostEpoch(int y, int mo, int d, int h, int mi, int s) {
  struct tm t = {};
  t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
  t.tm_hour = h; t.tm_min = mi; t.tm_sec = s;
  return (long)timegm(&t);
}

int main() {
  oauthToken = "sk-ant-oat01-test";
  oauthExpiresMs = (int64_t)time(nullptr) * 1000LL + 86400000LL;  // fresh; skip refresh
  SessionUsage u;

  // --- ISO-8601 parser vs the host C library ---
  CHECK(net::parseIso8601Utc("2026-07-21T18:40:00.348133+00:00") == hostEpoch(2026, 7, 21, 18, 40, 0));
  CHECK(net::parseIso8601Utc("2026-07-27T03:00:00Z") == hostEpoch(2026, 7, 27, 3, 0, 0));
  CHECK(net::parseIso8601Utc("") == 0);
  CHECK(net::parseIso8601Utc("garbage") == 0);

  // --- Max 5x: three windows + plan label (the live-confirmed shape) ---
  install(200, usageBody(70, 12, scopedLimit("Fable", 14).c_str()),
          200, profileBody("claude_max", "default_claude_max_5x"));
  CHECK(sub::poll(u));
  CHECK(u.valid);
  CHECK(u.sessionPct == 70);
  CHECK(u.weeklyPct == 12);
  CHECK(u.hasScoped);
  CHECK(u.scopedPct == 14);
  CHECK_STR(u.scopedLabel, "Fable");
  CHECK(u.sessionResetAt == hostEpoch(2026, 7, 21, 18, 40, 0));
  CHECK(u.scopedResetAt == hostEpoch(2026, 7, 27, 3, 0, 0));
  CHECK(!u.limited);
  CHECK_STR(u.plan, "MAX 5x");
  CHECK(sub::working == 0);                     // usage endpoint became the strategy

  // --- Max 20x: scoped bar still shown, plan label picks up the multiplier ---
  advancePlanClock();
  install(200, usageBody(35, 44, scopedLimit("Fable", 61).c_str()),
          200, profileBody("claude_max", "default_claude_max_20x"));
  CHECK(sub::poll(u));
  CHECK(u.hasScoped && u.scopedPct == 61);
  CHECK_STR(u.plan, "MAX 20x");

  // --- Pro: no weekly_scoped entry -> scoped bar hides (was shown above) ---
  advancePlanClock();
  install(200, usageBody(20, 5, ""),
          200, profileBody("claude_pro", "default_claude_pro"));
  CHECK(sub::poll(u));
  CHECK(!u.hasScoped);                          // cleared, not stale from Max
  CHECK(u.sessionPct == 20 && u.weeklyPct == 5);
  CHECK_STR(u.plan, "PRO");

  // --- Free: same two-window shape ---
  advancePlanClock();
  install(200, usageBody(3, 1, ""),
          200, profileBody("claude_free", "default_claude_free"));
  CHECK(sub::poll(u));
  CHECK(!u.hasScoped);
  CHECK_STR(u.plan, "FREE");

  // --- a profile failure must not clobber the previously fetched label ---
  advancePlanClock();
  install(200, usageBody(3, 1, ""), 500, "oops");
  CHECK(sub::poll(u));
  CHECK_STR(u.plan, "FREE");

  // --- a window at 100% maps to the limited flag ---
  install(200, usageBody(100, 12, scopedLimit("Fable", 14).c_str()),
          200, profileBody("claude_max", "default_claude_max_5x"));
  CHECK(sub::poll(u));
  CHECK(u.limited && u.sessionPct == 100);

  // --- endpoint 404s -> falls through to the unified-header probes ---
  std::map<std::string, std::string> hdrs = {
    {"anthropic-ratelimit-unified-5h-utilization", "0.42"},
    {"anthropic-ratelimit-unified-5h-reset",       "1785200000"},
    {"anthropic-ratelimit-unified-7d-utilization", "0.07"},
    {"anthropic-ratelimit-unified-7d-reset",       "1785600000"},
    {"anthropic-ratelimit-unified-status",         "allowed"},
  };
  install(404, "", 404, "", hdrs);
  CHECK(u.hasScoped);                           // entering with a live scoped bar (Max above)
  for (int i = 1; i < sub::DOWNGRADE_AFTER_MISSES; i++) {
    CHECK(!sub::poll(u));                        // blips tolerated: bar held...
    CHECK(u.hasScoped);
  }
  CHECK(sub::poll(u));                           // the DOWNGRADE_AFTER_MISSES-th miss fails over
  CHECK(u.sessionPct == 42 && u.weeklyPct == 7);
  CHECK(u.sessionResetAt == 1785200000L);
  CHECK(!u.limited);
  CHECK(!u.hasScoped);                          // header reading clears the scoped bar
  CHECK(u.scopedLabel[0] == 0);                 // ...rather than freezing stale Fable data
  CHECK(sub::working == 1);                     // rediscovered count_tokens

  // --- invalid JSON and unrecognised shapes also fall through ---
  sub::working = -1;
  install(200, "definitely not json", 404, "", hdrs);
  CHECK(sub::poll(u));
  CHECK(sub::working == 1);

  sub::working = -1;
  install(200, "{\"unexpected\":true}", 404, "", hdrs);
  CHECK(sub::poll(u));
  CHECK(sub::working == 1);

  // --- limits[] missing but top-level five_hour/seven_day present ---
  sub::working = -1;
  install(200,
          "{\"five_hour\":{\"utilization\":55.0,\"resets_at\":\"2026-07-21T18:40:00+00:00\"},"
          "\"seven_day\":{\"utilization\":9.0,\"resets_at\":\"2026-07-27T03:00:00+00:00\"}}",
          404, "");
  CHECK(sub::poll(u));
  CHECK(u.sessionPct == 55 && u.weeklyPct == 9);
  CHECK(!u.limited);
  CHECK(sub::working == 0);

  // --- float-typed percent values (like the sibling utilization fields) must
  //     still parse — an int-only gate would silently drop the Fable bar ---
  sub::working = -1;
  install(200,
          "{\"limits\":["
          "{\"kind\":\"session\",\"percent\":70.0,\"resets_at\":\"2026-07-21T18:40:00+00:00\",\"scope\":null},"
          "{\"kind\":\"weekly_all\",\"percent\":12.4,\"resets_at\":\"2026-07-27T03:00:00+00:00\",\"scope\":null},"
          "{\"kind\":\"weekly_scoped\",\"percent\":13.6,\"resets_at\":\"2026-07-27T03:00:00+00:00\","
          "\"scope\":{\"model\":{\"display_name\":\"Fable\"}}}]}",
          404, "");
  CHECK(sub::poll(u));
  CHECK(u.sessionPct == 70);
  CHECK(u.weeklyPct == 12);                      // 12.4 rounds down
  CHECK(u.hasScoped && u.scopedPct == 14);       // 13.6 rounds up
  CHECK(sub::working == 0);

  // --- partial limits[] (session only): the missing weekly window fills from
  //     the top-level seven_day object instead of staying stale ---
  sub::working = -1;
  u.weeklyPct = 77;                              // stale value that must be replaced
  install(200,
          "{\"five_hour\":{\"utilization\":48.0,\"resets_at\":\"2026-07-21T18:40:00+00:00\"},"
          "\"seven_day\":{\"utilization\":22.0,\"resets_at\":\"2026-07-27T03:00:00+00:00\"},"
          "\"limits\":[{\"kind\":\"session\",\"group\":\"session\",\"percent\":48,"
          "\"severity\":\"normal\",\"resets_at\":\"2026-07-21T18:40:00+00:00\",\"scope\":null,\"is_active\":true}]}",
          404, "");
  CHECK(sub::poll(u));
  CHECK(u.sessionPct == 48);                     // from limits[]
  CHECK(u.weeklyPct == 22);                      // from the top-level fallback
  CHECK(u.weeklyResetAt == hostEpoch(2026, 7, 27, 3, 0, 0));
  CHECK(sub::working == 0);

  // --- 100% in the fallback shape must also raise the limited flag ---
  install(200,
          "{\"five_hour\":{\"utilization\":100.0,\"resets_at\":\"2026-07-21T18:40:00+00:00\"},"
          "\"seven_day\":{\"utilization\":9.0,\"resets_at\":\"2026-07-27T03:00:00+00:00\"}}",
          404, "");
  CHECK(sub::poll(u));
  CHECK(u.limited && u.sessionPct == 100);

  // --- downgrade hysteresis: a lone endpoint blip is TOLERATED — the Fable bar
  //     survives instead of flapping. Only DOWNGRADE_AFTER_MISSES consecutive
  //     misses fail over to the header probe (the ~10-min bar-disappearance of
  //     2026-07-21 was one blip immediately blanking the bar) ---
  install(200, usageBody(70, 12, scopedLimit("Fable", 14).c_str()),
          200, profileBody("claude_max", "default_claude_max_5x"));
  CHECK(sub::poll(u));
  CHECK(sub::working == 0 && u.hasScoped);
  install(404, "", 404, "", hdrs);               // endpoint down; probe headers ready
  for (int i = 1; i < sub::DOWNGRADE_AFTER_MISSES; i++) {
    CHECK(!sub::poll(u));                         // soft miss -> poll reports failure...
    CHECK(sub::working == 0);                     // ...but the known-good strategy is held...
    CHECK(u.hasScoped);                           // ...and the Fable bar is NOT dropped
  }
  CHECK(sub::poll(u));                            // the DOWNGRADE_AFTER_MISSES-th miss fails over
  CHECK(sub::working == 1);                       // failed over to count_tokens
  CHECK(!u.hasScoped);                            // bar hidden, not stale
  // --- upgrade retry: the periodic strategy-0 retry restores the endpoint and
  //     the bar returns — WITHOUT a reboot ---
  install(200, usageBody(70, 12, scopedLimit("Fable", 14).c_str()),
          200, profileBody("claude_max", "default_claude_max_5x"), hdrs);
  int pollsToRecover = 0;                        // endpoint back: bar must return
  while (sub::working != 0 && pollsToRecover <= 12) { CHECK(sub::poll(u)); pollsToRecover++; }
  CHECK(sub::working == 0);
  CHECK(u.hasScoped);
  CHECK_STR(u.scopedLabel, "Fable");
  printf("  (upgrade retry recovered after %d polls)\n", pollsToRecover);

  if (fails) { printf("%d check(s) FAILED\n", fails); return 1; }
  printf("all checks passed\n");
  return 0;
}
