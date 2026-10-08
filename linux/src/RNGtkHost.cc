#include "RNGtkHost.h"

#include "DevUI.h"
#include "GtkImageLoader.h"
#include "GtkMountingManager.h"
#include "GtkPointerHandler.h"
#include "JsMessageQueueThread.h"
#include "PangoText.h"
#include "PlatformConstantsModule.h"
#include "rn_view.h"

#include <glog/logging.h>
#include <jsi/jsi.h>
#include <logger/react_native_log.h>
#include <react/devsupport/SourceCodeModule.h>
#include <react/http/IHttpClient.h>
#include <react/io/ImageLoaderModule.h>
#include <react/renderer/animated/NativeAnimatedNodesManagerProvider.h>
#include <react/http/IWebSocketClient.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/scheduler/SurfaceDelegate.h>
#include <react/runtime/ReactHost.h>
#include <react/threading/MessageQueueThreadImpl.h>
#include <react/utils/ContextContainer.h>
#include <react/utils/RunLoopObserverManager.h>

#include <cstdio>

using namespace facebook::react;

namespace rngtk {

namespace {

// LogBox runs as its own React surface (AppRegistry "LogBox").
constexpr SurfaceId kLogBoxSurfaceId = 1001;

// A GSource whose prepare() runs each time the main loop is about to poll:
// GLib's equivalent of a CFRunLoop "before waiting" observer, which is when
// React Native's event beat expects RunLoopObserverManager::onRender().
struct ObserverSource {
  GSource source;
  RNGtkHost *host;
};

// Runs `fn` on the GTK main thread: now, if this is it; later otherwise.
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

void logToConsole(const std::string &message, unsigned int level) {
  const char *tag = level >= ReactNativeLogLevelError     ? "error"
                    : level == ReactNativeLogLevelWarning ? "warn"
                                                          : "log";
  fprintf(stderr, "[js %s] %s\n", tag, message.c_str());
}

LayoutConstraints fixedSize(float width, float height) {
  Size size{.width = width, .height = height};
  return LayoutConstraints{
      .minimumSize = size,
      .maximumSize = size,
      .layoutDirection = LayoutDirection::LeftToRight,
  };
}

}  // namespace

// Shows LogBox's surface over the app. LogBoxModule calls show() and hide()
// on the JS thread (surface calls are thread-safe there; the widget is
// shown on the main thread); it is created and destroyed with each JS
// instance, so destroyContentView() can come from the reload thread.
class RNGtkHost::LogBoxDelegate : public SurfaceDelegate {
 public:
  LogBoxDelegate(RNGtkHost &host, GtkWidget *root) : host_(host), root_(root) {}

  void createContentView(std::string appKey) override {
    appKey_ = std::move(appKey);
    ready_ = true;
  }
  bool isContentViewReady() override { return ready_; }

  void destroyContentView() override {
    ready_ = false;
    if (showing_.exchange(false)) {
      // A reload restarts the LogBox surface with an empty log; keep it
      // hidden until JS asks again.
      g_idle_add_full(
          G_PRIORITY_DEFAULT,
          [](gpointer root) -> gboolean {
            gtk_widget_set_visible(GTK_WIDGET(root), FALSE);
            return G_SOURCE_REMOVE;
          },
          g_object_ref(root_), g_object_unref);
    }
  }

  void show() override {
    showing_ = true;
    setVisible(true);
    auto &reactHost = *host_.reactHost_;
    if (!reactHost.isSurfaceRunning(kLogBoxSurfaceId)) {
      reactHost.startSurface(kLogBoxSurfaceId, appKey_,
                             folly::dynamic::object(),
                             fixedSize(host_.width_, host_.height_),
                             LayoutContext{.pointScaleFactor = 1.0f});
    }
  }

  void hide() override {
    showing_ = false;
    host_.reactHost_->stopSurface(kLogBoxSurfaceId);
    setVisible(false);
  }

  bool isShowing() override { return showing_; }

 private:
  void setVisible(bool visible) {
    GtkWidget *root = GTK_WIDGET(g_object_ref(root_));
    on_main([root, visible] {
      gtk_widget_set_visible(root, visible);
      g_object_unref(root);
    });
  }

