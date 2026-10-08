// Checks the libsoup HTTP and WebSocket clients against a running Metro:
//
//   rngtk-net-check [http://localhost:8081]
//
// GET /status must answer "packager-status:running"; a POST with a body and
// a request to a closed port must complete; ws://.../message must connect.
// Callbacks arrive on the network thread while this thread waits, as
// DevServerHelper's bundle download does.
#include <react/http/IHttpClient.h>
#include <react/http/IWebSocketClient.h>

#include <chrono>
#include <cstdio>
#include <future>
#include <string>

using namespace facebook::react;

namespace {

int failures = 0;

void check(bool ok, const std::string &what) {
  printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) failures++;
}

struct Result {
  int status = 0;
  std::string body;
  std::string error;
  bool timedOut = false;
};

Result fetch(IHttpClient &client, const std::string &method,
             const std::string &url, const http::Body &body = {},
             uint32_t timeout = 0) {
  auto result = std::make_shared<Result>();
  auto done = std::make_shared<std::promise<void>>();
  http::NetworkCallbacks callbacks{
      .onResponse = [result](uint16_t status,
                             const http::Headers &) { result->status = status; },
      .onBody =
          [result](std::unique_ptr<folly::IOBuf> buf) {
            result->body = buf->moveToFbString().toStdString();
          },
      .onResponseComplete =
          [result, done](const std::string &error, bool timedOut) {
            result->error = error;
            result->timedOut = timedOut;
            done->set_value();
          },
  };
  client.sendRequest(std::move(callbacks), method, url,
                     {{"Content-Type", "text/plain"}}, body, timeout);
  auto future = done->get_future();
  if (future.wait_for(std::chrono::seconds(15)) != std::future_status::ready) {
    result->error = "no completion within 15 s";
  }
  return *result;
}

}  // namespace

int main(int argc, char **argv) {
  std::string base = argc > 1 ? argv[1] : "http://localhost:8081";
  auto client = getHttpClientFactory()();

  Result status = fetch(*client, "GET", base + "/status");
  check(status.status == 200 && status.body == "packager-status:running",
        "GET /status -> " + std::to_string(status.status) + " \"" +
            status.body + "\"");

  // Metro answers unknown paths with 404 and a body; it must still complete.
  http::Body body;
  body.string = "hello";
  Result post = fetch(*client, "POST", base + "/rngtk-net-check", body);
  check(post.error.empty() && post.status >= 400,
        "POST with a body completes (" + std::to_string(post.status) + ")");

  Result refused = fetch(*client, "GET", "http://127.0.0.1:1/");
  check(!refused.error.empty(), "connection errors are reported (" +
                                    refused.error + ")");

  auto ws = getWebSocketClientFactory()();
  std::promise<std::pair<bool, std::string>> connected;
  std::string wsUrl = "ws://" + base.substr(base.find("://") + 3) + "/message";
  ws->connect(wsUrl, [&](bool ok, const std::string &error) {
    connected.set_value({ok, error});
  });
  auto future = connected.get_future();
  bool ok = future.wait_for(std::chrono::seconds(10)) ==
            std::future_status::ready;
  auto [wsOk, wsError] = ok ? future.get() : std::pair{false, "timeout"};
  check(wsOk, "WebSocket " + wsUrl + " connects" +
                  (wsError.empty() ? "" : " (" + wsError + ")"));
  ws->close("done");

  return failures ? 1 : 0;
}
