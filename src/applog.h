// applog.h — tiny in-RAM ring buffer of recent events, shown on the web /status
// page and mirrored to Serial. Memory-bounded (N*LEN bytes ≈ 1.7 KB). Compiled
// into the firmware only (the host renderer never includes this).
#pragma once
#include <Arduino.h>
#include <stdarg.h>
#include <time.h>

namespace applog {

static const int N   = 16;     // recent lines retained
static const int LEN = 108;    // bytes per line (timestamp + message + NUL)
static char lines[N][LEN];
static int  head  = 0;         // next write slot
static int  count = 0;

inline void add(const char* fmt, ...) {
  char msg[88];
  va_list ap; va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);

  char ts[12];
  time_t now = time(nullptr);
  if (now > 1700000000) {                 // clock synced -> wall time
    struct tm t; localtime_r(&now, &t);
    snprintf(ts, sizeof ts, "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
  } else {                                // pre-NTP -> uptime
    snprintf(ts, sizeof ts, "+%lus", (unsigned long)(millis() / 1000));
  }
  snprintf(lines[head], LEN, "%s  %s", ts, msg);
  Serial.println(lines[head]);
  head = (head + 1) % N;
  if (count < N) count++;
}

inline int size() { return count; }
// i = 0 is oldest, size()-1 is newest.
inline const char* at(int i) {
  int start = (head - count + 2 * N) % N;
  return lines[(start + i) % N];
}

} // namespace applog
