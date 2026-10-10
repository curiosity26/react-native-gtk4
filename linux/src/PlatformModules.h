// React Native's platform APIs on Linux, as TurboModules in the shapes the
// JS modules call off Android (iOS's, mostly):
//
// - Linking (LinkingManager): openURL with the desktop's default handler
//   for the scheme (GIO; in a sandbox, GtkUriLauncher and the OpenURI
//   portal), canOpenURL (is there one),
//   getInitialURL (a URL the app was started with, from its command line
//   or a .desktop file's %u), 'url' events for URLs passed to the running
//   app (GApplication's open). openSettings rejects: no app settings.
// - Clipboard: GDK's clipboard.
// - Vibration: accepted, does nothing (desktops don't vibrate).
// - DeviceEventManager: BackHandler.exitApp() quits the app. The host
//   sends hardwareBackPress for Alt+Left, the Back key and the mouse's
//   back button.
// - I18nManager: right-to-left from the locale (GTK's default
//   direction), with allowRTL / forceRTL / swapLeftAndRightInRTL kept in
//   the user's config, applied when the app (re)starts, as on iOS.
// - AppState: 'active' while the window is the active one, 'inactive'
//   when another window is, 'background' when minimized; with
//   appStateDidChange and Android's appStateFocusChange (focus/blur).
//
// Share and PixelRatio live elsewhere (overrides/Libraries/Share,
// RNGtkHost's DeviceInfo).
#pragma once

#include <ReactCommon/TurboModule.h>
#include <folly/dynamic.h>
#include <gtk/gtk.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace rngtk {

// The right-to-left settings I18nManager reads and writes, persisted in
// $XDG_CONFIG_HOME/react-native-gtk4/<appId>/i18n.ini. Any thread.
class I18nSettings {
 public:
  explicit I18nSettings(const std::string &appId);
  bool isRTL() const;
  bool doLeftAndRightSwapInRTL() const;
  std::string localeIdentifier() const { return locale_; }
  void setAllowRTL(bool allow);
  void setForceRTL(bool force);
  void setSwapLeftAndRight(bool swap);

 private:
  void save();
  mutable std::mutex mutex_;
  std::string path_;
  std::string locale_;
  bool localeRTL_ = false;
  bool allowRTL_ = true, forceRTL_ = false, swap_ = true;
};

// What the modules share with the host.
struct PlatformState {
  // Linking: the URL the app started with (until JS asks; any thread).
  std::mutex mutex;
  std::optional<std::string> initialURL;
  // AppState, kept current by the host on the main thread.
  std::atomic<int> appState{0};  // 0 active, 1 inactive, 2 background
  std::shared_ptr<I18nSettings> i18n;
  // Main thread: the window URLs open over, and (tests) a stand-in for
  // launching them; true if it took the URL.
  std::function<GtkWindow *()> window;
  std::function<bool(const std::string &)> openURLOverride;
  // The app's id (GApplication's), and (main thread) a device event to JS:
  // [name, payload].
  std::string appId;
  std::function<void(folly::dynamic)> emitDeviceEvent;
};

const char *appStateName(int state);

// The module named `name`, or null.
std::shared_ptr<facebook::react::TurboModule> makePlatformModule(
    const std::string &name,
    const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker,
    const std::shared_ptr<PlatformState> &state);

}  // namespace rngtk
