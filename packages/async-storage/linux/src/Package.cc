// What autolinking (run-linux) registers: the RNAsyncStorage module.
#include "AsyncStorageModule.h"

#include <rngtk/Extensions.h>

std::shared_ptr<const rngtk::Package> rngtk_async_storage_package() {
  auto package = std::make_shared<rngtk::Package>();
  package->name = "@curiosity26/react-native-gtk4-async-storage";
  package->turboModules.push_back(
      [](const std::string &name, const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker)
          -> std::shared_ptr<facebook::react::TurboModule> {
        if (name == rngtk_async_storage::AsyncStorageModule::kName) {
          return std::make_shared<rngtk_async_storage::AsyncStorageModule>(jsInvoker);
        }
        return nullptr;
      });
  return package;
}
