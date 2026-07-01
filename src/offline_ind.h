// offline_ind.h — data-freshness state (ONLINE/STALE/OFFLINE) + colour-drain
// when the data source is unreachable. The on-device indicator is just the small
// static corner dot drawn by ui::drawStatus(); the full health breakdown is on
// the web /status page. (No blinking glyph — it was visually noisy.)
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include "config.h"
#include "theme.h"

namespace offline_ind {

enum State { ONLINE, STALE, OFFLINE };

static constexpr unsigned long STALE_AFTER_MS   = 90UL  * 1000UL;
static constexpr unsigned long OFFLINE_AFTER_MS = 240UL * 1000UL;

static State         currentState   = OFFLINE;
static unsigned long lastSuccessMs  = 0;
static bool          hadSuccessEver = false;

inline void recordSuccess() { lastSuccessMs = millis(); hadSuccessEver = true; }
inline void recordFailure() { (void)0; }

inline State update() {
  if (WiFi.status() != WL_CONNECTED || !hadSuccessEver) {
    currentState = OFFLINE; return currentState;
  }
  unsigned long age = millis() - lastSuccessMs;
  if (age >= OFFLINE_AFTER_MS)     currentState = OFFLINE;
  else if (age >= STALE_AFTER_MS)  currentState = STALE;
  else                             currentState = ONLINE;
  return currentState;
}

inline State state() { return currentState; }

// Drains accent colours to grey when not fully online.
inline uint16_t tintColor(uint16_t base) {
  return (currentState == ONLINE) ? base : COL_DIM;
}

// Seconds since the last good reading (for "updated Ns ago" style hints).
inline long ageSec() {
  if (!hadSuccessEver) return -1;
  return (long)((millis() - lastSuccessMs) / 1000);
}

} // namespace offline_ind
