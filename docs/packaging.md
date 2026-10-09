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
| `--format <format>` | `dir` (an installed tree with `install.sh`) or `deb` |
| `--output <dir>` | Where the package goes (default `linux/build/package`) |
| `--version <version>` | The app's version (default: app.json's `linux.version`, or package.json's `version`) |
| `--smoke` | Launch the packaged app with `--smoke` afterwards and exit with its status |
| `--prefix <dir>` | `dir`: install straight into a prefix |
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
| `maintainer` | package.json's `author`, `$DEBEMAIL`, git's user | `"Name <email>"` for the `.deb` |
| `deb` | | `{package, section, revision}`: the Debian package name (default: the app's, lowercase) |

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

## The Showcase

`examples/hello-world` is set up as an app for packaging (its `app.json`
and `linux/`), with the Showcase as its component:

```sh
cd examples/hello-world
npx react-native package-linux --smoke
```
