#include "RNGtkHost.h"

#include "AccessibilityInfo.h"
#include "Appearance.h"
#include "DevUI.h"
#include "Dialogs.h"
#include "GtkMenus.h"
#include "Notifications.h"
#include "rngtk/Extensions.h"
#include "GtkImageLoader.h"
#include "GtkKeyboardHandler.h"
#include "GtkMountingManager.h"
#include "GtkPointerHandler.h"
#include "JsMessageQueueThread.h"
#include "PangoText.h"
#include "PlatformConstantsModule.h"
#include "PlatformModules.h"
#include "rn_text_input.h"
#include "rn_view.h"

#include <glog/logging.h>
#include <jsi/jsi.h>
#include <logger/react_native_log.h>
#include <react/coremodules/DeviceInfoModule.h>
#include <react/devsupport/SourceCodeModule.h>
#include <react/http/IHttpClient.h>
#include <react/io/ImageLoaderModule.h>
#include <react/renderer/animated/AnimatedModule.h>
#include <react/renderer/animated/NativeAnimatedNodesManagerProvider.h>
#include <react/http/IWebSocketClient.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/scheduler/SurfaceDelegate.h>
#include <react/runtime/ReactHost.h>
#include <react/threading/MessageQueueThreadImpl.h>
#include <react/utils/ContextContainer.h>
#include <react/utils/RunLoopObserverManager.h>

#include <algorithm>
#include <cstdio>

using namespace facebook::react;

