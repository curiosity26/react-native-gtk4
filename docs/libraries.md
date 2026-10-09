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

