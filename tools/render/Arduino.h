// Host mock of <Arduino.h> — just enough for the CYD-Claudemon UI headers.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <string>
#include "pgmspace.h"

// --- Arduino String, backed by std::string ---
class String {
public:
  std::string s;
  String() {}
  String(const char* p) : s(p ? p : "") {}
  String(const std::string& x) : s(x) {}
  String(char c) : s(1, c) {}
  String(int v)           { char b[32]; snprintf(b, sizeof b, "%d",  v); s = b; }
  String(unsigned int v)  { char b[32]; snprintf(b, sizeof b, "%u",  v); s = b; }
  String(long v)          { char b[32]; snprintf(b, sizeof b, "%ld", v); s = b; }
  String(unsigned long v) { char b[32]; snprintf(b, sizeof b, "%lu", v); s = b; }
  String(double v, int dec = 2) { char b[40]; snprintf(b, sizeof b, "%.*f", dec, v); s = b; }
  const char* c_str() const { return s.c_str(); }
  size_t length() const { return s.size(); }
  bool isEmpty() const { return s.empty(); }
  bool startsWith(const char* p) const { return s.rfind(p, 0) == 0; }
  String substring(size_t a) const { return String(s.substr(a)); }
  String substring(size_t a, size_t b) const { return String(s.substr(a, b - a)); }
  String& operator+=(const String& o) { s += o.s; return *this; }
  String& operator+=(const char* o)   { s += o;   return *this; }
  String operator+(const String& o) const { String r(*this); r += o; return r; }
  String operator+(const char* o)   const { String r(*this); r.s += o; return r; }
  bool operator==(const char* o)   const { return s == o; }
  bool operator==(const String& o) const { return s == o.s; }
  int indexOf(char c) const { auto p = s.find(c); return p == std::string::npos ? -1 : (int)p; }
  float toFloat() const { return (float)atof(s.c_str()); }
};
inline String operator+(const char* a, const String& b) { String r(a); r += b; return r; }

// --- timing / rng (driven by the harness) ---
extern unsigned long g_millis;
inline unsigned long millis() { return g_millis; }
inline void delay(unsigned long) {}
inline long random(long n) { return n > 0 ? (long)(rand() % n) : 0; }
inline long random(long a, long b) { return b > a ? a + (long)(rand() % (b - a)) : a; }
inline void randomSeed(unsigned long s) { srand((unsigned)s); }
inline uint32_t esp_random() { return (uint32_t)rand(); }

inline long map(long x, long im, long ix, long om, long ox) {
  return (x - im) * (ox - om) / (ix - im) + om;
}

#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

// --- Serial stub (data-layer headers log through it; discarded on host) ---
struct SerialStub {
  void println(const char*) {}
  void printf(const char*, ...) {}
};
static SerialStub Serial;

// --- ESP time helper (filled by the harness) ---
bool getLocalTime(struct tm* info, uint32_t ms = 0);

// --- GPIO / LEDC stubs (display_pm.h backlight; never exercised in render) ---
#define OUTPUT 1
#define HIGH 1
#define LOW 0
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline void ledcSetup(uint8_t, uint32_t, uint8_t) {}
inline void ledcAttachPin(uint8_t, uint8_t) {}
inline bool ledcAttach(uint8_t, uint32_t, uint8_t) { return true; }
inline void ledcWrite(uint8_t, uint32_t) {}

// --- ESP info object ---
struct EspClass {
  const char* getChipModel()   { return "ESP32-D0WD"; }
  uint8_t     getChipRevision(){ return 3; }
  uint8_t     getChipCores()   { return 2; }
  uint32_t    getCpuFreqMHz()  { return 240; }
  uint32_t    getFlashChipSize(){ return 4u * 1024u * 1024u; }
  uint32_t    getSketchSize()  { return 1206000; }
  uint32_t    getFreeHeap()    { return 205000; }
  uint32_t    getHeapSize()    { return 327680; }
  void        restart()        {}
};
extern EspClass ESP;
