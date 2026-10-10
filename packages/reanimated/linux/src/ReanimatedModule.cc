// react-native-reanimated on Linux: ReanimatedModule (the TurboModule the
// library's JS calls: installTurboModule) and the platform side of its
// shared C++ engine (ReanimatedModuleProxy), as apple/ReanimatedModule.mm
// and REANodesManager.mm do it on iOS:
//
// - the UI runtime and scheduler are react-native-worklets' (from the JS
//   globals its module sets up), on the GTK main thread;
// - frames (requestRender) come from the main window's GdkFrameClock; after
//   each, the proxy's performOperations commits the animated props (a
//   shadow tree commit on the main thread, which mounts there at once);
// - synchronous prop updates (opt-in, IOS_SYNCHRONOUSLY_UPDATE_UI_PROPS)
//   and the mounted props layout animations read go through the host;
// - every Fabric event reaches the proxy first (useEvent,
//   useAnimatedScrollHandler, gesture-handler's events with worklets);
// - setGestureState (GestureStateManager in worklets) is gesture-handler's
//   "gesture-handler.setGestureState" service.
#include <rngtk/CxxModule.h>
#include <rngtk/Extensions.h>

#include <reanimated/Compat/WorkletsApi.h>
#include <reanimated/NativeModules/ReanimatedModuleProxy.h>
#include <reanimated/RuntimeDecorators/RNRuntimeDecorator.h>
#include <reanimated/Tools/PlatformDepMethodsHolder.h>

#include <react/renderer/core/EventListener.h>
#include <react/renderer/scheduler/Scheduler.h>

#include <gtk/gtk.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace facebook;
using namespace facebook::react;

namespace rngtk_reanimated {

namespace {

rngtk::Host *host = nullptr;
std::atomic<bool> reducedMotion{false};

// What REANodesManager does on iOS: callbacks for the next frame, then the
// proxy's operations (main thread).
class Frames : public std::enable_shared_from_this<Frames> {
 public:
  void post(std::function<void(double)> callback) {
    callbacks_.push_back(std::move(callback));
    if (scheduled_) return;
    scheduled_ = true;
    host->requestFrame([weak = weak_from_this()](double timestamp) {
      if (auto self = weak.lock()) self->onFrame(timestamp);
    });
  }

  void maybeFlush() {
    if (!scheduled_) performOperations();
  }

  void performOperations() {
    if (auto proxy = proxy_.lock()) proxy->performOperations();
  }

  std::weak_ptr<reanimated::ReanimatedModuleProxy> proxy_;

 private:
  void onFrame(double timestamp) {
    scheduled_ = false;
    // A callback that asks for another frame gets the next one.
    auto callbacks = std::move(callbacks_);
    callbacks_.clear();
    for (auto &callback : callbacks) callback(timestamp);
    performOperations();
  }

  std::vector<std::function<void(double)>> callbacks_;
  bool scheduled_ = false;
};

reanimated::PlatformDepMethodsHolder platformMethods(const std::shared_ptr<Frames> &frames) {
  using namespace reanimated;
  PlatformDepMethodsHolder methods;
  methods.requestRender = [frames](std::function<void(const double)> onRender) {
    if (host->isMainThread()) {
      frames->post(std::move(onRender));
    } else {
      host->runOnMainThread([frames, onRender = std::move(onRender)]() mutable { frames->post(std::move(onRender)); });
    }
  };
  // No screen snapshots on GTK (react-native-screens' iOS transitions).
  methods.forceScreenSnapshotFunction = [](Tag) { return false; };
  methods.readMountedViewPropsFunction = [](Tag tag) { return host->mountedProps(tag); };
  methods.synchronouslyUpdateUIPropsFunction = [](const int tag, const folly::dynamic &props) {
    host->setNativePropsForTag(tag, props);
  };
  methods.getAnimationTimestamp = [] { return host->frameTime(); };
  // No motion sensors.
  methods.registerSensor = [](int, int, int, std::function<void(double[], int)>) { return -1; };
  methods.unregisterSensor = [](int) {};
  methods.setGestureStateFunction = [](int handlerTag, int state) {
    if (auto service = host->service("gesture-handler.setGestureState")) {
      (*std::static_pointer_cast<std::function<void(int, int)>>(service))(handlerTag, state);
    }
  };
  // The on-screen keyboard isn't the app's on the desktop: no events.
  methods.subscribeForKeyboardEvents = [](std::function<void(int, int)>, bool, bool) { return -1; };
  methods.unsubscribeFromKeyboardEvents = [](int) {};
  methods.maybeFlushUIUpdatesQueueFunction = [frames] { frames->maybeFlush(); };
  // Pseudo selectors (:hover, :active in CSS animations): not yet.
  methods.attachPseudoSelector = [](Tag, PseudoSelector, std::function<void(bool)>) {};
  methods.detachPseudoSelector = [](Tag, PseudoSelector) {};
  return methods;
}

class Module final : public rngtk::CxxModule<Module> {
 public:
  explicit Module(std::shared_ptr<CallInvoker> js) : CxxModule("ReanimatedModule", std::move(js)) {
    method<&Module::installTurboModule>("installTurboModule");
  }

