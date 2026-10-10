// react-native-worklets on Linux: the library's shared C++ (its UI worklet
// runtime, a Hermes runtime of its own; serializables, synchronizables,
// runOnUI/runOnJS) on the host's threads:
//
// - the UI runtime runs on the GTK main thread (a UIScheduler that posts to
//   it), with requestAnimationFrame on the main window's GdkFrameClock;
// - the JS scheduler is React Native's (the module's CallInvoker), with
//   the host telling which thread is the JS one;
// - WorkletsModule (the TurboModule the library's JS calls:
//   installTurboModule, start) sets it up on the JS runtime.
//
// Other packages reach the UI runtime through the host's
// "worklets.uiRuntime" service (gesture-handler's _setGestureStateSync,
// Reanimated).
#include <rngtk/CxxModule.h>
#include <rngtk/Extensions.h>

#include <worklets/NativeModules/WorkletsModuleProxyInitializer.h>
#include <worklets/Tools/JSScheduler.h>
#include <worklets/Tools/RNRuntimeStatus.h>
#include <worklets/Tools/UIScheduler.h>
#include <worklets/WorkletRuntime/RuntimeBindings.h>

#include <glog/logging.h>

#include <memory>
#include <string>

using namespace facebook;
using namespace facebook::react;

namespace rngtk_worklets {

namespace {

rngtk::Host *host = nullptr;

// The UI thread is GTK's main thread.
class GtkUIScheduler final : public worklets::UIScheduler,
                             public std::enable_shared_from_this<GtkUIScheduler> {
 public:
  void scheduleOnUI(std::function<void()> job) override {
    if (isOnUIThread()) {
      job();
      return;
    }
    UIScheduler::scheduleOnUI(std::move(job));
    if (!scheduledOnUI_.exchange(true)) {
      host->runOnMainThread([weak = weak_from_this()] {
        if (auto self = weak.lock()) self->triggerUI();
      });
    }
  }

 protected:
  bool queryIsOnUIThread() const override { return host->isMainThread(); }
};

std::shared_ptr<worklets::RuntimeBindings> runtimeBindings() {
  return std::make_shared<worklets::RuntimeBindings>(worklets::RuntimeBindings{
      .requestAnimationFrame =
          [](std::function<void(const double)> &&callback) { host->requestFrame(std::move(callback)); },
      // console.* on worklet runtimes: as the host logs JS's (to stderr).
      .nativeLoggingHook =
          [](jsi::Runtime &rt, const jsi::Value &, const jsi::Value *args, size_t count) -> jsi::Value {
        if (count >= 1 && args[0].isString()) {
          int level = count >= 2 && args[1].isNumber() ? int(args[1].asNumber()) : 0;
          std::string message = args[0].asString(rt).utf8(rt);
          if (level >= 2) LOG(WARNING) << "[worklet] " << message;
          fprintf(stderr, "[js log] %s\n", message.c_str());
        }
        return jsi::Value::undefined();
      },
      .networkingBackend = nullptr,
  });
}

class Module final : public rngtk::CxxModule<Module> {
 public:
  explicit Module(std::shared_ptr<CallInvoker> js) : CxxModule("WorkletsModule", std::move(js)) {
    method<&Module::installTurboModule>("installTurboModule");
    method<&Module::prepareBundleMode>("prepareBundleMode");
    method<&Module::toggleSlowAnimationsOnUIRuntime>("toggleSlowAnimationsOnUIRuntime");
    method<&Module::start>("start");
  }

  ~Module() override {
    if (status_) status_->setDead();
    if (initializer_) initializer_->invalidate();
    if (host) host->provideService("worklets.uiRuntime", nullptr);
  }

  jsi::Value get(jsi::Runtime &rt, const jsi::PropNameID &name) override {
    if (!initializer_) createInitializer(rt);
    return CxxModule::get(rt, name);
  }

  bool installTurboModule(jsi::Runtime &rt, bool bundleModeEnabled) {
    proxy_ = initializer_->finalize(rt, bundleModeEnabled, [] { return worklets::BundleModeConfig{}; });
    // The UI runtime, for other packages: a job runs on it on the main
    // thread.
    std::weak_ptr<worklets::WorkletsModuleProxy> weak = proxy_;
    auto runOnUI = std::make_shared<std::function<void(std::function<void(jsi::Runtime &)>)>>(
        [weak](std::function<void(jsi::Runtime &)> job) {
          auto proxy = weak.lock();
          if (!proxy) return;
          proxy->getUIScheduler()->scheduleOnUI([weak, job = std::move(job)] {
            if (auto proxy = weak.lock()) {
              if (auto runtime = proxy->getUIWorkletRuntime()) runtime->runSync(job);
            }
          });
        });
    host->provideService("worklets.uiRuntime", runOnUI);
    return true;
  }

  // Bundle mode (the whole bundle on worklet runtimes) isn't supported on
  // Linux yet.
  bool prepareBundleMode(jsi::Runtime &) { return false; }

  bool toggleSlowAnimationsOnUIRuntime(jsi::Runtime &) { return false; }

  bool start(jsi::Runtime &) {
    if (proxy_) proxy_->start();
    return true;
  }

 private:
  void createInitializer(jsi::Runtime &rt) {
    auto jsScheduler =
        std::make_shared<worklets::JSScheduler>(rt, jsInvoker_, [] { return host->isJSThread(); });
    uiScheduler_ = std::make_shared<GtkUIScheduler>();
    status_ = std::make_shared<worklets::RNRuntimeStatus>();
    initializer_ = std::make_shared<worklets::WorkletsModuleProxyInitializer>(jsScheduler, uiScheduler_,
                                                                              runtimeBindings(), status_);
    // Makes the UI runtime (a Hermes runtime, not yet started).
    initializer_->prepareProxy();
  }

  std::shared_ptr<GtkUIScheduler> uiScheduler_;
  std::shared_ptr<worklets::RNRuntimeStatus> status_;
  std::shared_ptr<worklets::WorkletsModuleProxyInitializer> initializer_;
  std::shared_ptr<worklets::WorkletsModuleProxy> proxy_;
};

}  // namespace

}  // namespace rngtk_worklets

std::shared_ptr<const rngtk::Package> rngtk_worklets_package() {
  using namespace rngtk_worklets;
  auto package = std::make_shared<rngtk::Package>();
  package->name = "@curiosity26/react-native-gtk4-worklets";
  package->turboModules.push_back(
      [](const std::string &name, const std::shared_ptr<CallInvoker> &js) -> std::shared_ptr<TurboModule> {
        return name == "WorkletsModule" ? std::make_shared<Module>(js) : nullptr;
      });
  package->setUp = [](rngtk::Host &h) { host = &h; };
  return package;
}
