#include "GtkImageLoader.h"

#include <glog/logging.h>

#include <fstream>
#include <sstream>
#include <thread>

using namespace facebook::react;

namespace rngtk {

namespace {

constexpr size_t kCacheLimitBytes = 128u << 20;

void on_main(std::function<void()> fn) {
  g_main_context_invoke_full(
      nullptr, G_PRIORITY_DEFAULT,
      [](gpointer data) -> gboolean {
        (*static_cast<std::function<void()> *>(data))();
        return G_SOURCE_REMOVE;
      },
      new std::function<void()>(std::move(fn)),
      [](gpointer data) { delete static_cast<std::function<void()> *>(data); });
}

// Decoding and file reads run here, off the main loop.
void on_worker(std::function<void()> fn) {
  static GThreadPool *pool = g_thread_pool_new(
      [](gpointer data, gpointer) {
        auto *fn = static_cast<std::function<void()> *>(data);
        (*fn)();
        delete fn;
      },
      nullptr, 2, FALSE, nullptr);
  g_thread_pool_push(pool, new std::function<void()>(std::move(fn)), nullptr);
}

int hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// data:[<mediatype>][;base64],<data>
bool decode_data_uri(const std::string &uri, std::string *out) {
  auto comma = uri.find(',');
  if (comma == std::string::npos) return false;
  std::string meta = uri.substr(5, comma - 5);
  std::string payload = uri.substr(comma + 1);
  if (meta.size() >= 7 && meta.compare(meta.size() - 7, 7, ";base64") == 0) {
    gsize len = 0;
    guchar *data = g_base64_decode(payload.c_str(), &len);
    out->assign(reinterpret_cast<char *>(data), len);
    g_free(data);
    return true;
  }
  out->clear();
  for (size_t i = 0; i < payload.size(); i++) {
    if (payload[i] == '%' && i + 2 < payload.size() &&
        hex(payload[i + 1]) >= 0 && hex(payload[i + 2]) >= 0) {
      out->push_back(char(hex(payload[i + 1]) * 16 + hex(payload[i + 2])));
      i += 2;
    } else {
      out->push_back(payload[i]);
    }
  }
  return true;
}

}  // namespace

GtkImageLoader::GtkImageLoader(HttpClientFactory httpClientFactory)
    : httpClientFactory_(std::move(httpClientFactory)),
      httpClient_(httpClientFactory_()) {}

std::shared_ptr<GdkTexture> GtkImageLoader::wrap(GdkTexture *texture) {
  if (!texture) return nullptr;
  return std::shared_ptr<GdkTexture>(
      texture, [](GdkTexture *t) { g_object_unref(t); });
}

void GtkImageLoader::load(
    const std::string &uri,
    const std::vector<std::pair<std::string, std::string>> &headers,
    Callback done) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = index_.find(uri); it != index_.end()) {
      cache_.splice(cache_.begin(), cache_, it->second);  // most recent
      Result result{it->second->second, "", 200};
      on_main([done, result] { done(result); });
      return;
    }
    auto &waiting = inFlight_[uri];
    waiting.push_back(std::move(done));
    if (waiting.size() > 1) return;  // already loading
  }

  auto self = shared_from_this();
  if (uri.rfind("http://", 0) == 0 || uri.rfind("https://", 0) == 0) {
    struct State {
      int status = 0;
    };
    auto state = std::make_shared<State>();
    auto body = std::make_shared<std::string>();
    http::NetworkCallbacks callbacks{
        .onResponse = [state](uint16_t status,
                              const http::Headers &) { state->status = status; },
        .onBody =
            [body](std::unique_ptr<folly::IOBuf> buf) {
              *body = buf->moveToFbString().toStdString();
            },
        .onResponseComplete =
            [self, uri, state, body](const std::string &error, bool timeout) {
              if (!error.empty() || timeout) {
                self->finish(uri, Result{nullptr,
                                         timeout ? "Timed out" : error, 0});
              } else if (state->status >= 400) {
                self->finish(uri, Result{nullptr,
                                         "HTTP " + std::to_string(state->status),
                                         state->status});
              } else {
                self->decode(uri, body, state->status);
              }
            },
    };
    http::Headers h(headers.begin(), headers.end());
    httpClient_->sendRequest(std::move(callbacks), "GET", uri, h);
    return;
  }

  on_worker([self, uri] {
    auto bytes = std::make_shared<std::string>();
    if (uri.rfind("data:", 0) == 0) {
      if (!decode_data_uri(uri, bytes.get())) {
        self->finish(uri, Result{nullptr, "Malformed data URI", 0});
        return;
      }
    } else {
      // file:// URL or a plain path.
      std::string path = uri;
      if (uri.rfind("file://", 0) == 0) {
        gchar *p = g_filename_from_uri(uri.c_str(), nullptr, nullptr);
        path = p ? p : uri.substr(7);
        g_free(p);
      }
      std::ifstream in(path, std::ios::binary);
      if (!in) {
        self->finish(uri, Result{nullptr, "No such file: " + path, 0});
        return;
      }
      std::ostringstream ss;
      ss << in.rdbuf();
      *bytes = ss.str();
    }
    self->decode(uri, bytes, 0);
  });
}

