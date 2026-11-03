#!/usr/bin/env bash
# Builds Hermes (libhermesvm + hermesc) against React Native's JSI, the way
# ReactAndroid's hermes-engine "WithDebugger" tasks do, and lays out its
# public headers next to it.
#   scripts/build-hermes.sh [--headers-only] [deps-dir]
# (run scripts/fetch-rn-deps.py first). --headers-only lays out the headers
# without building: what native libraries need next to a prebuilt host.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
headers_only=0
if [[ ${1:-} == --headers-only ]]; then
  headers_only=1
  shift
fi
deps=${1:-${RNGTK_DEPS_DIR:-$here/../third-party/deps}}
deps=$(cd "$deps" && pwd -P)
src=$(cd "$deps/hermes" && pwd -P)
build=$(readlink -f "$deps/hermes-build")
headers=$deps/hermes-headers

if ((!headers_only)); then

# Same compiler as React Native's core (clang), so the C++ ABI matches.
cc=${CC:-clang}
cxx=${CXX:-clang++}

# clang 20+ flags Hermes's deliberate memcpy of PinnedHermesValue (a plain
# 64-bit value) with -Wnontrivial-memcall, once per file that includes
# HermesValue-inline.h. Turn it off where the compiler knows the warning;
# older clang would warn about an unknown option instead.
cxxflags=${CXXFLAGS:-}
if "$cxx" -Werror -Wnontrivial-memcall -x c++ -fsyntax-only /dev/null 2>/dev/null; then
  cxxflags="$cxxflags -Wno-nontrivial-memcall"
fi

cmake -S "$src" -B "$build" -G Ninja --log-level=ERROR -Wno-dev \
  -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" \
  -DCMAKE_CXX_FLAGS="$cxxflags" \
  -DJSI_DIR="$deps/react-native/packages/react-native/ReactCommon/jsi" \
  -DCMAKE_BUILD_TYPE=Release \
  -DHERMES_ENABLE_DEBUGGER=True \
  -DHERMESVM_HEAP_HV_MODE=HEAP_HV_PREFER32
cmake --build "$build" --target hermesc hermesvm
fi

# Same headers as prepareHeadersForPrefab: API/ and public/, minus jsi.
rm -rf "$headers"
for dir in API public; do
  (cd "$src/$dir" && find . -name '*.h' -not -path './jsi/*' -print0 |
    while IFS= read -r -d '' f; do install -D -m644 "$f" "$headers/$f"; done)
done
if ((headers_only)); then
  echo "hermes: headers in $headers"
else
  echo "hermes: $build/lib/libhermesvm.so, headers in $headers"
fi
