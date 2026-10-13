# Changelog

## 0.85.3 (2026-10-13)

- [0.85] Merge release/0.84: npm publishing (#69)
- [0.85] Merge release/0.84: prebuilt host id fix (#75)
- [0.85] Merge release/0.84: Hermes headers for prebuilt hosts (#80)

## 0.85.2 (2026-10-13)

- Prebuilt hosts without a token, and for Debian 13, Fedora 44/45 and EL10 (0.85, from 0.84) (#62)

## 0.85.1 (2026-10-11)

- Fix: `setSize` on a window that's already shown resizes it on Wayland
  too (GDK only computed the new size on the compositor's next configure,
  which might not come).

## 0.85.0 (2026-10-11)

The React Native 0.85 line: React Native 0.85.3, branched from 0.84.0.

- React Native 0.85.3, React 19.2.3, Hermes V1 (hermes-v250829098.0.10), Metro 0.84.
- No host changes: the 0.84 fixes (font-scale commit hook, Native Animated mount props) carry over.
- Reanimated and Worklets are still not on this line; they need React Native 0.86.

## 0.84.2 (2026-10-13)

- Prebuilt hosts without a token, and for Debian 13, Fedora 44/45 and EL10 (0.84, from 0.83) (#61)
- Hosts build on Fedora 44/45 and EL10 with React Native 0.84 (from 0.83) (#65)

## 0.84.1 (2026-10-11)

- Fix: `setSize` on a window that's already shown resizes it on Wayland
  too (GDK only computed the new size on the compositor's next configure,
  which might not come).

## 0.84.0 (2026-10-11)

The React Native 0.84 line: React Native 0.84.1, branched from 0.83.0.

- React Native 0.84.1, React 19.2.3, Hermes V1 (hermes-v250829098.0.9).
- The host builds against Hermes V1 (`HERMES_V1_ENABLED`).
- Native Animated: an animation's final value no longer reverts on the next
  mount (a held TouchableOpacity went back to full opacity).
- Builds with libstdc++ 14 (React Native's TimerManager.cpp lacks `<algorithm>`).
- Reanimated and Worklets are still not on this line; they need React Native 0.86.

## 0.83.3 (2026-10-13)

- [0.83] Prebuilt hosts: hash only the linux/ files the package ships (#72)

## 0.83.2 (2026-10-13)

- Prebuilt hosts without a token, and for Debian 13, Fedora 44/45 and EL10 (0.83) (#60)
- Hosts build on Fedora 44/45 and EL10 with React Native 0.83 and 0.84 (#64)

## 0.83.1 (2026-10-11)

- Fix: `setSize` on a window that's already shown resizes it on Wayland
  too (GDK only computed the new size on the compositor's next configure,
  which might not come).

## 0.83.0 (2026-10-11)

The first release: React Native 0.83.10 for Linux on GTK4, as an
out-of-tree platform like react-native-windows and react-native-macos.

### Core (Phase 1)

- A GTK4 host for React Native 0.83.10: Hermes, Fabric and TurboModules,
  React Native's C++ core built from source (ReactCxxPlatform, as React
  Native's Fantom tester).
- `<View>` and `<Text>` drawn with GSK and Pango: borders, radii, shadows,
  transforms, filters, gradients, nested text.
- Pressable, the Touchables, Button and hover from mouse and touch input;
  ScrollView with GTK's kinetic scrolling; FlatList and SectionList;
  Image (assets, http, files, data URIs); TextInput, Switch and
  ActivityIndicator as GTK's own controls.
- The `linux` platform for Metro (`Platform.OS === 'linux'`, `.linux.js`
  files) and Linux `Platform.constants`.
- The dev loop: Metro, reload, fast refresh, LogBox, React Native
  DevTools; `fetch`, `XMLHttpRequest` and `WebSocket` on libsoup.
- The CLI: `init-linux` and `run-linux`, with a shared build cache.

### Desktop (Phase 2)

- Dark mode (`Appearance`, `useColorScheme`) and libadwaita
  `PlatformColor`s.
- Text selection; keyboard focus, Tab order, focus rings, key events;
  `onMouseEnter`/`Leave`, `onAuxClick`, `tooltip`.
- Accessibility for Orca through AT-SPI, and `AccessibilityInfo`.
- Linking, AppState, Clipboard, font scaling, I18nManager RTL.

### Desktop parity (Phase 3)

- `<Modal>` in a window of its own; `Alert.alert` and `Alert.prompt`; file
  dialogs (`Dialogs`).
- Context menus and the app's menu bar; more windows (`Windows.open`);
  drag and drop with react-native-macos' props; desktop notifications.
- Window styles: windows without the desktop's title bar (`titleBar:
  'hidden'` or `'none'`, `AppOptions::titleBar`), `<TitleBar>` and
  `<WindowControls>` for one of the app's own, `windowDragRegion` on any
  View, and transparent windows.
- Native modules and components in C++: `init-linux-library`, and
  autolinking in `run-linux`.

### Packaging (Phase 4)

- `react-native package-linux`: the app's identity from app.json's `linux`
  block (id, name, summary, icon, categories, license, ...), a `.desktop`
  file, AppStream MetaInfo and hicolor icons. Formats: an installable tree
  (`dir`, with `install.sh`), Flatpak (a Flathub-style manifest that
  builds from source offline, and a bundle), `.deb` and `.rpm`.
- Packages ship a Release build of the host.
- Prebuilt hosts: the release attaches Debug and Release hosts for x86_64
  and aarch64 (Ubuntu 24.04 and derivatives), and `run-linux` and
  `package-linux` use them instead of building React Native and Hermes
  (`RNGTK_NO_PREBUILT=1` builds from source).
- Community library ports in `packages/`, autolinked: async-storage (JSON
  files under `$XDG_DATA_HOME/<app id>`), netinfo (GNetworkMonitor and
  NetworkManager), safe-area-context (`initialWindowMetrics`) and
  vector-icons (fonts through fontconfig).
- react-native-svg (librsvg) and react-native-webview (WebKitGTK 6.0)
  ports, with container components in the library API
  (`NativeComponent::insertChild`/`removeChild`).
- The app's command line takes `--initial-props JSON`.

### Gestures, animations and navigation (Phase 6)

- react-native-screens port (`@curiosity26/react-native-gtk4-screens`,
  libadwaita): native-stack on `AdwNavigationView` with `AdwHeaderBar`
  headers (header options, React views in the header, a search bar),
  slide transitions and back gestures, modals and form sheets as layers,
  `usePreventRemove`. Without the port, React Navigation's native-stack
  runs on react-native-screens' web components (the Metro config picks
  them on Linux).
- react-native-gesture-handler port (`@curiosity26/react-native-gtk4-gesture-handler`):
  `RNGestureHandlerModule` and `RNGestureHandlerDetector`, recognizers and
  orchestrator ported from the library's web implementation, on the host's
  pointer input (Tap, LongPress, Pan, Fling, Hover, Manual, Pinch and
  Rotation with touchpad pinches, Native), `RNGestureHandlerButton`
  (RectButton, Touchable and its feedback) and the library's ScrollView.
- react-native-worklets port (`@curiosity26/react-native-gtk4-worklets`):
  the library's shared C++ with its UI runtime on GTK's main thread;
  gesture-handler's bindings on it.
- react-native-reanimated 4 port (`@curiosity26/react-native-gtk4-reanimated`):
  the library's C++ engine and `ReanimatedModule` on worklets' UI runtime:
  shared values, animated styles and animations on the frame clock,
  animated scroll handlers and events, `measure`/`scrollTo`, frame
  callbacks, gesture-handler gestures driving animations with worklets,
  layout animations (entering, exiting, layout transitions), CSS
  animations and transitions; gesture-handler's `ReanimatedSwipeable` and
  `ReanimatedDrawerLayout`.
- Library API: `Package::setUp` gets an `rngtk::Host`: pointer observers
  (with touchpad pinches and scrolling), cancelling the JS responder's
  touches, views, components and event emitters by tag, `setNativeProps`
  (native Animated's path), scroll observers, device events,
  `runAfterMounts`; and for libraries that drive Fabric (Reanimated): the
  main and JS threads, the RuntimeScheduler, Fabric's Scheduler (commit
  hooks, event listeners), GdkFrameClock frames, props by tag, Hermes;
  services packages share (worklets' UI runtime).
- `BackHandler`: Alt+Left, the Back key and the mouse's back button send
  `hardwareBackPress`, as on react-native-windows; `exitApp()` quits.

### Releases (Phase 7)

- Versions follow React Native's major.minor: this package and every
  port are 0.87.x on React Native 0.87, each with its own patch, and the
  ports' peer range on this package is `0.87.x`. Older React Native
  lines are release branches (`release/0.83`) published under
  `v0.83-stable`.
- The release workflow, run by hand on `main` or a release line, gives
  each package that changed since its last release the next patch,
  commits the versions and changelogs, and publishes them. Pull requests
  don't change versions.
