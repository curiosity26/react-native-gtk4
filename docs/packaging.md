# Packaging

`react-native package-linux` builds the app for Release and packages it.
It gives the app an identity on the desktop: a name, an icon, a `.desktop`
file and AppStream MetaInfo for software centres.

```sh
npx react-native package-linux                  # --format dir (the default)
npx react-native package-linux --format dir --version 1.2.0 --smoke
```

| Option | |
| --- | --- |
| `--format <format>` | `dir` (an installed tree with `install.sh`), `flatpak`, `deb` or `rpm` |
| `--output <dir>` | Where the package goes (default `linux/build/package`) |
| `--version <version>` | The app's version (default: app.json's `linux.version`, or package.json's `version`) |
| `--smoke` | Launch the packaged app with `--smoke` afterwards and exit with its status (Flatpak: installs the bundle for your user first) |
| `--local` | Flatpak: build from the project's directories instead of tarballs, with ccache |
| `--manifest-only` | Flatpak: write the manifest and its sources, don't build |
| `--prefix <dir>` | `dir`: install straight into a prefix (what the Flatpak build does inside the sandbox) |
| `--host <dir>` | Build against this host's CMake config instead of the cache's |
| `--logging` | Show all build output |
| `--no-checks` | Skip the prerequisite checks |

## The app's identity: app.json

`init-linux` adds a `linux` block to `app.json`, and `package-linux` reads
it:

```json
{
  "name": "MyApp",
  "displayName": "MyApp",
  "linux": {
    "appId": "com.example.MyApp",
    "displayName": "My App",
    "summary": "Does one thing well",
    "description": "What it does, in a paragraph or two.\n\n- a list\n- of features",
    "icon": "linux/icon.svg",
    "categories": ["Utility"],
    "keywords": ["thing"],
    "version": "1.0.0",
    "license": "MIT",
    "licenseFile": "LICENSE",
    "homepage": "https://example.com",
    "developer": {"id": "com.example", "name": "Example Ltd"},
    "screenshots": [{"url": "https://example.com/main.png", "caption": "The main window"}],
    "releases": [{"version": "0.9.0", "date": "2026-09-01", "description": "First beta."}]
  }
}
```

