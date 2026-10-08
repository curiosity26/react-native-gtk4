#!/usr/bin/env bash
# End-to-end check of the Metro dev loop with examples/hello-world:
#
#   1. starts Metro (or reuses one already serving the example),
#   2. rngtk-net-check: the libsoup HTTP/WebSocket clients against Metro,
#   3. rn-gtk-host --dev-server --self-test: the Hello World checks on a
#      bundle from Metro, then four reloads, each re-checked with no leaked
#      widgets or threads: three of the host's own (Ctrl+R) and one from
#      Metro (POST /reload, what `r` in Metro's terminal sends),
#   4. fast refresh: edits App.js and waits for the new text without a reload,
#   5. LogBox: makes App throw, waits for LogBox to show, then clicks its
#      Dismiss button (pointer input into LogBox's surface),
#
# then restores App.js and stops the Metro it started.
#
#   scripts/test-dev-loop.sh                      # current display
#   GDK_BACKEND=x11 scripts/test-dev-loop.sh      # X11 / XWayland
#   LOGBOX_SCREENSHOT=docs/images/dev-loop-logbox.png scripts/test-dev-loop.sh
#
# Needs the host built (cmake --build build/linux), the example's npm
# dependencies installed, and Node >= 22.13 on PATH.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
example=$root/examples/hello-world
build=${BUILD_DIR:-$root/build/linux}
port=${METRO_PORT:-8081}
out=$root/build/dev-loop-test
mkdir -p "$out"
app_js=$example/App.js
cp "$app_js" "$out/App.js.orig"

metro_pid=
host_pid=
refreshed_text="Fast refresh works"
cleanup() {
  cp "$out/App.js.orig" "$app_js"
  [[ -n $host_pid ]] && kill "$host_pid" 2>/dev/null || true
  if [[ -z $metro_pid ]] && metro_up; then
    # A Metro that keeps running must see the restore before the next run
    # asks it for a bundle.
    for _ in $(seq 50); do
      curl -sf "http://localhost:$port/index.bundle?platform=linux&dev=true" |
        grep -q "LogBox check\|$refreshed_text" || break
      sleep 0.2
    done
  fi
  if [[ -n $metro_pid ]]; then
    # npx and node: stop the whole group, and wait for the port, so a next
    # run never reuses a Metro that is shutting down.
    kill -- "-$metro_pid" 2>/dev/null || true
    for _ in $(seq 50); do metro_up || break; sleep 0.2; done
  fi
}
trap cleanup EXIT

metro_up() { curl -sf "http://localhost:$port/status" 2>/dev/null | grep -q packager-status:running; }

if metro_up; then
  echo "Using the Metro already running on port $port"
else
  echo "Starting Metro on port $port (log: $out/metro.log)"
  (cd "$example" && exec setsid npx react-native start --port "$port" --no-interactive) \
    >"$out/metro.log" 2>&1 </dev/null &
  metro_pid=$!
  for _ in $(seq 120); do metro_up && break; sleep 0.5; done
  metro_up || { cat "$out/metro.log"; echo "FAIL Metro did not start"; exit 1; }
fi

echo "== libsoup clients"
"$build/rngtk-net-check" "http://localhost:$port"

# Waits until the host prints `READY <what>`, or fails if it exits first.
wait_ready() {
  for _ in $(seq 600); do
    grep -q "^READY $1" "$out/host.log" && return 0
    kill -0 "$host_pid" 2>/dev/null || return 1
    sleep 0.1
  done
  return 1
}

echo "== rn-gtk-host --dev-server (${GDK_BACKEND:-default backend})"
"$build/rn-gtk-host" --dev-server "localhost:$port" --self-test --test-reload --reloads 3 --expect-reload --verbose \
  --expect-text "$refreshed_text" --expect-logbox --dismiss-logbox \
  ${LOGBOX_SCREENSHOT:+--logbox-screenshot "$LOGBOX_SCREENSHOT"} \
  >"$out/host.log" 2>&1 &
host_pid=$!

status=0
if wait_ready expect-reload; then
  echo "asking Metro to reload"
  curl -sf -X POST "http://localhost:$port/reload" >/dev/null
fi
if wait_ready expect-text; then
  echo "editing App.js: subtitle -> \"$refreshed_text\""
  sed -i "s/React Native on GTK4/$refreshed_text/" "$app_js"
  if wait_ready expect-logbox; then
    echo "editing App.js: App throws"
    sed -i "s/^export default function App() {$/&\n  throw new Error('LogBox check: App threw');/" "$app_js"
  fi
fi
wait "$host_pid" || status=$?
host_pid=

grep -E "^(PASS|FAIL)|^reload|^saw|^platform line" "$out/host.log" || true
grep -E "FAIL timed out|^dev banner" "$out/host.log" || true
grep -o "dev banner: .*" "$out/host.log" | sort | uniq -c || true
fails=$(grep -c "^FAIL" "$out/host.log" || true)
if [[ $status -ne 0 || $fails -ne 0 ]]; then
  echo "FAIL dev loop (exit $status, $fails failed checks; log: $out/host.log)"
  exit 1
fi
echo "PASS dev loop ($(grep -c '^PASS' "$out/host.log") checks)"
