// The app's windows (Windows in js/Windows.js): more top-level windows,
// each the surface of a registered component in the one JS runtime, and
// the main window's title, size and focus.
//
// - A window's id is its surface id (a React root tag: 11, 21, 31...; the
//   main window's is the one run() was given, 1 in apps).
// - Each window follows its size like the main one: its surface relays out
//   and JS hears 'resize' (and useWindowDimensions in it updates).
// - The close button: 'close-requested', then the window closes ('closed')
//   unless the app intercepts closing and closes it itself. The main
//   window hides instead while other windows are open (its surface, and
//   the app, keep running); the app quits once no window is left, unless
//   told not to.
// - 'focus' and 'blur' as each window becomes active or stops being.
// - titleBar 'hidden' / 'none' and transparent (apply_window_style): the
//   app draws its own title bar (<TitleBar>), or shows the desktop through.
#include "RNGtkHost.h"

#include "rngtk/CxxModule.h"
#include "DevControls.h"
#include "GtkKeyboardHandler.h"
#include "GtkMenus.h"
#include "GtkMountingManager.h"
#include "GtkPointerHandler.h"
#include "PlatformModules.h"
#include "rn_view.h"

#include <glog/logging.h>
#include <react/runtime/ReactHost.h>

using namespace facebook::react;

namespace rngtk {

struct RNGtkHost::AppWindow {
  SurfaceId id = 0;
  GtkWindow *window = nullptr;  // ours until destroyed
  GtkWidget *overlay = nullptr;
  GtkWidget *root = nullptr;
  std::unique_ptr<GtkPointerHandler> pointer;
  std::unique_ptr<GtkKeyboardHandler> keyboard;
  float width = 0, height = 0;
  GdkFrameClock *clock = nullptr;
  gulong layoutHandler = 0;
};

namespace {

void on_main_while(std::weak_ptr<int> alive, std::function<void()> fn) {
  auto *call = new std::pair<std::weak_ptr<int>, std::function<void()>>(std::move(alive),
                                                                          std::move(fn));
  g_main_context_invoke_full(
      nullptr, G_PRIORITY_DEFAULT,
      [](gpointer data) -> gboolean {
        auto *call = static_cast<std::pair<std::weak_ptr<int>, std::function<void()>> *>(data);
        if (call->first.lock()) call->second();
        return G_SOURCE_REMOVE;
      },
      call,
      [](gpointer data) {
        delete static_cast<std::pair<std::weak_ptr<int>, std::function<void()>> *>(data);
      });
}

// A window, for its frame clock's layout handler.
struct WindowRef {
  RNGtkHost *host;
  SurfaceId id;
};

GQuark window_id_quark() {
  static GQuark q = g_quark_from_static_string("rngtk-window-id");
  return q;
}

// Our hooks off the window, and the window (if GTK hasn't destroyed it).
template <typename W>
void tear_down(W &w, gpointer host) {
  if (w.clock) {
    if (g_signal_handler_is_connected(w.clock, w.layoutHandler)) {
      g_signal_handler_disconnect(w.clock, w.layoutHandler);
    }
    g_clear_object(&w.clock);
  }
  w.pointer.reset();
  w.keyboard.reset();
  if (w.window) {
    GtkWindow *window = w.window;
    g_object_remove_weak_pointer(G_OBJECT(window), reinterpret_cast<gpointer *>(&w.window));
    w.window = nullptr;
    g_signal_handlers_disconnect_by_data(window, host);
    gtk_window_destroy(window);
  }
}

TitleBar title_bar_from(const std::string &name) {
  if (name == "hidden") return TitleBar::Hidden;
  if (name == "none") return TitleBar::None;
  return TitleBar::Default;
}

}  // namespace

void apply_window_style(GtkWindow *window, TitleBar titleBar, bool transparent) {
  switch (titleBar) {
    case TitleBar::Default:
      break;
    case TitleBar::Hidden: {
      // A title bar of nothing: GTK draws the frame (client-side, on X11
      // too) and no bar; the content starts at the top.
      GtkWidget *empty = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
      gtk_widget_set_visible(empty, FALSE);
      gtk_window_set_titlebar(window, empty);
      break;
    }
    case TitleBar::None:
      gtk_window_set_decorated(window, FALSE);
      break;
  }
  if (!transparent) return;
  GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(window));
  if (!gdk_display_is_composited(display)) {
    LOG(WARNING) << "transparent window: the display has no compositor; it stays opaque";
    return;
  }
  static GtkCssProvider *css = [display] {
    GtkCssProvider *provider = gtk_css_provider_new();
    // No background; without a title bar, no frame either (a shadow and
    // rounded corners around nothing).
    gtk_css_provider_load_from_string(
        provider,
        "window.rngtk-transparent { background: none; }\n"
        "window.rngtk-transparent.rngtk-frameless { box-shadow: none; border-radius: 0; "
        "outline: none; border: none; }\n");
    gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    return provider;
  }();
  (void)css;
  gtk_widget_add_css_class(GTK_WIDGET(window), "rngtk-transparent");
  if (titleBar != TitleBar::Default) {
    gtk_widget_add_css_class(GTK_WIDGET(window), "rngtk-frameless");
  }
}

