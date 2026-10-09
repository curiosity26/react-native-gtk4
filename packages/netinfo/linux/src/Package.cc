// What autolinking (run-linux) registers: the RNCNetInfo module.
#include "NetInfoModule.h"

#include <rngtk/Extensions.h>

std::shared_ptr<const rngtk::Package> rngtk_netinfo_package() {
  auto package = std::make_shared<rngtk::Package>();
  package->name = "@curiosity26/react-native-gtk4-netinfo";
  package->turboModules.push_back(
      [](const std::string &name, const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker)
          -> std::shared_ptr<facebook::react::TurboModule> {
        if (name == rngtk_netinfo::NetInfoModule::kName) {
          return std::make_shared<rngtk_netinfo::NetInfoModule>(jsInvoker);
        }
        return nullptr;
      });
  return package;
}