namespace rngtk {

namespace {

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

LayoutConstraints fixedSize(float width, float height, bool rtl) {
  Size size{.width = width, .height = height};
  return LayoutConstraints{
      .minimumSize = size,
      .maximumSize = size,
      .layoutDirection =
          rtl ? LayoutDirection::RightToLeft : LayoutDirection::LeftToRight,
  };
}

// GNOME's text scaling (Settings > Accessibility > Large Text, or
// text-scaling-factor) reaches GTK as gtk-xft-dpi: 96 dpi is 1.
double fontScaleOf(GtkSettings *settings) {
  int dpi = 0;
  g_object_get(settings, "gtk-xft-dpi", &dpi, nullptr);
  return dpi > 0 ? dpi / 1024.0 / 96.0 : 1.0;
}

folly::dynamic metricsToDynamic(double width, double height, double scale,
                               double fontScale) {
  return folly::dynamic::object("width", width)("height", height)(
      "scale", scale)("fontScale", fontScale);
}

}  // namespace

// Dimensions for JS (Dimensions, useWindowDimensions, PixelRatio).
// ReactCxxPlatform's DeviceInfo module returns a placeholder 1280x720;
// this one reports the surface as the window and the monitor as the
// screen. Changes reach JS as `didUpdateDimensions` (see setSize()).
class RNGtkHost::DeviceInfoModule
    : public NativeDeviceInfoCxxSpec<RNGtkHost::DeviceInfoModule> {
 public:
  DeviceInfoModule(std::shared_ptr<CallInvoker> jsInvoker,
                   const RNGtkHost &host)
      : NativeDeviceInfoCxxSpec(std::move(jsInvoker)), host_(host) {}

  DeviceInfoConstants getConstants(facebook::jsi::Runtime &) {
    auto d = host_.dimensions();
    auto metrics = [](const Metrics &m) {
      return DisplayMetrics{m.width, m.height, m.scale, m.fontScale};
    };
    return DeviceInfoConstants{.Dimensions = {.window = metrics(d.window),
                                              .screen = metrics(d.screen)}};
  }

 private:
  const RNGtkHost &host_;
};

// Appearance (Appearance.getColorScheme(), useColorScheme()): the scheme
// Appearance resolves, read on the JS thread; setColorScheme() applies on
// the main thread, and the change comes back as appearanceChanged.
class RNGtkHost::AppearanceModule
    : public NativeAppearanceCxxSpec<RNGtkHost::AppearanceModule> {
 public:
  AppearanceModule(std::shared_ptr<CallInvoker> jsInvoker,
                   std::weak_ptr<Appearance> appearance)
      : NativeAppearanceCxxSpec(std::move(jsInvoker)),
        appearance_(std::move(appearance)) {}

  std::string getColorScheme(facebook::jsi::Runtime &) {
    auto appearance = appearance_.lock();
    return appearance && appearance->isDark() ? "dark" : "light";
  }

  void setColorScheme(facebook::jsi::Runtime &, std::string scheme) {
    on_main([weak = appearance_, scheme = Appearance::parseScheme(scheme)] {
      if (auto appearance = weak.lock()) appearance->setOverride(scheme);
    });
  }

  // Events go through RCTDeviceEventEmitter.
  void addListener(facebook::jsi::Runtime &, std::string) {}
  void removeListeners(facebook::jsi::Runtime &, double) {}

 private:
  std::weak_ptr<Appearance> appearance_;
};

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
                             host_.layoutConstraints(), host_.layoutContext());
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
  // Kept alive until the destructor disconnects from it.
  g_object_ref(overlay_);
  mountingManager_ =
      std::make_shared<GtkMountingManager>([this](SurfaceId surfaceId) {
        if (!reactHost_) return;  // shutting down
        reactHost_->runOnScheduler([surfaceId](Scheduler &scheduler) {
          scheduler.reportMount(surfaceId);
        });
      });
  runLoopObservers_ = std::make_shared<RunLoopObserverManager>();
  // Before the controls are measured: the theme variant can change them.
  appearance_ = std::make_shared<Appearance>(
      gtk_widget_get_display(GTK_WIDGET(overlay_)),
      options_.followSystemAppearance, [this] { onAppearanceChanged(); });
  accessibilityStatus_ = std::make_shared<AccessibilityStatus>(
      gtk_widget_get_display(GTK_WIDGET(overlay_)),
      options_.followSystemAccessibility, [this](const char *event, bool value) {
        if (std::string(event) == "screenReaderChanged") {
          mountingManager_->setScreenReaderActive(value);
        }
        if (loaded_ && reactHost_) {
          reactHost_->emitDeviceEvent(folly::dynamic::array(event, value));
        }
      });
  mountingManager_->setScreenReaderActive(
      accessibilityStatus_->screenReaderEnabled());
  measure_native_controls();

  platform_ = std::make_shared<PlatformState>();
  platform_->initialURL = options_.initialURL.empty()
                              ? std::nullopt
                              : std::optional<std::string>(options_.initialURL);
  platform_->i18n = std::make_shared<I18nSettings>(options_.appId);
  platform_->window = [this]() -> GtkWindow * {
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(overlay_));
    return root && GTK_IS_WINDOW(root) ? GTK_WINDOW(root) : nullptr;
  };
  platform_->openURLOverride = options_.openURLOverride;
  platform_->appId = options_.appId;
  platform_->emitDeviceEvent = [this, alive = std::weak_ptr<int>(alive_)](folly::dynamic args) {
    if (alive.lock() && loaded_ && reactHost_) reactHost_->emitDeviceEvent(std::move(args));
  };
  // Right-to-left (I18nManager) for GTK's own widgets too.
  if (platform_->i18n->isRTL()) gtk_widget_set_default_direction(GTK_TEXT_DIR_RTL);
  fontDpiHandler_ = g_signal_connect(
      gtk_widget_get_settings(GTK_WIDGET(overlay_)), "notify::gtk-xft-dpi",
      G_CALLBACK(onFontDpi), this);
  decorationLayoutHandler_ = g_signal_connect_swapped(
      gtk_widget_get_settings(GTK_WIDGET(overlay_)), "notify::gtk-decoration-layout",
      G_CALLBACK(+[](RNGtkHost *self) { self->remeasureWindowControls(); }), this);

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
    {
      std::lock_guard<std::mutex> lock(beatMutex_);
      creatingInstance_ = false;
    }
    // A reload applies RTL settings I18nManager changed.
    on_main([this] { refreshLayout(); });
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
  TurboModuleProviders turboModuleProviders = options_.extraTurboModules;
  // Libraries' (rngtk/Extensions.h), then the host's own.
  for (const auto &package : options_.packages) {
    if (!package) continue;
    turboModuleProviders.insert(turboModuleProviders.end(), package->turboModules.begin(),
                                package->turboModules.end());
    mountingManager_->addNativeComponents(package->components);
  }
  turboModuleProviders.insert(turboModuleProviders.end(), {
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
        if (name == DeviceInfoModule::kModuleName) {
          return std::make_shared<DeviceInfoModule>(jsInvoker, *this);
        }
        if (name == "LinuxWindows") return makeWindowsModule(jsInvoker);
        if (auto module = makePlatformModule(name, jsInvoker, platform_)) {
          return module;
        }
        if (auto module = makeDialogModule(name, jsInvoker, platform_)) {
          return module;
        }
        if (auto module = makeMenuModule(name, jsInvoker, platform_)) {
          return module;
        }
        if (auto module = makeNotificationsModule(name, jsInvoker, platform_)) {
          return module;
        }
        if (name == AccessibilityManagerModule::kModuleName) {
          return std::make_shared<AccessibilityManagerModule>(
              jsInvoker,
              AccessibilityManagerModule::Host{
                  accessibilityStatus_,
                  [this](int tag) { focusForAccessibility(tag); },
                  [this](const std::string &text,
                         GtkAccessibleAnnouncementPriority priority) {
                    announce(text, priority);
                  }});
        }
        if (name == AppearanceModule::kModuleName) {
          return std::make_shared<AppearanceModule>(
              jsInvoker, std::weak_ptr<Appearance>(appearance_));
        }
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
  });

  if (options_.devMode) {
    GtkWidget *logBoxRoot = rn_view_new();
    gtk_widget_set_visible(logBoxRoot, FALSE);
    gtk_widget_set_halign(logBoxRoot, GTK_ALIGN_START);
    gtk_widget_set_valign(logBoxRoot, GTK_ALIGN_START);
    gtk_overlay_add_overlay(overlay_, logBoxRoot);
    mountingManager_->registerSurface(kLogBoxSurfaceId, logBoxRoot);
    logBoxPointerHandler_ =
        std::make_unique<GtkPointerHandler>(*mountingManager_, logBoxRoot);
    logBoxKeyboardHandler_ =
        std::make_unique<GtkKeyboardHandler>(*mountingManager_, logBoxRoot);
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

  // Native Animated (useNativeDriver: TouchableOpacity's fade...). Each JS
  // instance gets its own NativeAnimatedNodesManagerProvider: the provider
  // caches the manager it makes for the first runtime, so one shared by
  // ReactHost would keep driving the destroyed instance after a reload.
  turboModuleProviders.push_back(
      [this](const std::string &name,
             const std::shared_ptr<CallInvoker> &jsInvoker)
          -> std::shared_ptr<TurboModule> {
        if (name != AnimatedModule::kModuleName) return nullptr;
        return std::make_shared<AnimatedModule>(jsInvoker,
                                                makeAnimatedProvider());
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
      /* animatedNodesManagerProvider: per instance, above */ nullptr);

  static GSourceFuncs funcs = {beforeWaiting, nullptr, nullptr, nullptr,
                               nullptr, nullptr};
  observerSource_ = g_source_new(&funcs, sizeof(ObserverSource));
  auto *os = reinterpret_cast<ObserverSource *>(observerSource_);
  os->host = this;
  g_source_set_name(observerSource_, "react-native-run-loop-observer");
  g_source_attach(observerSource_, nullptr);
}

// The manager asks for a callback each frame while animations run; GTK's
// frame clock gives it one, and the values reach widgets through
// GtkMountingManager::synchronouslyUpdateViewOnUIThread. Start and stop
// come from the JS thread (or the reload thread, when an old instance's
// manager goes away). They carry their instance's generation, so a late
// stop from a previous instance can't cancel the current one's callback.
std::shared_ptr<NativeAnimatedNodesManagerProvider>
RNGtkHost::makeAnimatedProvider() {
  int generation = ++animationGeneration_;
  return std::make_shared<NativeAnimatedNodesManagerProvider>(
      [this, generation](std::function<void()> &&onRender, bool /*isAsync*/) {
        {
          std::lock_guard<std::mutex> lock(animationMutex_);
          if (generation < animationOwner_) return;  // an old instance
          animationOwner_ = generation;
          onAnimationRender_ =
              std::make_shared<std::function<void()>>(std::move(onRender));
        }
        on_main([this] { updateAnimationTick(); });
      },
      [this, generation](bool /*isAsync*/) {
        {
          std::lock_guard<std::mutex> lock(animationMutex_);
          if (generation != animationOwner_) return;
          onAnimationRender_ = nullptr;
        }
        on_main([this] { updateAnimationTick(); });
      });
}

// Main thread: a frame callback while there is something to render.
void RNGtkHost::updateAnimationTick() {
  bool wanted;
  {
    std::lock_guard<std::mutex> lock(animationMutex_);
    wanted = onAnimationRender_ != nullptr;
  }
  if (wanted && !animationTick_) {
    animationTick_ = gtk_widget_add_tick_callback(GTK_WIDGET(overlay_),
                                                  onAnimationFrame, this, nullptr);
  } else if (!wanted && animationTick_) {
    gtk_widget_remove_tick_callback(GTK_WIDGET(overlay_), animationTick_);
    animationTick_ = 0;
  }
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
  if (fontDpiHandler_) {
    g_signal_handler_disconnect(gtk_widget_get_settings(GTK_WIDGET(overlay_)),
                                fontDpiHandler_);
  }
  if (decorationLayoutHandler_) {
    g_signal_handler_disconnect(gtk_widget_get_settings(GTK_WIDGET(overlay_)),
                                decorationLayoutHandler_);
  }
  closeAllWindows();
  for (GtkWindow *w : trackedWindows_) {
    g_signal_handlers_disconnect_by_data(w, this);
    g_object_set_data(G_OBJECT(w), "rngtk-host", nullptr);
    g_object_weak_unref(G_OBJECT(w), onTrackedWindowGone, this);
  }
  trackedWindows_.clear();
  if (trackedApp_) {
    g_signal_handlers_disconnect_by_data(trackedApp_, this);
    g_object_remove_weak_pointer(G_OBJECT(trackedApp_), reinterpret_cast<gpointer *>(&trackedApp_));
  }
  if (window_) g_signal_handlers_disconnect_by_data(window_, this);
  g_clear_object(&window_);
  if (layoutHandler_) g_signal_handler_disconnect(layoutClock_, layoutHandler_);
  g_clear_object(&layoutClock_);
  g_signal_handlers_disconnect_by_data(overlay_, this);
  if (animationTick_) {
    gtk_widget_remove_tick_callback(GTK_WIDGET(overlay_), animationTick_);
  }
  pointerHandler_.reset();
  logBoxPointerHandler_.reset();
  keyboardHandler_.reset();
  logBoxKeyboardHandler_.reset();
  if (loader_.joinable()) loader_.join();
  if (observerSource_) {
    g_source_destroy(observerSource_);
    g_source_unref(observerSource_);
  }
  // Stop the surfaces (their last mounts land here), then destroy
  // ReactHost, which joins the JS thread.
  reactHost_->stopAllSurfaces();
  reactHost_.reset();
  g_object_unref(overlay_);
}

bool RNGtkHost::run(const std::string &script, SurfaceId surfaceId,
                    const std::string &moduleName, GtkWidget *root,
                    float width, float height, folly::dynamic initialProps) {
  initialProps_ = std::move(initialProps);
  width_ = width;
  height_ = height;
  root_ = root;
  updateDimensions();
  // Measure text with the same font options the widgets draw with.
  PangoContext *context = gtk_widget_create_pango_context(root);
  set_main_thread_pango_context(context);
  g_object_unref(context);

  rn_widget_set_frame(root, 0, 0, width, height);
  mountingManager_->registerSurface(surfaceId, root);
  pointerHandler_ = std::make_unique<GtkPointerHandler>(*mountingManager_, root);
  keyboardHandler_ = std::make_unique<GtkKeyboardHandler>(*mountingManager_, root);
  mountingManager_->setOnBackRequested([this] {
    if (loaded_ && reactHost_) {
      reactHost_->emitDeviceEvent(folly::dynamic::array("hardwareBackPress"));
    }
  });
  mountingManager_->setOnUserScroll([this] {
    if (pointerHandler_) pointerHandler_->cancelTouches();
    if (logBoxPointerHandler_) logBoxPointerHandler_->cancelTouches();
  });
  if (logBox_) {
    rn_widget_set_frame(mountingManager_->viewForTag(kLogBoxSurfaceId), 0, 0,
                        width, height);
  }

  // The overlay's frame clock exists once it is realized; GTK's layout
  // phase (window allocation) runs before our handler on it.
  g_signal_connect(overlay_, "realize", G_CALLBACK(onOverlayRealize), this);
  if (gtk_widget_get_realized(GTK_WIDGET(overlay_))) {
    onOverlayRealize(GTK_WIDGET(overlay_), this);
  }
  setFollowsWindowSize(options_.followsWindowSize);

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
                           layoutConstraints(), layoutContext());
}

