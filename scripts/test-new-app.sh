#!/usr/bin/env bash
# End to end: new React Native apps, created outside this repository with
# the community CLI, get Linux from this package's npm tarball (so the
# package's "files" list is what they see), build with run-linux and run:
# Debug from Metro, Release from the bundled JS.
#
#   scripts/test-new-app.sh [WORK_DIR]
#
# WORK_DIR (default: build/new-app-e2e) holds the tarball and the apps. It
# must not be inside a directory with its own node_modules.
#
# Environment:
#   BACKENDS="wayland x11"   GDK backends to smoke-test on
#   APPS="MyApp MyApp2"      the first is tested fully; the others build
#                            against the same cache (timing its reuse) and
#                            smoke-test their Release build once
#   METRO_PORT=8095
#   SCREENSHOTS=DIR          where new-app-{debug,release}-<backend>.png go
#                            (default WORK_DIR/screenshots)
#   RNGTK_CACHE_DIR, RNGTK_DEPS_DIR   as for run-linux
#
# Needs Node >= 22.13, network access for npm, and a display.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/.." && pwd)
work=${1:-$repo/build/new-app-e2e}
mkdir -p "$work"
work=$(cd "$work" && pwd)
read -r -a backends <<<"${BACKENDS:-wayland x11}"
read -r -a apps <<<"${APPS:-MyApp MyApp2}"
port=${METRO_PORT:-8095}
shots=${SCREENSHOTS:-$work/screenshots}
mkdir -p "$shots"
rn_version=$(sed -n 's/^reactNative=//p' "$repo/rn-version.properties")
cli_version=20.2.0
timings=$work/timings.txt
: >"$timings"

failures=0
pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; failures=$((failures + 1)); }
note_time() { echo "$1 $2" | tee -a "$timings"; }

metro_pid=
stop_metro() {
  if [[ -n "$metro_pid" ]]; then
    kill -- "-$metro_pid" 2>/dev/null || true
    for _ in $(seq 50); do
      curl -s "http://localhost:$port/status" >/dev/null 2>&1 || break
      sleep 0.2
    done
    metro_pid=
  fi
}
trap stop_metro EXIT

echo "== packing @curiosity26/react-native-gtk4"
rm -f "$work"/curiosity26-react-native-gtk4-*.tgz
(cd "$repo" && npm pack --silent --pack-destination "$work" >/dev/null)
tarball=$(ls "$work"/curiosity26-react-native-gtk4-*.tgz)
echo "$tarball"

create_app() {
  local app=$1
  if [[ ! -f "$work/$app/package.json" ]]; then
    echo "== creating $app (React Native $rn_version)"
    (cd "$work" && npx --yes "@react-native-community/cli@$cli_version" init "$app" \
      --version "$rn_version" --skip-install --skip-git-init --install-pods false)
    (cd "$work/$app" && npm install --no-audit --no-fund)
  fi
  (cd "$work/$app" && npm install --no-audit --no-fund "$tarball")
}

init_linux() {
  local app=$1 out
  echo "== $app: react-native init-linux"
  (cd "$work/$app" && npx react-native init-linux)
  [[ -f "$work/$app/linux/CMakeLists.txt" && -f "$work/$app/linux/main.cc" ]] &&
    grep -q "withLinux(" "$work/$app/metro.config.js" &&
    grep -q '"linux": "react-native run-linux"' "$work/$app/package.json" &&
    pass "$app: init-linux set up linux/, metro.config.js and the linux script" ||
    fail "$app: init-linux"
  out=$(cd "$work/$app" && npx react-native init-linux)
  if grep -qE '^(created|package.json:|metro.config.js:)' <<<"$out"; then
    fail "$app: a second init-linux changed something: $out"
  else
    pass "$app: a second init-linux changes nothing"
  fi
}

# run_linux APP LABEL ARGS...: times `react-native run-linux ARGS`.
run_linux() {
  local app=$1 label=$2 start end
  shift 2
  echo "== $app: react-native run-linux $*"
  start=$(date +%s)
  if (cd "$work/$app" && npx react-native run-linux "$@"); then
    end=$(date +%s)
    note_time "$label" "$((end - start))s"
    pass "$app: run-linux $*"
  else
    fail "$app: run-linux $*"
  fi
}

# smoke APP CONFIG BACKEND [ARGS...]: runs the built app with --smoke.
smoke() {
  local app=$1 config=$2 backend=$3 shot
  shift 3
  shot=$shots/new-app-$(tr '[:upper:]' '[:lower:]' <<<"$config")-$backend.png
  [[ "$app" == "${apps[0]}" ]] || shot=$work/$app-$config-$backend.png
  rm -f "$shot"
  if (cd "$work/$app" && GDK_BACKEND=$backend timeout 300 \
      "linux/build/$config/$app" --smoke --screenshot "$shot" --timeout 240000 "$@") &&
     [[ -s "$shot" ]]; then
    pass "$app: $config smoke test on $backend ($shot)"
  else
    fail "$app: $config smoke test on $backend"
  fi
}

start_metro() {
  local app=$1
  echo "== $app: Metro on port $port"
  (cd "$work/$app" && exec setsid npx react-native start --port "$port" --no-interactive) \
    >"$work/$app-metro.log" 2>&1 &
  metro_pid=$!
  for _ in $(seq 300); do
    curl -s "http://localhost:$port/status" 2>/dev/null | grep -q running && return 0
    sleep 0.2
  done
  fail "$app: Metro did not start (see $work/$app-metro.log)"
  return 1
}

app=${apps[0]}
create_app "$app"
init_linux "$app"
run_linux "$app" "first-debug-build($app)" --build-only
if start_metro "$app"; then
  for backend in "${backends[@]}"; do
    smoke "$app" Debug "$backend" --dev-server "localhost:$port"
  done
  stop_metro
fi
run_linux "$app" "release-build($app)" --release --build-only
for backend in "${backends[@]}"; do
  smoke "$app" Release "$backend"
done
# The CLI's own launch path: run-linux builds (nothing to do) and runs the
# Release app attached with --smoke.
GDK_BACKEND=${backends[0]} run_linux "$app" "release-run($app)" \
  --release --smoke --screenshot "$work/$app-run-linux.png"

for app in "${apps[@]:1}"; do
  create_app "$app"
  init_linux "$app"
  run_linux "$app" "cached-debug-build($app)" --build-only
  run_linux "$app" "cached-release-build($app)" --release --build-only
  smoke "$app" Release "${backends[0]}"
done

echo
echo "timings:"
cat "$timings"
if ((failures)); then
  echo "$failures failure(s)"
  exit 1
fi
echo "all passed"
