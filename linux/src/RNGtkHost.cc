#include "RNGtkHost.h"

#include "GtkMessageQueueThread.h"
#include "GtkMountingManager.h"
#include "PangoText.h"
#include "PlatformConstantsModule.h"
#include "rn_view.h"

#include <glog/logging.h>
#include <logger/react_native_log.h>
#include <react/http/IHttpClient.h>
#include <react/http/IWebSocketClient.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/runtime/ReactHost.h>
#include <react/threading/MessageQueueThreadImpl.h>
#include <react/utils/ContextContainer.h>
#include <react/utils/RunLoopObserverManager.h>

#include <cstdio>

using namespace facebook::react;

namespace rngtk {

namespace {

// A GSource whose prepare() runs each time the main loop is about to poll:
// GLib's equivalent of a CFRunLoop "before waiting" observer, which is when
// React Native's event beat expects RunLoopObserverManager::onRender().
struct ObserverSource {
  GSource source;
  RunLoopObserverManager *observers;
};

void logToConsole(const std::string &message, unsigned int level) {
  const char *tag = level >= ReactNativeLogLevelError     ? "error"
                    : level == ReactNativeLogLevelWarning ? "warn"
                                                          : "log";
  fprintf(stderr, "[js %s] %s\n", tag, message.c_str());
}

}  // namespace

gboolean RNGtkHost::beforeWaiting(GSource *source, gint *timeout) {
  *timeout = -1;
  reinterpret_cast<ObserverSource *>(source)->observers->onRender();
  return FALSE;
}

RNGtkHost::RNGtkHost(bool isTesting) {
  mountingManager_ =
      std::make_shared<GtkMountingManager>([this](SurfaceId surfaceId) {
        reactHost_->runOnScheduler([surfaceId](Scheduler &scheduler) {
          scheduler.reportMount(surfaceId);
        });
      });
  runLoopObservers_ = std::make_shared<RunLoopObserverManager>();

  auto contextContainer = std::make_shared<const ContextContainer>();
  contextContainer->insert(MessageQueueThreadFactoryKey,
                           MessageQueueThreadFactory([this]() {
                             auto queue =
                                 std::make_shared<GtkMessageQueueThread>();
                             queue_ = queue;
                             return queue;
                           }));
  contextContainer->insert(HttpClientFactoryKey, getHttpClientFactory());
  contextContainer->insert(WebSocketClientFactoryKey,
                           getWebSocketClientFactory());

  ReactInstanceConfig config{
      .appId = "dev.curiosity26.RNGtk4",
      .deviceName = "GTK4",
  };
  // Bundles load from disk; Metro and the inspector come later.
  config.enableDevMode = false;
  config.enableInspector = false;

  // Providers are asked before ReactCxxPlatform's built-in modules, so
  // these replace its Android-shaped PlatformConstants.
  TurboModuleProviders turboModuleProviders{
      [constants = collectPlatformConstants(gdk_display_get_default(),
                                            isTesting)](
          const std::string &name,
          const std::shared_ptr<CallInvoker> &jsInvoker)
          -> std::shared_ptr<TurboModule> {
        if (name == PlatformConstantsModule::kModuleName) {
          return std::make_shared<PlatformConstantsModule>(jsInvoker,
                                                           constants);
        }
        return nullptr;
      },
  };

  reactHost_ = std::make_unique<ReactHost>(
      config, mountingManager_, runLoopObservers_, std::move(contextContainer),
      [this](facebook::jsi::Runtime &, const JsErrorHandler::ProcessedError &error) {
        jsErrors_++;
        LOG(ERROR) << "JS error: " << error;
      },
      logToConsole, /*devUIDelegate=*/nullptr,
      std::move(turboModuleProviders));

  static GSourceFuncs funcs = {beforeWaiting, nullptr, nullptr, nullptr,
                               nullptr, nullptr};
  observerSource_ = g_source_new(&funcs, sizeof(ObserverSource));
  auto *os = reinterpret_cast<ObserverSource *>(observerSource_);
  os->observers = runLoopObservers_.get();
  g_source_set_name(observerSource_, "react-native-run-loop-observer");
  g_source_attach(observerSource_, nullptr);
}

RNGtkHost::~RNGtkHost() {
  if (observerSource_) {
    g_source_destroy(observerSource_);
    g_source_unref(observerSource_);
  }
  reactHost_->stopAllSurfaces();
  if (auto queue = queue_.lock()) queue->drain();
}

bool RNGtkHost::loadBundle(const std::string &path) {
  return reactHost_->loadScript(path, path);
}

void RNGtkHost::startSurface(SurfaceId surfaceId, const std::string &moduleName,
                             GtkWidget *root, float width, float height) {
  // Measure text with the same font options the widgets draw with.
  PangoContext *context = gtk_widget_create_pango_context(root);
  set_main_thread_pango_context(context);
  g_object_unref(context);

  rn_widget_set_frame(root, 0, 0, width, height);
  mountingManager_->registerSurface(surfaceId, root);

  Size size{.width = width, .height = height};
  LayoutConstraints constraints{
      .minimumSize = size,
      .maximumSize = size,
      .layoutDirection = LayoutDirection::LeftToRight,
  };
  LayoutContext layoutContext{.pointScaleFactor = 1.0f};
  reactHost_->startSurface(surfaceId, moduleName, folly::dynamic::object(),
                           constraints, layoutContext);
}

void RNGtkHost::stopSurface(SurfaceId surfaceId) {
  reactHost_->stopSurface(surfaceId);
  mountingManager_->unregisterSurface(surfaceId);
}

bool RNGtkHost::isIdle() const {
  auto queue = queue_.lock();
  return !queue || !queue->hasPendingWork();
}

}  // namespace rngtk