LayoutConstraints RNGtkHost::layoutConstraints() const {
  return fixedSize(width_, height_, platform_->i18n->isRTL());
}

LayoutContext RNGtkHost::layoutContext() const {
  return LayoutContext{
      .pointScaleFactor = 1.0f,
      .fontSizeMultiplier = Float(dimensions().window.fontScale)};
}

// Main thread: the surfaces take the current size, direction and font
// scale, and JS hears about new dimensions.
void RNGtkHost::refreshLayout() {
  updateDimensions();
  mountingManager_->setFontScale(float(dimensions().window.fontScale));
  if (!loaded_ || !reactHost_) return;
  for (SurfaceId id : {surfaceId_, kLogBoxSurfaceId}) {
    if (reactHost_->isSurfaceRunning(id)) {
      reactHost_->setSurfaceConstraints(id, layoutConstraints(), layoutContext());
    }
  }
  Dimensions d = dimensions();
  reactHost_->emitDeviceEvent(folly::dynamic::array(
      "didUpdateDimensions",
      folly::dynamic::object(
          "window", metricsToDynamic(d.window.width, d.window.height,
                                     d.window.scale, d.window.fontScale))(
          "screen", metricsToDynamic(d.screen.width, d.screen.height,
                                     d.screen.scale, d.screen.fontScale))));
}

