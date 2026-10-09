// RNSVGSvgViewModule: toDataURL(tag, options, callback), the <Svg>'s PNG
// as base64 (what Svg's ref.toDataURL() calls), rendered with librsvg at
// its size or options' width and height. And the package autolinking
// registers.
#include "SvgComponents.h"

#include <cairo.h>
#include <gtk/gtk.h>
#include <librsvg/rsvg.h>
#include <react/bridging/Function.h>
#include <rngtk/CxxModule.h>

#include <string>

using namespace facebook::react;

namespace rngtk_svg {

namespace {

cairo_status_t appendPng(void *closure, const unsigned char *data, unsigned int length) {
  static_cast<std::string *>(closure)->append(reinterpret_cast<const char *>(data), length);
  return CAIRO_STATUS_SUCCESS;
}

// The PNG (base64) of an SvgView's document, at width x height (0: its own).
std::string pngBase64(int tag, double width, double height) {
  double w = 0, h = 0;
  std::string document = documentForTag(tag, &w, &h);
  if (document.empty()) return "";
  if (width > 0) w = width;
  if (height > 0) h = height;
  if (w <= 0 || h <= 0) return "";
  RsvgHandle *handle =
      rsvg_handle_new_from_data(reinterpret_cast<const guint8 *>(document.data()), document.size(), nullptr);
  if (!handle) return "";
  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, int(w), int(h));
  cairo_t *cr = cairo_create(surface);
  RsvgRectangle viewport = {0, 0, w, h};
  rsvg_handle_render_document(handle, cr, &viewport, nullptr);
  cairo_destroy(cr);
  std::string png;
  cairo_surface_write_to_png_stream(surface, appendPng, &png);
  cairo_surface_destroy(surface);
  g_object_unref(handle);
  gchar *base64 = g_base64_encode(reinterpret_cast<const guchar *>(png.data()), png.size());
  std::string out = base64;
  g_free(base64);
  return out;
}

struct Request {
  int tag;
  double width = 0, height = 0;
  AsyncCallback<std::string> callback;
};

}  // namespace

class SvgViewModule : public rngtk::CxxModule<SvgViewModule> {
 public:
  static constexpr const char *kName = "RNSVGSvgViewModule";
  explicit SvgViewModule(std::shared_ptr<CallInvoker> jsInvoker) : CxxModule(kName, std::move(jsInvoker)) {
    method<&SvgViewModule::toDataURL>("toDataURL");
  }

  void toDataURL(facebook::jsi::Runtime &, std::optional<double> tag, std::optional<folly::dynamic> options,
                 AsyncCallback<std::string> callback) {
    auto *request = new Request{int(tag.value_or(-1)), 0, 0, std::move(callback)};
    if (options && options->isObject()) {
      if (auto *w = options->get_ptr("width"); w && w->isNumber()) request->width = w->asDouble();
      if (auto *h = options->get_ptr("height"); h && h->isNumber()) request->height = h->asDouble();
    }
    // Widgets live on the main thread.
    g_main_context_invoke(
        nullptr,
        [](gpointer data) -> gboolean {
          std::unique_ptr<Request> r(static_cast<Request *>(data));
          r->callback(pngBase64(r->tag, r->width, r->height));
          return G_SOURCE_REMOVE;
        },
        request);
  }
};

}  // namespace rngtk_svg

std::shared_ptr<const rngtk::Package> rngtk_svg_package() {
  auto package = std::make_shared<rngtk::Package>();
  package->name = "@curiosity26/react-native-gtk4-svg";
  package->components = rngtk_svg::svgComponents();
  package->turboModules.push_back(
      [](const std::string &name, const std::shared_ptr<CallInvoker> &jsInvoker)
          -> std::shared_ptr<facebook::react::TurboModule> {
        if (name == rngtk_svg::SvgViewModule::kName) {
          return std::make_shared<rngtk_svg::SvgViewModule>(jsInvoker);
        }
        return nullptr;
      });
  return package;
}
