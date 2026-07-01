// Host mock of <Preferences.h> — render path never reads/writes NVS.
#pragma once
#include "Arduino.h"
class Preferences {
public:
  bool begin(const char*, bool = false) { return true; }
  void end() {}
  void clear() {}
  String   getString(const char*, const char* def = "") { return String(def); }
  long long getLong64(const char*, long long def = 0) { return def; }
  double   getDouble(const char*, double def = 0) { return def; }
  uint8_t  getUChar (const char*, uint8_t def = 0) { return def; }
  uint16_t getUShort(const char*, uint16_t def = 0) { return def; }
  bool     getBool  (const char*, bool def = false) { return def; }
  void putString(const char*, const String&) {}
  void putString(const char*, const char*) {}
  void putLong64(const char*, long long) {}
  void putDouble(const char*, double) {}
  void putUChar (const char*, uint8_t) {}
  void putUShort(const char*, uint16_t) {}
  void putBool  (const char*, bool) {}
};
