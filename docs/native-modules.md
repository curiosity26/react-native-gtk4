# Native modules and components

A React Native library adds Linux to its native code with a `linux/`
folder: C++ against React Native's own C++ API (TurboModules, Fabric
components) and GTK. Apps that depend on the library get it through
autolinking: `npx react-native run-linux` builds every dependency that has
a `linux/` folder into the app and registers it with the host.

## Start one: `init-linux-library`

In the library's folder (with `react-native` and
`@curiosity26/react-native-gtk4` among its devDependencies):

```sh
npx react-native init-linux-library            # names from package.json
npx react-native init-linux-library --name Calendar
```

It writes this package's library template (`template-library/`), named
after the library (`react-native-gtk-calendar` gives `GtkCalendar`):

```
linux/CMakeLists.txt                  the `gtk_calendar` static library, on ReactNativeGtk::sdk
linux/src/GtkCalendarModule.{h,cc}    a TurboModule, "GtkCalendar"
linux/src/GtkCalendarView.{h,cc}      a native component, "GtkCalendarView" (a GtkCalendar)
linux/src/GtkCalendarPackage.cc       gtk_calendar_package(): what autolinking calls
src/NativeGtkCalendar.ts              the module's JS side (TurboModuleRegistry.get)
src/GtkCalendarViewNativeComponent.ts the component's (codegenNativeComponent)
src/index.ts                          (only if the library has no index)
react-native.config.js                the Linux entry (or the lines to add to yours)
```

The template works as it is: the module has a synchronous method, a
promise resolved on GTK's main thread, and one taking and returning any
JSON-like value; the component has props, an event (`onDateChange`) and a
command (`showToday`). Change them into yours.

## The package: `rngtk/Extensions.h`

A library gives the host an `rngtk::Package`:

```cpp
#include <rngtk/Extensions.h>

std::shared_ptr<const rngtk::Package> gtk_calendar_package() {
  auto package = std::make_shared<rngtk::Package>();
  package->name = "gtk-calendar";
  package->turboModules.push_back(
      [](const std::string &name, const std::shared_ptr<facebook::react::CallInvoker> &js)
          -> std::shared_ptr<facebook::react::TurboModule> {
        return name == "GtkCalendar" ? std::make_shared<GtkCalendarModule>(js) : nullptr;
      });
  package->components.push_back(gtkCalendarViewComponent());
  return package;
}
```

**TurboModules.** Any `facebook::react::TurboModule`: one from React
Native's codegen (a `Native...CxxSpec` subclass), or `rngtk::CxxModule`,
which bridges plain C++ methods the way codegen's Cxx specs do, without a
spec:

```cpp
class GtkCalendarModule : public rngtk::CxxModule<GtkCalendarModule> {
 public:
  explicit GtkCalendarModule(std::shared_ptr<facebook::react::CallInvoker> js)
      : CxxModule("GtkCalendar", std::move(js)) {
    method<&GtkCalendarModule::greet>("greet");
  }
  std::string greet(facebook::jsi::Runtime &, std::string name) { return "Hello, " + name; }
};
```

Arguments and results convert with React Native's bridging: `std::string`,
`double`, `bool`, `std::optional`, `folly::dynamic`, `jsi` types,
`AsyncPromise<T>` (a Promise) and `AsyncCallback<...>`. Methods run on the
JS thread; GTK is only for the main thread, so post GTK work there
(`g_main_context_invoke`) and resolve promises from there.

**Native components.** An `rngtk::NativeComponent` is a Fabric component
whose views are GTK widgets:

| | |
| --- | --- |
| `descriptor` | the component's descriptor provider: its props, event emitter and shadow node (`concreteComponentDescriptorProvider<...>()`). The template writes these by hand (what codegen writes for iOS and Android) |
| `create(view)` | a new widget. The host owns it, places it at its Yoga frame inside its parent, and gives it the view's opacity, transform, tooltip, pointer events, and accessibility label, hint and hidden state (it keeps GTK's own role and keyboard focus) |
| `update(widget, oldView, newView)` | props, state or the event emitter changed (`oldView` is empty the first time). Keep `newView.eventEmitter` to send events from GTK signals |
| `command(widget, name, args)` | a command from JS (`codegenNativeCommands`) |
| `insertChild(parent, child, index)`, `removeChild(parent, child)` | a container: React's children of the component are handed to it in order instead of the host placing them. The host still creates, updates and destroys each child. packages/svg uses them for an `<Svg>`'s elements |

