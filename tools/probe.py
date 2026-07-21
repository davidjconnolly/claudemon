#!/usr/bin/env python3
"""Host-side API probe for CYD-Claudemon local development.

Reads the same credentials the firmware uses from src/secrets.h (gitignored;
copy from src/secrets.h.example) and calls the real Anthropic endpoints from
your computer — so you can see exactly what the device computes without flashing
hardware or running Wokwi. Credentials are never printed.

  python3 tools/probe.py

It mirrors the firmware: follows the cost/usage API pagination (~7 buckets/page)
and buckets by real date, then prints the COST page $ figures, token totals, and
the USAGE % rate-limit headers.

Note: it does NOT refresh the OAuth token. Refreshing rotates the refresh token,
which would invalidate the copy your device relies on, so the rate-limit peek is
best-effort and may 401 if the access token in secrets.h is stale (the device
refreshes its own copy, so USAGE still works on-device).
"""
import json, re, os, sys, ssl, urllib.request, urllib.error, urllib.parse
from datetime import datetime, timezone, date

HOST = "https://api.anthropic.com"
VER  = "2023-06-01"
HERE = os.path.dirname(os.path.abspath(__file__))
SECRETS = os.path.join(HERE, "..", "src", "secrets.h")
CTX = ssl.create_default_context()

def load_secrets():
    if not os.path.exists(SECRETS):
        sys.exit(f"no {SECRETS} — copy src/secrets.h.example and fill it in")
    txt = open(SECRETS).read()
    def grab(name):
        m = re.search(r'^\s*#define\s+' + name + r'\s+"([^"]*)"', txt, re.M)
        return m.group(1) if m else ""
    return {k: grab(k) for k in ("DEV_ADMIN_KEY", "DEV_OAUTH_AT", "DEV_OAUTH_RT")}

def req(method, url, headers, body=None):
    r = urllib.request.Request(url, data=(body.encode() if body else None),
                               headers=headers, method=method)
    try:
        with urllib.request.urlopen(r, timeout=30, context=CTX) as x:
            return x.status, dict(x.headers), x.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read().decode()
    except Exception as e:
        return -1, {}, str(e)

def iso(dt): return dt.strftime("%Y-%m-%dT%H:%M:%SZ")

def fetch_all_buckets(base_url, headers):
    """Follow next_page pagination; return (ok, list_of_buckets)."""
    out, page = [], None
    for _ in range(24):
        u = base_url + (f"&page={urllib.parse.quote(page)}" if page else "")
        code, _, body = req("GET", u, headers)
        if code != 200:
            return (len(out) > 0, out, code, body)
        d = json.loads(body)
        out += d.get("data", [])
        if d.get("has_more") and d.get("next_page"):
            page = d["next_page"]
        else:
            return (True, out, 200, "")
    return (True, out, 200, "")

def days_ago(bucket, today):
    s = bucket.get("starting_at", "")[:10]
    try:
        return (today - date.fromisoformat(s)).days
    except ValueError:
        return -1

