# Releasing

The package and the library ports in `packages/` are published to GitHub
Packages by [.github/workflows/release.yml](../.github/workflows/release.yml),
from `main` or from a release line, and copied from there to npm
(registry.npmjs.org) once that's turned on ([npm](#npm)).

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
- **host**, when this package has a new version, for each system
  (Ubuntu 24.04 on the runner; Debian 13, Fedora 44 and 45, and EL10 in
  `debian:13`, `fedora:44`, `fedora:45` and `almalinux:10` containers) and
  architecture (x86_64 on `ubuntu-24.04`, aarch64 on `ubuntu-24.04-arm`):
  `scripts/prebuilt-host.js build` builds React Native's dependencies,
  Hermes and the host library from source, Debug and Release, and packs
  each install into
  `react-native-gtk4-host-<version>-<system>-<arch>-<debug|release>.tar.gz`
  (`<system>`: `ubuntu24.04`, `debian13`, `fedora44`, `fedora45` or `el10`).
  The absolute paths in its SDK file become tokens that unpacking fills
  in.
- **publish**: `scripts/release.js version` writes the new versions and
  dated changelog sections, and the run commits them to the branch
  (`Release: core 0.87.1, svg 0.87.3`) and pushes; runs the tests; for
  this package, writes `prebuilt-hosts.json` (the tarballs' sha256,
  the package's host source id, the release's download URL), creates the
  GitHub release `v<version>` on the release commit with the tarballs,
  `prebuilt-hosts.json` and the CHANGELOG.md section as notes (the repository's latest release only
  from `main`), and publishes it; then publishes each port with a new
  version and tags it `<name>@<version>`. Each goes out under the branch's
  dist-tags.

A package's tag is made once it's published, so a run that stops halfway
can be run again: a release commit without its tags goes out at its
versions, and what's already on GitHub Packages is skipped. The run pushes
the release commit to the branch: with the release app's token when the
app is set up ([Branch protection](#branch-protection)), else GITHUB_TOKEN.

Locally:

```sh
scripts/release.js check    # the versioning rule
scripts/release.js plan     # what a release from here would publish
npm pack --dry-run          # what the package contains
```

## Branch protection

`main` and `release/*` take changes only through pull requests whose checks
pass: the rulesets in [.github/rulesets](../.github/rulesets) block direct
pushes, force pushes and deletion, and require the JS tests (`test`) and,
on `main`, the host build and self-tests (`self-tests`, from
[host.yml](../.github/workflows/host.yml); the lines get it once it's
cherry-picked into them). No approving review is required.

The one direct push is the release commit. GITHUB_TOKEN can't be put on a
ruleset's bypass list, so the release pushes it as a GitHub App of the
repository's own that is. Setting it up, once (before turning the rulesets
on, or releases can't push):

1. Create the app at <https://github.com/settings/apps/new>: any name
   (e.g. `rngtk-release`), the repository as its homepage, Webhook
   "Active" unticked, Repository permissions: Contents "Read and write",
   only on this account.
2. On the app's page: note its App ID, "Generate a private key" (a `.pem`
   file downloads), and "Install App" on this repository only.
3. Repository Settings, Secrets and variables, Actions: a variable
   `RELEASE_APP_ID` (the App ID) and a secret `RELEASE_APP_PRIVATE_KEY`
   (the `.pem` file's contents).
4. Repository Settings, Rules, Rulesets, New ruleset, Import a ruleset:
   `.github/rulesets/main.json`, then `.github/rulesets/release-lines.json`.
   In each, Bypass list, Add bypass, the app; then Create.

A release dry run then shows "The release app's token" step passing.

## npm

GitHub Packages is where a release publishes; npm gets a copy. With the
repository variable `NPM_PUBLISH` set to `true`, the workflow's last job,
**npm**, runs `scripts/release.js npm`: for each package, every version on
GitHub Packages that npm doesn't have is copied over, the same tarball
(so `prebuilt-hosts.json`, its checksums and its GitHub release URLs come
along unchanged), oldest first, then npm's dist-tags are set to GitHub
Packages'. It doesn't depend on the branch: a run on any line copies every
line's versions. A version released from the commit the run is on gets npm
provenance; an older one doesn't (its build wasn't this run). A release
that stopped before npm is finished by the next run, or by one with
"npm only" ticked, which releases nothing and just copies.

npm authenticates the job by trusted publishing (OIDC from GitHub
Actions, no secret). npm only lets a trusted publisher be added to a
package that exists, so the first copy uses a token:

1. On npmjs.com, create a granular access token with read and write on
   the `@curiosity26` scope's packages, and add it to the repository's
   Actions secrets as `NPM_TOKEN`.
2. Add the repository variable `NPM_PUBLISH` = `true` (Settings, Secrets
   and variables, Actions, Variables).
3. Run Release on `main` with "publish" and "npm only" ticked: every
   package and version goes to npm.
4. For each package on npmjs.com (Settings, Trusted publishing): GitHub
   Actions, user `curiosity26`, repository `react-native-gtk4`, workflow
   `release.yml`, no environment, and tick "Allow npm dist-tag".
5. Delete the `NPM_TOKEN` secret and the token, and set each package's
   publishing access to "Require two-factor authentication and disallow
   tokens".

`scripts/release.js npm --dry-run` shows locally what a run would copy
(it needs a token that reads GitHub Packages).

## How apps use the prebuilt hosts

`run-linux` and `package-linux` look for a prebuilt host before building
one (lib/cli/prebuilt.js). They use it when the package has
`prebuilt-hosts.json` (published packages do; a package installed from a
git tag, like `github:curiosity26/react-native-gtk4#v0.87.0`, fetches it
from the GitHub release `v<version>`, which needs no token), the package's
`linux/` is the one it was built from, the system is one they're built
for (Ubuntu 24.04 and derivatives like Linux Mint 22, Debian 13 and LMDE
7, Fedora 44 and 45, or EL10: RHEL, CentOS Stream, AlmaLinux, Rocky Linux), and
`ldd` finds every library it links. Otherwise, or with
`RNGTK_NO_PREBUILT=1`, they build the host from source as before. Apps
with native libraries also fetch React Native's sources for the headers
(`fetch-rn-deps.py`, then `build-hermes.sh --headers-only`).

Releases from before the manifest was attached to them get it from
[.github/workflows/release-manifests.yml](../.github/workflows/release-manifests.yml):
run it by hand with the versions, and it copies each published package's
`prebuilt-hosts.json` onto its GitHub release.

To move to a new React Native version, see [upgrading.md](upgrading.md).