They run on the main thread. A native component has no measure function:
give it a size in JS (`style={{width: 320, height: 300}}`). Without
`create`, the host makes the component's view itself (an RNView, which
lays out React children) and still calls `update`: packages/screens does
that for `RNSScreen`.

**The host.** A package's `setUp(rngtk::Host &host)` runs once the host is
up, on the main thread, and may keep the `Host` (it lives until the app
quits):

| `rngtk::Host` | |
| --- | --- |
| `addPointerObserver(fn)` | every pointer event on the app's surfaces (`rngtk::PointerInput`: press, move, release, cancel, leave, scroll; mouse or touch, the pointer id, root coordinates, the React view under it), before the host turns it into touches; `fn` returning true keeps the host from handling it. packages/gesture-handler's recognizers run on it |
| `cancelTouches(root)` | the JS responder (Pressable, ScrollView) loses the touches in progress on that surface (touchCancel), as when a native gesture takes over |
| `viewForTag(tag)`, `tagForView(widget)`, `eventEmitterForView(widget)` | mounted views by React tag, and a view's event emitter (`emitter->dispatchEvent("onMyEvent", payload)` for a prop `onMyEvent`) |
| `emitDeviceEvent(name, payload)` | `RCTDeviceEventEmitter` events |
| `runAfterMounts(fn)` | `fn` on the main thread after the mount transactions committed so far (a TurboModule naming a view finds it mounted); any thread |

## Autolinking

`run-linux` asks the CLI for the app's dependencies; each one whose
`linux/CMakeLists.txt` exists is linked. It writes `autolinking.cmake` (adds
each `linux/` folder to the app's build) and `autolinking.cc`
(`rngtk::autolinkedPackages()`) into `linux/build/<Debug|Release>/autolinking/`,
and passes the folder to the app's CMake. The app template's `rngtk_app()`
builds and links them, and its `main.cc` hands the packages to the host:

```cpp
options.packages = rngtk::autolinkedPackages();
```

(Apps made by an earlier `init-linux` need that line in `linux/main.cc`,
and `#include <rngtk/Autolinking.h>`.)

The CMake target and the package function default from the package name
(`@acme/react-native-foo`: `acme_react_native_foo` and
`acme_react_native_foo_package`); the library's `react-native.config.js`
can name them:

```js
module.exports = {
  dependency: {
    platforms: {
      linux: {cmakeTarget: 'gtk_calendar', packageFunction: 'gtk_calendar_package'},
    },
  },
};
```

## The SDK: `ReactNativeGtk::sdk`

A library's CMake links `ReactNativeGtk::sdk` PRIVATE (from the
`find_package(ReactNativeGtk)` the app's build has done): React Native's
headers (in the dependency cache, `~/.cache/react-native-gtk4/<version>/deps`),
the host's own headers (`rngtk/Extensions.h`, `rngtk/CxxModule.h`, the
props it builds React Native with), the same definitions and flags
(folly's configuration, C++20, `NDEBUG` as the host was built), and the
host library and GTK. PRIVATE, because its flags are for the library's
code: `NDEBUG` as the host's would otherwise reach the app's own `main.cc`
(whose Release build loads its bundle instead of Metro by `NDEBUG`).
`librngtk_host.so` exports React Native's C++
(`facebook::`), and the folly, glog and fmt it is built with, so library
code links against the copy the app runs; a library must not bring its own
React Native or folly.

## Tests

- `rn-gtk-host --module GalleryNativeModule --self-test` builds
  `template-library/linux` into the harness as a package and checks the
  module (sync, promise, dynamic values) and the component (its widget at
  its frame, props, the event from a picked day, the command), on Wayland
  and X11.
- `scripts/test-new-app.sh` makes a library of its own with
  `init-linux-library`, installs it in a new app, and checks that
  `run-linux --release` autolinks it and that the module answers and the
  component mounts, against the installed host and SDK.
- `npm test` covers the CLI side (names, generated files, the template's
  copy).