// ---- The Windows module ------------------------------------------------------

class RNGtkHost::WindowsModule : public CxxModule<RNGtkHost::WindowsModule> {
 public:
  WindowsModule(std::shared_ptr<CallInvoker> jsInvoker, RNGtkHost &host)
      : CxxModule("LinuxWindows", std::move(jsInvoker)), host_(host), alive_(host.alive_) {
    method<&WindowsModule::open>("open");
    method<&WindowsModule::close>("close");
    method<&WindowsModule::requestClose>("requestClose");
    method<&WindowsModule::setTitle>("setTitle");
    method<&WindowsModule::setSize>("setSize");
    method<&WindowsModule::setMinSize>("setMinSize");
    method<&WindowsModule::focus>("focus");
    method<&WindowsModule::setInterceptClose>("setInterceptClose");
    method<&WindowsModule::setQuitOnLastWindowClosed>("setQuitOnLastWindowClosed");
    method<&WindowsModule::getWindows>("getWindows");
    method<&WindowsModule::getMetrics>("getMetrics");
    method<&WindowsModule::getMainWindowId>("getMainWindowId");
  }

  // options: {component, initialProps, title, width, height, minWidth,
  // minHeight, resizable, interceptClose, titleBar, transparent}. The window opens on the main
  // loop; its id comes back now.
  int open(facebook::jsi::Runtime &, folly::dynamic options) {
    WindowOptions o;
    auto get = [&](const char *key) -> const folly::dynamic * {
      auto it = options.isObject() ? options.find(key) : options.items().end();
      return options.isObject() && it != options.items().end() ? &it->second : nullptr;
    };
    if (auto *v = get("component"); v && v->isString()) o.moduleName = v->getString();
    if (auto *v = get("initialProps"); v && v->isObject()) o.initialProps = *v;
    if (auto *v = get("title"); v && v->isString()) o.title = v->getString();
    if (auto *v = get("width"); v && v->isNumber()) o.width = int(v->asDouble());
    if (auto *v = get("height"); v && v->isNumber()) o.height = int(v->asDouble());
    if (auto *v = get("minWidth"); v && v->isNumber()) o.minWidth = int(v->asDouble());
    if (auto *v = get("minHeight"); v && v->isNumber()) o.minHeight = int(v->asDouble());
    if (auto *v = get("resizable"); v && v->isBool()) o.resizable = v->getBool();
    if (auto *v = get("interceptClose"); v && v->isBool()) o.interceptClose = v->getBool();
    if (auto *v = get("titleBar"); v && v->isString()) o.titleBar = title_bar_from(v->getString());
    if (auto *v = get("transparent"); v && v->isBool()) o.transparent = v->getBool();
    SurfaceId id = host_.allocateWindowId();
    run([id, o = std::move(o)](RNGtkHost &host) { host.openWindow(id, o); });
    return id;
  }
  void close(facebook::jsi::Runtime &, int id) {
    run([id](RNGtkHost &host) { host.closeWindow(id); });
  }
  // As the close button: 'close-requested', then closed unless the app
  // intercepts closing.
  void requestClose(facebook::jsi::Runtime &, int id) {
    run([id](RNGtkHost &host) {
      if (GtkWindow *window = host.windowFor(id)) gtk_window_close(window);
    });
  }
  void setTitle(facebook::jsi::Runtime &, int id, std::string title) {
    run([id, title](RNGtkHost &host) { host.setWindowTitle(id, title); });
  }
  void setSize(facebook::jsi::Runtime &, int id, double width, double height) {
    run([=](RNGtkHost &host) { host.setWindowSize(id, int(width), int(height)); });
  }
  void setMinSize(facebook::jsi::Runtime &, int id, double width, double height) {
    run([=](RNGtkHost &host) { host.setWindowMinSize(id, int(width), int(height)); });
  }
  void focus(facebook::jsi::Runtime &, int id) {
    run([id](RNGtkHost &host) { host.focusWindow(id); });
  }
  void setInterceptClose(facebook::jsi::Runtime &, int id, bool intercept) {
    run([=](RNGtkHost &host) { host.setInterceptClose(id, intercept); });
  }
  void setQuitOnLastWindowClosed(facebook::jsi::Runtime &, bool quit) {
    run([quit](RNGtkHost &host) { host.setQuitOnLastWindowClosed(quit); });
  }
  folly::dynamic getWindows(facebook::jsi::Runtime &) { return host_.windowList(); }
  folly::dynamic getMetrics(facebook::jsi::Runtime &, int id) { return host_.windowMetrics(id); }
  int getMainWindowId(facebook::jsi::Runtime &) { return host_.mainWindowId(); }

