# Releasing

The package and the library ports in `packages/` are published to GitHub
Packages by [.github/workflows/release.yml](../.github/workflows/release.yml),
from `main` or from a release line.

## Versions

Every package's major.minor is React Native's, from
`rn-version.properties`: 0.87.x on React Native 0.87, 0.83.x on 0.83, like
react-native-windows and react-native-macos. The patch is each package's
own. A fix to the svg port releases the svg port alone (0.87.0 to 0.87.1);
a fix to the host releases this package alone. A new React Native minor
is a new line, and every package starts again at <major>.<minor>.0.

The ports' peer range on this package (and reanimated's on worklets) is
the line, `0.87.x`. `scripts/release.js check` (and `npm test`) holds the
packages to all of this.

| Branch | React Native | npm dist-tags |
|---|---|---|
| `main` | the latest (0.87) | `latest`, `v0.87-stable` |
| `release/0.83` | 0.83 | `v0.83-stable` |

Each release line is branched from the one before it (`release/0.84` from
`release/0.83`) and moved up with `scripts/upgrade-rn.sh`, which also runs
`scripts/release.js line`: every package to <major>.<minor>.0 with a
changelog section for it. Fixes from `main` are cherry-picked into the
lines; each line releases its own patches.

## Releasing a version

1. Bump the packages that changed, on the branch:
   `scripts/release.js bump svg` (a folder in `packages/`, `core` for this
   package, or `all`). It raises the patch and adds
   `## <version> (unreleased)` to that package's CHANGELOG.md: write what
   changed there, and turn `(unreleased)` into the date.
2. Merge that (a pull request that changes a version dry-runs the
   workflow).
3. Run the workflow on the branch: Actions, Release, Run workflow, pick
   `main` or `release/X.Y`, tick "publish". Unticked, it's a dry run.

`scripts/release.js plan` shows which versions GitHub Packages doesn't
have yet; the workflow publishes those, and only those, so a run with
nothing new publishes nothing and a run that stopped halfway can be run
again.

The workflow:

- **plan**: `scripts/release.js check`, then which versions to publish
  (in the run's summary).
- **host**, when this package has a new version (x86_64 on
  `ubuntu-24.04`, aarch64 on `ubuntu-24.04-arm`):
  `scripts/prebuilt-host.js build` builds React Native's dependencies,
  Hermes and the host library from source, Debug and Release, and packs
  each install into
  `react-native-gtk4-host-<version>-ubuntu24.04-<arch>-<debug|release>.tar.gz`.
  The absolute paths in its SDK file become tokens that unpacking fills
  in.
- **publish**: runs the tests; for this package, writes
  `prebuilt-hosts.json` (the four tarballs' sha256, the package's host
  source id, the release's download URL), creates the GitHub release
  `v<version>` with the tarballs and the CHANGELOG.md section as notes
  (the repository's latest release only from `main`), and publishes it;
  then publishes each port with a new version and tags it
  `<name>@<version>` (`react-native-gtk4-svg@0.87.1`). Each goes out under
  the branch's dist-tags.

Locally:

```sh
scripts/release.js check    # the versioning rule
scripts/release.js plan     # what GitHub Packages doesn't have yet
npm pack --dry-run          # what the package contains
```

## How apps use the prebuilt hosts

`run-linux` and `package-linux` look for a prebuilt host before building
one (lib/cli/prebuilt.js). They use it when the package has
`prebuilt-hosts.json` (only published packages do), the package's
`linux/` is the one it was built from, the system is Ubuntu 24.04 or
based on it, and `ldd` finds every library it links. Otherwise, or with
`RNGTK_NO_PREBUILT=1`, they build the host from source as before. Apps
with native libraries also fetch React Native's sources for the headers
(`fetch-rn-deps.py`, then `build-hermes.sh --headers-only`).

To move to a new React Native version, see [upgrading.md](upgrading.md).
