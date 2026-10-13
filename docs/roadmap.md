# Roadmap

How react-native-gtk4 was planned and built, phase by phase. Every phase
below is done; what's next is tracked in GitHub issues and pull requests.
For using the package, start with the [README](../README.md) and
[getting-started.md](getting-started.md).

## Phases

0. Bootstrap: GTK host widgets, Hello World, widget benchmark, then the
   React Native runtime build (Fantom / ReactCxxPlatform) mounting into them.
   **Done.**
1. Core components, dev loop (Metro, fast refresh), CLI (`init-linux`,
   `run-linux`, app template, shared build cache). **Done.**
2. Platform APIs, desktop props, accessibility. **Done.**
3. Desktop parity: modals, windows, menus, drag and drop, dialogs,
   notifications; native module template and autolinking. (No system
   tray: stock GNOME has none.) **Done.**
4. Packaging and community library ports: `package-linux` with the app's
   identity in app.json, an installable tree with a `.desktop` file,
   AppStream MetaInfo and icons, Flatpak, .deb and .rpm
   ([packaging.md](packaging.md)); prebuilt hosts and the npm
   release ([releasing.md](releasing.md)); ports of
   async-storage, netinfo, safe-area-context, vector-icons, svg and
   webview ([libraries.md](libraries.md)); upgrading React
   Native ([upgrading.md](upgrading.md)). **Done.**
5. Window customization: `transparent: true` for `Windows.open` and the
   main window, and `titleBar: 'default' | 'hidden' | 'custom'` with a
   `<TitleBar>` component that renders React content into the window's
   title bar (`gtk_window_set_titlebar`, dragging through
   GtkWindowHandle, window controls following `gtk-decoration-layout`).
   Documented differences: no blur or vibrancy on GNOME, and Wayland
   can't position windows or keep them on top. **Done.**
6. Gesture handler, Reanimated and Screens: react-native-screens' web
   variants (native-stack as on the web), then react-native-gesture-handler
   on GTK's gesture controllers, then react-native-reanimated (and
   react-native-worklets) on their shared C++, with the host API they need
   (the UIManager, the runtime executor, direct prop updates). See
   [library-assessments.md](library-assessments.md). **Done.**
7. Releases: the package and the ports published to GitHub Packages from
   `main` and each release line (`release/0.83`), versioned on React
   Native's major.minor with their own patches
   ([releasing.md](releasing.md)).
   Lines for React Native 0.83 to 0.86 (`release/0.83` to `release/0.86`)
   besides `main` (0.87), each on its `v<major>.<minor>-stable` dist-tag.
   **Done.**
8. Distribution: prebuilt hosts for Ubuntu 24.04 / Mint 22, Debian 13,
   Fedora 44 and 45 and Enterprise Linux 10 (x86_64 and aarch64) on each
   GitHub release, and every package copied to the public npm registry
   with trusted publishing, so installs need no token. **Done.**

## What each phase added

**Phase 1 (core):** The GTK host runs React Native
0.87.1 (Hermes, Fabric). `<View>` and `<Text>` render RN's styling with GSK
and Pango: borders, radii, shadows, transforms, filters, gradients, nested
text. Mouse and touch input drive Pressable, the Touchables, Button and
hover. ScrollView scrolls with GTK's kinetic scrolling, FlatList and
SectionList virtualize 10k rows, and Image loads assets, http, files and
data URIs. TextInput, Switch and ActivityIndicator are GTK's own
controls (input methods, clipboard and undo included). See
[components.md](components.md). Apps bundle for their own `linux`
platform: `Platform.OS === 'linux'`, `.linux.js` files, and Linux
`Platform.constants` (distro, GTK version, Wayland or X11). In development
apps load from Metro, with reload, fast refresh, LogBox and React Native
DevTools; `fetch`, `XMLHttpRequest` and `WebSocket` run on libsoup
([dev-loop.md](dev-loop.md)). The CLI adds Linux to any React
Native app: `init-linux` and `run-linux`.

**Phase 2 (desktop):** dark mode (`Appearance`,
`useColorScheme`, libadwaita `PlatformColor`s that follow the desktop's
style, [apis.md](apis.md)); text selection by mouse; keyboard
focus, Tab order, focus rings and key events, `onMouseEnter`/`Leave`,
`onAuxClick` and `tooltip`, with react-native-windows / react-native-macos
props ([components.md](components.md#keyboard)); accessibility
for Orca through AT-SPI, and `AccessibilityInfo`
([components.md](components.md#accessibility)); Linking, AppState,
Clipboard, font scaling, I18nManager RTL ([apis.md](apis.md)).

**Phase 3 (desktop parity):** `<Modal>` opens a window of its
own over the app: full screen or a dialog-sized sheet, fade and slide,
transparent, nested, a modal dialog to Orca
([components.md](components.md#modal)). `Alert.alert` and
`Alert.prompt` on a GTK alert dialog, and file dialogs (`Dialogs` from
`@curiosity26/react-native-gtk4`) on the desktop's file chooser
([apis.md](apis.md#alert)). Context menus on any view
(`contextMenu`, `ContextMenu`) and the app's menu bar (`MenuBar`), with
submenus, checkbox and radio items and shortcuts
([components.md](components.md#context-menus)). More windows
(`Windows.open`), each a registered component in the same JS runtime,
with their own size for `useWindowDimensions`
([apis.md](apis.md#windows)); title bars of the app's own
(`<TitleBar>`) and transparent windows
([apis.md](apis.md#window-styles)). Drag and drop with
react-native-macos' props (`draggedTypes`, `onDrop`): files, links, text
and images in, selected text and `draggable` images out
([components.md](components.md#drag-and-drop)). Desktop
notifications with buttons (`Notifications`,
[apis.md](apis.md#notifications)). Native modules and
components in C++ and GTK: `init-linux-library` makes a library's `linux/`
folder, and `run-linux` autolinks it
([native-modules.md](native-modules.md)).

## The Phase 0 spike

`spike/` is the first step: the host widgets a React Native `<View>` and
`<Text>` will mount into, a Hello World, and a 1k/10k widget-count benchmark.
Results and findings: [phase0-results.md](phase0-results.md).

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
