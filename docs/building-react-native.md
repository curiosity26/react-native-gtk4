# Building React Native for Linux (GTK4)

This is for working on this repository. Apps don't need any of it:
`react-native run-linux` does the same build into a shared cache (see
[getting-started.md](getting-started.md)).

Run these on the Ubuntu 24.04 machine you develop on.

```sh
sudo apt install -y clang cmake ninja-build libssl-dev libicu-dev libreadline-dev \
  libgtk-4-dev libsoup-3.0-dev
# Node 24 LTS (Metro 0.87 needs >= 22.13; Ubuntu 24.04's apt nodejs is 18)
python3 scripts/fetch-rn-deps.py   # RN 0.88.0-rc.4 source, third-party C++ deps, codegen, Hermes source
scripts/build-hermes.sh            # libhermesvm + hermesc + headers
(cd examples/hello-world && npm install && npm run bundle)   # react-native bundle --platform linux --assets-dest build

# The GTK host (clang is picked by default)
cmake -S linux -B build/linux -G Ninja
cmake --build build/linux
build/linux/rn-gtk-host --bundle examples/hello-world/build/index.bundle.js
build/linux/rn-gtk-host --bundle examples/hello-world/build/index.bundle.js --self-test
GDK_BACKEND=x11 build/linux/rn-gtk-host --bundle examples/hello-world/build/index.bundle.js --self-test

npm test                           # unit tests for the Metro config and the CLI
```

