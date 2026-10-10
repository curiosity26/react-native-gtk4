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
- See the branch's notes for the harness's self-tests.