 private:
  void run(std::function<void(RNGtkHost &)> fn) {
    RNGtkHost *host = &host_;
    on_main_while(alive_, [host, fn = std::move(fn)] { fn(*host); });
  }

  RNGtkHost &host_;
  std::weak_ptr<int> alive_;
};

std::shared_ptr<TurboModule> RNGtkHost::makeWindowsModule(
    const std::shared_ptr<CallInvoker> &jsInvoker) {
  return std::make_shared<WindowsModule>(jsInvoker, *this);
}

// ---- Windows -------------------------------------------------------------------

SurfaceId RNGtkHost::allocateWindowId() {
  // Root tags end in 1 (React's convention); skip LogBox's.
  for (;;) {
    SurfaceId id = nextWindowId_.fetch_add(10);
    if (id != kLogBoxSurfaceId && id != surfaceId_) return id;
  }
}

LayoutConstraints RNGtkHost::constraintsFor(float width, float height) const {
  Size size{.width = width, .height = height};
  return LayoutConstraints{
      .minimumSize = size,
      .maximumSize = size,
      .layoutDirection = platform_->i18n->isRTL() ? LayoutDirection::RightToLeft
                                                  : LayoutDirection::LeftToRight,
  };
}

void RNGtkHost::storeMetrics(SurfaceId id, float width, float height) {
  std::lock_guard<std::mutex> lock(windowsMutex_);
  auto &info = windowInfo_[id];
  info.width = width;
  info.height = height;
}

void RNGtkHost::emitWindowEvent(SurfaceId id, const char *type, folly::dynamic extra) {
  if (!loaded_ || !reactHost_) return;
  folly::dynamic event = folly::dynamic::object("id", id)("type", type);
  for (auto &[key, value] : extra.items()) event[key] = value;
  reactHost_->emitDeviceEvent(folly::dynamic::array("rngtkWindowEvent", std::move(event)));
}

SurfaceId RNGtkHost::idForWindow(GtkWindow *window) const {
  if (window && window == window_) return surfaceId_;
  return window ? GPOINTER_TO_INT(g_object_get_qdata(G_OBJECT(window), window_id_quark())) : 0;
}

