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
#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/scheduler/Scheduler.h>
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
  // A container: its children (the views React mounts inside it, which can
  // be other components of the library's) are handed to these instead of
  // the host placing them, in order (index). The host still creates,
  // updates and destroys each child. Optional: without them the component
  // has no children.
  std::function<void(GtkWidget *parent, GtkWidget *child, int index)> insertChild;
  std::function<void(GtkWidget *parent, GtkWidget *child)> removeChild;
};

// Pointer input on the app's surfaces (main windows and Windows.open's),
// as the host receives it, before it becomes React Native's touches.
struct PointerInput {
  // Scroll: a wheel or touchpad scroll of (dx, dy). Pinch: a touchpad
  // pinch (pinchPhase, scale, angleDelta), at the pointer.
  enum class Phase { Down, Move, Up, Cancel, Leave, Scroll, Pinch };
  enum class PinchPhase { Begin, Update, End, Cancel };
  enum class Device { Mouse, Touch };
  Phase phase = Phase::Move;
  Device device = Device::Mouse;
  // 0 for the mouse; touch points count from 1.
  int pointerId = 0;
  // In the surface root's coordinates.
  double x = 0, y = 0;
  // 1 primary, 2 middle, 3 secondary (GDK's numbering; 8 and 9: back and
  // forward).
  int button = 1;
  GdkModifierType modifiers = GdkModifierType(0);
  uint32_t timeMs = 0;
  double dx = 0, dy = 0;  // Scroll; Pinch: the fingers' movement
  // Pinch: the scale since it began, and the rotation since the last event
  // (radians, clockwise).
  PinchPhase pinchPhase = PinchPhase::Update;
  double scale = 1, angleDelta = 0;
  // The surface's root view, and the React view under the pointer (the
  // host's hit-testing: transforms, clipping, pointerEvents), or null.
  GtkWidget *root = nullptr;
  GtkWidget *target = nullptr;
};

// The host, for a package's native code that needs more than modules and
// components (gesture recognizers, say). Main thread only.
class Host {
 public:
  virtual ~Host() = default;
  // Sees every pointer event on the surfaces, before the host turns it into
  // touches and pointer events; returning true keeps the host from handling
  // it.
  virtual void addPointerObserver(std::function<bool(const PointerInput &)> observer) = 0;
  // Ends the touches in progress on the surface `root` with touchCancel:
  // the JS responder (Pressable, ScrollView's) loses them, as when a native
  // gesture takes over on the other platforms.
  virtual void cancelTouches(GtkWidget *root) = 0;
  // A mounted view by React tag (null if none), and a view's tag (0 if it
  // isn't one).
  virtual GtkWidget *viewForTag(int tag) = 0;
  virtual int tagForView(GtkWidget *view) = 0;
  // A mounted view's event emitter, to send it events (emitter->
  // dispatchEvent("onMyEvent", payload)), or null.
  virtual facebook::react::SharedEventEmitter eventEmitterForView(GtkWidget *view) = 0;
  // The React component of a mounted view ("View", "ScrollView", "Switch",
  // a library's...), or "".
  virtual std::string componentName(GtkWidget *view) = 0;
  // Applies props to a mounted view now, without a React commit, as native
  // Animated does (opacity, transform, colors...); they hold until a commit
  // updates the view (its props or its layout), which applies the
  // committed props (Reanimated's commit hook keeps its values in those).
  virtual void setNativeProps(GtkWidget *view, folly::dynamic props) = 0;
  // Called when the user scrolls a ScrollView (a drag, the wheel), with its
  // view.
  virtual void addScrollObserver(std::function<void(GtkWidget *scrollView)> observer) = 0;
  // A device event to JS (RCTDeviceEventEmitter.emit(name, payload)).
  virtual void emitDeviceEvent(const std::string &name, folly::dynamic payload) = 0;
  // Runs `fn` on the main thread after the mount transactions committed
  // so far (a TurboModule call naming a view JS just rendered finds it
  // mounted). Any thread; in order.
  virtual void runAfterMounts(std::function<void()> fn) = 0;

  // ---- For libraries that drive Fabric themselves (Reanimated) ----
  // These may be called from any thread.
  //
  // The GTK main thread and the JS thread (the current JS instance's).
  virtual bool isMainThread() = 0;
  virtual bool isJSThread() = 0;
  // Runs `fn` on the main thread, soon (not inline).
  virtual void runOnMainThread(std::function<void()> fn) = 0;
  // Runs `task` on the JS thread with the runtime, through React Native's
  // RuntimeScheduler.
  virtual void runOnJSThread(std::function<void(facebook::jsi::Runtime &)> task) = 0;
  // Runs `task` now with the JS instance's Fabric scheduler (its
  // getUIManager(): commit and mount hooks, the shadow trees; and
  // addEventListener: every event before JS gets it). Not while a reload is
  // replacing the instance: then it doesn't run.
  virtual void runOnScheduler(std::function<void(facebook::react::Scheduler &)> task) = 0;
  // Calls `callback` once on the main thread at the next frame of the main
  // window's GdkFrameClock, with its frame time (ms, monotonic), as
  // requestAnimationFrame does.
  virtual void requestFrame(std::function<void(double timestampMs)> callback) = 0;
  // The frame time of the frame being drawn (ms, the clock of
  // requestFrame), or the monotonic time between frames.
  virtual double frameTime() = 0;
  // setNativeProps for a view by React tag.
  virtual void setNativePropsForTag(int tag, folly::dynamic props) = 0;
};

// What a library adds: TurboModules and native components, and code that
// runs once the host is up (with it, until the app quits).
struct Package {
  std::string name;
  facebook::react::TurboModuleProviders turboModules;
  std::vector<NativeComponent> components;
  std::function<void(Host &host)> setUp;
};

}  // namespace rngtk