void RNGtkHost::onFontDpi(GObject *, GParamSpec *, gpointer self) {
  static_cast<RNGtkHost *>(self)->refreshLayout();
}

RNGtkHost::Dimensions RNGtkHost::dimensions() const {
  std::lock_guard<std::mutex> lock(dimensionsMutex_);
  return dimensions_;
}

// Main thread: the window from the surface size, the screen from the
// monitor the window is on (or the first one, before it is shown).
void RNGtkHost::updateDimensions() {
  GtkWidget *widget = GTK_WIDGET(overlay_);
  GdkDisplay *display = gtk_widget_get_display(widget);
  GdkMonitor *monitor = nullptr;
  if (GtkNative *native = gtk_widget_get_native(widget)) {
    if (GdkSurface *surface = gtk_native_get_surface(native)) {
      monitor = gdk_display_get_monitor_at_surface(display, surface);
      if (monitor) g_object_ref(monitor);
    }
  }
  if (!monitor) {
    GListModel *monitors = gdk_display_get_monitors(display);
    if (g_list_model_get_n_items(monitors) > 0) {
      monitor = GDK_MONITOR(g_list_model_get_item(monitors, 0));
    }
  }
  double scale = monitor ? gdk_monitor_get_scale(monitor) : 1;
  double fontScale = fontScaleOf(gtk_widget_get_settings(widget));
  Dimensions d;
  d.window = {width_, height_, scale, fontScale};
  d.screen = d.window;
  if (monitor) {
    GdkRectangle geometry;
    gdk_monitor_get_geometry(monitor, &geometry);
    d.screen = {double(geometry.width), double(geometry.height), scale,
                fontScale};
    g_object_unref(monitor);
  }
  std::lock_guard<std::mutex> lock(dimensionsMutex_);
  dimensions_ = d;
}

