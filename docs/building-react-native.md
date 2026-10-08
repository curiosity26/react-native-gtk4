# Building React Native's C++ core for the GTK host (work in progress)

Run these on the Ubuntu 24.04 machine you develop on.

```sh
sudo apt install -y clang cmake ninja-build libssl-dev libicu-dev libreadline-dev nodejs npm
python3 scripts/fetch-rn-deps.py   # RN 0.87.1 source, third-party C++ deps, codegen, Hermes source
scripts/build-hermes.sh            # libhermesvm + hermesc + headers
(cd examples/hello-world && npm install && npm run bundle)
```

Everything lands in `third-party/deps/` (git-ignored). Versions are pinned in
`rn-version.properties`.

## Findings so far

These came from compiling React Native's Fantom tester (the C++ host we are
basing ours on) on Ubuntu 24.04:

- Use **clang**. React Native's headers use `#pragma mark`, which GCC
  rejects under `-Werror`.
- GCC 13's libstdc++ (Ubuntu 24.04's default) no longer pulls in
  `<cstdint>` transitively, and a few RN files (`HttpUtils.h`,
  `AnsiParser.h`) rely on that. Compile with `-include cstdint` until
  upstream adds the include. Folly also needs
  `-Wno-deprecated-declarations` with this libstdc++.
- Hermes and React Native must use the same C++ standard library, since
  JSI passes `std::string` across the boundary.
- Codegen must run on React Native's git source, not the npm package's
  `src/`, because the npm package leaves out the specs for some internal
  native modules.
- GitHub source tarballs (`/archive/…`) can be blocked by proxies, so
  the fetch script uses shallow git clones instead.
