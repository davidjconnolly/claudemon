// Host mock of <WiFiClientSecure.h> — transport is faked in HTTPClient.h.
#pragma once
#include "Arduino.h"
class WiFiClientSecure {
public:
  void setInsecure() {}
};
