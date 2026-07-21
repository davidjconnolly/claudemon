// Host mock of <HTTPClient.h> — a canned transport. Each test installs
// MockResponse fixtures; begin() picks the FIRST entry whose urlContains is a
// substring of the requested URL (so list more-specific paths first, e.g.
// "count_tokens" before "/v1/messages").
#pragma once
#include "Arduino.h"
#include "WiFiClientSecure.h"
#include <map>
#include <string>
#include <vector>

#define HTTPC_STRICT_FOLLOW_REDIRECTS 1

struct MockResponse {
  std::string urlContains;
  int code = 404;
  std::string body = "{}";
  std::map<std::string, std::string> headers;
};
extern std::vector<MockResponse> g_mockResponses;

class HTTPClient {
  MockResponse cur;
public:
  void setTimeout(int) {}
  void setReuse(bool) {}
  void setFollowRedirects(int) {}
  bool begin(WiFiClientSecure&, const String& url) {
    cur = MockResponse{};                       // default: 404 {}
    for (auto& m : g_mockResponses)
      if (url.s.find(m.urlContains) != std::string::npos) { cur = m; break; }
    return true;
  }
  void addHeader(const char*, const String&) {}
  void collectHeaders(const char**, int) {}
  int POST(uint8_t*, size_t) { return cur.code; }
  int GET() { return cur.code; }
  String getString() { return String(cur.body.c_str()); }
  String header(const char* name) {
    auto it = cur.headers.find(name);
    return it == cur.headers.end() ? String("") : String(it->second.c_str());
  }
  void end() {}
};
