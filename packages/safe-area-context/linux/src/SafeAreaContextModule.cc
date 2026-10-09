// RNCSafeAreaContext: react-native-safe-area-context's TurboModule
// (src/specs/NativeSafeAreaContext.ts). Its constants are the app's first
// window's metrics: desktop windows have no notches or system bars inside
// them, so the insets are zero and the frame is the window's size.
// SafeAreaProvider and SafeAreaView are the package's pure-JS components
// (its Windows ones), which follow the window from there.
#include <gtk/gtk.h>
#include <rngtk/CxxModule.h>
#include <rngtk/Extensions.h>

#include <chrono>
#include <future>

using namespace facebook::react;

namespace rngtk_safe_area_context {

namespace {

struct Size {
  int width = 0, height = 0;
};

// The active (or first) window's content size, read on the main thread.
Size windowSize() {
  GApplication *app = g_application_get_default();
  if (!app || !GTK_IS_APPLICATION(app)) return {};
  GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(app));
  if (!window) {
    GList *windows = gtk_application_get_windows(GTK_APPLICATION(app));
    window = windows ? GTK_WINDOW(windows->data) : nullptr;
  }
  if (!window) return {};
  // The content's size (React Native's root), not the window's with its
  // title bar.
  GtkWidget *content = gtk_window_get_child(window);
  Size size;
  if (content) size = {gtk_widget_get_width(content), gtk_widget_get_height(content)};
  if (size.width <= 0 || size.height <= 0) gtk_window_get_default_size(window, &size.width, &size.height);
  return size;
}

}  // namespace

class SafeAreaContextModule : public rngtk::CxxModule<SafeAreaContextModule> {
 public:
  static constexpr const char *kName = "RNCSafeAreaContext";
  explicit SafeAreaContextModule(std::shared_ptr<CallInvoker> jsInvoker)
      : CxxModule(kName, std::move(jsInvoker)) {
    method<&SafeAreaContextModule::getConstants>("getConstants");
  }

  folly::dynamic getConstants(facebook::jsi::Runtime &) {
    // GTK lives on the main thread, which is free while JS starts up; if it
    // doesn't answer in time, there are no initial metrics (the package's
    // default) and the provider measures the window itself.
    auto promise = std::make_shared<std::promise<Size>>();
    auto future = promise->get_future();
    auto *data = new std::shared_ptr<std::promise<Size>>(promise);
    g_main_context_invoke_full(
        nullptr, G_PRIORITY_HIGH,
        [](gpointer p) -> gboolean {
          (*static_cast<std::shared_ptr<std::promise<Size>> *>(p))->set_value(windowSize());
          return G_SOURCE_REMOVE;
        },
        data, [](gpointer p) { delete static_cast<std::shared_ptr<std::promise<Size>> *>(p); });
    if (future.wait_for(std::chrono::milliseconds(250)) != std::future_status::ready) {
      return folly::dynamic::object;
    }
    Size size = future.get();
    if (size.width <= 0 || size.height <= 0) return folly::dynamic::object;
    folly::dynamic zero = folly::dynamic::object("top", 0)("right", 0)("bottom", 0)("left", 0);
    folly::dynamic frame = folly::dynamic::object("x", 0)("y", 0)("width", size.width)("height", size.height);
    return folly::dynamic::object("initialWindowMetrics",
                                  folly::dynamic::object("insets", zero)("frame", frame));
  }
};

}  // namespace rngtk_safe_area_context

std::shared_ptr<const rngtk::Package> rngtk_safe_area_context_package() {
  auto package = std::make_shared<rngtk::Package>();
  package->name = "@curiosity26/react-native-gtk4-safe-area-context";
  package->turboModules.push_back(
      [](const std::string &name, const std::shared_ptr<CallInvoker> &jsInvoker)
          -> std::shared_ptr<facebook::react::TurboModule> {
        if (name == rngtk_safe_area_context::SafeAreaContextModule::kName) {
          return std::make_shared<rngtk_safe_area_context::SafeAreaContextModule>(jsInvoker);
        }
        return nullptr;
      });
  return package;
}