void RNGtkHost::setSize(float width, float height) {
  if (width == width_ && height == height_) return;
  width_ = width;
  height_ = height;
  storeMetrics(surfaceId_, width, height);
  if (window_) {
    emitWindowEvent(surfaceId_, "resize", folly::dynamic::object("width", width)("height", height));
  }
  if (root_) rn_widget_set_frame(root_, 0, 0, width, height);
  if (logBoxRoot_) rn_widget_set_frame(logBoxRoot_, 0, 0, width, height);
  // startAppSurface() reads the new size before JS runs; after, the
  // surfaces relayout and JS gets didUpdateDimensions (what Android and
  // iOS send: Dimensions.set() takes the payload).
  refreshLayout();
}

void RNGtkHost::setFollowsWindowSize(bool follows) {
  followsWindowSize_ = follows;
  if (follows) gtk_widget_queue_resize(GTK_WIDGET(overlay_));
}

void RNGtkHost::onOverlayRealize(GtkWidget *widget, gpointer self) {
  auto *host = static_cast<RNGtkHost *>(self);
  host->connectWindowState();
  GdkFrameClock *clock = gtk_widget_get_frame_clock(widget);
  if (clock == host->layoutClock_) return;
  if (host->layoutHandler_) {
    g_signal_handler_disconnect(host->layoutClock_, host->layoutHandler_);
    host->layoutHandler_ = 0;
  }
  g_clear_object(&host->layoutClock_);
  if (!clock) return;
  host->layoutClock_ = GDK_FRAME_CLOCK(g_object_ref(clock));
  host->layoutHandler_ =
      g_signal_connect_after(clock, "layout", G_CALLBACK(onLayout), host);
}

