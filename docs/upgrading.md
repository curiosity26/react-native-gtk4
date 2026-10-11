# Upgrading React Native

This package builds against one React Native version (`rn-version.properties`).
Moving it to another one takes the script, a look at what changed upstream
under `overrides/`, a host build, and the tests.

## 1. The script

```sh
scripts/upgrade-rn.sh 0.88.0          # or a release candidate: 0.88.0-rc.4
git diff
```

`scripts/upgrade-rn.sh` sets the versions that go with that React Native:

| | |
| --- | --- |
| `rn-version.properties` | `reactNative`, and `hermes`: react-native's `hermes-compiler` dependency, as Hermes' tag (`hermes-v<version>`) |
| `package.json` | the `react-native` peer dependency |
| `examples/hello-world` | `react-native`, `react`, `@react-native/babel-preset` and `metro-config`, `metro`, the community CLI (from `@react-native-community/template` at that version), then `npm install` |
| `scripts/codegen-deps` | `react-native` and `@react-native/codegen`, and its lock, regenerated |
| docs, README, workflows, scripts | the React Native and CLI version strings |

It also leaves a report in `build/upgrade-<version>/`:

- `overrides.diff`: upstream's changes, from the old version to the new
  one, to each React Native file an override in `overrides/` replaces.
  Overrides that re-export another platform's file (Image, BaseViewConfig,
  PlatformColorValueTypes follow Android's) pick the changes up by
  themselves. Copies (Modal, TextInput, Alert, Button,
  useWindowDimensions) need them ported by hand. `patch` gets most hunks
  across, since a copy differs from upstream only in its imports and a few
  marked lines:

  ```sh
  patch -p0 overrides/Libraries/Modal/Modal.linux.js < build/upgrade-<version>/Libraries_Modal_Modal.js.patch
  ```

  (split `overrides.diff` per file first, or apply its hunks by hand). Check
  every override's `react-native-upstream/...` imports still exist.
- `new-splits.txt`: files new in this version that exist only as
  `.ios.js` and `.android.js`. The Metro config gives Linux the Android
  one; check that it fits, or add an override.

## 2. Build and test

React Native's sources, Hermes and the host build into the cache per
version (`~/.cache/react-native-gtk4/<version>/`), so the old version's
build stays.

```sh
npm test
cd examples/hello-world
npx react-native run-linux --release --smoke           # fetches, builds Hermes and the host
GDK_BACKEND=x11 npx react-native run-linux --release --smoke
cd ../..
cmake -S linux -B build/linux-<version> -G Ninja \
  -DRNGTK_DEPS_DIR=$HOME/.cache/react-native-gtk4/<version>/deps
cmake --build build/linux-<version>
GDK_BACKEND=wayland ctest --test-dir build/linux-<version>
GDK_BACKEND=x11 ctest --test-dir build/linux-<version>
scripts/test-new-app.sh                                # a new app from the community template
```

Host build failures are usually React Native's C++ API changing: a prop's
type, a header that moved, a module that's new. The host's CMakeLists.txt
follows React Native's Fantom tester
(`private/react-native-fantom/tester/CMakeLists.txt`), so diff that file
between the two versions first. Keep fixes compatible with both versions
where it's cheap (an overload, an `if(EXISTS ...)`), so the upgrade itself
stays a version bump.

## Trial: React Native 0.88.0-rc.4 (October 2026)

0.88.0 wasn't out yet, so this used its fourth release candidate (Hermes
hermes-v260318099.0.4, React 19.3.0, CLI 20.2.0), on a local branch.

**The script** found 12 changed upstream files under the overrides and
no new platform splits.

**What broke, and the fixes:**

1. **JS: `Libraries/Modal/NativeModalManager` is gone.** 0.88 removes the
   old renderer's iOS `modalDismissed` event, and with it the module our
   Modal override imported, so bundling failed. The fix ports upstream's
   change: no `ModalEventEmitter`. It works on 0.87 too.
2. **C++: `ImageProps::tintColor` is `std::optional<SharedColor>`.**
   `GtkImages.cc` didn't compile. Fixed with a `to_rgba` overload for
   optional colors, which works with both versions.