void RNGtkHost::openWindow(SurfaceId id, WindowOptions options) {
  GtkApplication *app = window_ ? gtk_window_get_application(window_) : nullptr;
  if (!app || windows_.count(id)) {
    LOG(WARNING) << "Windows.open: no app window to open " << options.moduleName << " beside";
    emitWindowEvent(id, "closed");
    return;
  }
  auto w = std::make_shared<AppWindow>();
  w->id = id;
  w->width = float(std::max(1, options.width));
  w->height = float(std::max(1, options.height));
  w->window = GTK_WINDOW(gtk_application_window_new(app));
  // GTK owns it (and may destroy it when the app shuts down).
  g_object_add_weak_pointer(G_OBJECT(w->window), reinterpret_cast<gpointer *>(&w->window));
  g_object_set_qdata(G_OBJECT(w->window), window_id_quark(), GINT_TO_POINTER(id));
  // The app's menu bar too, if it has one.
  gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(w->window), menubar_has_items());
  gtk_window_set_title(w->window,
                       (options.title.empty() ? options.moduleName : options.title).c_str());
  // Its natural size (the root view's frame, under the title bar), as the
  // main window gets.
  gtk_window_set_resizable(w->window, options.resizable);
  apply_window_style(w->window, options.titleBar, options.transparent);
  w->overlay = gtk_overlay_new();
  if (options.minWidth > 0 || options.minHeight > 0) {
    gtk_widget_set_size_request(w->overlay, options.minWidth, options.minHeight);
  }
  w->root = rn_view_new();
  gtk_widget_set_halign(w->root, GTK_ALIGN_START);
  gtk_widget_set_valign(w->root, GTK_ALIGN_START);
  gtk_overlay_set_child(GTK_OVERLAY(w->overlay), w->root);
  gtk_window_set_child(w->window, w->overlay);
  rn_widget_set_frame(w->root, 0, 0, w->width, w->height);
  mountingManager_->registerSurface(id, w->root);
  w->pointer = std::make_unique<GtkPointerHandler>(*mountingManager_, w->root);
  w->keyboard = std::make_unique<GtkKeyboardHandler>(*mountingManager_, w->root);
  if (options.interceptClose) interceptClose_.insert(id);
  {
    std::lock_guard<std::mutex> lock(windowsMutex_);
    windowInfo_[id] = WindowInfo{gtk_window_get_title(w->window), w->width, w->height};
  }

  g_signal_connect(w->window, "close-request", G_CALLBACK(+[](GtkWindow *window, gpointer self) -> gboolean {
                     auto *host = static_cast<RNGtkHost *>(self);
                     SurfaceId id = host->idForWindow(window);
                     host->emitWindowEvent(id, "close-requested");
                     if (!host->interceptClose_.count(id)) host->closeWindow(id);
                     return TRUE;
                   }),
                   this);
  g_signal_connect(w->window, "realize", G_CALLBACK(+[](GtkWidget *widget, gpointer self) {
                     auto *host = static_cast<RNGtkHost *>(self);
                     SurfaceId id = host->idForWindow(GTK_WINDOW(widget));
                     auto it = host->windows_.find(id);
                     if (it == host->windows_.end()) return;
                     AppWindow &w = *it->second;
                     GdkFrameClock *clock = gtk_widget_get_frame_clock(widget);
                     if (!clock || clock == w.clock) return;
                     if (w.clock) g_signal_handler_disconnect(w.clock, w.layoutHandler);
                     g_set_object(&w.clock, clock);
                     w.layoutHandler = g_signal_connect_data(
                         clock, "layout",
                         G_CALLBACK(+[](GdkFrameClock *, gpointer data) {
                           auto *ref = static_cast<WindowRef *>(data);
                           ref->host->onWindowLayout(ref->id);
                         }),
                         new WindowRef{host, id},
                         [](gpointer data, GClosure *) { delete static_cast<WindowRef *>(data); },
                         G_CONNECT_AFTER);
                   }),
                   this);
  trackWindow(w->window);
  // Dev mode: Ctrl+R and the dev menu from this window too.
  if (options_.devMode) addDevControls(GTK_WIDGET(w->window), this, false);

  AppWindow &window = *w;
  windows_[id] = std::move(w);
  if (loaded_ && reactHost_) {
    reactHost_->startSurface(id, options.moduleName, options.initialProps,
                             constraintsFor(window.width, window.height), layoutContext());
  }
  gtk_window_present(window.window);
  emitWindowEvent(id, "open", folly::dynamic::object("width", window.width)("height", window.height));
}

