#!/usr/bin/env bash
# Runs examples/hello-world from Metro, with reload, fast refresh and LogBox.
# Two terminals:
#
#   npm run start:hello-world     # 1: Metro (r reloads, j opens DevTools)
#   npm run dev:hello-world       # 2: this: the GTK window
#
# Extra arguments go to rn-gtk-host (e.g. --width 1024 --height 768).
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
port=${METRO_PORT:-8081}
host=${BUILD_DIR:-$root/build/linux}/rn-gtk-host
if [[ ! -x $host ]]; then
  echo "Build the host first: cmake -S linux -B build/linux -G Ninja && cmake --build build/linux" >&2
  exit 1
fi
if ! curl -sf "http://localhost:$port/status" 2>/dev/null | grep -q packager-status:running; then
  echo "Metro is not running on port $port. In another terminal:" >&2
  echo "  npm run start:hello-world" >&2
  exit 1
fi
echo "Ctrl+R reloads, Ctrl+D opens the dev menu. Saving App.js fast-refreshes."
exec "$host" --dev-server "localhost:$port" "$@"
