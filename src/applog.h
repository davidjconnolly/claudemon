// applog.h — event log: a small in-RAM ring for quick "recent activity" views
// plus a flash-persisted copy (LittleFS on the otherwise-unused 128 KB data
// partition) that survives reboots and power cycles. Events are transition-only
// (a handful per hour), so flash wear is negligible. The current segment
// rotates to a single predecessor at ROTATE_AT, keeping total flash use well
// under the partition size. Compiled into the firmware only (the host renderer
// never includes this).
#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_system.h>
#include <stdarg.h>
#include <time.h>
#include "config.h"

namespace applog {

static const int N   = 32;     // recent lines retained in RAM (web /status)
static const int LEN = 108;    // bytes per line (timestamp + message + NUL)
static char lines[N][LEN];
static int  head  = 0;         // next write slot
static int  count = 0;

static const char*  LOG_PATH  = "/log.txt";     // current segment
static const char*  OLD_PATH  = "/log.0.txt";   // previous segment (post-rotation)
static const size_t ROTATE_AT = 40 * 1024;      // rotate when current exceeds this
static bool fsOk = false;

inline void stamp(char* ts, size_t n) {
  time_t now = time(nullptr);
  if (now > 1700000000) {                 // clock synced -> date + wall time
    struct tm t; localtime_r(&now, &t);
    snprintf(ts, n, "%02d-%02d %02d:%02d:%02d",
             t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  } else {                                // pre-NTP -> uptime
    snprintf(ts, n, "+%lus", (unsigned long)(millis() / 1000));
  }
}

inline void ringPush(const char* line) {
  snprintf(lines[head], LEN, "%s", line);
  head = (head + 1) % N;
  if (count < N) count++;
}

inline void persist(const char* line) {
  if (!fsOk) return;
  File f = LittleFS.open(LOG_PATH, FILE_APPEND);
  if (!f) return;
  if (f.size() > ROTATE_AT) {             // rotate: current -> previous (old previous dropped)
    f.close();
    LittleFS.remove(OLD_PATH);
    LittleFS.rename(LOG_PATH, OLD_PATH);
    f = LittleFS.open(LOG_PATH, FILE_APPEND);
    if (!f) return;
  }
  f.println(line);
  f.close();
}

inline void add(const char* fmt, ...) {
  char msg[88];
  va_list ap; va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);

  char ts[16]; stamp(ts, sizeof ts);
  char line[LEN];
  snprintf(line, sizeof line, "%s  %s", ts, msg);
  Serial.println(line);
  ringPush(line);
  persist(line);
}

// Mount the flash log, preload its tail into the RAM ring (so "recent activity"
// still shows pre-reboot events after a power cycle), and record the boot with
// its reset reason — a crash/watchdog reboot is then distinguishable from a
// deliberate power cycle. Call once, early in setup().
inline void init() {
  fsOk = LittleFS.begin(true);            // format on first use
  if (fsOk) {
    File f = LittleFS.open(LOG_PATH, FILE_READ);
    if (f) {
      const size_t TAIL = (size_t)N * LEN;
      size_t sz = f.size();
      if (sz > TAIL) {                    // skip to the tail, discard partial first line
        f.seek(sz - TAIL);
        while (f.available() && f.read() != '\n') {}
      }
      char line[LEN]; int col = 0;
      while (f.available()) {             // byte-at-a-time is fine for <=3.5 KB once at boot
        char c = (char)f.read();
        if (c == '\n')      { line[col] = 0; if (col) ringPush(line); col = 0; }
        else if (c != '\r' && col < LEN - 1) line[col++] = c;
      }
      f.close();
    }
  }
  const char* rr;
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  rr = "power-on";    break;
    case ESP_RST_SW:       rr = "software";    break;
    case ESP_RST_PANIC:    rr = "crash/panic"; break;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      rr = "watchdog";    break;
    case ESP_RST_BROWNOUT: rr = "brownout";    break;
    default:               rr = "other";       break;
  }
  add("---- boot v%s (%s reset)%s", CLAUDEMON_VERSION, rr,
      fsOk ? "" : " [flash log unavailable]");
}

// Wipe both persisted segments (factory reset — the log holds SSID/IP details).
inline void clearAll() {
  if (!fsOk) return;
  LittleFS.remove(LOG_PATH);
  LittleFS.remove(OLD_PATH);
}

inline int size() { return count; }
// i = 0 is oldest, size()-1 is newest.
inline const char* at(int i) {
  int start = (head - count + 2 * N) % N;
  return lines[(start + i) % N];
}

inline size_t curBytes() {
  if (!fsOk) return 0;
  File f = LittleFS.open(LOG_PATH, FILE_READ);
  if (!f) return 0;
  size_t s = f.size(); f.close(); return s;
}

// Lines in the current segment (for the on-device pager).
inline int fileLines() {
  if (!fsOk) return 0;
  File f = LittleFS.open(LOG_PATH, FILE_READ);
  if (!f) return 0;
  int n = 0; uint8_t buf[256]; int r;
  while ((r = f.read(buf, sizeof buf)) > 0)
    for (int i = 0; i < r; i++) if (buf[i] == '\n') n++;
  f.close();
  return n;
}

// Copy up to `want` lines starting at 0-based line `start` of the current
// segment into dst (each row LEN bytes). Returns lines copied. A trailing
// unterminated fragment (power cut mid-write) is skipped, matching fileLines().
inline int readFileLines(int start, int want, char (*dst)[LEN]) {
  if (!fsOk || want <= 0) return 0;
  File f = LittleFS.open(LOG_PATH, FILE_READ);
  if (!f) return 0;
  int idx = 0, got = 0, col = 0;
  uint8_t buf[256]; int r;
  bool done = false;
  while (!done && (r = f.read(buf, sizeof buf)) > 0) {
    for (int i = 0; i < r; i++) {
      char c = (char)buf[i];
      if (c == '\n') {
        if (idx >= start) { dst[got][col] = 0; if (++got >= want) { done = true; break; } }
        idx++; col = 0;
      } else if (c != '\r' && idx >= start && col < LEN - 1) {
        dst[got][col++] = c;
      }
    }
  }
  f.close();
  return got;
}

} // namespace applog
