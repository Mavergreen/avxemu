#!/bin/sh
# platform: macOS-only -- transports the cross build and installed Shipyard to a 10.9 guest
# usage: bundle-mavericks.sh BUILD OUT-DIR
# platform: generated CTest files use absolute paths; preserve them across hosts.
set -eu
B=$(cd "${1:?build directory}" && pwd)
OUT=${2:?output directory}
REPO=$(cd "$(dirname "$0")/.." && pwd)
SHIPYARD=/usr/local/mavergreen/shipyard
[ -x "$SHIPYARD/bin/ctest" ]
[ -f "$B/CTestTestfile.cmake" ]
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
stage=$(mktemp -d "${TMPDIR:-/tmp}/avxemu-vm.XXXXXX")
trap 'rm -rf "$stage"' EXIT
export COPYFILE_DISABLE=1
mkdir -p "$stage/root$REPO" "$stage/root$B" "$stage/root/usr/local/mavergreen"
# spec: DEVELOPING.md "Mavericks CI" -- transport committed source and the generated registry.
git -C "$REPO" archive --output="$stage/source.tar" HEAD
tar -xf "$stage/source.tar" -C "$stage/root$REPO"
cp -R "$B/." "$stage/root$B/"
rm -rf "$stage/root$B/dist" "$stage/root$B/Testing"
# platform: install@v1 provides universal, Mavericks-compatible CTest and its dependencies.
cp -R "$SHIPYARD" "$stage/root/usr/local/mavergreen/"
printf '%s\n' "$REPO" > "$OUT/source-path"
printf '%s\n' "$B" > "$OUT/build-path"
printf '%s\n' "${TMPDIR:-/tmp}" > "$OUT/tmp-path"
tar -czf "$OUT/root.tar.gz" -C "$stage/root" .