void RNGtkHost::onLayout(GdkFrameClock *, gpointer self) {
  auto *host = static_cast<RNGtkHost *>(self);
  if (!host->followsWindowSize_) return;
  int width = gtk_widget_get_width(GTK_WIDGET(host->overlay_));
  int height = gtk_widget_get_height(GTK_WIDGET(host->overlay_));
  if (width > 0 && height > 0) host->setSize(width, height);
}

// AppState follows the app's windows (all of the GtkApplication's: the
// main one, those Windows opened, Modals', dialogs): active while one of
// them is the active window, inactive when none is, background when every
// visible one is minimized (X11) or suspended (Wayland compositors that
// tell: not visible), or none is visible.
void RNGtkHost::connectWindowState() {
  GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(overlay_));
  if (!root || !GTK_IS_WINDOW(root) || GTK_WINDOW(root) == window_) return;
  if (window_) g_signal_handlers_disconnect_by_data(window_, this);
  g_set_object(&window_, GTK_WINDOW(root));
  {
    std::lock_guard<std::mutex> lock(windowsMutex_);
    const char *title = gtk_window_get_title(window_);
    windowInfo_[surfaceId_] = WindowInfo{title ? title : "", width_, height_};
  }
  // The close button (Windows: close-requested, closed, hiding while other
  // windows are open).
  g_signal_connect(window_, "close-request", G_CALLBACK(+[](GtkWindow *, gpointer self) -> gboolean {
                     return static_cast<RNGtkHost *>(self)->onMainCloseRequest();
                   }),
                   this);
  g_signal_connect(window_, "notify::title", G_CALLBACK(+[](GtkWindow *w, GParamSpec *, gpointer self) {
                     auto *host = static_cast<RNGtkHost *>(self);
                     std::lock_guard<std::mutex> lock(host->windowsMutex_);
                     const char *title = gtk_window_get_title(w);
                     host->windowInfo_[host->surfaceId_].title = title ? title : "";
                   }),
                   this);
  GtkApplication *app = gtk_window_get_application(window_);
  if (app && app != trackedApp_) {
    trackedApp_ = app;
    g_object_add_weak_pointer(G_OBJECT(app), reinterpret_cast<gpointer *>(&trackedApp_));
    for (GList *l = gtk_application_get_windows(app); l; l = l->next) {
      trackWindow(GTK_WINDOW(l->data));
    }
    g_signal_connect_swapped(app, "window-added",
                             G_CALLBACK(+[](RNGtkHost *host, GtkWindow *w) { host->trackWindow(w); }),
                             this);
    g_signal_connect_swapped(app, "window-removed",
                             G_CALLBACK(+[](RNGtkHost *host, GtkWindow *) { host->updateAppState(); }),
                             this);
  } else if (!app) {
    trackWindow(window_);
  }
  updateAppState();
}

