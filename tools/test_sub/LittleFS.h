// Host mock of <LittleFS.h> — applog.h compiles against it; begin() reports
// no filesystem, so the flash-log paths are never executed on host.
#pragma once
#include "Arduino.h"

#define FILE_READ   "r"
#define FILE_APPEND "a"

class File {
public:
  explicit operator bool() const { return false; }
  size_t size() { return 0; }
  void   close() {}
  void   println(const char*) {}
  bool   seek(size_t) { return false; }
  int    available() { return 0; }
  int    read() { return -1; }
  int    read(uint8_t*, size_t) { return 0; }
};

class LittleFSClass {
public:
  bool begin(bool = false) { return false; }
  File open(const char*, const char* = FILE_READ) { return File(); }
  bool exists(const char*) { return false; }
  bool remove(const char*) { return false; }
  bool rename(const char*, const char*) { return false; }
};
static LittleFSClass LittleFS;