`cmake --build build/linux` builds two things from the same sources:
`rn-gtk-host`, the test harness (self-tests, galleries, dev-loop checks),
and `librngtk_host.so`, the library apps link (`ReactNativeGtk::host`:
`rngtk::runApp`, plus React Native's C++ for native libraries).
`cmake --install build/linux --prefix DIR` installs the library with
`libhermesvm.so`, `libjsi.so`, its headers,
`lib/cmake/ReactNativeGtk/ReactNativeGtkConfig.cmake` and the native
library SDK (`ReactNativeGtk::sdk`, [native-modules.md](native-modules.md));
run-linux does that into its cache with `-DRNGTK_BUILD_HARNESS=OFF`.
`rn-gtk-host` also builds the native library template (`template-library/`)
in, as a package, for `GalleryNativeModule`.

`libsoup-3.0-dev` is for networking (`fetch`, `XMLHttpRequest`, `WebSocket`)
and the Metro dev loop: see [dev-loop.md](dev-loop.md) for running from
Metro with reload, fast refresh and LogBox.

Everything lands in `third-party/deps/` (git-ignored). Versions are pinned in
`rn-version.properties`.

## The `linux` platform

Bundles target their own Metro platform, `linux`, the out-of-tree way that
react-native-windows and react-native-macos use: React Native's package is
not patched or forked.

```js
// metro.config.js
const {getDefaultConfig} = require('@curiosity26/react-native-gtk4/metro-config');
module.exports = getDefaultConfig(__dirname);

// or, to add Linux to a config you already build:
const {withLinux} = require('@curiosity26/react-native-gtk4/metro-config');
module.exports = withLinux(config);
```

```sh
metro build index.js --platform linux --out build/index.bundle.js
```

(`examples/hello-world` requires `../../metro-config` directly, since it
lives in this repo.)

`withLinux` adds `linux` to `resolver.platforms`, so your own `Foo.linux.js`
files win over `Foo.js`, and wraps `resolver.resolveRequest` (an existing one
still runs first). For modules inside the `react-native` package only:

- If `overrides/<path in react-native>.linux.js` exists in this package, it
  is used. There is one for each platform-split file in React Native 0.88.0-rc.4.
- Otherwise, a module React Native splits into `.ios.js` and `.android.js`
  resolves to the Android variant (the C++ core is shared with Android).

Other packages resolve normally: a library that only ships `.ios.js` and
`.android.js` files fails to resolve on Linux instead of silently getting
Android code.

Overrides import React Native's own files as
`react-native-upstream/<path>`, which the resolver maps into the app's
`react-native`. (A `react-native/<path>` deep import would work too, but
React Native's dev Babel preset warns about each one at runtime.)

| Override | Behaves like | Why |
| --- | --- | --- |
| `Utilities/Platform` | own | `OS: 'linux'`; `select()` checks `linux`, `native`, `default`; constants from the host |
| `Utilities/BackHandler` | iOS | desktops have no hardware back button |
| `Components/DrawerAndroid/DrawerLayoutAndroid`, `Components/ToastAndroid/ToastAndroid` | iOS | Android-only APIs: the "unsupported" fallbacks |
| `Components/AccessibilityInfo/legacySendAccessibilityEvent` | iOS | pre-Fabric only; the iOS one tolerates a missing module |
| `Image/Image`, `Network/RCTNetworking`, `StyleSheet/PlatformColorValueTypes` | Android | what React Native's shared C++ core speaks |
| `NativeComponent/BaseViewConfig` | Android, plus | Android's view config, plus the host's keyboard, mouse, context menu, drag and drop and iOS accessibility props ([components.md](components.md#keyboard)) |
| `Alert/Alert` (not platform-split), `Alert/RCTAlertManager` | own | the host's AlertManager (a GTK message dialog) for both alert and prompt, with iOS's and Android's options together ([apis.md](apis.md#alert)); `alertWithArgs` keeps iOS's shape |
| `devsupport/rndevtools/ReactDevToolsSettingsManager` | Android | optional native module; iOS needs its Settings module |
| `Share/Share` (not platform-split) | own | rejects every platform but iOS and Android; a stub that resolves dismissed ([apis.md](apis.md#share-and-vibration)) |
| `Image/ImageViewNativeComponent` (not platform-split) | Android's view config | Image.android.js sends `defaultSource` as a string and `shouldNotifyLoadEvents`, which iOS's config drops |
| `Components/View/View` (not platform-split) | own | adds `contextMenu` (items with `onSelect`, serialized for the host by `js/menuItems.js`); a plain function instead of Flow's component syntax |
| `Utilities/useWindowDimensions` (not platform-split) | own | the size of the window the caller is in (its root tag), for apps with several windows; React Native's in the main window |
| `Modal/Modal` (not platform-split) | iOS | `visible={false}` reaches the native view, which closes its window and sends `onDismiss` before Modal unmounts (iOS's order; Android unmounts at once). Also passes `accessibilityLabel` on, as the dialog's name |
| `Components/TextInput/TextInput`, `Components/TextInput/TextInputState` (not platform-split) | iOS (and `contextMenu`) | they only render, focus and blur for 'ios' and 'android'; Linux uses the iOS native components (the host builds React Native's iOS C++ TextInput) |

An override can also replace a file React Native doesn't split by
platform; the resolver checks `overrides/` for every react-native module.
`ImageViewNativeComponent.linux.js`, `Modal.linux.js`, `TextInput.linux.js`,
`TextInputState.linux.js` and `View.linux.js` are such copies, each with a
few lines changed; `Alert.linux.js` is a rewrite.
`ProgressBarAndroid`, `Settings` and `PlatformColorValueTypesIOS` need no
override: their platform-less files are already the non-Android/non-iOS
versions.

### Platform.constants

The host registers its own `PlatformConstants` TurboModule
(`linux/src/PlatformConstantsModule.cc`) ahead of ReactCxxPlatform's, which
returns placeholder Android values.

| Key | Value |
| --- | --- |
| `Version` | `VERSION_ID` from `/etc/os-release` (`"24.04"`, `"41"`, `"22.3"`); the kernel release on distros without one. A string, as on iOS. |
| `Release` | `PRETTY_NAME` from `/etc/os-release` |
| `osId`, `osVersion` | `ID` and `VERSION_ID` from `/etc/os-release` |
| `kernelRelease` | `uname -r` |
| `gtkVersion` | the running GTK, e.g. `"4.14.5"` |
| `windowSystem` | `'wayland'`, `'x11'` or `'unknown'`, from the `GdkDisplay` |
| `desktop` | `XDG_CURRENT_DESKTOP` (`"ubuntu:GNOME"`, `"X-Cinnamon"`), or `''` |
| `reactNativeVersion` | from `rn-version.properties`, compiled in |
| `isTesting` | true under `rn-gtk-host --self-test` |
| `isDisableAnimations` | true when GTK's `gtk-enable-animations` setting is off |

`isTV` and `isVision` are always false.

### Dimensions

ReactCxxPlatform's `DeviceInfo` module reports a fixed 1280x720. The host
registers its own (in `linux/src/RNGtkHost.cc`): `window` is the app's
surface, `screen` the geometry of the monitor the window is on, both in
GTK's logical pixels (React Native's points), with the monitor's scale as
`scale` and GNOME's text scaling as `fontScale` (see
[apis.md](apis.md#pixelratio-and-font-scale)). The surface follows the window
(`RNGtkHostOptions::followsWindowSize`): after each GTK layout the host
resizes the surface, re-lays it out with `setSurfaceConstraints`, and emits
`didUpdateDimensions`, so `Dimensions.addEventListener('change')` and
`useWindowDimensions()` update as the window is resized. `rn-gtk-host
--self-test` keeps the size it was given; the GalleryControls self-test
turns resizing on and checks that a resize reaches JS.

Windows opened with `Windows` (see [apis.md](apis.md#windows)) are surfaces
of their own; `useWindowDimensions()` (overridden in
`Utilities/useWindowDimensions.linux.js`) reports the caller's own window
from its root tag, and `Dimensions` stays the main window's.

### The package's own APIs

What React Native has no API for (`Dialogs`, ...) is in this package's
`js/` folder, which `import {...} from '@curiosity26/react-native-gtk4'`
gets (`exports` in package.json). `withLinux` treats it like `overrides/`:
Metro watches it, and its imports (`react-native`) resolve from the app.
The example app maps the package name to `js/` in its own
`metro.config.js`, since the package isn't in its `node_modules`.

### TypeScript

React Native declares `PlatformOSType` and its Platform types as type
aliases, which cannot be extended from outside, so TypeScript does not know
`'linux'` yet. `@curiosity26/react-native-gtk4` exports `LinuxPlatform` and
`PlatformConstantsLinux` for narrowing by hand (see `types/index.d.ts`).

## Findings so far

These came from compiling React Native's Fantom tester (the C++ host we are
basing ours on) on Ubuntu 24.04:

- Use **clang**. React Native's headers use `#pragma mark`, which GCC
  rejects under `-Werror`.
- GCC 13's libstdc++ (Ubuntu 24.04's default) no longer pulls in
  `<cstdint>` transitively, and a few RN files (`HttpUtils.h`,
  `AnsiParser.h`) rely on that. Compile with `-include cstdint` until
  upstream adds the include. Folly also needs
  `-Wno-deprecated-declarations` with this libstdc++.
- Hermes and React Native must use the same C++ standard library, since
  JSI passes `std::string` across the boundary.
- Codegen must run on React Native's git source, not the npm package's
  `src/`, because the npm package leaves out the specs for some internal
  native modules.
- GitHub source tarballs (`/archive/…`) can be blocked by proxies, so
  the fetch script uses shallow git clones instead.
