// AccessibilityInfo on Linux.
//
// React Native's AccessibilityInfo.js calls iOS's AccessibilityManager
// module on every platform but Android, so the host provides one in that
// shape:
// - isScreenReaderEnabled: the AT-SPI bus's org.a11y.Status
//   ScreenReaderEnabled (GNOME sets it while Orca runs), followed live
//   ('screenReaderChanged').
// - isReduceMotionEnabled: GtkSettings' gtk-enable-animations off
//   ('reduceMotionChanged').
// - announceForAccessibility(WithOptions): gtk_accessible_announce.
// - setAccessibilityFocus(reactTag): GTK has no separate screen reader
//   focus, so the view takes keyboard focus (for that once, if it isn't
//   focusable), which is what Orca follows.
// - Bold text, grayscale, invert colors, reduce transparency, darker
//   colors: false.
#pragma once

#include <ReactCommon/TurboModule.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace rngtk {

// Screen reader and reduce motion, read on the main thread; any thread
// may read the values.
class AccessibilityStatus {
 public:
  // On the main thread, after a value changed: the iOS event name
  // ("screenReaderChanged", "reduceMotionChanged") and the new value.
  using OnChange = std::function<void(const char *event, bool value)>;

  // followSystem false: no screen reader (self-tests).
  AccessibilityStatus(GdkDisplay *display, bool followSystem, OnChange onChange);
  ~AccessibilityStatus();
  AccessibilityStatus(const AccessibilityStatus &) = delete;
  AccessibilityStatus &operator=(const AccessibilityStatus &) = delete;

  bool screenReaderEnabled() const { return screenReader_; }
  bool reduceMotionEnabled() const { return reduceMotion_; }
  // Tests: as if a screen reader started or stopped.
  void setScreenReaderEnabled(bool enabled);

 private:
  static void onBusProperties(GDBusProxy *, GVariant *changed, GStrv,
                              gpointer self);
  static void onAnimations(GObject *, GParamSpec *, gpointer self);
  void readScreenReader();

  GtkSettings *settings_;
  OnChange onChange_;
  GDBusProxy *a11yBus_{nullptr};
  gulong animationsHandler_{0};
  std::atomic<bool> screenReader_{false};
  std::atomic<bool> reduceMotion_{false};
};

class AccessibilityManagerModule : public facebook::react::TurboModule {
 public:
  static constexpr const char *kModuleName = "AccessibilityManager";

  struct Host {
    std::weak_ptr<AccessibilityStatus> status;
    // Main thread.
    std::function<void(int reactTag)> focus;
    std::function<void(const std::string &, GtkAccessibleAnnouncementPriority)>
        announce;
  };

  AccessibilityManagerModule(
      std::shared_ptr<facebook::react::CallInvoker> jsInvoker, Host host);

 private:
  Host host_;
};

}  // namespace rngtk
