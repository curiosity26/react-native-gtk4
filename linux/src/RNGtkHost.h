// Owns a React Native instance (Hermes + Fabric) whose JS work runs on the
// GTK main loop and whose views mount into GTK widgets.
#pragma once

#include <gtk/gtk.h>
#include <react/renderer/core/ReactPrimitives.h>

#include <memory>
#include <string>

namespace facebook::react {
class ReactHost;
class RunLoopObserverManager;
}  // namespace facebook::react

namespace rngtk {

class GtkMessageQueueThread;
class GtkMountingManager;

class RNGtkHost {
 public:
  RNGtkHost();
  ~RNGtkHost();
  RNGtkHost(const RNGtkHost &) = delete;
  RNGtkHost &operator=(const RNGtkHost &) = delete;

  // Queues the bundle to run; JS starts once the main loop runs.
  bool loadBundle(const std::string &path);

  // Runs `moduleName` (an AppRegistry component) at a fixed size, mounting
  // into `root`, an RNView the caller has placed in a window.
  void startSurface(facebook::react::SurfaceId surfaceId,
                    const std::string &moduleName, GtkWidget *root,
                    float width, float height);
  void stopSurface(facebook::react::SurfaceId surfaceId);

  GtkMountingManager &mountingManager() { return *mountingManager_; }
  // True when no JS work is queued.
  bool isIdle() const;
  int jsErrorCount() const { return jsErrors_; }

 private:
  static gboolean beforeWaiting(GSource *source, gint *timeout);

  std::shared_ptr<GtkMountingManager> mountingManager_;
  std::shared_ptr<facebook::react::RunLoopObserverManager> runLoopObservers_;
  std::weak_ptr<GtkMessageQueueThread> queue_;
  std::unique_ptr<facebook::react::ReactHost> reactHost_;
  GSource *observerSource_{nullptr};
  int jsErrors_{0};
};

}  // namespace rngtk
