// The dev-mode chrome around a React Native surface: a banner for
// "Loading from Metro…" and DevLoadingView messages ("Refreshing..."), a
// debugger-paused bar, and the dev menu button. Lives in a GtkOverlay above
// the app's root view.
//
// ReactHost calls IDevUIDelegate from its own threads (bundle download,
// reload); every method hops to the GTK main thread.
#pragma once

#include <gtk/gtk.h>
#include <react/devsupport/IDevUIDelegate.h>

#include <functional>
#include <memory>
#include <string>

namespace rngtk {

class DevUI : public facebook::react::IDevUIDelegate,
              public std::enable_shared_from_this<DevUI> {
 public:
  // Adds the banner and menu button to `overlay`. `menu` is the dev menu's
  // model; its actions must be reachable from the overlay.
  static std::shared_ptr<DevUI> create(GtkOverlay *overlay, GMenuModel *menu);
  ~DevUI() noexcept override;

  void showDownloadBundleProgress() override;
  void hideDownloadBundleProgress() override;
  void showLoadingView(const std::string &message,
                       facebook::react::SharedColor textColor,
                       facebook::react::SharedColor backgroundColor) override;
  void hideLoadingView() override;
  void showDebuggerOverlay(std::function<void()> &&resumeDebuggerFn) override;
  void hideDebuggerOverlay() override;

  // Main thread only.
  void showError(const std::string &message);
  void popupMenu();
  // The banner text, or "" when hidden (for tests).
  std::string bannerText() const;

 private:
  DevUI() = default;
  void onMain(std::function<void(DevUI &)> fn);
  void setBanner(const std::string &message, const std::string &fg,
                 const std::string &bg);
  void hideBanner();

  GtkWidget *banner_{nullptr};
  GtkWidget *bannerLabel_{nullptr};
  GtkWidget *resumeButton_{nullptr};
  GtkWidget *menuButton_{nullptr};
  GtkCssProvider *css_{nullptr};
  std::function<void()> resumeDebugger_;
};

}  // namespace rngtk
