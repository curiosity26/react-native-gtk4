// HTTP and WebSocket clients for ReactCxxPlatform, on libsoup 3.
//
// They back JS fetch/XMLHttpRequest and WebSocket, and the dev loop (bundle
// download, Metro's /message socket, the HMR socket).
//
// All soup work runs on one "rngtk-network" thread with its own
// GMainContext, never on the GTK main loop: DevServerHelper blocks on a
// future for the bundle, and that must not need the thread it blocks. So
// callbacks arrive on the network thread; React Native's consumers
// (NetworkingModule, WebSocketModule, PackagerConnection, DevServerHelper)
// hop to the JS thread themselves.
#include <glog/logging.h>
#include <libsoup/soup.h>
#include <react/http/IHttpClient.h>
#include <react/http/IWebSocketClient.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

namespace facebook::react {

namespace {

// ---------------------------------------------------------------------------
// The network thread

class NetworkThread {
 public:
  static NetworkThread &get() {
    // Never destroyed: requests may still be finishing during exit.
    static auto *instance = new NetworkThread();
    return *instance;
  }

  // Runs `fn` on the network thread (later, even when called from it).
  void post(std::function<void()> fn) {
    auto *data = new std::function<void()>(std::move(fn));
    GSource *source = g_idle_source_new();
    g_source_set_priority(source, G_PRIORITY_DEFAULT);
    g_source_set_callback(
        source,
        [](gpointer p) -> gboolean {
          (*static_cast<std::function<void()> *>(p))();
          return G_SOURCE_REMOVE;
        },
        data, [](gpointer p) { delete static_cast<std::function<void()> *>(p); });
    g_source_attach(source, context_);
    g_source_unref(source);
  }

  GMainContext *context() const { return context_; }
  SoupSession *session() const { return session_; }

 private:
  NetworkThread() : context_(g_main_context_new()) {
    std::mutex mutex;
    std::condition_variable ready;
    bool started = false;
    thread_ = std::thread([&] {
      g_main_context_push_thread_default(context_);
      // Created here so its async operations belong to this context.
      session_ = soup_session_new_with_options("user-agent",
                                               "react-native-gtk4", nullptr);
      // React Native applies its own per-request timeouts.
      soup_session_set_timeout(session_, 0);
      GMainLoop *loop = g_main_loop_new(context_, FALSE);
      {
        std::lock_guard<std::mutex> lock(mutex);
        started = true;
      }
      ready.notify_one();
      g_main_loop_run(loop);
    });
    std::unique_lock<std::mutex> lock(mutex);
    ready.wait(lock, [&] { return started; });
    thread_.detach();
  }

  GMainContext *context_;
  SoupSession *session_{nullptr};
  std::thread thread_;
};

std::string errorMessage(GError *error) {
  return error && error->message ? error->message : "unknown error";
}

// ---------------------------------------------------------------------------
// HTTP

constexpr gsize kReadChunk = 64 * 1024;

// One request; lives until its last callback, touched only on the network
// thread (cancel() posts there).
struct Request : std::enable_shared_from_this<Request> {
  http::NetworkCallbacks callbacks;
  SoupMessage *message{nullptr};
  GCancellable *cancellable{g_cancellable_new()};
  GSource *timeoutSource{nullptr};
  GInputStream *stream{nullptr};
  std::string body;      // whole body (non-incremental)
  std::string pending;   // incremental text not yet taken by the callback
  int64_t loaded{0};
  int64_t total{-1};
  int64_t uploaded{0};
  int64_t uploadTotal{0};
  bool cancelled{false};
  bool timedOut{false};
  bool done{false};

  ~Request() {
    if (timeoutSource) {
      g_source_destroy(timeoutSource);
      g_source_unref(timeoutSource);
    }
    g_clear_object(&stream);
    g_clear_object(&message);
    g_clear_object(&cancellable);
  }