| Field | Default | |
| --- | --- | --- |
| `appId` | `com.<name>` | The GApplication id, and the name of the `.desktop` file, MetaInfo and icons. Reverse DNS: `linux/main.cc`'s `options.appId` must be the same (package-linux warns if it isn't). Flathub wants a domain you control, or `io.github.<user>.<App>` |
| `displayName` | app.json's `displayName` | The name in menus and software centres |
| `summary` | "`<displayName>`, a React Native app" | One line, no full stop at the end |
| `description` | the summary | Paragraphs separated by blank lines; a paragraph whose lines all start with `- ` is a list |
| `icon` | `linux/icon.svg` (init-linux's placeholder) | An SVG, or a square PNG of 128px or more. Installed into `share/icons/hicolor` at 16 to 512px (SVGs also as `scalable`) |
| `categories` | `["Utility"]` | [freedesktop.org categories](https://specifications.freedesktop.org/menu-spec/latest/category-registry.html); one must be a main one (`AudioVideo`, `Development`, `Education`, `Game`, `Graphics`, `Network`, `Office`, `Science`, `Settings`, `System`, `Utility`) |
| `keywords` | none | Extra search terms for the `.desktop` file |
| `version` | package.json's `version` | `--version` overrides both |
| `license` | package.json's `license` | An SPDX expression (`MIT`, `GPL-3.0-or-later`), or `LicenseRef-proprietary` |
| `licenseFile` | `LICENSE`, `COPYING`, ... in the project | Installed under `share/licenses/<appId>/` |
| `homepage` | package.json's `homepage`, or its GitHub `repository` | |
| `developer` | none | `{id, name}`: Flathub requires it |
| `screenshots` | none | URLs (strings, or `{url, caption}`): software centres show them; Flathub requires one |
| `releases` | the current version, dated today | Older releases for the MetaInfo's history. The date is `SOURCE_DATE_EPOCH`'s when it's set, for reproducible builds |
| `network` | `false` | Flatpak: the sandbox gets the network (`--share=network`) only when this is `true` |
| `flatpak` | | `{runtimeVersion, node, llvm, finishArgs}`: another GNOME runtime or SDK extensions, more `finish-args` |
| `maintainer` | package.json's `author`, `$DEBEMAIL`, git's user | `"Name <email>"` for `.deb` and `.rpm` |
| `deb` | | `{package, section, revision}`: the Debian package name (default: the app's, lowercase) |
| `rpm` | | `{name}`: the RPM name (default: the app's, lowercase) |

Packages are built against a Release build of the host library: optimized,
with React Native's debugger code kept so it matches the Hermes build
(`REACT_NATIVE_DEBUG_OPTIMIZED`). It has a cache entry of its own
(`host/<id>-release`, about 4 minutes to build once on a 2-core VM, or
prebuilt) and makes the Showcase's package 17 MB instead of 41 MB.

## What gets installed

`rngtk_app()` in `linux/CMakeLists.txt` (the package's CMake config) gives
Release builds a CMake install step, which `package-linux` runs into a
staging prefix:

```
bin/MyApp                                         RPATH $ORIGIN/../lib/MyApp
lib/MyApp/librngtk_host.so libhermesvm.so libjsi.so
share/MyApp/index.bundle.js  share/MyApp/assets/  where the app looks for them
share/applications/com.example.MyApp.desktop
share/metainfo/com.example.MyApp.metainfo.xml
share/icons/hicolor/{16x16,...,512x512,scalable}/apps/com.example.MyApp.{png,svg}
share/licenses/com.example.MyApp/LICENSE          and react-native-gtk4/LICENSE
```

The host libraries are private to the app (`lib/<app>/`, not `lib/`), so
apps built against different versions don't clash.

`package-linux` writes the `.desktop` file, MetaInfo, icons and licenses
into `linux/build/Release/packaging/share/` first; `cmake --install`
copies them. A plain `cmake --install linux/build/Release --prefix ...`
after `run-linux --release` installs the app without them.

It checks the result with `desktop-file-validate` and
`appstreamcli validate` when they're installed (`desktop-file-utils` and
`appstream` on Debian and Ubuntu, Fedora).

## Formats

### dir

`linux/build/package/<App>-<version>-<arch>/` is the prefix itself (41 MB
for the Showcase). Run `bin/<App>` from there, or install it with the
`install.sh` next to it:

```sh
linux/build/package/MyApp-1.0.0-x86_64/install.sh              # into ~/.local
linux/build/package/MyApp-1.0.0-x86_64/install.sh /opt/myapp   # or a prefix of your own
linux/build/package/MyApp-1.0.0-x86_64/install.sh --uninstall  # removes it again
```

The app then shows up in GNOME's app grid and dock with its icon, and its
notifications carry its name and icon. Outside `/usr` and `/usr/local`,
`install.sh` writes the executable's absolute path into the `.desktop`
file's `Exec`. GNOME Shell's `PATH` has no `~/.local/bin`, and the Shell
ignores an app whose `Exec` it can't find. It then refuses the app's
notifications (`org.gtk.Notifications.Error.InvalidApp`) and shows its
windows with a generic icon.

### flatpak

```sh
npx react-native package-linux --format flatpak            # Flathub-style: archives with checksums
npx react-native package-linux --format flatpak --local    # faster rebuilds while developing
```

writes `linux/build/package/flatpak/<appId>.json`, a
[flatpak-builder](https://docs.flatpak.org/en/latest/flatpak-builder.html)
manifest, and builds `linux/build/package/<App>-<version>-<arch>.flatpak`
from it. Install that with `flatpak install --user <file>.flatpak`.

The manifest builds everything from source in the GNOME 51 SDK, with no
network during the build, as Flathub requires:

- **`react-native-gtk4`**: React Native's sources, Hermes and the
  third-party C++ libraries (boost, folly, glog, fmt, ...) as the tag
  archives React Native pins, with their sha256. The package's host
  sources. The npm packages React Native's codegen runs with
  (`scripts/codegen-deps`). Builds Hermes and the Release host into
  `/app/rngtk`, which is removed from the finished app.
- **`<App>`**: the app's sources and its `node_modules` as offline npm
  sources (`app-npm-sources.json`, from
  [flatpak-node-generator](https://github.com/flatpak/flatpak-builder-tools/tree/master/node)).
  It runs `npm ci --offline` and then `package-linux --prefix /app`
  inside the sandbox.

It needs `flatpak`, `flatpak-builder` and flatpak-node-generator (pipx
install it from flatpak-builder-tools, or point `FLATPAK_NODE_GENERATOR` at
it). The GNOME SDK and the `node24` and `llvm22` extensions are installed
from Flathub for your user on the first build. flatpak-builder's state
lives in the package's cache (`flatpak/`) for every app. The first build
compiles React Native, Hermes and the host; after that only the app's
module rebuilds, unless the package changed.

`finish-args` are minimal: `--socket=wayland`, `--socket=fallback-x11`,
`--share=ipc`, `--device=dri`, plus `--share=network` when app.json's
`linux.network` is `true`. Files (`Dialogs`), notifications and
`Linking.openURL` go through the desktop portals, which GTK and GIO use
by themselves inside a sandbox. App-specific permissions go in
`linux.flatpak.finishArgs`.

`fallback-x11` gives the app X11 only on an X11 session; on a Wayland
session it's Wayland, whatever `GDK_BACKEND` says. To try X11 from a
Wayland session: `flatpak run --nosocket=wayland --socket=x11 <appId>`.

Tested with the Showcase on an aarch64 VM (Ubuntu 24.04, 2 cores): the
first build took about 11 minutes, compiling Hermes and the host in the
SDK. The bundle is 3.2 MB. It runs on Wayland and X11, and its
notifications, file dialogs and `openURL` go through the Notification,
FileChooser and OpenURI portals.

For Flathub, replace the app's `sources/<App>-<version>.tar.gz` with its
release archive URL (or a git tag) and sha256. The rest of the manifest
already uses public URLs. A package installed from GitHub Packages comes
from npm's own cache, because flatpak-builder can't authenticate to it.

### deb

```sh
npx react-native package-linux --format deb
sudo apt install ./linux/build/package/myapp_1.0.0-1_amd64.deb
```

The tree under `/usr`, with `Depends` from `dpkg-shlibdeps` (the system
libraries the app and its host link), a machine-readable
`/usr/share/doc/<package>/copyright` with the license texts, and a
`changelog.Debian.gz`. It's checked with `lintian` when it's installed
(`sudo apt install lintian`). Build it on the release you ship for:
`Depends` names that release's library packages (`libicu74`, ...).

### rpm

```sh
npx react-native package-linux --format rpm
sudo dnf install ./linux/build/package/myapp-1.0.0-1.<arch>.rpm
```

A generated spec (kept next to the `.rpm`) installs the tree under
`/usr`. rpm's dependency generators find the system libraries, and the
host libraries stay private (`__provides_exclude_from`,
`__requires_exclude`), with the executable's RUNPATH pointing at them
(`<name>.rpmlintrc` next to the spec tells rpmlint so). It's checked with
`rpmlint`. Build it on the distribution it's for. Fedora's library
sonames differ from Ubuntu's (ICU's, for one), so an RPM built on Ubuntu
won't install on Fedora.

From another distribution, build it in a container (podman or docker):

```sh
node_modules/@curiosity26/react-native-gtk4/scripts/package-in-container.sh . --format rpm
```

The script makes a Fedora 44 builder image once (`--image` picks another
distribution, a Debian or Ubuntu one for a `.deb`). It copies the app's
sources in, runs `npm ci` and `package-linux` there, and leaves the
package in `linux/build/package-fedora-44/`. Its build cache is a
volume, so later runs only rebuild the app. On the VM above, the first
run took 7.5 minutes; the RPM installs with dnf on a clean Fedora 44 and
runs on Wayland and X11 (GTK 4.22).

## The Showcase

`examples/hello-world` is set up as an app for packaging (its `app.json`
and `linux/`), with the Showcase as its component:

```sh
cd examples/hello-world
npx react-native package-linux --smoke
```
