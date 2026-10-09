// Native modules and components for React Native on GTK4: what a library's
// linux/ folder gives the host (see template-library/ and
// docs/native-modules.md). Libraries build against ReactNativeGtk::sdk,
// which carries React Native's headers and the host's build flags; apps get
// their libraries' packages through autolinking (run-linux) or by hand
// (AppOptions::packages).
//
//   std::shared_ptr<const rngtk::Package> my_library_package() {
//     auto package = std::make_shared<rngtk::Package>();
//     package->name = "my-library";
//     package->turboModules.push_back(
//         [](const std::string &name, const std::shared_ptr<facebook::react::CallInvoker> &js)
//             -> std::shared_ptr<facebook::react::TurboModule> {
//           return name == "MyModule" ? std::make_shared<MyModule>(js) : nullptr;
//         });
//     package->components.push_back(rngtk::NativeComponent{...});
//     return package;
//   }
//
// Threads: TurboModule methods run on the JS thread; touch GTK only on the
// main thread (g_main_context_invoke). A NativeComponent's functions run on
// the main thread.
#pragma once

#include <folly/dynamic.h>
#include <gtk/gtk.h>
#include <react/nativemodule/TurboModuleProvider.h>
#include <react/renderer/componentregistry/ComponentDescriptorProvider.h>
#include <react/renderer/mounting/ShadowView.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rngtk {

// A Fabric component whose views are GTK widgets the library makes. The
// host places each widget at its Yoga frame inside its parent view, gives
// it the view's opacity, transform, tooltip, pointer events and
// accessibility label, hint and hidden state (the widget keeps GTK's own
// role and keyboard focus), and calls these:
struct NativeComponent {
  // The component's descriptor: its name, props, state, event emitter
  // (concreteComponentDescriptorProvider<MyComponentDescriptor>()).
  facebook::react::ComponentDescriptorProvider descriptor;
  // A new widget for `view` (it is owned by the host after).
  std::function<GtkWidget *(const facebook::react::ShadowView &view)> create;
  // Props, state or the event emitter changed (oldView is empty the first
  // time). Keep view.eventEmitter to send events from GTK's signals.
  std::function<void(GtkWidget *widget, const facebook::react::ShadowView &oldView,
                     const facebook::react::ShadowView &newView)>
      update;
  // A command from JS (Commands.myCommand(ref, ...args)). Optional.
  std::function<void(GtkWidget *widget, const std::string &name, const folly::dynamic &args)>
      command;
};

// What a library adds: TurboModules and native components.
struct Package {
  std::string name;
  facebook::react::TurboModuleProviders turboModules;
  std::vector<NativeComponent> components;
};

}  // namespace rngtk
