# Agent and contributor notes

Rules for anyone (people or coding agents) changing this repository. The
user docs are in [docs/](docs/); this file is about not breaking things.

## Branches and releases

- `main` tracks the latest React Native. `release/X.Y` lines track older
  ones; fixes land on `main` first, then go to `release/0.83` and merge
  forward (0.83 → 0.84 → 0.85 → 0.86). See [docs/releasing.md](docs/releasing.md).
- `main` and `release/*` are protected: changes go through pull requests
  with the `test` (and on `main`, `self-tests`) checks green.

## Security: CodeQL code scanning

`.github/workflows/codeql.yml` scans JavaScript, Python, C/C++ and the
workflows on every pull request into `main` or `release/*`. Don't merge a
pull request that adds an alert; fix the code (dismiss an alert only when
it is a real false positive, and say why). The patterns below are the ones
it has caught here before.

### Escaping strings (js/incomplete-sanitization)

Any string from outside (package names, paths, versions, app metadata)
that goes into a regular expression or a generated file must be escaped
for every character that syntax treats specially, the escape character
itself first.

- Regular expressions: use `escapeRegExp` from `lib/cli/escape.js`. Never
  `s.replace(/\./g, '\\.')` or `s.replace('.', '\\.')` (a string pattern
  replaces only the first match).
- Generated files: escape the backslash too, in the same pass or first.
  - CMake quoted arguments: `\`, `"` and `$` (see `cmakePath` in
    `lib/cli/autolinking.js`).
  - Desktop entries: `desktopValue` (`\\`, `\n`, `\t`), then `\;` inside
    lists (`lib/cli/appInfo.js`).
  - C/C++ string literals and shell: don't interpolate outside strings;
    pass them as data (arguments, environment) or check them against a
    strict pattern first (`cIdentifier` makes identifiers).

### Regular expressions on outside input (js/polynomial-redos)

Avoid patterns that backtrack polynomially on long input, such as
`/_+$/`, `/\s+$/` or nested quantifiers like `/(a+)+/`, on strings a user
or a dependency controls. Collapse runs first and match a single
character (`/^_|_$/g` after `.replace(/[^a-z0-9]+/g, '_')`), or trim
without a regex.

### Workflows (actions/missing-workflow-permissions and friends)

- Every workflow sets a top-level `permissions:` block, `contents: read`
  unless it needs more; a job that writes asks for it on the job.
- Don't put `${{ github.event.* }}` or `${{ inputs.* }}` straight into a
  `run:` script: pass them through `env:` and quote the variable.
- Don't check out pull request code in `pull_request_target` workflows.
- Pin third-party actions to a release tag at least; secrets only on the
  steps that need them.

### Flow files that CodeQL can't parse

`overrides/` holds Flow-typed copies of React Native's `Libraries/` files.
Modern Flow syntax (`x as T`, `readonly`, `+default`) is a parse error for
CodeQL, so `.github/codeql/codeql-config.yml` leaves `overrides/` out of
the scan. Keep it that way:

- Only React Native overrides go in `overrides/`; put new logic,
  especially anything handling input, files, processes or the network, in
  `js/`, `lib/` or `scripts/`, which are scanned.
- Outside `overrides/`, write plain JavaScript (or Flow comment types)
  that CodeQL parses. A "parse error" on CodeQL's tool status page means a
  file isn't being scanned at all: fix it or move it, don't ignore it.

## Building and testing

`npm test` runs the CLI, Metro config and release script tests. The GTK
host and its self-tests build with CMake; see
[docs/dev-loop.md](docs/dev-loop.md) and
[docs/building-react-native.md](docs/building-react-native.md).