  ~Module() override {
    // The proxy's last reference goes on the main thread, where its UI
    // runtime work runs.
    if (proxy_ && host) host->runOnMainThread([proxy = std::move(proxy_), frames = std::move(frames_)] {});
  }

  bool installTurboModule(jsi::Runtime &rt) {
    auto global = rt.global();
    if (!global.hasProperty(rt, "__UI_WORKLET_RUNTIME_HOLDER") || !global.hasProperty(rt, "__UI_SCHEDULER_HOLDER")) {
      return false;
    }
    auto uiWorkletRuntime =
        worklets::getWorkletRuntimeFromHolder(rt, global.getPropertyAsObject(rt, "__UI_WORKLET_RUNTIME_HOLDER"));
    auto uiScheduler = worklets::getUISchedulerFromHolder(rt, global.getPropertyAsObject(rt, "__UI_SCHEDULER_HOLDER"));

    frames_ = std::make_shared<Frames>();
    auto methods = platformMethods(frames_);
    proxy_ = std::make_shared<reanimated::ReanimatedModuleProxy>(uiWorkletRuntime, uiScheduler, rt, jsInvoker_,
                                                                 methods, reducedMotion.load());
    proxy_->init(methods);
    frames_->proxy_ = proxy_;

    auto &uiRuntime = worklets::getJSIRuntimeFromWorkletRuntime(uiWorkletRuntime);
    reanimated::RNRuntimeDecorator::decorate(rt, uiRuntime, proxy_);

    // Fabric: the commit and mount hooks, and every event before JS gets it
    // (on the main thread, where input comes from; others are skipped, as
    // on iOS).
    bool fabric = false;
    std::weak_ptr<reanimated::ReanimatedModuleProxy> weak = proxy_;
    host->runOnScheduler([&](Scheduler &scheduler) {
      proxy_->initializeFabric(scheduler.getUIManager());
      scheduler.addEventListener(std::make_shared<EventListener>([weak](const RawEvent &event) {
        if (!host->isMainThread()) return false;
        auto proxy = weak.lock();
        return proxy ? proxy->handleRawEvent(event, host->frameTime()) : false;
      }));
      fabric = true;
    });
    return fabric;
  }

 private:
  std::shared_ptr<Frames> frames_;
  std::shared_ptr<reanimated::ReanimatedModuleProxy> proxy_;
};

}  // namespace

}  // namespace rngtk_reanimated

std::shared_ptr<const rngtk::Package> rngtk_reanimated_package() {
  using namespace rngtk_reanimated;
  auto package = std::make_shared<rngtk::Package>();
  package->name = "@curiosity26/react-native-gtk4-reanimated";
  package->turboModules.push_back(
      [](const std::string &name, const std::shared_ptr<CallInvoker> &js) -> std::shared_ptr<TurboModule> {
        return name == "ReanimatedModule" ? std::make_shared<Module>(js) : nullptr;
      });
  package->setUp = [](rngtk::Host &h) {
    host = &h;
    // Reduced motion (Reanimated's ReduceMotion.System): GNOME's "Reduce
    // animation" setting, as AccessibilityInfo reads it.
    gboolean animations = TRUE;
    g_object_get(gtk_settings_get_default(), "gtk-enable-animations", &animations, nullptr);
    reducedMotion = !animations;
  };
  return package;
}
