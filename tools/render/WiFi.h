// Host mock of <WiFi.h> — pretend we're online with a plausible IP/RSSI.
#pragma once
#include "Arduino.h"

enum { WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_CONNECTED = 3, WL_DISCONNECTED = 6 };

struct IPAddress {
  String toString() const { return String("192.168.0.142"); }
};

class WiFiClass {
public:
  int status() { return WL_CONNECTED; }
  int RSSI()   { return -58; }
  IPAddress localIP() { return IPAddress(); }
  void begin(const char*, const char* = "") {}
};
extern WiFiClass WiFi;
