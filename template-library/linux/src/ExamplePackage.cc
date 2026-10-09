// What this library gives the app: autolinking (run-linux) calls
// example_package() and passes it to the host.
#include "ExampleModule.h"
#include "ExampleView.h"

#include <rngtk/Extensions.h>

std::shared_ptr<const rngtk::Package> example_package() {
  auto package = std::make_shared<rngtk::Package>();
  package->name = "example";
  package->turboModules.push_back(
      [](const std::string &name, const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker)
          -> std::shared_ptr<facebook::react::TurboModule> {
        if (name == example::ExampleModule::kName) {
          return std::make_shared<example::ExampleModule>(jsInvoker);
        }
        return nullptr;
      });
  package->components.push_back(example::exampleViewComponent());
  return package;
}
