// Networking is not implemented yet: these clients accept requests and never
// answer. ReactHost requires the factories to exist.
#include <react/http/IHttpClient.h>
#include <react/http/IWebSocketClient.h>

namespace facebook::react {

namespace {

class StubRequestToken : public http::IRequestToken {
 public:
  void cancel() noexcept override {}
};

class StubHttpClient : public IHttpClient {
 public:
  std::unique_ptr<http::IRequestToken> sendRequest(
      http::NetworkCallbacks && /*callback*/,
      const std::string & /*method*/,
      const std::string & /*url*/,
      const http::Headers & /*headers*/,
      const http::Body & /*body*/,
      uint32_t /*timeout*/,
      std::optional<std::string> /*loggingId*/) override {
    return std::make_unique<StubRequestToken>();
  }
};

class StubWebSocketClient : public IWebSocketClient {
 public:
  void setOnClosedCallback(OnClosedCallback && /*callback*/) noexcept override {}
  void setOnMessageCallback(OnMessageCallback && /*callback*/) noexcept override {}
  void connect(const std::string & /*url*/,
               OnConnectCallback && /*onConnectCallback*/) override {}
  void close(const std::string & /*reason*/) override {}
  void send(const std::string & /*message*/) override {}
  void ping() override {}
};

}  // namespace

HttpClientFactory getHttpClientFactory() {
  return []() { return std::make_unique<StubHttpClient>(); };
}

WebSocketClientFactory getWebSocketClientFactory() {
  return []() { return std::make_unique<StubWebSocketClient>(); };
}

}  // namespace facebook::react