  RNGtkHost &host_;
  GtkWidget *root_;
  std::string appKey_;
  std::atomic<bool> ready_{false};
  std::atomic<bool> showing_{false};
};

// The event beat: React Native flushes queued events (input, scroll, image
// loads) into JS when the UI loop is about to sleep, as with iOS's main
// run loop observer. induce() only schedules work on the JS thread.
gboolean RNGtkHost::beforeWaiting(GSource *source, gint *timeout) {
  *timeout = -1;
  RNGtkHost *host = reinterpret_cast<ObserverSource *>(source)->host;
  // Not while a JS instance is being created: ReactHost then replaces the
  // observer that onRender() reads (on its reload thread).
  std::lock_guard<std::mutex> lock(host->beatMutex_);
  if (!host->creatingInstance_) host->runLoopObservers_->onRender();
  return FALSE;
}

RNGtkHost::RNGtkHost(RNGtkHostOptions options, GtkOverlay *overlay)
    : options_(std::move(options)), overlay_(overlay) {
  mountingManager_ =
      std::make_shared<GtkMountingManager>([this](SurfaceId surfaceId) {
        if (!reactHost_) return;  // shutting down
        reactHost_->runOnScheduler([surfaceId](Scheduler &scheduler) {
          scheduler.reportMount(surfaceId);
        });
      });
  runLoopObservers_ = std::make_shared<RunLoopObserverManager>();
  measure_native_controls();

  auto contextContainer = std::make_shared<const ContextContainer>();
  // Called once per JS instance, i.e. again on every reload.
  // Called once per JS instance, i.e. again on every reload: a new JS
  // thread each time. The instance is ready (and the event beat may run
  // again) once ReactHost hands the mounting manager its UIManager.
  contextContainer->insert(MessageQueueThreadFactoryKey,
                           MessageQueueThreadFactory([this]() {
                             {
                               std::lock_guard<std::mutex> lock(beatMutex_);
                               creatingInstance_ = true;
                             }
                             auto queue =
                                 std::make_shared<JsMessageQueueThread>();
                             {
                               std::lock_guard<std::mutex> lock(queueMutex_);
                               queue_ = queue;
                             }
                             instances_++;
                             return queue;
                           }));
  mountingManager_->setOnUIManagerChanged([this] {
    std::lock_guard<std::mutex> lock(beatMutex_);
    creatingInstance_ = false;
  });
  contextContainer->insert(HttpClientFactoryKey, getHttpClientFactory());
  contextContainer->insert(WebSocketClientFactoryKey,
                           getWebSocketClientFactory());
  // Images: ImageManager finds the loader here; the mounting manager uses
  // it for defaultSource; the ImageLoader module for getSize/prefetch.
  imageLoader_ = std::make_shared<GtkImageLoader>(getHttpClientFactory());
  contextContainer->insert(GtkImageLoader::kContextKey, imageLoader_);
  mountingManager_->setImageLoader(imageLoader_);

  ReactInstanceConfig config{
      .appId = options_.appId,
      .deviceName = "GTK4",
  };
  config.enableDevMode = options_.devMode;
  config.enableInspector = options_.devMode && options_.inspector;
  config.devServerHost = options_.devServerHost;
  config.devServerPort = options_.devServerPort;

  // Providers are asked before ReactCxxPlatform's built-in modules, so
  // these replace its Android-shaped PlatformConstants.
  TurboModuleProviders turboModuleProviders{
      [constants = collectPlatformConstants(gdk_display_get_default(),
                                            options_.isTesting)](
          const std::string &name,
          const std::shared_ptr<CallInvoker> &jsInvoker)
          -> std::shared_ptr<TurboModule> {
        if (name == PlatformConstantsModule::kModuleName) {
          return std::make_shared<PlatformConstantsModule>(jsInvoker,
                                                           constants);
        }
        return nullptr;
      },
      [this](const std::string &name,
             const std::shared_ptr<CallInvoker> &jsInvoker)
          -> std::shared_ptr<TurboModule> {
        if (name == ImageLoaderModule::kModuleName) {
          return std::make_shared<ImageLoaderModule>(
              jsInvoker, std::weak_ptr<IImageLoader>(imageLoader_));
        }
        // Release bundles: the bundle's file:// URL, so require()d images
        // resolve to the assets/ folder next to it. (Dev mode keeps
        // ReactHost's SourceCode module, with Metro's URL.)
        if (name == SourceCodeModule::kModuleName && !options_.devMode) {
          return std::make_shared<SourceCodeModule>(jsInvoker, bundleURL_);
        }
        return nullptr;
      },
  };

  if (options_.devMode) {
    GtkWidget *logBoxRoot = rn_view_new();
    gtk_widget_set_visible(logBoxRoot, FALSE);
    gtk_widget_set_halign(logBoxRoot, GTK_ALIGN_START);
    gtk_widget_set_valign(logBoxRoot, GTK_ALIGN_START);
    gtk_overlay_add_overlay(overlay_, logBoxRoot);
    mountingManager_->registerSurface(kLogBoxSurfaceId, logBoxRoot);
    logBoxPointerHandler_ =
        std::make_unique<GtkPointerHandler>(*mountingManager_, logBoxRoot);
    logBoxRoot_ = logBoxRoot;
    logBox_ = std::make_shared<LogBoxDelegate>(*this, logBoxRoot);

    GMenu *menu = g_menu_new();
    g_menu_append(menu, "Reload", "dev.reload");
    if (config.enableInspector) {
      g_menu_append(menu, "Open DevTools", "dev.open-debugger");
    }
    devUI_ = DevUI::create(overlay_, G_MENU_MODEL(menu));
    g_object_unref(menu);
  }

  // Native Animated (useNativeDriver: TouchableOpacity's fade...) asks for
  // a callback each frame while animations run; GTK's frame clock gives it
  // one, and the values reach widgets through
  // GtkMountingManager::synchronouslyUpdateViewOnUIThread.
  auto animatedProvider = std::make_shared<NativeAnimatedNodesManagerProvider>(
      // Called on the JS thread; the frame callback is GTK's, so it's
      // added and removed on the main thread.
      [this](std::function<void()> &&onRender, bool /*isAsync*/) {
        {
          std::lock_guard<std::mutex> lock(animationMutex_);
          onAnimationRender_ = std::make_shared<std::function<void()>>(
              std::move(onRender));
        }
        on_main([this] {
          if (!animationTick_) {
            animationTick_ = gtk_widget_add_tick_callback(
                GTK_WIDGET(overlay_), onAnimationFrame, this, nullptr);
          }
        });
      },
      [this](bool /*isAsync*/) {
        {
          std::lock_guard<std::mutex> lock(animationMutex_);
          onAnimationRender_ = nullptr;
        }
        on_main([this] {
          if (animationTick_) {
            gtk_widget_remove_tick_callback(GTK_WIDGET(overlay_),
                                            animationTick_);
            animationTick_ = 0;
          }
        });
      });

  reactHost_ = std::make_unique<ReactHost>(
      config, mountingManager_, runLoopObservers_, std::move(contextContainer),
      [this](facebook::jsi::Runtime &, const JsErrorHandler::ProcessedError &error) {
        jsErrors_++;
        LOG(ERROR) << "JS error: " << error;
        // LogBox shows errors once JS runs. Before that (a bundle that
        // doesn't compile, e.g. Metro's build-error response) only the
        // banner can.
        if (devUI_ && error.isFatal &&
            error.message.rfind("[runtime not ready]", 0) == 0) {
          std::string message =
              error.originalMessage ? *error.originalMessage : error.message;
          message = message.substr(0, message.find(", sourceURL:"));
          showErrorBanner(
              message +
              "\nSee Metro's terminal for build errors; fix, then press Ctrl+R.");
        }
      },
      logToConsole, devUI_, std::move(turboModuleProviders), logBox_,
      std::move(animatedProvider));

  static GSourceFuncs funcs = {beforeWaiting, nullptr, nullptr, nullptr,
                               nullptr, nullptr};
  observerSource_ = g_source_new(&funcs, sizeof(ObserverSource));
  auto *os = reinterpret_cast<ObserverSource *>(observerSource_);
  os->host = this;
  g_source_set_name(observerSource_, "react-native-run-loop-observer");
  g_source_attach(observerSource_, nullptr);
}

gboolean RNGtkHost::onAnimationFrame(GtkWidget *, GdkFrameClock *,
                                     gpointer self) {
  auto *host = static_cast<RNGtkHost *>(self);
  std::shared_ptr<std::function<void()>> render;
  {
    std::lock_guard<std::mutex> lock(host->animationMutex_);
    render = host->onAnimationRender_;
  }
  // C++ Animated steps on the main thread (its node graph is shared with
  // the JS thread under its own locks) and applies values through
  // synchronouslyUpdateViewOnUIThread.
  if (render) (*render)();
  return G_SOURCE_CONTINUE;
}

RNGtkHost::~RNGtkHost() {
  if (animationTick_) {
    gtk_widget_remove_tick_callback(GTK_WIDGET(overlay_), animationTick_);
  }
  pointerHandler_.reset();
  logBoxPointerHandler_.reset();
  if (loader_.joinable()) loader_.join();
  if (observerSource_) {
    g_source_destroy(observerSource_);
    g_source_unref(observerSource_);
  }
  // Stop the surfaces (their last mounts land here), then destroy
  // ReactHost, which joins the JS thread.
  reactHost_->stopAllSurfaces();
  reactHost_.reset();
}

bool RNGtkHost::run(const std::string &script, SurfaceId surfaceId,
                    const std::string &moduleName, GtkWidget *root,
                    float width, float height, folly::dynamic initialProps) {
  initialProps_ = std::move(initialProps);
  width_ = width;
  height_ = height;
  // Measure text with the same font options the widgets draw with.
  PangoContext *context = gtk_widget_create_pango_context(root);
  set_main_thread_pango_context(context);
  g_object_unref(context);

  rn_widget_set_frame(root, 0, 0, width, height);
  mountingManager_->registerSurface(surfaceId, root);
  pointerHandler_ = std::make_unique<GtkPointerHandler>(*mountingManager_, root);
  mountingManager_->setOnUserScroll([this] {
    if (pointerHandler_) pointerHandler_->cancelTouches();
    if (logBoxPointerHandler_) logBoxPointerHandler_->cancelTouches();
  });
  if (logBox_) {
    rn_widget_set_frame(mountingManager_->viewForTag(kLogBoxSurfaceId), 0, 0,
                        width, height);
  }

  script_ = script;
  surfaceId_ = surfaceId;
  moduleName_ = moduleName;
  if (!options_.devMode) {
    gchar *absolute = g_canonicalize_filename(script.c_str(), nullptr);
    gchar *url = g_filename_to_uri(absolute, nullptr, nullptr);
    bundleURL_ = url ? url : "";
    g_free(url);
    g_free(absolute);
    if (!reactHost_->loadScript(script, script)) return false;
    loaded_ = true;
    startAppSurface();
    return true;
  }
  loadFromDevServer();
  return true;
}

void RNGtkHost::startAppSurface() {
  reactHost_->startSurface(surfaceId_, moduleName_, initialProps_,
                           fixedSize(width_, height_),
                           LayoutContext{.pointScaleFactor = 1.0f});
}

void RNGtkHost::loadFromDevServer() {
  if (loader_.joinable()) loader_.join();
  loadFailed_ = false;
  // The download blocks on a future, so it runs off the main loop, which
  // keeps painting the loading banner meanwhile. ReactHost's own reload
  // thread does the same.
  loader_ = std::thread([this] {
    // No fallback bundle: a failed download should say so.
    if (reactHost_->loadScript("", script_)) {
      loaded_ = true;
      startAppSurface();
      return;
    }
    std::string message = "Could not load " + script_ +
                          ".bundle from Metro at " + options_.devServerHost +
                          ":" + std::to_string(options_.devServerPort) +
                          ". Start it with `npm start`, then press Ctrl+R.";
    fprintf(stderr, "%s\n", message.c_str());
    loadFailed_ = true;
    showErrorBanner(message);
  });
}

void RNGtkHost::showErrorBanner(const std::string &message) {
  // From any thread; the DevUI hops to the main thread for the widgets.
  g_main_context_invoke_full(
      nullptr, G_PRIORITY_DEFAULT,
      [](gpointer data) -> gboolean {
        auto *call = static_cast<std::pair<RNGtkHost *, std::string> *>(data);
        if (call->first->devUI_) call->first->devUI_->showError(call->second);
        return G_SOURCE_REMOVE;
      },
      new std::pair<RNGtkHost *, std::string>(this, message),
      [](gpointer data) {
        delete static_cast<std::pair<RNGtkHost *, std::string> *>(data);
      });
}

void RNGtkHost::reload() {
  if (!options_.devMode) return;
  if (!loaded_) {
    // Nothing is running yet (Metro was down): try the download again.
    if (loadFailed_) loadFromDevServer();
    return;
  }
  // DevSettings.reload() is what Metro's `r` and LogBox's reload button
  // call; it reaches ReactHost's reload through DevSettingsModule.
  reactHost_->runOnRuntimeScheduler([](facebook::jsi::Runtime &rt) {
    try {
      auto proxy = rt.global().getPropertyAsObject(rt, "nativeModuleProxy");
      auto devSettings = proxy.getProperty(rt, "DevSettings");
      if (!devSettings.isObject()) return;
      auto object = devSettings.asObject(rt);
      object.getPropertyAsFunction(rt, "reload").callWithThis(rt, object);
    } catch (const std::exception &e) {
      LOG(ERROR) << "Reload failed: " << e.what();
    }
  });
}

void RNGtkHost::openDebugger() { reactHost_->openDebugger(); }

void RNGtkHost::showDevMenu() {
  if (devUI_) devUI_->popupMenu();
}

bool RNGtkHost::isIdle() const {
  std::shared_ptr<JsMessageQueueThread> queue;
  {
    std::lock_guard<std::mutex> lock(queueMutex_);
    queue = queue_.lock();
  }
  return (!queue || queue->isIdle()) && mountingManager_->isIdle();
}

bool RNGtkHost::isLogBoxShowing() const {
  return logBox_ && logBox_->isShowing();
}

}  // namespace rngtk