void RNGtkHost::onWindowLayout(SurfaceId id) {
  auto it = windows_.find(id);
  if (it == windows_.end()) return;
  AppWindow &w = *it->second;
  int width = gtk_widget_get_width(w.overlay), height = gtk_widget_get_height(w.overlay);
  if (width <= 0 || height <= 0 || (float(width) == w.width && float(height) == w.height)) return;
  w.width = float(width);
  w.height = float(height);
  rn_widget_set_frame(w.root, 0, 0, w.width, w.height);
  storeMetrics(id, w.width, w.height);
  if (loaded_ && reactHost_ && reactHost_->isSurfaceRunning(id)) {
    reactHost_->setSurfaceConstraints(id, constraintsFor(w.width, w.height), layoutContext());
  }
  emitWindowEvent(id, "resize", folly::dynamic::object("width", w.width)("height", w.height));
}

void RNGtkHost::closeWindow(SurfaceId id) {
  if (id == surfaceId_) {
    if (!window_) return;
    // The main window: hidden while others are open, or when the app
    // stays up without windows; otherwise it goes, and the app with it.
    bool others = false;
    for (const auto &[_, w] : windows_) {
    others |= w->window && gtk_widget_get_visible(GTK_WIDGET(w->window));
  }
    if (others || !options_.quitOnLastWindowClosed) {
      gtk_widget_set_visible(GTK_WIDGET(window_), FALSE);
      mainHidden_ = true;
      emitWindowEvent(id, "closed");
      updateAppState();
      return;
    }
    emitWindowEvent(id, "closed");
    gtk_window_destroy(window_);
    return;
  }
  destroyWindow(id);
  quitIfNoWindows();
}

void RNGtkHost::destroyWindow(SurfaceId id) {
  auto it = windows_.find(id);
  if (it == windows_.end()) return;
  std::shared_ptr<AppWindow> w = std::move(it->second);
  windows_.erase(it);
  interceptClose_.erase(id);
  {
    std::lock_guard<std::mutex> lock(windowsMutex_);
    windowInfo_.erase(id);
  }
  // Unmounts the surface (here, or after the mounts queued before it);
  // then its root goes.
  if (reactHost_ && reactHost_->isSurfaceRunning(id)) reactHost_->stopSurface(id);
  GtkWidget *root = GTK_WIDGET(g_object_ref(w->root));
  mountingManager_->afterPendingMounts([mm = std::weak_ptr<GtkMountingManager>(mountingManager_),
                                        id, root] {
    if (auto m = mm.lock()) m->unregisterSurface(id);
    g_object_unref(root);
  });
  tear_down(*w, this);
  emitWindowEvent(id, "closed");
  updateAppState();
}

void RNGtkHost::closeAllWindows() {
  while (!windows_.empty()) {
    std::shared_ptr<AppWindow> w = windows_.begin()->second;
    windows_.erase(windows_.begin());
    tear_down(*w, this);
  }
}

void RNGtkHost::quitIfNoWindows() {
  if (!options_.quitOnLastWindowClosed || !window_) return;
  if (gtk_widget_get_visible(GTK_WIDGET(window_))) return;
  for (const auto &[_, w] : windows_) {
    if (w->window && gtk_widget_get_visible(GTK_WIDGET(w->window))) return;
  }
  // Once the closed window's surface has unmounted, and JS has let go of
  // it (React Native's leak check runs on the JS thread after a stop).
  GtkApplication *app = gtk_window_get_application(window_);
  if (!app) return;
  auto quit = [app = GTK_APPLICATION(g_object_ref(app))] {
    g_application_quit(G_APPLICATION(app));
    g_object_unref(app);
  };
  mountingManager_->afterPendingMounts([this, quit, alive = std::weak_ptr<int>(alive_)] {
    if (!alive.lock()) return;
    if (!loaded_ || !reactHost_) {
      quit();
      return;
    }
    reactHost_->runOnRuntimeScheduler([quit, alive](facebook::jsi::Runtime &) {
      on_main_while(alive, quit);
    });
  });
}

