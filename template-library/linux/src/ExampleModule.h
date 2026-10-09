// The "Example" TurboModule (src/NativeExample.ts is its JS side).
//
// rngtk::CxxModule bridges plain C++ methods: arguments and results convert
// with React Native's bridging (std::string, double, bool, folly::dynamic,
// std::optional, AsyncPromise for promises, AsyncCallback for callbacks).
// Methods run on the JS thread; post GTK work to the main thread.
#pragma once

#include <react/bridging/Promise.h>
#include <rngtk/CxxModule.h>

#include <string>

namespace example {

class ExampleModule : public rngtk::CxxModule<ExampleModule> {
 public:
  static constexpr const char *kName = "Example";
  explicit ExampleModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

  // Synchronous: returns at once.
  std::string greet(facebook::jsi::Runtime &rt, std::string name);
  // A promise, resolved on the main thread (where GTK lives).
  facebook::react::AsyncPromise<std::string> gtkVersion(facebook::jsi::Runtime &rt);
  // Any JSON-like value in and out.
  folly::dynamic describe(facebook::jsi::Runtime &rt, folly::dynamic value);
};

}  // namespace example
