#!/bin/sh
# platform: macOS-only -- pkgbuild makes the negative-control package
#   usage: extract-test.sh PKG BUILT-DYLIB
#          The extraction recipe installers depend on must yield exactly the dylib that was tested,
#          from the real product archive (which also carries dev.mavergreen.base), and must refuse a
#          package with no avxemu payload.
set -eu
pkg=${1:?usage: extract-test.sh PKG BUILT-DYLIB}
built=${2:?usage: extract-test.sh PKG BUILT-DYLIB}
here=$(cd "$(dirname "$0")" && pwd)
x="$here/../contrib/extract-libavxemu.sh"
d=$(mktemp -d "${TMPDIR:-/tmp}/extract-test.XXXXXX")
trap 'rm -rf "$d"' EXIT
mkdir "$d/out" "$d/empty-root" "$d/out2"
sh "$x" "$pkg" "$d/out"
cmp "$d/out/libavxemu.dylib" "$built" || { echo "extracted dylib differs from the built one"; exit 1; }
pkgbuild --quiet --root "$d/empty-root" --identifier dev.mavergreen.test.empty --version 1 "$d/empty.pkg"
if sh "$x" "$d/empty.pkg" "$d/out2" 2>/dev/null; then echo "extraction accepted a package with no avxemu"; exit 1; fi
echo "extraction yields the tested dylib, and refuses a package without one"
