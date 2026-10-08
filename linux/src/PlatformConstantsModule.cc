#include "PlatformConstantsModule.h"

#include <jsi/JSIDynamic.h>
#include <sys/utsname.h>

#include <cstdlib>
#include <fstream>
#include <map>

namespace rngtk {

namespace {

// KEY=value lines of os-release(5); values may be quoted.
std::map<std::string, std::string> readOsRelease() {
  std::map<std::string, std::string> fields;
  for (const char *path : {"/etc/os-release", "/usr/lib/os-release"}) {
    std::ifstream in(path);
    if (!in) continue;
    std::string line;
    while (std::getline(in, line)) {
      auto eq = line.find('=');
      if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
      std::string value = line.substr(eq + 1);
      if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') &&
          value.back() == value.front()) {
        value = value.substr(1, value.size() - 2);
      }
      fields[line.substr(0, eq)] = value;
    }
    break;
  }
  return fields;
}

std::string windowSystem(GdkDisplay *display) {
  // By type name, so the host needs neither gdkwayland.h nor gdkx.h.
  std::string type = display ? G_OBJECT_TYPE_NAME(display) : "";
  if (type == "GdkWaylandDisplay") return "wayland";
  if (type == "GdkX11Display") return "x11";
  return "unknown";
}

}  // namespace

folly::dynamic collectPlatformConstants(GdkDisplay *display, bool isTesting) {
  auto os = readOsRelease();
  struct utsname uts{};
  std::string kernel = uname(&uts) == 0 ? uts.release : "";
  const char *desktop = getenv("XDG_CURRENT_DESKTOP");

  gboolean animations = TRUE;
  if (display) {
    g_object_get(gtk_settings_get_for_display(display),
                 "gtk-enable-animations", &animations, nullptr);
  }

  std::string gtkVersion = std::to_string(gtk_get_major_version()) + "." +
                           std::to_string(gtk_get_minor_version()) + "." +
                           std::to_string(gtk_get_micro_version());

  std::string pretty = os.count("PRETTY_NAME") ? os["PRETTY_NAME"]
                       : os.count("NAME")      ? os["NAME"]
                                               : "Linux";
  return folly::dynamic::object
      ("isTesting", isTesting)
      ("isDisableAnimations", !animations)
      ("reactNativeVersion", folly::dynamic::object
          ("major", RNGTK_RN_VERSION_MAJOR)
          ("minor", RNGTK_RN_VERSION_MINOR)
          ("patch", RNGTK_RN_VERSION_PATCH)
          ("prerelease", nullptr))
      // Like iOS, Version is the OS version as a string. Rolling-release
      // distros have no VERSION_ID; the kernel release stands in there.
      ("Version", os.count("VERSION_ID") ? os["VERSION_ID"] : kernel)
      ("Release", pretty)
      ("osId", os.count("ID") ? os["ID"] : "linux")
      ("osVersion", os.count("VERSION_ID") ? os["VERSION_ID"] : "")
      ("kernelRelease", kernel)
      ("gtkVersion", gtkVersion)
      ("windowSystem", windowSystem(display))
      ("desktop", desktop ? desktop : "");
}

PlatformConstantsModule::PlatformConstantsModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
    folly::dynamic constants)
    : TurboModule(kModuleName, std::move(jsInvoker)),
      constants_(std::move(constants)) {
  methodMap_["getConstants"] = MethodMetadata{
      0, [](facebook::jsi::Runtime &rt, TurboModule &module,
            const facebook::jsi::Value *, size_t) {
        auto &self = static_cast<PlatformConstantsModule &>(module);
        return facebook::jsi::valueFromDynamic(rt, self.constants_);
      }};
}

}  // namespace rngtk
