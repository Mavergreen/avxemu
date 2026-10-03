#!/bin/sh
# platform: macOS-only -- drives shipyard's stage_product.sh, build_component_pkg.sh and set_install_floor.sh
#   usage: build-pkg.sh BUILD-DIR VERSION OUT.pkg
#          Stages libavxemu.dylib (byte for byte as built and tested), the docs and the updater under
#          /usr/local/mavergreen/avxemu, and wraps them in a 10.9.5-floored product archive.
set -eu
B=${1:?usage: build-pkg.sh BUILD-DIR VERSION OUT.pkg}
V=${2:?usage: build-pkg.sh BUILD-DIR VERSION OUT.pkg}
OUT=${3:?usage: build-pkg.sh BUILD-DIR VERSION OUT.pkg}
REPO=$(cd "$(dirname "$0")/.." && pwd)
. "$REPO/build/msc.sh"
SHIPYARD=$SHIPYARD_SCRIPTS
[ -f "$B/libavxemu.dylib" ] || { echo "build-pkg: no $B/libavxemu.dylib" >&2; exit 1; }
[ -d "$B/avxemu-updater.app" ] || { echo "build-pkg: no updater; configure with -DAVXEMU_BUILD_UPDATER=ON" >&2; exit 1; }
work=$(mktemp -d "${TMPDIR:-/tmp}/avxemu-pkg.XXXXXX")
trap 'rm -rf "$work"' EXIT
ROOT=$work/root
T=$ROOT/usr/local/mavergreen/avxemu
install -d "$T/lib" "$T/share/doc/avxemu"
cp -p "$B/libavxemu.dylib" "$T/lib/libavxemu.dylib"
cp "$REPO/LICENSE" "$REPO/README.md" "$T/share/doc/avxemu/"
find "$ROOT" -name '._*' -delete
sh "$SHIPYARD/stage_product.sh" --stage "$ROOT" --product avxemu --name AVXEmu --version "$V" \
  --scripts-out "$work/scripts" --updater-app "$B/avxemu-updater.app"
sh "$SHIPYARD/build_component_pkg.sh" --root "$ROOT" --identifier dev.mavergreen.avxemu \
  --version "$V" --install-location / --scripts "$work/scripts" --out "$work/avxemu-component.pkg" >&2
mkdir -p "$(dirname "$OUT")"
sh "$SHIPYARD/set_install_floor.sh" --identifier dev.mavergreen.avxemu --title AVXEmu \
  --component "$work/avxemu-component.pkg" --out "$OUT" --require-scripts --host-arch x86_64 >&2
echo "built $OUT"