void RNGtkHost::trackWindow(GtkWindow *window) {
  if (std::find(trackedWindows_.begin(), trackedWindows_.end(), window) != trackedWindows_.end()) {
    return;
  }
  trackedWindows_.push_back(window);
  g_object_weak_ref(G_OBJECT(window), onTrackedWindowGone, this);
  g_object_set_data(G_OBJECT(window), "rngtk-host", this);
  g_signal_connect(window, "notify::is-active",
                   G_CALLBACK(+[](GtkWindow *w, GParamSpec *, gpointer self) {
                     auto *host = static_cast<RNGtkHost *>(self);
                     // Windows: focus and blur, for the app's own windows.
                     if (SurfaceId id = host->idForWindow(w)) {
                       host->emitWindowEvent(id, gtk_window_is_active(w) ? "focus" : "blur");
                     }
                     host->updateAppState();
                   }),
                   this);
  g_signal_connect(window, "notify::visible", G_CALLBACK(+[](GtkWindow *, GParamSpec *, gpointer self) {
                     static_cast<RNGtkHost *>(self)->updateAppState();
                   }),
                   this);
  // Minimized or suspended: the toplevel surface's state, once there is one.
  auto connectSurface = +[](GtkWidget *w, gpointer) {
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(w));
    if (!surface || !GDK_IS_TOPLEVEL(surface)) return;
    g_signal_connect_object(surface, "notify::state",
                            G_CALLBACK(+[](GdkSurface *, GParamSpec *, gpointer window) {
                              if (auto *host = static_cast<RNGtkHost *>(
                                      g_object_get_data(G_OBJECT(window), "rngtk-host"))) {
                                host->updateAppState();
                              }
                            }),
                            G_OBJECT(w), GConnectFlags(0));
  };
  g_signal_connect(window, "realize", G_CALLBACK(connectSurface), this);
  if (gtk_widget_get_realized(GTK_WIDGET(window))) connectSurface(GTK_WIDGET(window), this);
}

