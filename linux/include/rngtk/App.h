// The public API of ReactNativeGtk::host (librngtk_host.so): runs a React
// Native app in a GTK4 window. An app's linux/main.cc fills AppOptions and
// calls runApp; see template/linux.
//
// Command line (parsed by runApp):
//   --dev-server [HOST:PORT]  load from Metro (default in Debug builds,
//                             localhost:8081)
//   --bundle FILE             load a bundle file (default in Release builds:
//                             index.bundle.js next to the executable, or
//                             ../share/<executable>/index.bundle.js)
//   --smoke                   wait for the first mount, check there were no
//                             JS errors, print "SMOKE OK" and exit 0 (1 on
//                             error or timeout)
//   --screenshot FILE         with --smoke: save the window as a PNG
//   --timeout MS              with --smoke (default 60000)
//   --no-inspector            dev mode: don't connect to the inspector
//   --verbose                 log at info level
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace rngtk {

// Native modules and components from libraries (rngtk/Extensions.h).
struct Package;
using PackageList = std::vector<std::shared_ptr<const Package>>;

struct AppOptions {
  // GApplication id, e.g. "com.myapp".
  std::string appId;
  // Window title.
  std::string title;
  // The component registered with AppRegistry.registerComponent.
  std::string moduleName;
  int width = 800;
  int height = 600;
  // The entry file Metro bundles in dev mode, without extension.
  std::string entry = "index";
  // Load from Metro unless --bundle is given. Defaults to Debug builds of
  // the app (the inline default below is compiled into the app).
#ifdef NDEBUG
  bool devServerByDefault = false;
#else
  bool devServerByDefault = true;
#endif
  std::string devServerHost = "localhost";
  int devServerPort = 8081;
  // Quit when the app's last window closes (JS can change it:
  // Windows.setQuitOnLastWindowClosed). Off, closing the main window hides
  // it and the app keeps running.
  bool quitOnLastWindowClosed = true;
  // Libraries' native modules and components (rngtk/Autolinking.h:
  // autolinkedPackages()).
  PackageList packages;
};

// Runs the app until its window closes. Returns the process exit status.
int runApp(int argc, char **argv, const AppOptions &options);

// The React Native version the host was built against, e.g. "0.87.1".
const char *reactNativeVersion();

}  // namespace rngtk
