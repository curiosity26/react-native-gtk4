#!/usr/bin/env bash
# Builds Hermes (libhermesvm + hermesc) against React Native's JSI, the way
# ReactAndroid's hermes-engine "WithDebugger" tasks do, and lays out its
# public headers next to it.
#   scripts/build-hermes.sh [deps-dir]   (run scripts/fetch-rn-deps.py first)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
deps=${1:-${RNGTK_DEPS_DIR:-$here/../third-party/deps}}
deps=$(cd "$deps" && pwd -P)
src=$(cd "$deps/hermes" && pwd -P)
build=$(readlink -f "$deps/hermes-build")
headers=$deps/hermes-headers

cmake -S "$src" -B "$build" -G Ninja --log-level=ERROR -Wno-dev \
  -DJSI_DIR="$deps/react-native/packages/react-native/ReactCommon/jsi" \
  -DCMAKE_BUILD_TYPE=Release \
  -DHERMES_ENABLE_DEBUGGER=True \
  -DHERMESVM_HEAP_HV_MODE=HEAP_HV_PREFER32
cmake --build "$build" --target hermesc hermesvm

# Same headers as prepareHeadersForPrefab: API/ and public/, minus jsi.
rm -rf "$headers"
for dir in API public; do
  (cd "$src/$dir" && find . -name '*.h' -not -path './jsi/*' -print0 |
    while IFS= read -r -d '' f; do install -D -m644 "$f" "$headers/$f"; done)
done
echo "hermes: $build/lib/libhermesvm.so, headers in $headers"
