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
- **svg** and **webview**: the GNOME runtime has librsvg and WebKitGTK
  6.0. WebKit's web processes run in a sandbox of their own inside the
  Flatpak's.

## Not ported yet

[library-assessments.md](library-assessments.md) covers gesture-handler,
reanimated and screens: where they stand on Linux and what a port takes.

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