void GtkImageLoader::decode(const std::string &uri,
                            std::shared_ptr<std::string> bytes, int status) {
  auto self = shared_from_this();
  on_worker([self, uri, bytes, status] {
    GBytes *data = g_bytes_new(bytes->data(), bytes->size());
    GError *error = nullptr;
    GdkTexture *texture = gdk_texture_new_from_bytes(data, &error);
    if (!texture) {
      // Formats GDK doesn't decode itself (GIF, BMP, WebP, SVG...).
      g_clear_error(&error);
      GInputStream *stream = g_memory_input_stream_new_from_bytes(data);
      GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream(stream, nullptr, &error);
      g_object_unref(stream);
      if (pixbuf) {
        texture = gdk_texture_new_for_pixbuf(pixbuf);
        g_object_unref(pixbuf);
      }
    }
    g_bytes_unref(data);
    if (!texture) {
      std::string message = error && error->message ? error->message
                                                    : "Unsupported image";
      g_clear_error(&error);
      self->finish(uri, Result{nullptr, message, status});
      return;
    }
    self->finish(uri, Result{wrap(texture), "", status});
  });
}

void GtkImageLoader::finish(const std::string &uri, Result result) {
  std::vector<Callback> waiting;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (result.texture && !index_.count(uri)) {
      GdkTexture *t = result.texture.get();
      cacheBytes_ += size_t(gdk_texture_get_width(t)) * gdk_texture_get_height(t) * 4;
      cache_.emplace_front(uri, result.texture);
      index_[uri] = cache_.begin();
      while (cacheBytes_ > kCacheLimitBytes && cache_.size() > 1) {
        GdkTexture *old = cache_.back().second.get();
        cacheBytes_ -= size_t(gdk_texture_get_width(old)) *
                       gdk_texture_get_height(old) * 4;
        index_.erase(cache_.back().first);
        cache_.pop_back();
      }
    }
    if (auto it = inFlight_.find(uri); it != inFlight_.end()) {
      waiting = std::move(it->second);
      inFlight_.erase(it);
    }
  }
  if (!result.texture) {
    LOG(WARNING) << "Image failed: " << uri.substr(0, 120) << ": "
                 << result.error;
  }
  on_main([waiting = std::move(waiting), result] {
    for (const auto &done : waiting) done(result);
  });
}

void GtkImageLoader::loadImage(const std::string &uri,
                               const IImageLoaderOnLoadCallback &&onLoad) {
  load(uri, {}, [onLoad](const Result &result) {
    if (result.texture) {
      onLoad(gdk_texture_get_width(result.texture.get()),
             gdk_texture_get_height(result.texture.get()), nullptr);
    } else {
      onLoad(0, 0, result.error.c_str());
    }
  });
}

IImageLoader::CacheStatus GtkImageLoader::getCacheStatus(
    const std::string &uri) {
  std::lock_guard<std::mutex> lock(mutex_);
  return index_.count(uri) ? CacheStatus::Memory : CacheStatus::None;
}

}  // namespace rngtk
