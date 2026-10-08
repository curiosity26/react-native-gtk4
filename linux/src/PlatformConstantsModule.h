// The PlatformConstants TurboModule for Linux: what Platform.constants
// returns in JS (overrides/Libraries/Utilities/Platform.linux.js).
//
// ReactCxxPlatform ships a PlatformConstants module too, but it returns
// Android's shape with placeholder values, so the host registers this one
// ahead of it.
#pragma once

#include <ReactCommon/TurboModule.h>
#include <folly/dynamic.h>
#include <gtk/gtk.h>

#include <memory>
#include <string>

namespace rngtk {

// Reads the constants on the GTK main thread (GdkDisplay, GtkSettings).
// /etc/os-release, uname and XDG_CURRENT_DESKTOP are read here too, once.
folly::dynamic collectPlatformConstants(GdkDisplay *display, bool isTesting);

class PlatformConstantsModule : public facebook::react::TurboModule {
 public:
  static constexpr const char *kModuleName = "PlatformConstants";

  PlatformConstantsModule(
      std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
      folly::dynamic constants);

 private:
  folly::dynamic constants_;
};

}  // namespace rngtk
