// Owns a React Native instance (Hermes + Fabric) whose JS work runs on the
// GTK main loop and whose views mount into GTK widgets.
#pragma once

#include <gtk/gtk.h>
#include <react/renderer/core/ReactPrimitives.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace facebook::react {
class ReactHost;
class RunLoopObserverManager;
class SurfaceDelegate;
}  // namespace facebook::react

namespace rngtk {

class DevUI;
class GtkMessageQueueThread;
class GtkMountingManager;

struct RNGtkHostOptions {
  // Reported to JS as Platform.isTesting (in dev bundles).
  bool isTesting = false;
  // Load from Metro, with reload, fast refresh, LogBox and the dev menu.
  bool devMode = false;
  std::string devServerHost = "localhost";
  uint32_t devServerPort = 8081;
  // Connect to Metro's inspector proxy so React Native DevTools can attach.
  bool inspector = false;
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
           float height);

  // Dev mode: reloads the JS (like `r` in Metro's terminal).
  void reload();
  void openDebugger();
  void showDevMenu();

  GtkMountingManager &mountingManager() { return *mountingManager_; }
  DevUI *devUI() { return devUI_.get(); }
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
  void startAppSurface();
  void loadFromDevServer();
  void showErrorBanner(const std::string &message);
  static gboolean beforeWaiting(GSource *source, gint *timeout);

  RNGtkHostOptions options_;
  GtkOverlay *overlay_;
  float width_{0}, height_{0};
  std::string script_;
  facebook::react::SurfaceId surfaceId_{0};
  std::string moduleName_;
  std::shared_ptr<GtkMountingManager> mountingManager_;
  std::shared_ptr<facebook::react::RunLoopObserverManager> runLoopObservers_;
  std::weak_ptr<GtkMessageQueueThread> queue_;
  std::shared_ptr<DevUI> devUI_;
  std::shared_ptr<LogBoxDelegate> logBox_;
  std::unique_ptr<facebook::react::ReactHost> reactHost_;
  std::thread loader_;
  GSource *observerSource_{nullptr};
  std::atomic<int> jsErrors_{0};
  std::atomic<int> instances_{0};
  std::atomic<bool> loaded_{false};
  std::atomic<bool> loadFailed_{false};
};

}  // namespace rngtk
