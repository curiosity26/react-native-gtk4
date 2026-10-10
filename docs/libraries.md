# Community libraries

Libraries with JavaScript only work on Linux as they are. Libraries with
native code need a Linux side. For the most used ones, this repository has
ports in [packages/](../packages): a package of their native code for
GTK4, next to the library's own JavaScript. Install both; `run-linux` and
`package-linux` autolink the port (see [native-modules.md](native-modules.md)).

| Library | Linux package | What the port does |
| --- | --- | --- |
| [@react-native-async-storage/async-storage](https://github.com/react-native-async-storage/async-storage) 3.x | `@curiosity26/react-native-gtk4-async-storage` | The `RNAsyncStorage` TurboModule: each database is a JSON file under `$XDG_DATA_HOME/<app id>/async-storage/` (`~/.local/share/...`; in a Flatpak, `~/.var/app/<app id>/data/...`), written atomically. The default `AsyncStorage` is the `legacy` database |
| [@react-native-community/netinfo](https://github.com/react-native-netinfo/react-native-netinfo) 11+ | `@curiosity26/react-native-gtk4-netinfo` | The `RNCNetInfo` TurboModule on GIO's GNetworkMonitor: `isConnected`, `isInternetReachable` (connectivity FULL), `isConnectionExpensive` (metered), and change events. The type (`wifi`, `ethernet`, `cellular`, `vpn`, `bluetooth`), Wi-Fi SSID and IPv4 address come from NetworkManager over the system bus |
| [react-native-safe-area-context](https://github.com/AppAndFlow/react-native-safe-area-context) 5.x | `@curiosity26/react-native-gtk4-safe-area-context` | The `RNCSafeAreaContext` TurboModule's `initialWindowMetrics`: zero insets (nothing covers a desktop window's content) and the window's content size. The components are the library's own pure-JS ones (its Windows variants), which the Metro config picks on Linux |
| [react-native-gesture-handler](https://github.com/software-mansion/react-native-gesture-handler) 3 | `@curiosity26/react-native-gtk4-gesture-handler` | `RNGestureHandlerModule` and `RNGestureHandlerDetector`: the hook API, the builder API and the handler components, with recognizers ported from the library's web implementation running on the main thread on the host's pointer input (Tap, LongPress, Pan with touchpad scrolling, Fling, Hover, Manual, Pinch and Rotation with touchpad pinches, Native), its orchestrator (simultaneous, waitFor, blocks), gestures that activate cancelling React Native's responder, `RNGestureHandlerButton` (RectButton, Touchable with press feedback) and the library's ScrollView/FlatList |
| [react-native-screens](https://github.com/software-mansion/react-native-screens) 4 | `@curiosity26/react-native-gtk4-screens` | React Navigation's native-stack on libadwaita: `RNSScreenStack` is an `AdwNavigationView` (slide transitions; back with the header's button, a swipe, Escape, Alt+Left, the mouse's back button), each screen a page with an `AdwHeaderBar` from its header options (title, colors, `headerLeft`/`headerTitle`/`headerRight`, hidden or transparent headers, a search bar); modals and form sheets as layers over the stack. Needs `libadwaita-1-dev` to build. Without it, Linux gets the libraries' web components ([below](#navigation-react-native-screens-and-react-navigation)) |
| [react-native-worklets](https://github.com/software-mansion/react-native-reanimated/tree/main/packages/react-native-worklets) 0.13 | `@curiosity26/react-native-gtk4-worklets` | The library's shared C++ (built from the app's copy) with its UI runtime, a Hermes runtime of its own, on GTK's main thread: worklets, `runOnUI`/`runOnJS`, synchronizables, `requestAnimationFrame` on the window's frame clock. Add `react-native-worklets/plugin` to `babel.config.js` |
| [react-native-svg](https://github.com/software-mansion/react-native-svg) 15 | `@curiosity26/react-native-gtk4-svg` | The `RNSVG*` components: an `<Svg>` turns its elements back into an SVG document, which librsvg draws (shapes, paths, gradients, clip paths, masks, patterns, markers, text, filters, `<Use>`), and `RNSVGSvgViewModule` (`toDataURL`). Needs `librsvg2-dev` to build |
| [react-native-webview](https://github.com/react-native-webview/react-native-webview) 13+ | `@curiosity26/react-native-gtk4-webview` | `RNCWebView` on WebKitGTK 6.0, driven by the library's iOS JS (the Metro config picks it when the port is installed): sources, injected JS, two-way messages, the loading and navigation events, `onShouldStartLoadWithRequest`, the commands. Needs `libwebkitgtk-6.0-dev` to build |
| [@react-native-vector-icons/*](https://github.com/oblador/react-native-vector-icons) 11+, and `react-native-vector-icons` 10 | `@curiosity26/react-native-gtk4-vector-icons` | The icon fonts: copied from the app's `node_modules` next to the executable (`share/<app>/fonts` when installed) and registered with fontconfig at startup, with each font's PostScript name (what the library sets as `fontFamily`) as an alias |

```sh
npm install @react-native-async-storage/async-storage \
  @curiosity26/react-native-gtk4-async-storage
npx react-native run-linux
```

The ports' versions follow this package's. The Showcase has a page for each
(Storage, NetInfo, Safe Area, Icons): `cd examples/hello-world && npx
react-native run-linux`. The repository's `rn-gtk-host` harness doesn't
autolink, and its pages say so.

## Navigation: react-native-screens and React Navigation

[React Navigation](https://reactnavigation.org) 7 works on Linux: its JS
stack, tabs and drawer as they are, and `@react-navigation/native-stack`
in one of two ways:

- **Native, with `@curiosity26/react-native-gtk4-screens`** (libadwaita
  1.4+): GNOME's navigation view, header bars, slide transitions and back
  gestures, modals and form sheets as layers. See the package's
  [README](../packages/screens/README.md) for what's mapped.
- **Without it**, the Metro config gives Linux the web build of
  `react-native-screens` and `@react-navigation/native-stack` (a
  `Foo.web.tsx` where there is one, and the platform-less `Foo.tsx` over
  `Foo.native.tsx`), so native-stack draws its stack and header
  (`@react-navigation/elements`) as it does in a browser: header options
  (title, colors, `headerLeft`/`headerRight`, `headerShown`), `push`,
  `pop`, `popToTop`, modals and nested tab navigators, without screen
  transitions; `presentation: 'modal'` is a pushed screen, as on the web.

Either way, back works with Alt+Left and the mouse's back button
([BackHandler](apis.md#backhandler)), and the header's back button.

```sh
npm install @react-navigation/native @react-navigation/native-stack \
  react-native-screens react-native-safe-area-context \
  @curiosity26/react-native-gtk4-screens   # the native one
```

The Showcase's Navigation page has a stack with header options, a search
bar, a modal with header buttons, a form sheet, a guarded screen
(`usePreventRemove`) and tabs. `rn-gtk-host --module GalleryNavigation
--self-test` pushes and pops screens (buttons, the header's back button,
Alt+Left, the mouse's back button), types in the search bar, opens and
dismisses the modal (and Escape) and the sheet, tries to leave the guarded
screen and switches tabs, on the native components;
`gallery-navigation-web-self-test` does the same on the web ones (a
bundle made with `RNGTK_IGNORE_PORTS=@curiosity26/react-native-gtk4-screens`,
which has Metro act as if the port weren't installed).

## In a Flatpak

- **async-storage**: nothing to do. The data lives in the app's own data
  directory.
- **netinfo**: GNetworkMonitor goes through the network portal, so
  connected, reachable and metered all work. Without access to
  NetworkManager the type is `other`. Add
  `"flatpak": {"finishArgs": ["--system-talk-name=org.freedesktop.NetworkManager"]}`
  to app.json's `linux` block for the type, SSID and address (Flathub
  reviews that permission).
- **vector-icons**: the fonts are in the app (`/app/share/<app>/fonts`).
- **svg**, **webview** and **screens**: the GNOME runtime has librsvg,
  WebKitGTK 6.0 and libadwaita. WebKit's web processes run in a sandbox of their own inside the
  Flatpak's.

## Not ported yet

[library-assessments.md](library-assessments.md) covers gesture-handler,
reanimated and a native screens port: where they stand on Linux and what a
port takes.

## Web views on Ubuntu 24.04 and later

WebKitGTK runs web content in a bubblewrap sandbox, which needs
unprivileged user namespaces. Ubuntu 24.04's AppArmor allows them only to
programs with a profile that says so (`kernel.apparmor_restrict_unprivileged_userns`),
as Ubuntu's own WebKit apps (Epiphany, Devhelp) have. Without one, a
`<WebView>` shows a message saying so instead of the page; WebKit would
otherwise abort the app.

- **`.deb` packages** of apps that use the webview port install that
  profile (`/etc/apparmor.d/<package>`) and load it, so installed apps just
  work.
- **Development builds** (`run-linux`, the `dir` format) run from paths no
  profile names. Add one for your build directory, once:

  ```sh
  sudo tee /etc/apparmor.d/myapp-dev <<'EOF'
  abi <abi/4.0>,
  include <tunables/global>
  profile myapp-dev /home/*/src/MyApp/linux/build/*/MyApp flags=(unconfined) {
    userns,
  }
  EOF
  sudo apparmor_parser -r /etc/apparmor.d/myapp-dev
  ```

  (or, for a quick try only, run with `WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1`,
  which turns WebKit's sandbox off).
- **Flatpaks** and Fedora don't need anything.
