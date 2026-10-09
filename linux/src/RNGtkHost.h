// Owns a React Native instance (Hermes + Fabric) whose JS work runs on the
// GTK main loop and whose views mount into GTK widgets.
#pragma once

#include <folly/dynamic.h>
#include <gtk/gtk.h>
#include <react/nativemodule/TurboModuleProvider.h>
#include <react/renderer/core/ReactPrimitives.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <memory>
#include <string>
#include <thread>

namespace facebook::react {
class NativeAnimatedNodesManagerProvider;
class ReactHost;
class RunLoopObserverManager;
class SurfaceDelegate;
}  // namespace facebook::react

namespace rngtk {

class Appearance;
class DevUI;
class JsMessageQueueThread;
class GtkImageLoader;
class GtkMountingManager;
class GtkPointerHandler;

struct RNGtkHostOptions {
  // The app's id, sent to Metro and React Native DevTools.
  std::string appId = "dev.curiosity26.RNGtk4";
  // Reported to JS as Platform.isTesting (in dev bundles).
  bool isTesting = false;
  // Load from Metro, with reload, fast refresh, LogBox and the dev menu.
  bool devMode = false;
  std::string devServerHost = "localhost";
  uint32_t devServerPort = 8081;
  // Connect to Metro's inspector proxy so React Native DevTools can attach.
  bool inspector = false;
  // Resize the app's surface to the overlay's size whenever the window
  // resizes (see setFollowsWindowSize).
  bool followsWindowSize = false;
  // Follow the desktop's light/dark style (XDG portal). Off, the system
  // counts as light; Appearance.setColorScheme() still switches.
  bool followSystemAppearance = true;
  // More TurboModules, asked before the host's own.
  facebook::react::TurboModuleProviders extraTurboModules;
};

class RNGtkHost {
 public:
  // `overlay` holds the app's root view as its child; dev mode adds the
  // dev banner, the dev menu button and the LogBox surface on top of it.
  RNGtkHost(RNGtkHostOptions options, GtkOverlay *overlay);
  ~RNGtkHost();
  RNGtkHost(const RNGtkHost &) = delete;
  RNGtkHost &operator=(const RNGtkHost &) = delete;

  // Runs `moduleName` (an AppRegistry component) at a fixed size, mounting
  // into `root`, an RNView in the overlay.
  //
  // Release: `script` is a bundle file, loaded on the main loop.
  // Dev: `script` is the entry file's path relative to Metro's project root
  // without extension ("index"); the bundle downloads on a worker thread
  // while the main loop shows the loading banner.
  // Returns false if a release bundle can't be read.
  bool run(const std::string &script, facebook::react::SurfaceId surfaceId,
           const std::string &moduleName, GtkWidget *root, float width,
           float height,
           folly::dynamic initialProps = folly::dynamic::object());

  // Resizes the surface (and LogBox's) to `width` x `height` and tells JS:
  // Dimensions' window changes and emits a `change`.
  void setSize(float width, float height);
  // When on, setSize() follows the overlay's allocation after each layout.
  void setFollowsWindowSize(bool follows);

  // Dev mode: reloads the JS (like `r` in Metro's terminal).
  void reload();
  void openDebugger();
  void showDevMenu();

  GtkMountingManager &mountingManager() { return *mountingManager_; }
  // Input for the app's surface (tests drive it directly).
  GtkPointerHandler *pointerHandler() { return pointerHandler_.get(); }
  GtkPointerHandler *logBoxPointerHandler() {
    return logBoxPointerHandler_.get();
  }
  GtkWidget *logBoxRoot() const { return logBoxRoot_; }
  DevUI *devUI() { return devUI_.get(); }
  Appearance &appearance() { return *appearance_; }
  // True when no JS work is queued.
  bool isIdle() const;
  int jsErrorCount() const { return jsErrors_; }
  // JS instances created so far: 1 + the number of reloads.
  int instanceCount() const { return instances_; }
  // Dev mode: false until the bundle from Metro has loaded (or failed).
  bool isLoaded() const { return loaded_; }
  bool loadFailed() const { return loadFailed_; }
  bool isLogBoxShowing() const;

 private:
  class LogBoxDelegate;
  class DeviceInfoModule;
  class AppearanceModule;
  // Dimensions' window (the surface) and screen (the window's monitor),
  // in GTK's logical pixels (React Native's points).
  struct Metrics {
    double width, height, scale, fontScale;
  };
  struct Dimensions {
    Metrics window, screen;
  };
  Dimensions dimensions() const;
  void updateDimensions();
  static void onLayout(GdkFrameClock *, gpointer self);
  static void onOverlayRealize(GtkWidget *, gpointer self);
  static gboolean onAnimationFrame(GtkWidget *, GdkFrameClock *, gpointer self);
  std::shared_ptr<facebook::react::NativeAnimatedNodesManagerProvider>
  makeAnimatedProvider();
  void updateAnimationTick();
  void startAppSurface();
  void loadFromDevServer();
  void showErrorBanner(const std::string &message);
  void onAppearanceChanged();
  static gboolean beforeWaiting(GSource *source, gint *timeout);

  RNGtkHostOptions options_;
  GtkOverlay *overlay_;
  // Read on the loader thread when the surface starts.
  std::atomic<float> width_{0}, height_{0};
  GtkWidget *root_{nullptr};
  bool followsWindowSize_{false};
  GdkFrameClock *layoutClock_{nullptr};
  gulong layoutHandler_{0};
  mutable std::mutex dimensionsMutex_;
  Dimensions dimensions_{{0, 0, 1, 1}, {0, 0, 1, 1}};
  std::string script_;
  facebook::react::SurfaceId surfaceId_{0};
  std::string moduleName_;
  folly::dynamic initialProps_ = folly::dynamic::object();
  std::shared_ptr<GtkMountingManager> mountingManager_;
  std::shared_ptr<facebook::react::RunLoopObserverManager> runLoopObservers_;
  mutable std::mutex queueMutex_;
  std::weak_ptr<JsMessageQueueThread> queue_;
  // Guards the event beat against ReactHost re-creating it on reload.
  std::mutex beatMutex_;
  bool creatingInstance_{false};
  std::shared_ptr<DevUI> devUI_;
  // Outlives ReactHost (and its Appearance module).
  std::shared_ptr<Appearance> appearance_;
  std::shared_ptr<LogBoxDelegate> logBox_;
  std::unique_ptr<facebook::react::ReactHost> reactHost_;
  std::thread loader_;
  std::shared_ptr<GtkImageLoader> imageLoader_;
  std::string bundleURL_;  // file:// URL of a release bundle
  std::unique_ptr<GtkPointerHandler> pointerHandler_;
  std::unique_ptr<GtkPointerHandler> logBoxPointerHandler_;
  GtkWidget *logBoxRoot_{nullptr};
  // Native Animated runs a frame callback while animations are active.
  std::mutex animationMutex_;
  std::shared_ptr<std::function<void()>> onAnimationRender_;
  // The JS instance whose manager owns onAnimationRender_ (1, 2... per
  // reload), and the latest one created.
  int animationOwner_{0};
  std::atomic<int> animationGeneration_{0};
  guint animationTick_{0};
  GSource *observerSource_{nullptr};
  std::atomic<int> jsErrors_{0};
  std::atomic<int> instances_{0};
  std::atomic<bool> loaded_{false};
  std::atomic<bool> loadFailed_{false};
};

}  // namespace rngtk
