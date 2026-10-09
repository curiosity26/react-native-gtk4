# Getting started

Add Linux to a React Native app, the way `react-native-windows` and
`react-native-macos` do: install the package, run `init-linux` once, then
`run-linux`.

![A new React Native app on GTK4 (Release build, Wayland)](images/new-app-release-wayland.png)

## Prerequisites

React Native 0.87.1 and a desktop with GTK 4.14 or newer: Ubuntu 24.04,
Linux Mint 22, Debian 13, Fedora 40 or later. Wayland and X11 both work.

**Ubuntu, Debian, Linux Mint**

```sh
sudo apt install -y clang cmake ninja-build pkg-config git python3 \
  libgtk-4-dev libsoup-3.0-dev libssl-dev libicu-dev libreadline-dev
```

**Fedora**

```sh
sudo dnf install -y clang cmake ninja-build pkgconf-pkg-config git python3 \
  gtk4-devel libsoup3-devel openssl-devel libicu-devel readline-devel
```

**Node.js 22.13 or newer** (Metro 0.87 needs it). Ubuntu 24.04's packaged
`nodejs` is 18: install Node 22 or 24 LTS from <https://nodejs.org> or with
nvm.

`run-linux` checks all of this before it builds and prints the install
command for your distribution if something is missing.

## Create an app

```sh
npx @react-native-community/cli@20.2.0 init MyApp --version 0.87.1
cd MyApp
npm install @curiosity26/react-native-gtk4
npx react-native init-linux
npx react-native run-linux        # or: npm run linux
```