  void complete(const std::string &error) {
    if (done) return;
    done = true;
    if (timeoutSource) g_source_destroy(timeoutSource);
    // A cancelled request reports nothing: its owner has moved on.
    if (cancelled) return;
    if (callbacks.onResponseComplete) {
      callbacks.onResponseComplete(timedOut ? "" : error, timedOut);
    }
  }
};

// The request travels through GIO callbacks as a heap shared_ptr.
using RequestRef = std::shared_ptr<Request>;

void readNext(RequestRef request);

void onRead(GObject *source, GAsyncResult *result, gpointer data) {
  RequestRef request = std::move(*static_cast<RequestRef *>(data));
  delete static_cast<RequestRef *>(data);
  GError *error = nullptr;
  GBytes *bytes =
      g_input_stream_read_bytes_finish(G_INPUT_STREAM(source), result, &error);
  if (!bytes) {
    request->complete(errorMessage(error));
    g_clear_error(&error);
    return;
  }
  gsize size = 0;
  auto *chunk = static_cast<const char *>(g_bytes_get_data(bytes, &size));
  auto &cb = request->callbacks;
  if (size > 0 && !request->cancelled) {
    request->loaded += size;
    if (cb.sendIncrementalUpdates && cb.onBodyIncremental) {
      request->pending.append(chunk, size);
      // The callback returns how much it took (whole UTF-8 sequences).
      int64_t taken = cb.onBodyIncremental(
          request->loaded, request->total,
          folly::IOBuf::copyBuffer(request->pending));
      request->pending.erase(
          0, std::min<size_t>(std::max<int64_t>(taken, 0),
                              request->pending.size()));
    } else {
      request->body.append(chunk, size);
    }
    if (cb.sendProgressUpdates && cb.onBodyProgress) {
      cb.onBodyProgress(request->loaded, request->total);
    }
  }
  g_bytes_unref(bytes);
  if (size > 0) {
    readNext(std::move(request));
    return;
  }
  // End of body.
  if (!request->cancelled && cb.onBody &&
      !(cb.sendIncrementalUpdates && cb.onBodyIncremental)) {
    cb.onBody(folly::IOBuf::copyBuffer(request->body));
  }
  request->complete("");
}

void readNext(RequestRef request) {
  GInputStream *stream = request->stream;
  GCancellable *cancellable = request->cancellable;
  g_input_stream_read_bytes_async(stream, kReadChunk, G_PRIORITY_DEFAULT,
                                  cancellable, onRead,
                                  new RequestRef(std::move(request)));
}

void onSent(GObject *source, GAsyncResult *result, gpointer data) {
  RequestRef request = std::move(*static_cast<RequestRef *>(data));
  delete static_cast<RequestRef *>(data);
  GError *error = nullptr;
  request->stream =
      soup_session_send_finish(SOUP_SESSION(source), result, &error);
  if (!request->stream) {
    request->complete(errorMessage(error));
    g_clear_error(&error);
    return;
  }
  SoupMessageHeaders *responseHeaders =
      soup_message_get_response_headers(request->message);
  request->total = soup_message_headers_get_content_length(responseHeaders);
  if (request->total == 0 &&
      soup_message_headers_get_encoding(responseHeaders) !=
          SOUP_ENCODING_CONTENT_LENGTH) {
    request->total = -1;
  }
  if (!request->cancelled && request->callbacks.onResponse) {
    http::Headers headers;
    SoupMessageHeadersIter iter;
    soup_message_headers_iter_init(&iter, responseHeaders);
    const char *name;
    const char *value;
    while (soup_message_headers_iter_next(&iter, &name, &value)) {
      headers.emplace_back(name, value);
    }
    request->callbacks.onResponse(
        static_cast<uint16_t>(soup_message_get_status(request->message)),
        std::move(headers));
  }
  readNext(std::move(request));
}

std::optional<std::string> readFileUri(const std::string &uri) {
  gchar *path = g_filename_from_uri(uri.c_str(), nullptr, nullptr);
  std::string file = path ? path : uri;
  g_free(path);
  std::ifstream in(file, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

std::string headerValue(const http::Headers &headers, const char *name) {
  for (const auto &[k, v] : headers) {
    if (g_ascii_strcasecmp(k.c_str(), name) == 0) return v;
  }
  return "";
}

// Fills the request body; returns an error message, or "" on success.
std::string setBody(SoupMessage *message, const http::Headers &headers,
                    const http::Body &body, int64_t *size) {
  std::string contentType = headerValue(headers, "Content-Type");
  const char *type = contentType.empty() ? nullptr : contentType.c_str();
  auto setBytes = [&](GBytes *bytes) {
    *size = g_bytes_get_size(bytes);
    soup_message_set_request_body_from_bytes(message, type, bytes);
    g_bytes_unref(bytes);
  };
  if (body.string) {
    setBytes(g_bytes_new(body.string->data(), body.string->size()));
  } else if (body.base64) {
    gsize len = 0;
    guchar *data = g_base64_decode(body.base64->c_str(), &len);
    setBytes(g_bytes_new_take(data, len));
  } else if (body.formData) {
    SoupMultipart *multipart = soup_multipart_new(SOUP_FORM_MIME_TYPE_MULTIPART);
    for (const auto &field : *body.formData) {
      std::string data;
      if (field.string) {
        data = *field.string;
      } else if (field.uri) {
        auto contents = readFileUri(*field.uri);
        if (!contents) {
          soup_multipart_free(multipart);
          return "Could not read form data file " + *field.uri;
        }
        data = std::move(*contents);
      }
      SoupMessageHeaders *partHeaders =
          soup_message_headers_new(SOUP_MESSAGE_HEADERS_MULTIPART);
      bool hasDisposition = false;
      for (const auto &[k, v] : field.headers) {
        soup_message_headers_append(partHeaders, k.c_str(), v.c_str());
        hasDisposition |=
            g_ascii_strcasecmp(k.c_str(), "Content-Disposition") == 0;
      }
      if (!hasDisposition) {
        std::string disposition = "form-data; name=\"" + field.fieldName + "\"";
        soup_message_headers_append(partHeaders, "Content-Disposition",
                                    disposition.c_str());
      }
      GBytes *bytes = g_bytes_new(data.data(), data.size());
      soup_multipart_append_part(multipart, partHeaders, bytes);
      g_bytes_unref(bytes);
      soup_message_headers_unref(partHeaders);
    }
    GBytes *bytes = nullptr;
    soup_multipart_to_message(multipart, soup_message_get_request_headers(message),
                              &bytes);
    soup_multipart_free(multipart);
    *size = g_bytes_get_size(bytes);
    // The multipart boundary is in the Content-Type set above.
    soup_message_set_request_body_from_bytes(message, nullptr, bytes);
    g_bytes_unref(bytes);
  } else if (body.blob) {
    return "Blob request bodies are not supported yet";
  }
  return "";
}

class SoupRequestToken : public http::IRequestToken {
 public:
  explicit SoupRequestToken(std::weak_ptr<Request> request)
      : request_(std::move(request)) {}

  // Dropping the token does not cancel: DevServerHelper discards it.
  void cancel() noexcept override {
    NetworkThread::get().post([weak = request_] {
      if (auto request = weak.lock()) {
        request->cancelled = true;
        g_cancellable_cancel(request->cancellable);
      }
    });
  }

 private:
  std::weak_ptr<Request> request_;
};

class SoupHttpClient : public IHttpClient {
 public:
  std::unique_ptr<http::IRequestToken> sendRequest(
      http::NetworkCallbacks &&callbacks, const std::string &method,
      const std::string &url, const http::Headers &headers,
      const http::Body &body, uint32_t timeout,
      std::optional<std::string> /*loggingId*/) override {
    auto request = std::make_shared<Request>();
    request->callbacks = std::move(callbacks);
    std::weak_ptr<Request> weak = request;
    NetworkThread::get().post([request, method, url, headers, body, timeout] {
      start(request, method, url, headers, body, timeout);
    });
    return std::make_unique<SoupRequestToken>(weak);
  }

 private:
  static void start(const RequestRef &request, const std::string &method,
                    const std::string &url, const http::Headers &headers,
                    const http::Body &body, uint32_t timeout) {
    if (request->cancelled) return;
    request->message = soup_message_new(method.c_str(), url.c_str());
    if (!request->message) {
      request->complete("Invalid URL: " + url);
      return;
    }
    SoupMessageHeaders *requestHeaders =
        soup_message_get_request_headers(request->message);
    for (const auto &[name, value] : headers) {
      soup_message_headers_append(requestHeaders, name.c_str(), value.c_str());
    }
    std::string error =
        setBody(request->message, headers, body, &request->uploadTotal);
    if (!error.empty()) {
      request->complete(error);
      return;
    }
    if (request->callbacks.onUploadProgress && request->uploadTotal > 0) {
      g_signal_connect_data(
          request->message, "wrote-body-data",
          G_CALLBACK(+[](SoupMessage *, guint chunk, gpointer data) {
            auto *weak = static_cast<std::weak_ptr<Request> *>(data);
            if (auto r = weak->lock(); r && !r->cancelled) {
              r->uploaded += chunk;
              r->callbacks.onUploadProgress(r->uploaded, r->uploadTotal);
            }
          }),
          new std::weak_ptr<Request>(request),
          +[](gpointer data, GClosure *) {
            delete static_cast<std::weak_ptr<Request> *>(data);
          },
          GConnectFlags(0));
    }
    if (timeout > 0) {
      request->timeoutSource = g_timeout_source_new(timeout);
      g_source_set_callback(
          request->timeoutSource,
          [](gpointer data) -> gboolean {
            auto *weak = static_cast<std::weak_ptr<Request> *>(data);
            if (auto r = weak->lock(); r && !r->done) {
              r->timedOut = true;
              g_cancellable_cancel(r->cancellable);
            }
            return G_SOURCE_REMOVE;
          },
          new std::weak_ptr<Request>(request),
          [](gpointer data) {
            delete static_cast<std::weak_ptr<Request> *>(data);
          });
      g_source_attach(request->timeoutSource, NetworkThread::get().context());
    }
    soup_session_send_async(NetworkThread::get().session(), request->message,
                            G_PRIORITY_DEFAULT, request->cancellable, onSent,
                            new RequestRef(request));
  }
};

// ---------------------------------------------------------------------------
// WebSocket

class SoupWebSocketClient : public IWebSocketClient {
 public:
  SoupWebSocketClient() : impl_(std::make_shared<Impl>()) {}

  ~SoupWebSocketClient() override {
    {
      std::lock_guard<std::mutex> lock(impl_->mutex);
      impl_->onClosed = nullptr;
      impl_->onMessage = nullptr;
      impl_->onConnect = nullptr;
    }
    NetworkThread::get().post([impl = impl_] { impl->shutdown("destroyed"); });
  }

  void setOnClosedCallback(OnClosedCallback &&callback) noexcept override {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->onClosed = std::move(callback);
  }

  void setOnMessageCallback(OnMessageCallback &&callback) noexcept override {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->onMessage = std::move(callback);
  }

  void connect(const std::string &url, OnConnectCallback &&callback) override {
    {
      std::lock_guard<std::mutex> lock(impl_->mutex);
      impl_->onConnect = std::move(callback);
    }
    NetworkThread::get().post([impl = impl_, url] { impl->connect(url); });
  }

  void close(const std::string &reason) override {
    NetworkThread::get().post([impl = impl_, reason] { impl->shutdown(reason); });
  }

  void send(const std::string &message) override {
    NetworkThread::get().post([impl = impl_, message] { impl->send(message); });
  }

  void ping() override {
    // libsoup has no single-ping call; keepalive pings serve the same purpose.
    NetworkThread::get().post([impl = impl_] {
      if (impl->connection) {
        soup_websocket_connection_set_keepalive_interval(impl->connection, 15);
      }
    });
  }

 private:
  struct Impl : std::enable_shared_from_this<Impl> {
    std::mutex mutex;  // guards the callbacks
    OnConnectCallback onConnect;
    OnClosedCallback onClosed;
    OnMessageCallback onMessage;

    // Network thread only.
    SoupWebsocketConnection *connection{nullptr};
    GCancellable *cancellable{nullptr};
    std::deque<std::string> outbox;  // sent before the socket opened
    bool closing{false};

    ~Impl() {
      if (connection) {
        g_signal_handlers_disconnect_by_data(connection, this);
        g_object_unref(connection);
      }
      g_clear_object(&cancellable);
    }

    void connect(const std::string &url) {
      // libsoup takes http(s) URLs for the WebSocket handshake.
      std::string httpUrl = url;
      if (httpUrl.rfind("ws://", 0) == 0) httpUrl = "http://" + httpUrl.substr(5);
      if (httpUrl.rfind("wss://", 0) == 0) httpUrl = "https://" + httpUrl.substr(6);
      SoupMessage *message = soup_message_new(SOUP_METHOD_GET, httpUrl.c_str());
      if (!message) {
        connected(false, "Invalid URL: " + url);
        return;
      }
      cancellable = g_cancellable_new();
      soup_session_websocket_connect_async(
          NetworkThread::get().session(), message, nullptr, nullptr,
          G_PRIORITY_DEFAULT, cancellable, onConnected,
          new std::shared_ptr<Impl>(shared_from_this()));
      g_object_unref(message);
    }

    static void onConnected(GObject *source, GAsyncResult *result,
                            gpointer data) {
      auto self = std::move(*static_cast<std::shared_ptr<Impl> *>(data));
      delete static_cast<std::shared_ptr<Impl> *>(data);
      GError *error = nullptr;
      SoupWebsocketConnection *connection =
          soup_session_websocket_connect_finish(SOUP_SESSION(source), result,
                                                &error);
      if (!connection) {
        self->connected(false, errorMessage(error));
        g_clear_error(&error);
        return;
      }
      if (self->closing) {
        soup_websocket_connection_close(connection, SOUP_WEBSOCKET_CLOSE_NORMAL,
                                        nullptr);
        g_object_unref(connection);
        return;
      }
      self->connection = connection;
      // HMR updates and LogBox symbolication payloads exceed soup's 128 KiB
      // default.
      soup_websocket_connection_set_max_incoming_payload_size(connection, 0);
      g_signal_connect(connection, "message", G_CALLBACK(onMessageSignal),
                       self.get());
      g_signal_connect(connection, "closed", G_CALLBACK(onClosedSignal),
                       self.get());
      self->connected(true, "");
      while (!self->outbox.empty()) {
        self->send(self->outbox.front());
        self->outbox.pop_front();
      }
    }

    void connected(bool ok, const std::string &error) {
      OnConnectCallback cb;
      {
        std::lock_guard<std::mutex> lock(mutex);
        cb = onConnect;
      }
      if (cb) cb(ok, error);
    }

    static void onMessageSignal(SoupWebsocketConnection *, gint /*type*/,
                                GBytes *message, gpointer data) {
      auto *self = static_cast<Impl *>(data);
      gsize size = 0;
      auto *bytes = static_cast<const char *>(g_bytes_get_data(message, &size));
      OnMessageCallback cb;
      {
        std::lock_guard<std::mutex> lock(self->mutex);
        cb = self->onMessage;
      }
      if (cb) cb(std::string(bytes ? bytes : "", size));
    }

    static void onClosedSignal(SoupWebsocketConnection *connection,
                               gpointer data) {
      auto *self = static_cast<Impl *>(data);
      const char *reason = soup_websocket_connection_get_close_data(connection);
      OnClosedCallback cb;
      {
        std::lock_guard<std::mutex> lock(self->mutex);
        cb = self->onClosed;
      }
      if (cb) cb(reason ? reason : "");
    }

    void send(const std::string &message) {
      if (!connection) {
        if (!closing) outbox.push_back(message);
        return;
      }
      if (soup_websocket_connection_get_state(connection) !=
          SOUP_WEBSOCKET_STATE_OPEN) {
        return;
      }
      // Messages are text frames; send_text needs a NUL-terminated string.
      soup_websocket_connection_send_text(connection, message.c_str());
    }

    void shutdown(const std::string &reason) {
      closing = true;
      outbox.clear();
      if (cancellable) g_cancellable_cancel(cancellable);
      if (connection && soup_websocket_connection_get_state(connection) ==
                            SOUP_WEBSOCKET_STATE_OPEN) {
        soup_websocket_connection_close(connection, SOUP_WEBSOCKET_CLOSE_NORMAL,
                                        reason.c_str());
      }
    }
  };

  std::shared_ptr<Impl> impl_;
};

}  // namespace

HttpClientFactory getHttpClientFactory() {
  return []() { return std::make_unique<SoupHttpClient>(); };
}

WebSocketClientFactory getWebSocketClientFactory() {
  return []() { return std::make_unique<SoupWebSocketClient>(); };
}

}  // namespace facebook::react