3. **C++: new modules.** `react/cxxstableapi` (a header other modules now
   include, `PrivateGuard.h`), `react/nativemodule/resizeobserver` and
   `react/renderer/observers/resize` (ResizeObserver). The host's
   CMakeLists.txt adds them when the sources have them.
4. **TextInput's override** imports `TextStyle` instead of
   `____TextStyle_Internal`, as upstream now does. It's a type-only change
   (Babel strips it), so it doesn't break anything if missed.
5. **A test** expected React Native's version to be `x.y.z`, without a
   prerelease suffix.

Everything else carried over: React Native's other C++ API, Hermes 260318,
Metro and the CLI, the platform's JS (`Platform`, the overrides that
re-export Android's files), autolinking, and the six library ports.

**Results:**
- The host (Hermes, then React Native's C++ core and ours) built in 4
  minutes for the dependencies and Hermes, plus about 6 for the host.
- The Showcase gives SMOKE OK on Wayland and X11.
- `npm test` passes.
- The harness's 19 self-tests pass on Wayland and X11, built against 0.88
  (and still against 0.87.1, with the compatibility fixes).

## Trial: React Native 0.83.10 (October 2026)

A release line going back a version, for apps that can't move past React
Native 0.83 yet (react-native-macos 0.83.0 peers on 0.83.10;
react-native-windows has a 0.83 line too). It's on its own branch: React
Native 0.83.10, legacy Hermes (hermes-v0.14.1, react-native's
`hermes-compiler` 0.14.1, the same commit as its `sdks/.hermesversion`),
React 19.2.0, CLI 20.0.0, Metro 0.83.

**The script** went down a version as it goes up: 23 changed upstream
files under the overrides (mostly 0.87's refactors, reversed), no new
platform splits. Legacy Hermes has the same `hermesvm` target, so
`build-hermes.sh` and `fetch-rn-deps.py` needed no changes. The script
itself got two fixes: it rewrote the 0.88 trial's versions in this file,
and it skipped `scripts/test-new-app.sh`, which names only the CLI's
version.

**Not on this line:** the worklets and Reanimated ports
(react-native-worklets 0.13 and react-native-reanimated 4.7 need React
Native 0.86+). `packages/worklets` and `packages/reanimated` are removed,
with the Showcase's pages for them. rn-gtk-host builds them and their
self-tests only when `packages/reanimated` exists. The other ports
(async-storage, netinfo, safe-area-context, vector-icons, svg, webview,
gesture-handler, screens) work.

**What broke, and the fixes:**

1. **Metro: an override's package imports resolved from the app.** npm
   nested `@react-native/virtualized-lists` inside react-native, so the
   Modal override's import of it failed. Overrides' bare imports now
   resolve from react-native, as the file they replace would. Fixes a bug
   that any version could hit.
2. **JS: `Libraries/Utilities/HardwareBackPressEvent` doesn't exist.**
   0.83's BackHandler calls its listeners without an argument, and so does
   Linux's on this line.
3. **JS: no `colorAttribute` in `ReactNativeStyleAttributes`.**
   ImageViewNativeComponent uses `{process: processColor}`, which is what
   it is.
4. **JS: no `topKeyDown`/`topKeyUp` in Android's view config.** Every key
   raised "Unsupported top level event type". Linux's config lists them
   itself.
5. **react-native-screens 4.27+**: its codegen specs use
   `React.ComponentRef`, which 0.83's codegen rejects. The Showcase pins
   4.26.2 (same native props as 4.29 for the port).
6. **C++: no `react/nativemodule/TurboModuleProvider.h`.** 0.83 has the
   same type as `TurboModuleManagerDelegate(s)`;
   `rngtk/TurboModuleProviders.h` picks whichever exists.
7. **C++: no viewtransition modules.** Optional in the host's
   CMakeLists.txt, like 0.88's additions. (Fantom's tester CMakeLists.txt
   differs from 0.87's only there.)
8. **C++: smaller API differences**, each handled by a template helper
   with `if constexpr (requires ...)` that compiles against both versions:
   the linear gradient direction (wrapped in `GradientDirection`),
   `TextMeasureCacheKey::pointScaleFactor`, `TextAlignment::End`,
   `TextDecorationStyle::Wavy`, events' `timeStamp`, and
   `ImageResponseObserverCoordinator` taking observers by reference.
   `SourceCodeModule` takes a `DevServerHelper` in 0.83, so release
   bundles get a small SourceCode module of the host's own.

9. **Text kept its size when the font scale went back down** (GNOME's text
   scaling from 1.5 to 1.0). 0.83 dirties text nodes only in the commit
   where the scale changes; later commits bring back React's nodes laid out
   at the old scale, and Yoga reuses their layout. Upstream fixed it after
   0.83 (facebook/react-native#57246, which adds
   `LayoutMetrics::fontSizeMultiplier`). Without that, the host registers a
   commit hook (`FontScaleCommitHook`) that, once the font scale has
   changed, dirties every laid-out Text or TextInput not yet committed at
   the current scale, in each commit.

**Results:**
- `npm test` passes.
- The harness's 24 self-tests pass on Wayland and X11.
- The Showcase gives SMOKE OK on Wayland and X11 (the Release host built
  in 4.5 minutes, the app in 1).
- `scripts/test-new-app.sh` passes: apps from the 0.83.10 community
  template, Debug from Metro and Release on Wayland and X11, package-linux,
  and a native library from init-linux-library autolinked.
- The same host sources still build against 0.87.1.

## Trial: React Native 0.84.1 (October 2026)

The 0.84 release line, branched from release/0.83 (each line from the one
before it, so fixes merge forward): React Native 0.84.1, Hermes
hermes-v250829098.0.9 (Hermes V1 again), React 19.2.3, CLI 20.1.0.

**The script** found 10 changed upstream files under the overrides, all
formatting and type names, and no new platform splits. Android's view
config has keyDown and keyUp now, which Linux's lists too. Fantom's tester
CMakeLists.txt is the same as 0.83's.

**What broke, and the fixes:**

1. **C++: the legacy Hermes inspector.** 0.84 still has
   `hermes/inspector-modern/chrome` and compiles it unless
   `HERMES_V1_ENABLED` is defined (react-native-flags.cmake does that when
   the CMake variable is set); Hermes V1 doesn't have its headers. The
   host's CMakeLists.txt sets the variable unless the Hermes headers have
   legacy Hermes' inspector.
2. **Native Animated put back older values.** From 0.84 the value an
   animation ends on is committed through Fabric, but the animated nodes
   manager's "unsynced direct props" aren't updated on that path, and its
   mounting override patched the next mount with them: a held
   TouchableOpacity faded, then went back to full opacity. The host turns
   on `overrideBySynchronousMountPropsAtMountingAndroid`, which drops that
   fallback; views with connected animated nodes still get their nodes'
   current values.

**What carried over from 0.83:** every compatibility helper, the
font-scale commit hook (0.84 doesn't have the upstream fix either), and
the ports. 0.84's codegen accepts `React.ComponentRef`, so
react-native-screens is back to 4.29. Worklets and Reanimated still need
0.86.

**Results:**
- `npm test` passes.
- The harness's 24 self-tests pass on Wayland and X11.
- The Showcase gives SMOKE OK on Wayland and X11.
- `scripts/test-new-app.sh` passes (apps from the 0.84.1 template, Debug
  and Release on Wayland and X11, package-linux, a native library).

## Trial: React Native 0.85.3 (October 2026)

The 0.85 release line, branched from release/0.84: React Native 0.85.3,
Hermes hermes-v250829098.0.10, React 19.2.3, CLI 20.1.0, Metro 0.84.

**The script** found 9 changed upstream files under the overrides: type
names (`Readonly` for `$ReadOnly`), and `colorAttribute` back in
`ReactNativeStyleAttributes` (ImageViewNativeComponent's local one is the
same thing). No new platform splits; Fantom's tester CMakeLists.txt is the
same as 0.84's.

**What broke:** nothing. The host built against 0.85.3 as it was, with
0.83's and 0.84's compatibility fixes; the font-scale commit hook stays
(0.85 doesn't have the upstream fix), as does 0.84's Native Animated
setting. Worklets and Reanimated still need 0.86.

**Results:**
- `npm test` passes.
- The harness's 24 self-tests pass on Wayland and X11.
- The Showcase gives SMOKE OK on Wayland and X11.
- `scripts/test-new-app.sh` passes.