// The main window's close button.
bool RNGtkHost::onMainCloseRequest() {
  emitWindowEvent(surfaceId_, "close-requested");
  if (interceptClose_.count(surfaceId_)) return true;
  bool others = false;
  for (const auto &[_, w] : windows_) {
    others |= w->window && gtk_widget_get_visible(GTK_WIDGET(w->window));
  }
  if (others || !options_.quitOnLastWindowClosed) {
    closeWindow(surfaceId_);
    return true;
  }
  emitWindowEvent(surfaceId_, "closed");
  return false;  // GTK destroys it; the app ends
}

GtkWindow *RNGtkHost::windowFor(SurfaceId id) const {
  if (id == surfaceId_) return window_;
  auto it = windows_.find(id);
  return it == windows_.end() ? nullptr : it->second->window;
}

GtkWidget *RNGtkHost::rootFor(SurfaceId id) const {
  if (id == surfaceId_) return root_;
  auto it = windows_.find(id);
  return it == windows_.end() ? nullptr : it->second->root;
}

GtkPointerHandler *RNGtkHost::pointerHandlerFor(SurfaceId id) {
  if (id == surfaceId_) return pointerHandler_.get();
  auto it = windows_.find(id);
  return it == windows_.end() ? nullptr : it->second->pointer.get();
}

void RNGtkHost::setWindowTitle(SurfaceId id, const std::string &title) {
  GtkWindow *window = windowFor(id);
  if (!window) return;
  gtk_window_set_title(window, title.c_str());
  std::lock_guard<std::mutex> lock(windowsMutex_);
  windowInfo_[id].title = title;
}

void RNGtkHost::setWindowSize(SurfaceId id, int width, int height) {
  GtkWindow *window = windowFor(id);
  if (!window || width <= 0 || height <= 0) return;
  // The window's size is its content's (the title bar comes on top), so
  // ask for the difference.
  GtkWidget *content = gtk_window_get_child(window);
  int extraW = 0, extraH = 0;
  if (content && gtk_widget_get_width(content) > 0) {
    extraW = gtk_widget_get_width(GTK_WIDGET(window)) - gtk_widget_get_width(content);
    extraH = gtk_widget_get_height(GTK_WIDGET(window)) - gtk_widget_get_height(content);
  }
  gtk_window_set_default_size(window, width + std::max(0, extraW), height + std::max(0, extraH));
  // A maximized or tiled window keeps the size the desktop gave it.
  if (gtk_window_is_maximized(window)) gtk_window_unmaximize(window);
}

void RNGtkHost::setWindowMinSize(SurfaceId id, int width, int height) {
  GtkWindow *window = windowFor(id);
  if (GtkWidget *content = window ? gtk_window_get_child(window) : nullptr) {
    gtk_widget_set_size_request(content, std::max(0, width), std::max(0, height));
  }
}

void RNGtkHost::focusWindow(SurfaceId id) {
  GtkWindow *window = windowFor(id);
  if (!window) return;
  if (id == surfaceId_) mainHidden_ = false;
  gtk_window_present(window);
}

void RNGtkHost::setInterceptClose(SurfaceId id, bool intercept) {
  if (intercept) {
    interceptClose_.insert(id);
  } else {
    interceptClose_.erase(id);
  }
}

folly::dynamic RNGtkHost::windowList() const {
  std::lock_guard<std::mutex> lock(windowsMutex_);
  folly::dynamic list = folly::dynamic::array();
  for (const auto &[id, info] : windowInfo_) {
    folly::dynamic entry = folly::dynamic::object("id", id)("title", info.title)(
        "width", info.width)("height", info.height)("main", id == surfaceId_);
    if (id == surfaceId_) {
      list.insert(list.begin(), std::move(entry));
    } else {
      list.push_back(std::move(entry));
    }
  }
  return list;
}

folly::dynamic RNGtkHost::windowMetrics(SurfaceId id) const {
  Dimensions d = dimensions();
  if (id == surfaceId_) {
    return folly::dynamic::object("width", d.window.width)("height", d.window.height)(
        "scale", d.window.scale)("fontScale", d.window.fontScale);
  }
  std::lock_guard<std::mutex> lock(windowsMutex_);
  auto it = windowInfo_.find(id);
  if (it == windowInfo_.end()) return nullptr;
  return folly::dynamic::object("width", it->second.width)("height", it->second.height)(
      "scale", d.window.scale)("fontScale", d.window.fontScale);
}

}  // namespace rngtk
