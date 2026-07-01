// display_pm.h — backlight brightness, quiet hours, sleep/wake, auto-cycle.
// Ported from ohmyclawd; resolution-independent (NVS namespace = claudemon).
#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "time.h"
#include "config.h"

#define DPM_BACKLIGHT_PIN 21

namespace display_pm {

static constexpr uint32_t LEDC_FREQ_HZ  = 5000;
static constexpr uint8_t  LEDC_RES_BITS = 8;
static constexpr uint8_t  LEDC_CHANNEL  = 0;   // Arduino-ESP32 2.x only

static uint8_t  briPct        = 100;  // 0-100%
static uint8_t  qhStart       = 23;
static uint8_t  qhEnd         = 7;
static uint8_t  qhMode        = 0;    // 0=OFF 1=DIM 2=SLEEP
static uint8_t  cycSec        = 60;   // 0=OFF, 5-255 in 5s steps
static bool     inQuiet       = false;
static unsigned long wakeUntilMs = 0;
static bool     previewActive = false;
static uint8_t  currentDuty   = 0;
static uint8_t  targetDuty    = 0;
static unsigned long lastFadeMs = 0;
static bool     ledcOk        = false;

inline void writeDuty(uint8_t duty) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(DPM_BACKLIGHT_PIN, duty);
#else
  ledcWrite(LEDC_CHANNEL, duty);
#endif
}

inline void init(Preferences& prefs) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcOk = ledcAttach(DPM_BACKLIGHT_PIN, LEDC_FREQ_HZ, LEDC_RES_BITS);
#else
  ledcSetup(LEDC_CHANNEL, LEDC_FREQ_HZ, LEDC_RES_BITS);
  ledcAttachPin(DPM_BACKLIGHT_PIN, LEDC_CHANNEL);
  ledcOk = true;
#endif
  if (!ledcOk) {
    pinMode(DPM_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(DPM_BACKLIGHT_PIN, HIGH);
  }

  prefs.begin(NVS_NS, false);
  briPct  = prefs.getUChar("bri",  100);
  if (briPct < 10) briPct = 10;
  qhStart = prefs.getUChar("qh_s", 23);
  qhEnd   = prefs.getUChar("qh_e", 7);
  qhMode  = prefs.getUChar("qh_m", 0);
  cycSec  = prefs.getUChar("cyc",  60);
  prefs.end();

  if (briPct > 100) briPct = 100;
  if (qhStart > 23) qhStart = 23;
  if (qhEnd   > 23) qhEnd   = 7;
  if (qhMode  > 2)  qhMode  = 0;
  if (cycSec != 0 && cycSec < 5) cycSec = 60;

  targetDuty  = (briPct * 255) / 100;
  currentDuty = targetDuty;
  if (ledcOk) writeDuty(currentDuty);
}

inline void tick() {
  struct tm ti;
  bool haveTime = getLocalTime(&ti, 5);
  if (haveTime && qhStart != qhEnd) {
    uint8_t h = ti.tm_hour;
    inQuiet = (qhStart < qhEnd) ? (h >= qhStart && h < qhEnd)
                                : (h >= qhStart || h < qhEnd);
  } else {
    inQuiet = false;
  }

  if (!previewActive) {
    if (!inQuiet || qhMode == 0)      targetDuty = (briPct * 255) / 100;
    else if (qhMode == 1)             targetDuty = 25;
    else                              targetDuty = (wakeUntilMs > millis()) ? 25 : 0;
  }

  unsigned long now = millis();
  if (now - lastFadeMs >= 30) {
    lastFadeMs = now;
    if (currentDuty < targetDuty) {
      uint16_t next = (uint16_t)currentDuty + 8;
      currentDuty = (next > targetDuty) ? targetDuty : (uint8_t)next;
    } else if (currentDuty > targetDuty) {
      int16_t next = (int16_t)currentDuty - 8;
      currentDuty = (next < targetDuty) ? targetDuty : (uint8_t)next;
    }
    if (ledcOk) writeDuty(currentDuty);
  }
}

inline void wake(uint16_t ms)   { wakeUntilMs = millis() + ms; }
inline bool isSleeping()        { return inQuiet && qhMode == 2 && wakeUntilMs <= millis(); }
inline uint32_t getCycleIntervalMs() { return (cycSec == 0) ? 0UL : (uint32_t)cycSec * 1000UL; }

inline void setBrightness(uint8_t pct) {
  if (pct > 100) pct = 100;
  briPct = pct;
  if (!inQuiet || qhMode == 0) targetDuty = (briPct * 255) / 100;
  Preferences p; p.begin(NVS_NS, false); p.putUChar("bri", briPct); p.end();
}

inline void setQuietHours(uint8_t s, uint8_t e, uint8_t m) {
  qhStart = s % 24; qhEnd = e % 24; qhMode = m % 3;
  Preferences p; p.begin(NVS_NS, false);
  p.putUChar("qh_s", qhStart); p.putUChar("qh_e", qhEnd); p.putUChar("qh_m", qhMode);
  p.end();
}

inline void setCycle(uint8_t s) {
  cycSec = s;
  Preferences p; p.begin(NVS_NS, false); p.putUChar("cyc", cycSec); p.end();
}

} // namespace display_pm
