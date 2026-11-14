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
#define HTTPC_ERROR_READ_TIMEOUT (-11)

struct MockResponse {
  std::string urlContains;
  int code = 404;
  std::string body = "{}";
  std::map<std::string, std::string> headers;
  long stallAfter = -1;      // see WiFiClientSecure.h: peer goes silent after N body bytes
  bool sendLength = true;    // false = close-delimited body (getSize() == -1)
};
extern std::vector<MockResponse> g_mockResponses;

// Counts every request the transport serves. Lets a test assert the poller's
// "at most one request per step" invariant rather than just the end state.
extern int g_requestCount;

class HTTPClient {
  MockResponse cur;
public:
  void setTimeout(int) {}
  void setConnectTimeout(int) {}
  void setReuse(bool) {}
  void setFollowRedirects(int) {}
  void useHTTP10(bool) {}
  bool begin(WiFiClientSecure& client, const String& url) {
    cur = MockResponse{};                       // default: 404 {}
    for (auto& m : g_mockResponses)
      if (url.s.find(m.urlContains) != std::string::npos) { cur = m; break; }
    client.body = cur.body;                     // the body is read off the "socket"
    client.pos = 0;
    client.stallAfter = cur.stallAfter;
    return true;
  }
  int getSize() { return cur.sendLength ? (int)cur.body.size() : -1; }
  void addHeader(const char*, const String&) {}
  void collectHeaders(const char**, int) {}
  int POST(uint8_t*, size_t) { g_requestCount++; return cur.code; }
  int GET() { g_requestCount++; return cur.code; }
  String header(const char* name) {
    auto it = cur.headers.find(name);
    return it == cur.headers.end() ? String("") : String(it->second.c_str());
  }
  void end() {}
};