The package is published on GitHub Packages; tell npm where to find the
`@curiosity26` scope first (`~/.npmrc` or the app's `.npmrc`):

```
@curiosity26:registry=https://npm.pkg.github.com
```

### What `init-linux` does

- Adds `linux/`: a `CMakeLists.txt` and a `main.cc` that names the app's
  id (`com.myapp`), window title, size and the registered component.
  Edit them freely: `main.cc` is the app's own entry point.
- Adds `@curiosity26/react-native-gtk4` to `dependencies` if it isn't there.
- Adds the `"linux": "react-native run-linux"` script.
- Wraps `metro.config.js`'s export with `withLinux(...)`, which adds the
  `linux` platform to Metro. If the file is unusual, it prints the edit to
  make instead.

Running it again changes nothing. `--overwrite` replaces `linux/`'s files
with the template's; `--app-id org.example.MyApp` sets the application id.

### What `run-linux` does

1. Checks the prerequisites.
2. **First run only:** fetches React Native's C++ sources and third-party
   libraries and builds Hermes into a cache shared by all your apps.
3. **First run of a package version only:** builds the React Native host
   library (`ReactNativeGtk::host`, React Native's C++ core plus the GTK
   host) into the same cache.
4. Builds `linux/` with CMake and Ninja into `linux/build/Debug` (or
   `linux/build/Release`). That's one source file: seconds.
5. Debug: starts Metro in a new terminal window unless it's already running.
6. Launches the app.

| Option | |
| --- | --- |
| `--release` | Release build: bundles the JS and assets next to the executable |
| `--no-packager` | Don't start Metro |
| `--port <n>` | Metro's port (default 8081) |
| `--no-launch` | Build, but don't launch |
| `--build-only` | Only build: no Metro, no launch |
| `--logging` | Show all build output and keep the app attached to the terminal, with its logs |
| `--smoke` | Launch with `--smoke` (below) and exit with its status |
| `--screenshot <png>` | With `--smoke`, save the window |
| `--terminal <app>` | The terminal to start Metro in (default: `$REACT_TERMINAL`, `x-terminal-emulator`, `gnome-terminal`, ...) |
| `--no-checks` | Skip the prerequisite checks |

### The cache

```
~/.cache/react-native-gtk4/            $RNGTK_CACHE_DIR, or $XDG_CACHE_HOME/react-native-gtk4
  0.87.1/deps/                         React Native sources, C++ libraries, Hermes (~1 GB)
  0.87.1/host/<id>/build/              the host library's build (~750 MB)
  0.87.1/host/<id>/install/            librngtk_host.so, libhermesvm.so, libjsi.so, headers, CMake config
```

`<id>` is a hash of the package's native sources, so updating the package
builds a new host once; delete old ones whenever you like. `RNGTK_DEPS_DIR`
points at an existing dependency directory instead (this repository's
`third-party/deps`, say).

Build times on a 2-core ARM64 VM (Ubuntu 24.04, 4 GB RAM), measured with
`scripts/test-new-app.sh`: the first app took **7m 18s** with an empty
cache (fetching React Native and building Hermes 4m 05s, the host library
3m 13s, the app under a second). With the dependencies cached, a new
package version's host build took 3m 45s. A second app took **under a
second** for Debug and **2 to 9 seconds** for Release, nearly all of it
`react-native bundle`.

## Debug and Release

**Debug** (the default) loads the JS from Metro: Ctrl+R reloads, Ctrl+D (or
Ctrl+M) opens the dev menu, fast refresh and LogBox work, and Metro's `j`
opens React Native DevTools. See [dev-loop.md](dev-loop.md). The executable
runs the host library from the cache.

**Release** (`run-linux --release`) runs
`react-native bundle --platform linux --dev false` into the build
directory, so `linux/build/Release/` holds everything the app needs:

```
MyApp  index.bundle.js  assets/  librngtk_host.so  libhermesvm.so  libjsi.so
```

The app finds `index.bundle.js` next to its executable, or in
`../share/MyApp/` for an installed layout. Packaging (Flatpak first) is
Phase 4.

## The app's command line

`rngtk::runApp` (see `linux/main.cc` and the package's
`linux/include/rngtk/App.h`) parses:

| | |
| --- | --- |
| `--dev-server [HOST:PORT]` | Load from Metro (the Debug default, `localhost:8081`) |
| `--bundle FILE` | Load a bundle file (the Release default: `index.bundle.js` next to the executable) |
| `--smoke` | Wait for the first mount, check there were no JS errors, print `SMOKE OK` and exit 0 (1 on an error or timeout) |
| `--screenshot FILE` | With `--smoke`, save the window as a PNG |
| `--timeout MS` | With `--smoke` (default 120000) |
| `--no-inspector` | Debug: don't connect to React Native DevTools |
| `--verbose` | Log at info level |

## Troubleshooting

- **`MISS ...` before the build.** Install what the check lists; it prints
  the `apt`/`dnf` command for your distribution.
- **`Node.js 18 (need >= 22.13)`.** Your `node` is the distribution's;
  install a newer one (above) and make sure it's first on `PATH`.
- **`No linux/CMakeLists.txt here`.** Run `npx react-native init-linux`.
- **`unknown command 'run-linux'`.** The package isn't in the app's
  `dependencies` (the CLI only loads commands from dependencies), or
  `npm install` hasn't run.
- **The window shows "Could not connect to the development server".**
  Metro isn't running on that port: `npx react-native start` (or drop
  `--no-packager`), or pass `--port`.
- **Metro doesn't open in a new window.** `run-linux` looks for a terminal
  app; without one (or without a display) it starts Metro in the
  background and logs to `linux/build/metro.log`. Pass `--terminal kgx` or
  set `REACT_TERMINAL`.
- **`j` (DevTools) fails with "The SUID sandbox helper binary was found,
  but is not configured correctly".** Ubuntu 24.04 and Mint 22 block the
  sandbox React Native DevTools (Electron) uses. Add the AppArmor profile in
  [dev-loop.md](dev-loop.md#j-fails-the-suid-sandbox-helper-binary-was-found-but-is-not-configured-correctly)
  (recommended), or `chown root` and `chmod 4755` the `chrome-sandbox` the
  error names.
- **A broken or half-built cache.** Delete `~/.cache/react-native-gtk4/`
  (or just `<version>/host/`) and run again.
- **X11 instead of Wayland.** GTK picks the session's; `GDK_BACKEND=x11`
  forces X11.
- **Native modules.** A library with native code needs a `linux/` folder
  of its own: `run-linux` autolinks the dependencies that have one, and
  `npx react-native init-linux-library` starts one from a template (a
  TurboModule and a native component); see
  [native-modules.md](native-modules.md). Libraries with only Android and
  iOS code have no Linux side. `react-native-safe-area-context` (in React
  Native's app template) works through its own pure-JS fallback, which the
  package's Metro config selects on Linux (zero insets).
