#!/usr/bin/env bash
# Moves this package to another React Native version: the versions it pins,
# the example's and the codegen's dependencies, the docs, and a report of
# what changed upstream in the files overrides/ replaces.
#
#   scripts/upgrade-rn.sh VERSION [--work DIR]
#   scripts/upgrade-rn.sh 0.88.0
#   scripts/upgrade-rn.sh 0.88.0-rc.4
#
# What it changes (review it with git diff):
#   rn-version.properties        reactNative, and hermes (react-native's
#                                hermes-compiler version, as a tag)
#   package.json                 the react-native peerDependency
#   examples/hello-world         react-native, react, @react-native/*, metro,
#                                the community CLI (npm install)
#   scripts/codegen-deps         react-native and @react-native/codegen
#                                (its lock, regenerated)
#   docs, README, workflows,     the version strings (React Native's and the
#   scripts/test-new-app.sh      CLI's)
#
# What it reports, in WORK_DIR (default build/upgrade-VERSION):
#   overrides.diff    upstream's changes, old to new, to each file an
#                     override in overrides/ replaces: port them by hand
#   new-splits.txt    platform-split files (Foo.ios.js + Foo.android.js) new
#                     in Libraries/ and src/, which may want an override
#
# Then build and test (docs/upgrading.md): run-linux fetches the new React
# Native and builds Hermes and the host into the cache, which is per
# version, so the old version's build stays.
#
# Needs Node >= 22.13 and network access (npm).
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/.." && pwd)
new=${1:?usage: upgrade-rn.sh VERSION [--work DIR]}
shift
work=$repo/build/upgrade-$new
if [[ ${1:-} == --work ]]; then
  work=$2
  shift 2
fi
old=$(sed -n 's/^reactNative=//p' "$repo/rn-version.properties")
if [[ $old == "$new" ]]; then
  echo "already on React Native $new"
  exit 0
fi
mkdir -p "$work"
work=$(cd "$work" && pwd)
echo "== React Native $old -> $new (report in $work)"

# The versions that go with it: from react-native's own package.json and
# the community template at the same version.
npm view "react-native@$new" --json >"$work/react-native.json"
# (Release candidates' templates have a commit suffix: 0.88.0-rc.4-6025b00.)
template=$(npm view @react-native-community/template versions --json | node -e '
  const v = JSON.parse(require("fs").readFileSync(0, "utf8"));
  const re = new RegExp(`^${process.argv[1].replace(/[.]/g, "\\.")}(-[0-9a-f]+)?$`);
  const m = v.filter(x => re.test(x));
  if (!m.length) process.exit(1);
  console.log(m[m.length - 1]);
' "$new") || { echo "no @react-native-community/template for $new" >&2; exit 1; }
npm view "@react-native-community/template@$template" --json >"$work/template.json"
read -r hermes react cli metro < <(node -e '
  const rn = require(process.argv[1]), t = require(process.argv[2]);
  const deps = {...t.dependencies, ...t.devDependencies};
  const metro = (rn.dependencies["metro-runtime"] || "").replace(/^[\^~]/, "");
  console.log(rn.dependencies["hermes-compiler"], deps.react, deps["@react-native-community/cli"], metro);
' "$work/react-native.json" "$work/template.json")
old_cli=$(node -p 'require(process.argv[1]).devDependencies["@react-native-community/cli"]' "$repo/examples/hello-world/package.json")
echo "   Hermes hermes-v$hermes, React $react, CLI $cli, Metro $metro"

# Pinned versions.
sed -i "s/^reactNative=.*/reactNative=$new/; s/^hermes=.*/hermes=hermes-v$hermes/" "$repo/rn-version.properties"
node -e '
  const fs = require("fs");
  const [repo, rn, react, cli, metro] = process.argv.slice(1);
  const edit = (file, fn) => {
    const p = `${repo}/${file}`;
    const json = JSON.parse(fs.readFileSync(p, "utf8"));
    fn(json);
    fs.writeFileSync(p, JSON.stringify(json, null, 2) + "\n");
  };
  edit("package.json", p => (p.peerDependencies["react-native"] = rn));
  edit("examples/hello-world/package.json", p => {
    p.dependencies["react-native"] = rn;
    p.dependencies.react = react;
    for (const d of ["@react-native/babel-preset", "@react-native/metro-config"]) p.devDependencies[d] = rn;
    p.devDependencies["@react-native-community/cli"] = cli;
    if (metro) p.devDependencies.metro = `^${metro}`;
  });
  edit("scripts/codegen-deps/package.json", p => {
    p.dependencies["react-native"] = `^${rn}`;
    p.dependencies["@react-native/codegen"] = `^${rn}`;
  });
' "$repo" "$new" "$react" "$cli" "$metro"
echo "== npm install (the example, the codegen's lock)"
(cd "$repo/examples/hello-world" && npm install --no-audit --no-fund >/dev/null)
(cd "$repo/scripts/codegen-deps" && rm -f package-lock.json &&
  npm install --package-lock-only --ignore-scripts --no-audit --no-fund >/dev/null)

# Version strings in the docs and the scripts that name them.
old_re=${old//./\\.}
grep -rl --include=*.md --include=*.yml --include=*.sh -e "$old" \
  "$repo/README.md" "$repo/docs" "$repo/.github" "$repo/scripts" 2>/dev/null |
  while read -r f; do
    sed -i "s/$old_re/$new/g; s/cli@$old_cli/cli@$cli/g; s/cli_version=$old_cli/cli_version=$cli/g" "$f"
    echo "   updated $f"
  done

# What changed upstream under the overrides: both versions' npm packages.
echo "== diffing what overrides/ replaces"
for v in "$old" "$new"; do
  if [[ ! -d $work/rn-$v ]]; then
    tgz=$(cd "$work" && npm pack --silent "react-native@$v")
    mkdir -p "$work/rn-$v" && tar -xzf "$work/$tgz" -C "$work/rn-$v" --strip-components=1
    rm -f "$work/$tgz"
  fi
done
: >"$work/overrides.diff"
changed=0
while IFS= read -r override; do
  rel=${override#"$repo/overrides/"}
  base=${rel%.linux.js}
  for ext in .js .ios.js .android.js .native.js; do
    a=$work/rn-$old/$base$ext
    b=$work/rn-$new/$base$ext
    [[ -f $a || -f $b ]] || continue
    if ! diff -q "$a" "$b" >/dev/null 2>&1; then
      diff -u --label "$old/$base$ext" --label "$new/$base$ext" "$a" "$b" >>"$work/overrides.diff" || true
      echo "   changed upstream: $base$ext (overrides/$rel)"
      changed=$((changed + 1))
    fi
  done
done < <(find "$repo/overrides" -name '*.linux.js' | sort)

# Platform-split files new in this version (a .ios.js and an .android.js,
# no plain .js): the Metro config falls back to the android one; an
# override may be better.
splits() {
  (cd "$1" && find Libraries src -name '*.ios.js' 2>/dev/null | while read -r f; do
    b=${f%.ios.js}
    [[ -f $b.android.js && ! -f $b.js ]] && echo "$b"
  done | sort)
}
comm -13 <(splits "$work/rn-$old") <(splits "$work/rn-$new") >"$work/new-splits.txt"

echo
echo "React Native $new: $changed overridden upstream file(s) changed ($work/overrides.diff),"
echo "$(wc -l <"$work/new-splits.txt") new platform-split file(s) ($work/new-splits.txt)."
echo "Next (docs/upgrading.md): port the override changes, then"
echo "  npm test && (cd examples/hello-world && npx react-native run-linux --release --smoke)"
