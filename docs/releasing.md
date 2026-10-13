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
`scripts/release.js line`: every package to <major>.<minor>.0. Fixes from
`main` are cherry-picked into the lines; each line releases its own
patches.

## Releasing

Pull requests don't change versions: a release picks them. Each package
has release tags (`v0.87.1` for this package, `react-native-gtk4-svg@0.87.0`
for a port). A package whose files changed since its last tag on the
branch's line gets the next patch, and its changelog section lists the pull
requests that changed it. Two pull requests can't claim the same version,
and none is left out of a release: whatever merged since the last tag goes
into the next one. A package with no tag yet on the line (a new line, or
the first release) goes out at the version it has.

What counts as a package's files: a port's folder; for this package, its
`files` in package.json and package.json itself (not docs/, examples/ or
test/).

To release: Actions, Release, Run workflow, pick `main` or `release/X.Y`,
tick "publish". Unticked, it's a dry run. The run's summary shows each
package's new version and changes; `scripts/release.js plan` shows the
same locally.

The workflow:

- **plan**: `scripts/release.js check`, then what a release would
  publish.
- **host**, when this package has a new version (x86_64 on
  `ubuntu-24.04`, aarch64 on `ubuntu-24.04-arm`):
  `scripts/prebuilt-host.js build` builds React Native's dependencies,
  Hermes and the host library from source, Debug and Release, and packs
  each install into
  `react-native-gtk4-host-<version>-ubuntu24.04-<arch>-<debug|release>.tar.gz`.
  The absolute paths in its SDK file become tokens that unpacking fills
  in.
- **publish**: `scripts/release.js version` writes the new versions and
  dated changelog sections, and the run commits them to the branch
  (`Release: core 0.87.1, svg 0.87.3`) and pushes; runs the tests; for
  this package, writes `prebuilt-hosts.json` (the four tarballs' sha256,
  the package's host source id, the release's download URL), creates the
  GitHub release `v<version>` on the release commit with the tarballs,
  `prebuilt-hosts.json` and the CHANGELOG.md section as notes (the repository's latest release only
  from `main`), and publishes it; then publishes each port with a new
  version and tags it `<name>@<version>`. Each goes out under the branch's
  dist-tags.

A package's tag is made once it's published, so a run that stops halfway
can be run again: a release commit without its tags goes out at its
versions, and what's already on GitHub Packages is skipped. The run pushes
to the branch, so the branch has to accept pushes from GitHub Actions.

Locally:

```sh
scripts/release.js check    # the versioning rule
scripts/release.js plan     # what a release from here would publish
npm pack --dry-run          # what the package contains
```

## How apps use the prebuilt hosts

`run-linux` and `package-linux` look for a prebuilt host before building
one (lib/cli/prebuilt.js). They use it when the package has
`prebuilt-hosts.json` (published packages do; a package installed from a
git tag, like `github:curiosity26/react-native-gtk4#v0.87.0`, fetches it
from the GitHub release `v<version>`, which needs no token), the package's
`linux/` is the one it was built from, the system is Ubuntu 24.04 or
based on it, and `ldd` finds every library it links. Otherwise, or with
`RNGTK_NO_PREBUILT=1`, they build the host from source as before. Apps
with native libraries also fetch React Native's sources for the headers
(`fetch-rn-deps.py`, then `build-hermes.sh --headers-only`).

Releases from before the manifest was attached to them get it from
[.github/workflows/release-manifests.yml](../.github/workflows/release-manifests.yml):
run it by hand with the versions, and it copies each published package's
`prebuilt-hosts.json` onto its GitHub release.

To move to a new React Native version, see [upgrading.md](upgrading.md).
