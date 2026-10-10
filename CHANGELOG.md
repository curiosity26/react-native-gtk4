# Changelog

## 0.1.0 (unreleased)

The first release: React Native 0.87.1 for Linux on GTK4, as an
out-of-tree platform like react-native-windows and react-native-macos.

### Core (Phase 1)

- A GTK4 host for React Native 0.87.1: Hermes, Fabric and TurboModules,
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
- `BackHandler`: Alt+Left, the Back key and the mouse's back button send
  `hardwareBackPress`, as on react-native-windows; `exitApp()` quits.
