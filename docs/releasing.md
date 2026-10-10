# Releasing

Releases are tagged from `main` and built by
[.github/workflows/release.yml](../.github/workflows/release.yml).

1. Set the version in `package.json` (`npm version --no-git-tag-version
   0.2.0`), and in CHANGELOG.md turn `## <version> (unreleased)` into
   `## <version> (<date>)`.
2. Merge that into `main`.
3. Tag it: `git tag v0.2.0 && git push origin v0.2.0`.

The tag runs the workflow:

- **host** (x86_64 on `ubuntu-24.04`, aarch64 on `ubuntu-24.04-arm`):
  `scripts/prebuilt-host.js build` builds React Native's dependencies,
  Hermes and the host library from source, Debug and Release, and packs
  each install into
  `react-native-gtk4-host-<version>-ubuntu24.04-<arch>-<debug|release>.tar.gz`.
  The absolute paths in its SDK file become tokens that unpacking fills
  in.
- **publish**: checks that the tag is `v<package.json's version>`, writes
  `prebuilt-hosts.json` (the four tarballs' sha256, the package's host
  source id, the release's download URL) into the package, runs the
  tests, creates the GitHub release with the tarballs and the
  CHANGELOG.md section as notes, and publishes the package and the
  library ports in `packages/` (at the same version: bump theirs too) to
  GitHub Packages.

Running the workflow by hand (Actions, Release, Run workflow) is a dry
run: it builds and packs the hosts and runs `npm publish --dry-run`, and
publishes nothing.

Locally:

```sh
npm pack --dry-run          # what the package contains
npm publish --dry-run       # what publishing would do (needs the registry in .npmrc)
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
