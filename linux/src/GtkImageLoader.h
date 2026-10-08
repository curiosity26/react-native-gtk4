// Loads images for <Image> (through ImageManager) and for the ImageLoader
// native module (Image.getSize, Image.prefetch).
//
// Sources: http(s) through the libsoup client, file:// URLs and absolute
// paths, data: URIs (base64 or percent-encoded). Bytes are decoded into a
// GdkTexture on a worker thread (GDK's PNG/JPEG/TIFF loaders, or
// gdk-pixbuf's for other formats). Results are cached in memory by URI and
// delivered on the GTK main thread.
#pragma once

#include <gtk/gtk.h>
#include <react/http/IHttpClient.h>
#include <react/io/IImageLoader.h>

#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace rngtk {

class GtkImageLoader : public facebook::react::IImageLoader,
                       public std::enable_shared_from_this<GtkImageLoader> {
 public:
  struct Result {
    std::shared_ptr<GdkTexture> texture;  // null on failure
    std::string error;
    int status = 0;  // HTTP status, when there was a response
  };
  using Callback = std::function<void(const Result &)>;

  explicit GtkImageLoader(facebook::react::HttpClientFactory httpClientFactory);

  // The context container key ImageManager finds this loader under.
  static constexpr const char *kContextKey = "RNGtkImageLoader";

  // From any thread; `done` runs on the GTK main thread.
  void load(const std::string &uri,
            const std::vector<std::pair<std::string, std::string>> &headers,
            Callback done);

  // IImageLoader (ImageLoader native module).
  void loadImage(const std::string &uri,
                 const facebook::react::IImageLoaderOnLoadCallback &&onLoad)
      override;
  CacheStatus getCacheStatus(const std::string &uri) override;

  static std::shared_ptr<GdkTexture> wrap(GdkTexture *texture);

 private:
  void finish(const std::string &uri, Result result);
  void decode(const std::string &uri, std::shared_ptr<std::string> bytes,
              int status);

  facebook::react::HttpClientFactory httpClientFactory_;
  std::unique_ptr<facebook::react::IHttpClient> httpClient_;
  std::mutex mutex_;
  // URI -> texture, least recently used last out.
  std::list<std::pair<std::string, std::shared_ptr<GdkTexture>>> cache_;
  std::unordered_map<std::string, decltype(cache_)::iterator> index_;
  size_t cacheBytes_ = 0;
  // Requests waiting on the same URI share one load.
  std::unordered_map<std::string, std::vector<Callback>> inFlight_;
};

}  // namespace rngtk
