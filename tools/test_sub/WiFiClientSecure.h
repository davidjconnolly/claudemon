// Host mock of <WiFiClientSecure.h>. HTTPClient.h's begin() loads the canned
// response body here, and net::readBody() pulls it off this "socket" the same
// way it reads the real one.
#pragma once
#include "Arduino.h"
#include <stdint.h>
#include <string.h>
#include <string>

class WiFiClientSecure {
public:
  std::string body;
  size_t pos = 0;
  // Simulates a peer that goes silent after `stallAfter` bytes but never closes
  // (connected() stays true). Each empty poll advances the mock clock 1 ms, so
  // without a deadline the read spins forever: the 2026-11-13 2 h freeze.
  long stallAfter = -1;
  // Simulates a peer that closes early: connected() goes false at end of body.
  bool closed = true;

  void setInsecure() {}
  void setHandshakeTimeout(unsigned long) {}

  int available() {
    size_t end = (stallAfter >= 0 && (size_t)stallAfter < body.size()) ? (size_t)stallAfter : body.size();
    if (pos < end) return (int)(end - pos);
    if (stallAfter >= 0) g_millis += 1;
    return 0;
  }
  int read(uint8_t* buf, size_t n) {
    int a = available();
    if (a <= 0) return 0;
    if (n > (size_t)a) n = (size_t)a;
    memcpy(buf, body.data() + pos, n);
    pos += n;
    return (int)n;
  }
  uint8_t connected() { return stallAfter >= 0 || !closed || pos < body.size(); }
};
