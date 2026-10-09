# react-native-gtk4

React Native for Linux, rendered with GTK4. An out-of-tree platform package
in the spirit of [react-native-windows](https://github.com/microsoft/react-native-windows)
and [react-native-macos](https://github.com/microsoft/react-native-macos),
published as `@curiosity26/react-native-gtk4` on GitHub Packages.

**Status: Phase 1 (core) complete.** The GTK host runs React Native
0.87.1 (Hermes, Fabric). `<View>` and `<Text>` render RN's styling with GSK
and Pango: borders, radii, shadows, transforms, filters, gradients, nested
text. Mouse and touch input drive Pressable, the Touchables, Button and
hover. ScrollView scrolls with GTK's kinetic scrolling, FlatList and
SectionList virtualize 10k rows, and Image loads assets, http, files and
data URIs. TextInput, Switch and ActivityIndicator are GTK's own
controls (input methods, clipboard and undo included). See
[docs/components.md](docs/components.md). Apps bundle for their own `linux`
platform: `Platform.OS === 'linux'`, `.linux.js` files, and Linux
`Platform.constants` (distro, GTK version, Wayland or X11). In development
apps load from Metro, with reload, fast refresh, LogBox and React Native
DevTools; `fetch`, `XMLHttpRequest` and `WebSocket` run on libsoup
([docs/dev-loop.md](docs/dev-loop.md)). The CLI adds Linux to any React
Native app: `init-linux` and `run-linux`.

**Phase 2 (desktop) in progress:** dark mode (`Appearance`,
`useColorScheme`, libadwaita `PlatformColor`s that follow the desktop's
style, [docs/apis.md](docs/apis.md)); text selection by mouse; keyboard
focus, Tab order, focus rings and key events, `onMouseEnter`/`Leave`,
`onAuxClick` and `tooltip`, with react-native-windows / react-native-macos
props ([docs/components.md](docs/components.md#keyboard)).

## Quick start

```sh
npx @react-native-community/cli@20.2.0 init MyApp --version 0.87.1
cd MyApp
npm install @curiosity26/react-native-gtk4    # from GitHub Packages (see below)
npx react-native init-linux                   # adds linux/, Metro's linux platform, "npm run linux"
npx react-native run-linux                    # builds, starts Metro, opens the window
npx react-native run-linux --release          # a self-contained build in linux/build/Release
```

The first `run-linux` builds React Native's C++ core and Hermes into
`~/.cache/react-native-gtk4`, shared by all your apps; after that an app
builds in seconds. Prerequisites per distribution, what each command does,
and troubleshooting: [docs/getting-started.md](docs/getting-started.md).

The package is on GitHub Packages: add
`@curiosity26:registry=https://npm.pkg.github.com` to `~/.npmrc`.

![A new React Native app on GTK4 (Release build, Wayland)](docs/images/new-app-release-wayland.png)

## Working on this repository

The example app in `examples/hello-world` runs on the repository's own
host, `rn-gtk-host`, which also drives the self-tests and galleries. See
[docs/building-react-native.md](docs/building-react-native.md) and
[docs/architecture.md](docs/architecture.md).

```sh
npm run start:hello-world   # terminal 1: Metro
npm run dev:hello-world     # terminal 2: the app; Ctrl+R reloads, Ctrl+D dev menu
npm test                    # Metro config and CLI unit tests
scripts/test-new-app.sh     # end to end: new apps from the npm tarball
```

![Hello World on GTK4 under Wayland](docs/images/hello-world-linux-wayland.png)

## Targets

- GNOME on Ubuntu/Debian and Fedora, and Linux Mint Cinnamon.
- GTK 4.14 or newer (Ubuntu 24.04 / Mint 22.3). Newer GTK features are
  runtime-checked.
- X11 and Wayland are both first-class (Mint Cinnamon defaults to X11).
- KDE is out of scope for this package.

## Architecture

New architecture only: a C++20 host on React Native's `ReactCxxPlatform`
with Hermes, built with CMake, talking to the GTK C API directly. Fabric
mount instructions land on GTK widgets that place children at Yoga frames
and draw React Native styles with GSK. Text is measured with Pango, off the
main thread.

`linux/` builds the host as `librngtk_host.so` (CMake target
`ReactNativeGtk::host`), which holds React Native's core and exports one
call, `rngtk::runApp` ([linux/include/rngtk/App.h](linux/include/rngtk/App.h)).
An app's `linux/main.cc` fills in its id, title and component and calls
it. The repository's test harness, `rn-gtk-host`, links the same sources
directly. Threads: [docs/architecture.md](docs/architecture.md).

## Phase 0 spike

`spike/` is the first step: the host widgets a React Native `<View>` and
`<Text>` will mount into, a Hello World, and a 1k/10k widget-count benchmark.
Results and findings: [docs/phase0-results.md](docs/phase0-results.md).

```sh
sudo apt install libgtk-4-dev cmake ninja-build g++   # Ubuntu 24.04
npm run spike:build
spike/build/hello-world                  # opens the window
spike/build/widget-benchmark --count 10000
```

Headless checks (needs `xvfb`, `mutter`, `weston`):

```sh
npm run spike:test:x11
npm run spike:test:wayland
scripts/bench-matrix.sh                  # writes spike/out/bench/results.md
BACKENDS=native scripts/bench-matrix.sh  # on a real desktop session
```

## Roadmap

0. Bootstrap: GTK host widgets, Hello World, widget benchmark, then the
   React Native runtime build (Fantom / ReactCxxPlatform) mounting into them.
   **Done.**
1. Core components, dev loop (Metro, fast refresh), CLI (`init-linux`,
   `run-linux`, app template, shared build cache). **Done.**
2. Platform APIs, desktop props, accessibility.
3. Desktop parity: modals, windows, menus, drag and drop, dialogs,
   notifications, tray; native module template and autolinking.
4. Packaging (Flatpak first) and community library ports.