def main():
    s = load_secrets()
    now = datetime.now(timezone.utc)
    today = now.date()
    month_start = now.replace(day=1, hour=0, minute=0, second=0, microsecond=0)
    month_days = now.day
    print(f"now (UTC): {iso(now)}   month-to-date: day {month_days}\n")

    if s["DEV_ADMIN_KEY"]:
        ah = {"x-api-key": s["DEV_ADMIN_KEY"], "anthropic-version": VER,
              "User-Agent": "CYD-Claudemon-probe"}

        # ---- COST (mirror fetchCostMonth: cents -> USD, date-bucketed) ----
        url = (f"{HOST}/v1/organizations/cost_report?starting_at={iso(month_start)}"
               f"&ending_at={iso(now)}&group_by[]=description&limit=31")
        ok, buckets, code, body = fetch_all_buckets(url, ah)
        print(f"=== cost_report  HTTP {code}  ({len(buckets)} buckets across pages) ===")
        if ok:
            t = w = m = 0.0
            for b in buckets:
                da = days_ago(b, today)
                usd = sum(float(r.get("amount", "0") or 0) for r in b.get("results", [])) / 100.0
                if da == 0: t += usd
                if 0 <= da < 7: w += usd
                if 0 <= da < month_days: m += usd
            print(f"  TODAY  ${t:.2f}")
            print(f"  WEEK   ${w:.2f}")
            print(f"  MONTH  ${m:.2f}   <- should match your billing 'spent'")
        else:
            print("  " + body[:400])
        print()

        # ---- TOKENS (mirror rowTokens across the month) ----
        url = (f"{HOST}/v1/organizations/usage_report/messages?starting_at={iso(month_start)}"
               f"&ending_at={iso(now)}&bucket_width=1d&limit=31")
        ok, buckets, code, body = fetch_all_buckets(url, ah)
        print(f"=== usage_report  HTTP {code}  ({len(buckets)} buckets) ===")
        if ok:
            def rowtok(r):
                return ((r.get("uncached_input_tokens", 0) or 0) +
                        (r.get("output_tokens", 0) or 0) +
                        (r.get("cache_read_input_tokens", 0) or 0) +
                        (r.get("cache_creation_input_tokens", 0) or 0))
            tt = tw = tm = 0
            for b in buckets:
                da = days_ago(b, today)
                tok = sum(rowtok(r) for r in b.get("results", []))
                if da == 0: tt += tok
                if 0 <= da < 7: tw += tok
                if 0 <= da < month_days: tm += tok
            print(f"  tokens  today={tt:,}  week={tw:,}  month={tm:,}")
        else:
            print("  " + body[:400])
        print()
    else:
        print("(no DEV_ADMIN_KEY set — skipping cost/usage)\n")

    # ---- USAGE: the endpoint the claude.ai usage page reads (strategy 0) ----
    if s["DEV_OAUTH_AT"]:
        oh = {"Authorization": "Bearer " + s["DEV_OAUTH_AT"],
              "anthropic-beta": "oauth-2025-04-20"}
        code, _, body = req("GET", f"{HOST}/api/oauth/usage", oh)
        print(f"=== /api/oauth/usage  HTTP {code} ===")
        if code == 200:
            d = json.loads(body)
            for lim in d.get("limits", []):
                scope = ((lim.get("scope") or {}).get("model") or {}).get("display_name", "")
                name = lim.get("kind", "?") + (f" ({scope})" if scope else "")
                print(f"  {name:24s} {lim.get('percent')}%  resets {lim.get('resets_at')}")
        elif code == 401:
            print("  401 — access token in secrets.h is stale (expected; the device "
                  "refreshes its own). USAGE still works on-device.")
        else:
            print("  body:", body[:200])
        print()

        # ---- plan label ----
        code, _, body = req("GET", f"{HOST}/api/oauth/profile", oh)
        print(f"=== /api/oauth/profile  HTTP {code} ===")
        if code == 200:
            o = json.loads(body).get("organization", {})
            print(f"  organization_type: {o.get('organization_type')}")
            print(f"  rate_limit_tier:   {o.get('rate_limit_tier')}")
        elif code != 401:
            print("  body:", body[:200])
        print()

        # ---- unified rate-limit headers (fallback strategies 1/2) ----
        oh["anthropic-version"] = VER
        oh["Content-Type"] = "application/json"
        b = '{"model":"claude-haiku-4-5","messages":[{"role":"user","content":"."}]}'
        code, hdrs, body = req("POST", f"{HOST}/v1/messages/count_tokens", oh, b)
        print(f"=== rate-limit headers (count_tokens)  HTTP {code} ===")
        rl = {k: v for k, v in hdrs.items() if "ratelimit-unified" in k.lower()}
        if rl:
            for k, v in sorted(rl.items()):
                print(f"  {k}: {v}")
        elif code == 401:
            print("  401 — stale access token (see above)")
        else:
            print("  no unified headers; body:", body[:200])
    else:
        print("(no DEV_OAUTH_AT set — skipping rate-limit peek)")

if __name__ == "__main__":
    main()
