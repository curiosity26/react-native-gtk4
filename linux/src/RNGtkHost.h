// Owns a React Native instance (Hermes + Fabric) whose JS work runs on the
// GTK main loop and whose views mount into GTK widgets.
#pragma once

#include <folly/dynamic.h>
#include <rngtk/App.h>
#include <gtk/gtk.h>
#include <react/nativemodule/TurboModuleProvider.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/core/LayoutContext.h>
#include <react/renderer/core/ReactPrimitives.h>

#include <atomic>
#include <map>
#include <set>
#include <cstdint>
#include <functional>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace facebook::react {
class NativeAnimatedNodesManagerProvider;
class ReactHost;
class RunLoopObserverManager;
class SurfaceDelegate;
}  // namespace facebook::react

namespace rngtk {

class AccessibilityStatus;
struct PlatformState;
class Appearance;
class DevUI;
class JsMessageQueueThread;
class GtkImageLoader;
class GtkKeyboardHandler;
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
  // Follow the desktop's screen reader state (AT-SPI). Off, there is none.
  bool followSystemAccessibility = true;
  // More TurboModules, asked before the host's own.
  facebook::react::TurboModuleProviders extraTurboModules;
  // Libraries' native modules and components (rngtk/Extensions.h).
  PackageList packages;
  // Linking.getInitialURL(): the URL the app was started with.
  std::string initialURL;
  // Tests: called instead of launching a URL for Linking.openURL; true if
  // it took it.
  std::function<bool(const std::string &)> openURLOverride;
  // When the last of the app's windows closes, quit (Windows module). Off,
  // the main window hides instead and the app keeps running.
  bool quitOnLastWindowClosed = true;
};

// A window of the app's own (the Windows module, AppWindows.cc): another
// surface of a registered component, in the same JS runtime.
struct WindowOptions {
  std::string moduleName;
  folly::dynamic initialProps = folly::dynamic::object();
  std::string title;
  int width = 800, height = 600;
  int minWidth = 0, minHeight = 0;
  bool resizable = true;
  // The close button only asks (a 'close-requested' event); the app
  // closes the window itself.
  bool interceptClose = false;
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

  // Linking: a URL passed to the running app (GApplication's open): a
  // 'url' event, or the initial URL before JS runs.
  void openURL(const std::string &url);
  // Tests: the AppState as if the window changed (0 active, 1 inactive,
  // 2 background).
  void setAppStateForTesting(int state) { setAppState(state); }

