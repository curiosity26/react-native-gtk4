#!/usr/bin/env bash
# Runs `react-native package-linux` for an app inside a container of
# another distribution (podman, or docker): an .rpm for Fedora from Ubuntu,
# say. Packages link the distribution's own libraries, so build them on it.
#
#   scripts/package-in-container.sh [--image IMAGE] APP_DIR [package-linux options...]
#   scripts/package-in-container.sh examples/hello-world --format rpm
#
# IMAGE defaults to registry.fedoraproject.org/fedora:44, with the build
# prerequisites (and librsvg's, WebKitGTK's and libadwaita's, for the svg,
# webview and screens ports) added (an image
# "rngtk-builder-<distro>-<version>-<n>", made once; n goes up when the
# package lists change). The app's sources are copied in (its git files, or everything but
# node_modules and build trees; an app inside this repository brings the
# repository), npm ci runs inside, and the package lands in
# APP_DIR/linux/build/package-<distro>-<version>/. The build cache lives in
# a volume (rngtk-cache-<distro>-<version>), so the next run is quick.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
package_dir=$(cd "$here/.." && pwd)
image=registry.fedoraproject.org/fedora:44
if [[ ${1:-} == --image ]]; then
  image=$2
  shift 2
fi
app=${1:?usage: package-in-container.sh [--image IMAGE] APP_DIR [package-linux options...]}
shift
app=$(cd "$app" && pwd)
engine=$(command -v podman || command -v docker || true)
[[ -n $engine ]] || { echo "podman or docker is needed" >&2; exit 1; }

tag=$(basename "${image%%:*}")-${image##*:}
builder=rngtk-builder-$tag-2
volume=rngtk-cache-$tag
out=$app/linux/build/package-$tag
mkdir -p "$out"

if ! "$engine" image exists "$builder" 2>/dev/null && ! "$engine" image inspect "$builder" >/dev/null 2>&1; then
  echo "== building $builder from $image"
  case $image in
    *fedora*|*centos*|*rocky*|*alma*)
      install='dnf install -y --setopt=install_weak_deps=False clang cmake ninja-build pkgconf-pkg-config git python3 gtk4-devel libsoup3-devel openssl-devel libicu-devel readline-devel libatomic librsvg2-devel webkitgtk6.0-devel libadwaita-devel nodejs npm rpm-build rpmlint desktop-file-utils appstream python3-gobject gdk-pixbuf2 librsvg2 fontconfig-devel findutils tar gzip curl which file && dnf clean all' ;;
    *debian*|*ubuntu*)
      install='apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends clang cmake ninja-build pkg-config git python3 libgtk-4-dev libsoup-3.0-dev libssl-dev libicu-dev libreadline-dev librsvg2-dev libwebkitgtk-6.0-dev libadwaita-1-dev nodejs npm dpkg-dev fakeroot lintian desktop-file-utils appstream python3-gi gir1.2-gdkpixbuf-2.0 librsvg2-common ca-certificates curl file && rm -rf /var/lib/apt/lists/*' ;;
    *) echo "no package list for $image: pass a prepared image" >&2; exit 1 ;;
  esac
  printf 'FROM %s\nRUN %s\n' "$image" "$install" | "$engine" build -t "$builder" -f - "$here"
fi

# The sources: the repository for its own examples.
case $app/ in
  "$package_dir"/*) src=$package_dir; rel=${app#"$package_dir"/} ;;
  *) src=$app; rel=. ;;
esac
list=$(mktemp)
trap 'rm -f "$list"' EXIT
if git -C "$src" rev-parse >/dev/null 2>&1; then
  git -C "$src" ls-files -co --exclude-standard >"$list"
else
  (cd "$src" && find . -type f -not -path '*/node_modules/*' -not -path '*/build/*' -not -path './.git/*') >"$list"
fi

echo "== packaging $rel in $builder: package-linux $*"
tar -C "$src" -cf - -T "$list" | "$engine" run --rm -i \
  -v "$volume:/cache" -v "$out:/out" -e RNGTK_CACHE_DIR=/cache \
  "$builder" sh -c "
    set -e
    mkdir -p /work && tar -C /work -xf -
    cd /work/$rel
    npm ci --no-audit --no-fund
    npx react-native package-linux --no-checks --output /out $(printf '%q ' "$@")
  "
echo "== $out:"
ls -la "$out"
