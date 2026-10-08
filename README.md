# react-native-gtk4

React Native for Linux, rendered with GTK4. An out-of-tree platform package
in the spirit of [react-native-windows](https://github.com/microsoft/react-native-windows)
and [react-native-macos](https://github.com/microsoft/react-native-macos),
published as `@curiosity26/react-native-gtk4` on GitHub Packages.

**Status: Phase 0 (bootstrap).** Nothing here runs React Native yet.

## Targets

- GNOME on Ubuntu/Debian and Fedora, and Linux Mint Cinnamon.
- GTK 4.14 or newer (Ubuntu 24.04 / Mint 22.3). Newer GTK features are
  runtime-checked.
- X11 and Wayland are both first-class (Mint Cinnamon defaults to X11).
- KDE is out of scope for this package.

## Architecture (planned)

New architecture only: a C++20 host on React Native's `ReactCxxPlatform`
with Hermes, built with CMake, talking to the GTK C API directly. Fabric
mount instructions land on GTK widgets that place children at Yoga frames
and draw React Native styles with GSK. Text is measured with Pango, off the
main thread.

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
1. Core components, dev loop (Metro, fast refresh), CLI.
2. Platform APIs, desktop props, accessibility.
3. Desktop parity: modals, windows, menus, drag and drop, dialogs,
   notifications, tray; native module template and autolinking.
4. Packaging (Flatpak first) and community library ports.