  // Windows (AppWindows.cc), main thread unless noted. A window's id is its
  // surface's id (a root tag): the main window's is the one run() got.
  // Ids for new windows; any thread.
  facebook::react::SurfaceId allocateWindowId();
  void openWindow(facebook::react::SurfaceId id, WindowOptions options);
  // Closes it (the main window hides if others are open, or the app stays
  // up without windows: setQuitOnLastWindowClosed(false)).
  void closeWindow(facebook::react::SurfaceId id);
  void setWindowTitle(facebook::react::SurfaceId id, const std::string &title);
  void setWindowSize(facebook::react::SurfaceId id, int width, int height);
  void setWindowMinSize(facebook::react::SurfaceId id, int width, int height);
  void focusWindow(facebook::react::SurfaceId id);
  void setInterceptClose(facebook::react::SurfaceId id, bool intercept);
  void setQuitOnLastWindowClosed(bool quit) { options_.quitOnLastWindowClosed = quit; }
  // The open windows, main first: {id, title, width, height, focused}; and
  // one window's metrics for useWindowDimensions ({width, height, scale,
  // fontScale}, or null). Any thread.
  folly::dynamic windowList() const;
  folly::dynamic windowMetrics(facebook::react::SurfaceId id) const;
  facebook::react::SurfaceId mainWindowId() const { return surfaceId_; }
  // Tests.
  GtkWindow *windowFor(facebook::react::SurfaceId id) const;
  GtkWidget *rootFor(facebook::react::SurfaceId id) const;
  GtkPointerHandler *pointerHandlerFor(facebook::react::SurfaceId id);

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
  // Keys and focus for the app's surface (tests drive it directly).
  GtkKeyboardHandler *keyboardHandler() { return keyboardHandler_.get(); }
  DevUI *devUI() { return devUI_.get(); }
  Appearance &appearance() { return *appearance_; }
  AccessibilityStatus &accessibilityStatus() { return *accessibilityStatus_; }
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
  class WindowsModule;
  struct AppWindow;
  // LogBox runs as its own React surface (AppRegistry "LogBox").
  static constexpr facebook::react::SurfaceId kLogBoxSurfaceId = 1001;
  std::shared_ptr<facebook::react::TurboModule> makeWindowsModule(
      const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker);
  // Shutting down: the windows opened from JS go, without events.
  void closeAllWindows();
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
  // AppState from the app's windows: active while one is, inactive, or
  // background when they're all minimized/suspended or hidden.
  void connectWindowState();
  void trackWindow(GtkWindow *window);
  static void onTrackedWindowGone(gpointer self, GObject *window);
  void updateAppState();
  // Windows (AppWindows.cc).
  void emitWindowEvent(facebook::react::SurfaceId id, const char *type,
                       folly::dynamic extra = folly::dynamic::object());
  facebook::react::SurfaceId idForWindow(GtkWindow *window) const;
  bool onMainCloseRequest();
  void onWindowLayout(facebook::react::SurfaceId id);
  void destroyWindow(facebook::react::SurfaceId id);
  void quitIfNoWindows();
  void storeMetrics(facebook::react::SurfaceId id, float width, float height);
  facebook::react::LayoutConstraints constraintsFor(float width, float height) const;
  void setAppState(int state);
  // The surfaces' layout: size, RTL direction, font scale.
  facebook::react::LayoutConstraints layoutConstraints() const;
  facebook::react::LayoutContext layoutContext() const;
  void refreshLayout();
  static void onFontDpi(GObject *, GParamSpec *, gpointer self);
  // AccessibilityInfo.setAccessibilityFocus / announceForAccessibility.
  void focusForAccessibility(facebook::react::Tag tag);
  void announce(const std::string &text, GtkAccessibleAnnouncementPriority priority);
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
  std::shared_ptr<AccessibilityStatus> accessibilityStatus_;
  std::shared_ptr<PlatformState> platform_;
  GtkWindow *window_{nullptr};
  // The app's windows AppState follows (all of the GtkApplication's).
  GtkApplication *trackedApp_{nullptr};
  std::vector<GtkWindow *> trackedWindows_;  // weak
  // Windows opened from JS (AppWindows.cc), by surface id.
  // (shared_ptr: AppWindow is complete only in AppWindows.cc.)
  std::map<facebook::react::SurfaceId, std::shared_ptr<AppWindow>> windows_;
  std::atomic<int> nextWindowId_{11};
  std::set<facebook::react::SurfaceId> interceptClose_;
  bool mainHidden_{false};
  // Each window's size and title for JS (any thread).
  mutable std::mutex windowsMutex_;
  struct WindowInfo {
    std::string title;
    float width, height;
  };
  std::map<facebook::react::SurfaceId, WindowInfo> windowInfo_;
  // Lets callbacks posted to the main loop tell the host is gone.
  std::shared_ptr<int> alive_ = std::make_shared<int>(0);
  gulong fontDpiHandler_{0};
  std::shared_ptr<LogBoxDelegate> logBox_;
  std::unique_ptr<facebook::react::ReactHost> reactHost_;
  std::thread loader_;
  std::shared_ptr<GtkImageLoader> imageLoader_;
  std::string bundleURL_;  // file:// URL of a release bundle
  std::unique_ptr<GtkPointerHandler> pointerHandler_;
  std::unique_ptr<GtkPointerHandler> logBoxPointerHandler_;
  std::unique_ptr<GtkKeyboardHandler> keyboardHandler_;
  std::unique_ptr<GtkKeyboardHandler> logBoxKeyboardHandler_;
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
