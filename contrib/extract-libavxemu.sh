#!/bin/sh
# platform: macOS-only -- pkgutil expands the flat package
#   usage: extract-libavxemu.sh AVXEMU.pkg DEST-DIR
#          Writes DEST-DIR/libavxemu.dylib from an AVXEmu release package, using only tools Mac OS X
#          10.9 ships (pkgutil, gzip, cpio). Fails unless exactly one payload in the package carries
#          it. For installers that want the dylib without installing the package.
set -eu
pkg=${1:?usage: extract-libavxemu.sh AVXEMU.pkg DEST-DIR}
dest=${2:?usage: extract-libavxemu.sh AVXEMU.pkg DEST-DIR}
want=usr/local/mavergreen/avxemu/lib/libavxemu.dylib
[ -f "$pkg" ] || { echo "extract-libavxemu: no such package: $pkg" >&2; exit 1; }
[ -d "$dest" ] || { echo "extract-libavxemu: no such directory: $dest" >&2; exit 1; }
work=$(mktemp -d "${TMPDIR:-/tmp}/extract-libavxemu.XXXXXX")
trap 'rm -rf "$work"' EXIT
pkgutil --expand "$pkg" "$work/x"
found=0; n=0
for pl in "$work"/x/*/Payload "$work"/x/Payload; do
  [ -f "$pl" ] || continue
  n=$((n + 1))
  mkdir "$work/p$n"
  gzip -dc "$pl" > "$work/p$n.cpio"
  (cd "$work/p$n" && cpio -id --quiet < "$work/p$n.cpio")
  if [ -f "$work/p$n/$want" ]; then found=$((found + 1)); src="$work/p$n/$want"; fi
done
[ "$n" -gt 0 ] || { echo "extract-libavxemu: $pkg has no payloads" >&2; exit 1; }
[ "$found" -eq 1 ] || { echo "extract-libavxemu: $found payloads in $pkg carry $want (want exactly 1)" >&2; exit 1; }
cp "$src" "$dest/libavxemu.dylib"
echo "wrote $dest/libavxemu.dylib"