void RNGtkHost::onTrackedWindowGone(gpointer self, GObject *window) {
  auto &list = static_cast<RNGtkHost *>(self)->trackedWindows_;
  list.erase(std::remove(list.begin(), list.end(), reinterpret_cast<GtkWindow *>(window)),
             list.end());
}

void RNGtkHost::updateAppState() {
  bool active = false, shown = false;
  for (GtkWindow *w : trackedWindows_) {
    if (!gtk_widget_get_visible(GTK_WIDGET(w))) continue;
    if (gtk_window_is_active(w)) active = true;
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(w));
    bool hidden = surface && GDK_IS_TOPLEVEL(surface) &&
                  (gdk_toplevel_get_state(GDK_TOPLEVEL(surface)) &
                   (GDK_TOPLEVEL_STATE_MINIMIZED | GDK_TOPLEVEL_STATE_SUSPENDED));
    if (!hidden) shown = true;
  }
  if (trackedWindows_.empty()) return;
  setAppState(active ? 0 : shown ? 1 : 2);
}

void RNGtkHost::setAppState(int state) {
  int before = platform_->appState.exchange(state);
  if (before == state || !loaded_ || !reactHost_) return;
  reactHost_->emitDeviceEvent(folly::dynamic::array(
      "appStateDidChange", folly::dynamic::object("app_state", appStateName(state))));
  // Android's focus/blur (AppState 'focus' and 'blur' listeners).
  if ((before == 0) != (state == 0)) {
    reactHost_->emitDeviceEvent(
        folly::dynamic::array("appStateFocusChange", state == 0));
  }
}

void RNGtkHost::openURL(const std::string &url) {
  if (!loaded_ || !reactHost_) {
    std::lock_guard<std::mutex> lock(platform_->mutex);
    platform_->initialURL = url;
    return;
  }
  reactHost_->emitDeviceEvent(
      folly::dynamic::array("url", folly::dynamic::object("url", url)));
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

// Main thread: the scheme or accent changed. Mounted views re-resolve
// their PlatformColors, and JS hears about it (Appearance's listeners,
// useColorScheme()).
void RNGtkHost::remeasureWindowControls() {
  if (!measure_window_controls() || !loaded_ || !reactHost_) return;
  reactHost_->emitDeviceEvent(folly::dynamic::array("rngtkWindowControlsChanged"));
}

void RNGtkHost::onAppearanceChanged() {
  mountingManager_->refreshColors();
  remeasureWindowControls();
  if (!loaded_ || !reactHost_) return;
  reactHost_->emitDeviceEvent(folly::dynamic::array(
      "appearanceChanged",
      folly::dynamic::object("colorScheme",
                             appearance_->isDark() ? "dark" : "light")));
}

// GTK has no screen reader focus apart from keyboard focus, which Orca
// follows: the view takes it, made focusable until it loses it if needed.
void RNGtkHost::focusForAccessibility(Tag tag) {
  GtkWidget *widget = mountingManager_->viewForTag(tag);
  if (!widget) return;
  if (RN_IS_TEXT_INPUT(widget)) {
    rn_text_input_focus(RN_TEXT_INPUT(widget));
    return;
  }
  if (!gtk_widget_get_focusable(widget)) {
    gtk_widget_set_focusable(widget, TRUE);
    g_signal_connect(widget, "notify::has-focus",
                     G_CALLBACK(+[](GtkWidget *w, GParamSpec *, gpointer) {
                       if (gtk_widget_has_focus(w)) return;
                       g_signal_handlers_disconnect_matched(
                           w, G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr,
                           GINT_TO_POINTER(0x0a11));
                       gtk_widget_set_focusable(w, FALSE);
                     }),
                     GINT_TO_POINTER(0x0a11));
  }
  gtk_widget_grab_focus(widget);
}

void RNGtkHost::announce(const std::string &text,
                         GtkAccessibleAnnouncementPriority priority) {
  mountingManager_->announce(root_ ? root_ : GTK_WIDGET(overlay_), text,
                             priority);
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
